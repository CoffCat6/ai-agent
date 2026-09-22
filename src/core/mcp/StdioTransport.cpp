#include "core/mcp/StdioTransport.h"

#include <QJsonDocument>
#include <QStandardPaths>

namespace NexusAI {

#ifdef Q_OS_WIN
namespace {
// Quote a single token for cmd.exe's command line. A token is wrapped in
// double quotes when it contains a space, tab, or quote; embedded quotes
// are doubled (cmd's escape rule). Used to build the inner command line
// passed to `cmd /d /s /c "..."` via QProcess::setNativeArguments, which
// bypasses QProcess's own quoting that would conflict with cmd /s.
QString quoteForCmd(const QString& s) {
    if (s.isEmpty())
        return QStringLiteral("\"\"");
    const bool needQuote = s.contains(QLatin1Char(' '))
                        || s.contains(QLatin1Char('\t'))
                        || s.contains(QLatin1Char('"'));
    if (!needQuote)
        return s;
    QString out;
    out.reserve(s.size() + 2);
    out += QLatin1Char('"');
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('"'))
            out += QLatin1String("\"\"");
        else
            out += c;
    }
    out += QLatin1Char('"');
    return out;
}
} // namespace
#endif

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
            emit errorOccurred(formatError(
                QStringLiteral("Failed to start MCP server: ")
                + m_config.command));
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
    m_stderrBuffer.clear();   // fresh run: don't carry the previous stderr tail
    setState(State::Connecting);

#ifdef Q_OS_WIN
    // Windows: commands like npx / uvx are .cmd/.bat wrapper scripts, while
    // QProcess (CreateProcessW) only auto-appends .exe — spawning "npx"
    // directly fails with FailedToStart even though it runs fine from a
    // shell. Resolve the bare name through PATH+PATHEXT (findExecutable)
    // and route batch wrappers through cmd.exe, the same shim every
    // stdio MCP client on Windows needs. /d skips AutoRun scripts, /s
    // enables sane quoting for a resolved path that may contain spaces.
    QString program = m_config.command;
    QStringList args = m_config.args;
    QString nativeArgs;   // set for batch wrappers only
    if (!program.isEmpty()) {
        const QString resolved = QStandardPaths::findExecutable(program);
        const bool batch = resolved.endsWith(QLatin1String(".cmd"),
                                             Qt::CaseInsensitive)
                        || resolved.endsWith(QLatin1String(".bat"),
                                             Qt::CaseInsensitive);
        if (batch) {
            // Build the inner command line manually and pass it via
            // setNativeArguments to bypass QProcess's own quoting,
            // which conflicts with cmd /s /c's quote-stripping rules.
            // cmd /s removes exactly one leading and one trailing
            // quote from the /c argument, so wrap the whole inner
            // line in an extra quote pair to preserve embedded quotes
            // (e.g. a resolved path under "Program Files").
            QString inner = quoteForCmd(resolved);
            for (int i = 0; i < args.size(); ++i)
                inner += QLatin1Char(' ') + quoteForCmd(args.at(i));
            program = QStringLiteral("cmd.exe");
            nativeArgs = QStringLiteral("/d /s /c \"") + inner
                       + QLatin1Char('"');
        } else if (!resolved.isEmpty()) {
            program = resolved;
        }
        // Unresolved (not installed): keep the raw command so QProcess
        // fails with its regular FailedToStart diagnostics below.
    }
    m_process.setProgram(program);
    if (!nativeArgs.isEmpty())
        m_process.setNativeArguments(nativeArgs);
    else
        m_process.setArguments(args);
#else
    m_process.setProgram(m_config.command);
    m_process.setArguments(m_config.args);
#endif

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
    emit errorOccurred(formatError(
        QStringLiteral("MCP server exited (code %1, %2)")
            .arg(exitCode).arg(abnormal ? QStringLiteral("crashed")
                                        : QStringLiteral("exited"))));

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

QString StdioTransport::formatError(const QString& reason) const {
    const QByteArray trimmed = m_stderrBuffer.trimmed();
    if (trimmed.isEmpty())
        return reason;
    // Cap the inlined tail so a single log line stays readable; the full
    // buffer is already bounded to 64KB in appendStderr. Prefer UTF-8
    // (most MCP servers - Node.js/Python - emit UTF-8 stderr); fall back
    // to the system locale encoding if the bytes are not valid UTF-8.
    QString tail = QString::fromUtf8(trimmed);
    if (tail.contains(QChar::ReplacementCharacter))
        tail = QString::fromLocal8Bit(trimmed);
    if (tail.size() > 2000)
        tail = QStringLiteral("…") + tail.right(2000);
    return reason + QStringLiteral(" | stderr: ") + tail;
}

} // namespace NexusAI
