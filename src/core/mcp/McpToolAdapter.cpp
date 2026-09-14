#include "core/mcp/McpToolAdapter.h"

#include "core/mcp/McpClient.h"

#include <QDateTime>
#include <QMetaObject>
#include <QRegularExpression>
#include <QThread>

namespace NexusAI {

namespace {

constexpr int CallTimeoutMs = 35000;   // slightly above McpClient's own timeout

QString sanitizeNamePart(const QString& raw) {
    QString out = raw;
    static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_-]"));
    out.replace(invalid, QStringLiteral("_"));
    return out;
}

struct PendingCall {
    QMutex mutex;
    QWaitCondition condition;
    bool done{false};
    bool isError{false};
    quint64 requestId{0};
    QString text;
    QString error;
};

} // namespace

QString McpToolAdapter::registryNameFor(const QString& server, const QString& tool) {
    return QStringLiteral("mcp__%1__%2")
        .arg(sanitizeNamePart(server), sanitizeNamePart(tool));
}

McpToolAdapter::McpToolAdapter(McpClient* client, const QString& serverName,
                               const QString& toolName,
                               const QString& description,
                               const QJsonObject& inputSchema)
    : m_client(client)
    , m_serverName(serverName)
    , m_toolName(toolName)
    , m_description(description)
    , m_inputSchema(inputSchema) {
    m_registryName = registryNameFor(serverName, toolName);
}

ToolDefinition McpToolAdapter::definition() const {
    ToolDefinition def;
    def.name = m_registryName;
    // Untrusted third-party text: hard length cap before it reaches the
    // model context (docs/10 §2.4).
    def.description = m_description.left(1024);
    def.parameters = m_inputSchema;
    def.level = ToolLevel::RequiresConfirm;   // forced for MCP tools
    return def;
}

ToolResult McpToolAdapter::execute(const QJsonObject& args) {
    if (!m_client)
        return ToolResult::fail(QStringLiteral("MCP client unavailable"));

    auto pending = std::make_shared<PendingCall>();

    // Register the call on the GUI thread where the client/transport live.
    QMetaObject::invokeMethod(m_client, [this, pending, args]() {
        const quint64 id = m_client->callTool(m_toolName, args,
            [pending](bool isError, const QString& text, const QString& error) {
                QMutexLocker lock(&pending->mutex);
                pending->done = true;
                pending->isError = isError;
                pending->text = text;
                pending->error = error;
                pending->condition.wakeAll();
            });
        {
            QMutexLocker lock(&pending->mutex);
            pending->requestId = id;
        }
    }, Qt::QueuedConnection);

    const qint64 deadline =
        QDateTime::currentMSecsSinceEpoch() + CallTimeoutMs;

    while (true) {
        {
            QMutexLocker lock(&pending->mutex);
            if (pending->done) {
                if (pending->isError && !pending->text.isEmpty())
                    return ToolResult::fail(pending->text);
                if (pending->isError)
                    return ToolResult::fail(pending->error);
                return ToolResult::ok(pending->text);
            }
        }

        if (m_cancelled.load()) {
            const quint64 requestId = [this, pending]() -> quint64 {
                QMutexLocker lock(&pending->mutex);
                return pending->requestId;
            }();
            QMetaObject::invokeMethod(m_client, [this, requestId]() {
                m_client->cancelRequest(requestId);
            }, Qt::QueuedConnection);
            // Cooperative cancel: give the round trip a short grace period,
            // then bail out regardless.
            const qint64 cancelDeadline =
                QDateTime::currentMSecsSinceEpoch() + 3000;
            while (QDateTime::currentMSecsSinceEpoch() < cancelDeadline) {
                QMutexLocker lock(&pending->mutex);
                if (pending->done)
                    return ToolResult::fail(QStringLiteral("cancelled"));
                pending->condition.wait(&pending->mutex, 150);
            }
            return ToolResult::fail(QStringLiteral("cancelled"));
        }

        if (QDateTime::currentMSecsSinceEpoch() > deadline)
            return ToolResult::fail(QStringLiteral("MCP tool call timed out"));

        QThread::msleep(120);   // cancel-sliced wait (Batch 13 contract)
    }
}

void McpToolAdapter::cancel() {
    m_cancelled = true;   // atomic flag only — never touch GUI-thread state
}

} // namespace NexusAI
