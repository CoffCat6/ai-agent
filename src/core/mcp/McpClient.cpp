#include "core/mcp/McpClient.h"

#include "core/mcp/HttpTransport.h"
#include "core/mcp/StdioTransport.h"

#include <QJsonDocument>

namespace NexusAI {

McpClient::McpClient(const McpServerConfig& config, QObject* parent)
    : QObject(parent)
    , m_config(config) {
    if (m_config.transport == QLatin1String("stdio")) {
        StdioTransport::Config transportConfig;
        transportConfig.command = m_config.command;
        transportConfig.args = m_config.args;
        transportConfig.env = m_config.env;
        m_transport = std::make_unique<StdioTransport>(transportConfig, this);
    } else if (m_config.transport == QLatin1String("http")) {
        // Streamable HTTP (31b): static headers ride on every POST.
        HttpTransport::Config transportConfig;
        transportConfig.url = m_config.url;
        transportConfig.headers = m_config.headers;
        m_transport = std::make_unique<HttpTransport>(transportConfig, this);
    }

    if (m_transport) {
        connect(m_transport.get(), &McpTransport::messageReceived,
                this, &McpClient::onMessage);
        connect(m_transport.get(), &McpTransport::stateChanged,
                this, &McpClient::onTransportStateChanged);
        connect(m_transport.get(), &McpTransport::errorOccurred,
                this, [this](const QString& error) {
            // Keep the latest transport error so the Failed-state
            // connectionFailed signal can carry the real reason (e.g.
            // "Failed to start MCP server: npx") instead of a generic
            // "MCP server connection failed" that hides the cause.
            m_lastDiagnostic = error;
            emit diagnostic(error);
        });
    }
}

McpClient::~McpClient() {
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        if (it->timeout) it->timeout->deleteLater();
    m_pending.clear();
    if (m_transport)
        m_transport->stop();
}

void McpClient::connectToServer() {
    if (!m_transport) {
        emit connectionFailed(QStringLiteral("Unsupported transport: ")
            + m_config.transport);
        return;
    }
    setSessionState(SessionState::Connecting);
    m_transport->start();
}

void McpClient::disconnectFromServer() {
    setSessionState(SessionState::Disconnected);
    if (m_transport)
        m_transport->stop();
    // Emit BEFORE clearing: consumers (McpManager) unregister the tools
    // from the registry by walking tools() in their disconnected handler.
    emit disconnected();
    m_tools.clear();
    // Prompts 同步清空：servers() 的 promptCount 读 client->prompts()，
    // 断开后卡片计数必须归零（promptsChanged 触发 refreshServers）。
    m_prompts.clear();
    m_promptsCapability = false;
    emit promptsChanged();
}

void McpClient::onTransportStateChanged(McpTransport::State state) {
    if (state == McpTransport::State::Connected) {
        // Pipe is live (re)runs the MCP handshake — also covers transport
        // restarts after a crash.
        if (m_sessionState != SessionState::Connected)
            runHandshake();
    } else if (state == McpTransport::State::Failed) {
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            // docs/24 H1/H2: a transport-level failure is a tool FAILURE —
            // reporting isError=false made McpToolAdapter return an empty
            // SUCCESS for a call that never completed.
            if (it->toolHandler) it->toolHandler(true, QString(),
                QStringLiteral("MCP server connection failed"));
            if (it->timeout) it->timeout->deleteLater();
        }
        m_pending.clear();
        setSessionState(SessionState::Failed);
        emit connectionFailed(m_lastDiagnostic.isEmpty()
            ? QStringLiteral("MCP server connection failed")
            : QStringLiteral("MCP server connection failed: %1")
                  .arg(m_lastDiagnostic));
    } else if (state == McpTransport::State::Disconnected) {
        if (m_sessionState != SessionState::Disconnected) {
            setSessionState(SessionState::Disconnected);
            emit disconnected();
        }
    }
}

void McpClient::runHandshake() {
    setSessionState(SessionState::Connecting);
    sendRequest(QStringLiteral("initialize"),
        QJsonObject{
            {"protocolVersion", QStringLiteral("2025-03-26")},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{
                {"name", QStringLiteral("NexusAI")},
                {"version", QStringLiteral("0.2.0")}}},
        },
        [this](const QJsonObject& result, const QJsonObject& error) {
            if (!error.isEmpty()) {
                emit connectionFailed(QStringLiteral("initialize failed: ")
                    + error.value("message").toString());
                setSessionState(SessionState::Failed);
                return;
            }
            // Parse the server's declared capabilities: prompts/list is only
            // sent when the server actually advertises the prompts capability,
            // otherwise servers without it would answer method-not-found.
            m_promptsCapability = result.value("capabilities").toObject()
                                      .value("prompts").isObject();
            // Announce the client, then pull the tool list.
            sendNotification(QStringLiteral("notifications/initialized"), {});
            refreshTools();
            if (m_promptsCapability)
                refreshPrompts();
        });
}

