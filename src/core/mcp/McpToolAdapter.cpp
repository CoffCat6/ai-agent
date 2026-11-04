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
    def.description = m_description.left(MaxDescriptionChars);
    def.parameters = m_inputSchema;
    def.level = ToolLevel::RequiresConfirm;   // forced for MCP tools
    // Side effects are unknown until MCP annotations (readOnlyHint) are
    // parsed, so PlanOnly blocks MCP tools by default.
    def.readOnly = false;
    return def;
}

ToolResult McpToolAdapter::execute(const QJsonObject& args) {
    // docs/25 A1 (Batch 21 B3 pattern): clear any stale cancel from a
    // previous call. cancel() can land between two execute() invocations;
    // without this reset the registration lambda below would cancel the
    // NEXT request on sight, leaving the adapter poisoned until reconnect.
    // Token-based calls never touch this flag (B3-D).
    m_cancelled.store(false);
    return executeInternal(args, {});
}

ToolResult McpToolAdapter::execute(const QJsonObject& args,
                                   const Cancellation& cancelled) {
    // A null token means the legacy no-token dispatch path: keep the previous
    // instance-level behaviour (including clearing a stale flag).
    if (!cancelled) return execute(args);
    return executeInternal(args, cancelled);
}

ToolResult McpToolAdapter::executeInternal(const QJsonObject& args,
                                           const Cancellation& cancelled) {
    // One cancellation source per invocation: the caller's token when present,
    // otherwise the legacy instance-level flag. Never both, so cancelling one
    // call cannot abort a concurrent call on the same adapter.
    const auto isCancelled = [this, cancelled]() {
        return cancelled ? cancelled->load() : m_cancelled.load();
    };

    if (isCancelled())
        return ToolResult::fail(QStringLiteral("MCP tool call cancelled"));

    // docs/22 F1: the client may be torn down while this call is in flight
    // (server removed/disabled). QPointer nulls out as soon as destruction
    // starts; the receiver-context invokeMethod then drops the queued call
    // if the client dies before it is delivered.
    McpClient* client = m_client.data();
    if (!client)
        return ToolResult::fail(QStringLiteral("MCP client unavailable"));

    auto pending = std::make_shared<PendingCall>();

    // Register the call on the GUI thread where the client/transport live.
    QMetaObject::invokeMethod(client, [this, pending, args, isCancelled]() {
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
        // If this invocation was cancelled before the lambda ran, cancel now.
        if (isCancelled() && id != 0)
            m_client->cancelRequest(id);
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

        if (isCancelled()) {
            // Best effort: abort the JSON-RPC request on the GUI thread, then
            // leave immediately. No grace period — the protocol timeout is
            // 35 s and a cancel must not keep the pool thread (and the UI's
            // stop request) waiting for it.
            const quint64 requestId = [pending]() -> quint64 {
                QMutexLocker lock(&pending->mutex);
                return pending->requestId;
            }();
            McpClient* cancelTarget = m_client.data();
            if (cancelTarget && requestId != 0) {
                QMetaObject::invokeMethod(cancelTarget, [cancelTarget, requestId]() {
                    cancelTarget->cancelRequest(requestId);
                }, Qt::QueuedConnection);
            }
            return ToolResult::fail(QStringLiteral("MCP tool call cancelled"));
        }

        if (QDateTime::currentMSecsSinceEpoch() > deadline)
            return ToolResult::fail(QStringLiteral("MCP tool call timed out"));

        {
            // 60 ms slice: a per-call cancel is observed promptly without
            // busy-waiting the pool thread.
            QMutexLocker lock(&pending->mutex);
            if (!pending->done)
                pending->condition.wait(&pending->mutex, 60);
        }
    }
}

void McpToolAdapter::cancel() {
    m_cancelled = true;   // atomic flag only — never touch GUI-thread state
}

void McpToolAdapter::cancel(const Cancellation& cancelled) {
    if (cancelled)
        cancelled->store(true);
    else
        cancel();   // no per-call token: legacy instance stop
}

} // namespace NexusAI
