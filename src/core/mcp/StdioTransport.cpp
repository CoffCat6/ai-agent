#include "core/mcp/StdioTransport.h"

#include <QJsonDocument>

namespace NexusAI {

StdioTransport::StdioTransport(const Config& config, QObject* parent)
    : McpTransport(parent)
    , m_config(config) {
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, &StdioTransport::start);

    // Wire once in the constructor: Qt::UniqueConnection does not dedupe
    // lambda connections, and start() may run several times (restarts).
    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            &StdioTransport::onReadyRead);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        appendStderr(m_process.readAllStandardError());
    });
    connect(&m_process, &QProcess::started, this, [this]() {
        // The pipe is live; the MCP handshake runs at the client layer.
            setState(State::Connected);
    });
    connect(&m_process, &QProcess::finished, this, &StdioTransport::onFinished);
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            // The command never ran: no point in restarting.
            emit errorOccurred(QStringLiteral("Failed to start MCP server: ")
                + m_config.command);
            setState(State::Failed);
        }
        // Crashed / Timedout paths are handled in onFinished.
    });
}

StdioTransport::~StdioTransport() {
    stop();
    // Destruction may not leave the child behind (tests would see a stray
    // process); a short blocking reap is acceptable on this path.
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

void StdioTransport::start() {
    if (m_process.state() != QProcess::NotRunning) return;

    m_stopping = false;
    setState(State::Connecting);

    m_process.setProgram(m_config.command);
    m_process.setArguments(m_config.args);
    // QProcessEnvironment is value-based: insert into a local copy first —
    // chaining onto processEnvironment() edits a temporary and the child
    // never sees the extra variables.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = m_config.env.constBegin(); it != m_config.env.constEnd(); ++it)
        environment.insert(it.key(), it.value().toString());
    m_process.setProcessEnvironment(environment);

    m_process.start();
}

void StdioTransport::stop() {
    m_stopping = true;
    m_restartTimer.stop();
    if (m_process.state() == QProcess::NotRunning) {
        setState(State::Disconnected);
        return;
    }
    // Graceful: close stdin so the server can flush and exit; the finished
    // handler finalizes the state. Never waitForFinished on the GUI thread.
    if (m_process.state() == QProcess::Running)
        m_process.closeWriteChannel();
    m_process.terminate();
    QTimer::singleShot(3000, this, [this]() {
        if (m_process.state() == QProcess::Running) {
            m_process.kill();
        }
    });
}

void StdioTransport::sendMessage(const QJsonObject& message) {
    if (m_process.state() != QProcess::Running) {
        emit errorOccurred(QStringLiteral("MCP server not running"));
        return;
    }
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process.write(line + '\n');
}

void StdioTransport::onReadyRead() {

    m_lineBuffer += m_process.readAllStandardOutput();
    int newline = 0;
    while ((newline = m_lineBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_lineBuffer.left(newline).trimmed();
        m_lineBuffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        dispatchPayload(line);
    }
}

void StdioTransport::onFinished(int exitCode, QProcess::ExitStatus status) {
    m_lineBuffer.clear();
    appendStderr(m_process.readAllStandardError());

    if (m_stopping) {
        setState(State::Disconnected);
        return;
    }

    // Abnormal exit (crash or non-zero): schedule a bounded restart. The
    // client re-runs its handshake over the fresh process.
    const bool abnormal = status == QProcess::CrashExit;
    emit errorOccurred(QStringLiteral("MCP server exited (code %1, %2)")
        .arg(exitCode).arg(abnormal ? QStringLiteral("crashed")
                                    : QStringLiteral("exited")));

    if (m_restarts >= MaxRestarts) {
        setState(State::Failed);
        return;
    }
    ++m_restarts;
    m_restartTimer.start(1000 << (m_restarts - 1));   // 1s / 2s / 4s
}

void StdioTransport::appendStderr(const QByteArray& chunk) {
    // Kept for diagnostics only; cap the retained tail so a chatty server
    // cannot grow memory unbounded.
    m_stderrBuffer += chunk;
    if (m_stderrBuffer.size() > 64 * 1024)
        m_stderrBuffer = m_stderrBuffer.right(32 * 1024);
}

} // namespace NexusAI