void McpClient::refreshTools() {
    sendRequest(QStringLiteral("tools/list"), {},
        [this](const QJsonObject& result, const QJsonObject& error) {
            if (!error.isEmpty()) {
                emit connectionFailed(QStringLiteral("tools/list failed: ")
                    + error.value("message").toString());
                return;
            }
            QList<ToolInfo> tools;
            for (const auto& entry : result.value("tools").toArray()) {
                const QJsonObject t = entry.toObject();
                ToolInfo info;
                info.name = t.value("name").toString();
                info.description = t.value("description").toString();
                info.inputSchema = t.value("inputSchema").toObject();
                if (!info.name.isEmpty())
                    tools.append(info);
            }
            const bool firstConnect = m_sessionState != SessionState::Connected;
            m_tools = tools;
            setSessionState(SessionState::Connected);
            emit toolsChanged();
            if (firstConnect)
                emit connected();
        });
}


// docs/46: prompts/list 分页拉取。cursor 循环拉到 kMaxPrompts 为止；
// list_changed 中途再到的竞态用 busy/stale 处理——拉完后若已过期则重拉。
void McpClient::refreshPrompts() {
    if (!m_promptsCapability || !m_transport) return;
    if (m_promptsBusy) {
        m_promptsStale = true;
        return;
    }
    m_promptsBusy = true;
    fetchPromptsPage(QString(), std::make_shared<QList<PromptInfo>>());
}

void McpClient::fetchPromptsPage(const QString& cursor,
                                 std::shared_ptr<QList<PromptInfo>> acc) {
    QJsonObject params;
    if (!cursor.isEmpty())
        params.insert(QStringLiteral("cursor"), cursor);
    sendRequest(QStringLiteral("prompts/list"), params,
        [this, acc](const QJsonObject& result, const QJsonObject& error) {
            m_promptsBusy = false;
            if (!error.isEmpty()) {
                // 非致命：tools 已可用而 prompts 拉取失败不该连坐连接状态，
                // 记诊断即可（卡片照常显示工具，提示词计 0）。
                emit diagnostic(QStringLiteral("prompts/list failed: ")
                    + error.value("message").toString());
                if (m_promptsStale) {
                    m_promptsStale = false;
                    refreshPrompts();
                }
                return;
            }
            for (const auto& entry : result.value("prompts").toArray()) {
                if (acc->size() >= kMaxPrompts) break;
                const QJsonObject p = entry.toObject();
                PromptInfo info;
                info.name = p.value("name").toString();
                info.title = p.value("title").toString();
                info.description = p.value("description").toString();
                for (const auto& argEntry : p.value("arguments").toArray()) {
                    const QJsonObject a = argEntry.toObject();
                    PromptArgument arg;
                    arg.name = a.value("name").toString();
                    arg.description = a.value("description").toString();
                    arg.required = a.value("required").toBool();
                    info.arguments.append(arg);
                }
                if (!info.name.isEmpty())
                    acc->append(info);
            }
            const QString next = result.value("nextCursor").toString();
            if (!next.isEmpty() && acc->size() < kMaxPrompts) {
                m_promptsBusy = true;
                fetchPromptsPage(next, acc);
                return;
            }
            m_prompts = *acc;
            emit promptsChanged();
            if (m_promptsStale) {
                m_promptsStale = false;
                refreshPrompts();
            }
        });
}

quint64 McpClient::promptsGet(const QString& name, const QJsonObject& args,
                              std::function<void(bool, const QString&,
                                                 const QString&)> handler) {
    if (!m_transport) {
        if (handler) handler(false, QString(),
            QStringLiteral("MCP server not connected"));
        return 0;
    }
    // prompts/get 的结果结构与 tools/call 不同：顶层是 messages[]
    // （每条带 role + content），没有顶层 content 数组——不能走 toolHandler
    // 的拼接路径，在这里按 text 块拼接全部消息内容。空结果按失败处理，
    // 让调用方拿到明确错误而不是静默插入空串。
    return sendRequest(QStringLiteral("prompts/get"),
        QJsonObject{{"name", name}, {"arguments", args}},
        [handler](const QJsonObject& result, const QJsonObject& error) {
            if (!error.isEmpty()) {
                if (handler) handler(false, QString(),
                    error.value("message").toString());
                return;
            }
            QString text;
            for (const auto& msg : result.value("messages").toArray()) {
                const QJsonObject content =
                    msg.toObject().value("content").toObject();
                if (content.value("type").toString() == QLatin1String("text")) {
                    if (!text.isEmpty()) text += QLatin1Char('\n');
                    text += content.value("text").toString();
                }
            }
            if (handler)
                handler(!text.isEmpty(), text,
                        text.isEmpty()
                            ? QStringLiteral("prompt returned no text content")
                            : QString());
        });
}

quint64 McpClient::callTool(const QString& tool, const QJsonObject& args,
                            std::function<void(bool, const QString&,
                                               const QString&)> handler) {
    if (!m_transport) {
        if (handler) handler(true, QString(),
            QStringLiteral("MCP server not connected"));
        return 0;
    }
    return sendRequest(QStringLiteral("tools/call"),
        QJsonObject{{"name", tool}, {"arguments", args}}, nullptr,
        std::move(handler));
}

void McpClient::cancelRequest(quint64 id) {
    if (m_pending.contains(id)) {
        sendNotification(QStringLiteral("notifications/cancelled"),
                         QJsonObject{{"requestId", static_cast<qint64>(id)}});
        if (auto it = m_pending.find(id); it != m_pending.end()) {
            if (it->toolHandler)
                it->toolHandler(true, QString(), QStringLiteral("cancelled"));
            if (it->timeout) it->timeout->deleteLater();
            m_pending.erase(it);
        }
    }
}

quint64 McpClient::sendRequest(const QString& method, const QJsonObject& params,
                               std::function<void(const QJsonObject&,
                                                  const QJsonObject&)> handler,
                               std::function<void(bool, const QString&,
                                                  const QString&)> toolHandler) {
    if (!m_transport) {
        if (handler) handler({}, QJsonObject{
            {"message", QStringLiteral("no transport")}});
        return 0;
    }
    const quint64 id = m_nextId++;
    QJsonObject request{
        {"jsonrpc", QStringLiteral("2.0")},
        {"id", static_cast<qint64>(id)},
        {"method", method},
    };
    if (!params.isEmpty())
        request["params"] = params;

    auto& pending = m_pending[id];
    pending.handler = std::move(handler);
    pending.toolHandler = std::move(toolHandler);
    pending.timeout = new QTimer(this);
    pending.timeout->setSingleShot(true);
    connect(pending.timeout, &QTimer::timeout, this, [this, id]() {
        failRequest(id, QStringLiteral("request timed out"));
    });
    pending.timeout->start(RequestTimeoutMs);

    m_transport->sendMessage(request);
    return id;
}

void McpClient::sendNotification(const QString& method, const QJsonObject& params) {
    if (!m_transport) return;
    QJsonObject notification{
        {"jsonrpc", QStringLiteral("2.0")},
        {"method", method},
    };
    if (!params.isEmpty())
        notification["params"] = params;
    m_transport->sendMessage(notification);
}

void McpClient::onMessage(const QJsonObject& message) {
    // Notifications (no id) vs responses (id present).
    if (!message.contains("id")) {
        const QString method = message.value("method").toString();
        if (method == QLatin1String("notifications/tools/list_changed"))
            refreshTools();
        else if (method == QLatin1String("notifications/prompts/list_changed"))
            refreshPrompts();
        return;
    }

    const quint64 id = static_cast<quint64>(
        message.value("id").toVariant().toLongLong());
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return;

    // Steal the entry: the handler may re-enter (e.g. trigger a new request).
    PendingRequest pending = it.value();
    it = m_pending.erase(it);
    if (pending.timeout) pending.timeout->deleteLater();

    const QJsonObject error = message.value("error").toObject();
    if (!error.isEmpty()) {
        if (pending.handler) pending.handler({}, error);
        if (pending.toolHandler)
            pending.toolHandler(true, QString(),
                error.value("message").toString());
        return;
    }

    const QJsonObject result = message.value("result").toObject();

    // tools/call result: join the text content blocks (image/resource
    // blocks are noted as placeholders — Batch 30 rendering is text-only).
    if (pending.toolHandler) {
        const QJsonArray content = result.value("content").toArray();
        QString text;
        for (const auto& block : content) {
            const QJsonObject b = block.toObject();
            if (b.value("type").toString() == QLatin1String("text")) {
                if (!text.isEmpty()) text += QLatin1Char('\n');
                text += b.value("text").toString();
            } else if (b.value("type").toString() == QLatin1String("image")) {
                if (!text.isEmpty()) text += QLatin1Char('\n');
                text += QStringLiteral("[image content returned by tool]");
            }
        }
        const bool isError = result.value("isError").toBool();
        pending.toolHandler(isError, text,
            isError ? QStringLiteral("tool reported an error") : QString());
        return;
    }

    if (pending.handler) pending.handler(result, QJsonObject{});
}

void McpClient::failRequest(quint64 id, const QString& error) {
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return;
    PendingRequest pending = it.value();
    m_pending.erase(it);
    if (pending.timeout) pending.timeout->deleteLater();
    if (pending.handler)
        pending.handler({}, QJsonObject{{"message", error}});
    if (pending.toolHandler)
        pending.toolHandler(true, QString(), error);
    emit diagnostic(error);
}

void McpClient::setSessionState(SessionState state) {
    if (m_sessionState == state) return;
    m_sessionState = state;
}

} // namespace NexusAI
