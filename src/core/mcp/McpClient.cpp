#include "core/mcp/McpClient.h"

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
    }

    if (m_transport) {
        connect(m_transport.get(), &McpTransport::messageReceived,
                this, &McpClient::onMessage);
        connect(m_transport.get(), &McpTransport::stateChanged,
                this, &McpClient::onTransportStateChanged);
        connect(m_transport.get(), &McpTransport::errorOccurred,
                this, [this](const QString& error) { emit diagnostic(error); });
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
    m_tools.clear();
    if (m_transport)
        m_transport->stop();
    emit disconnected();
}

void McpClient::onTransportStateChanged(McpTransport::State state) {
    if (state == McpTransport::State::Connected) {
        // Pipe is live (re)runs the MCP handshake — also covers transport
        // restarts after a crash.
        if (m_sessionState != SessionState::Connected)
            runHandshake();
    } else if (state == McpTransport::State::Failed) {
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            if (it->toolHandler) it->toolHandler(false, QString(),
                QStringLiteral("MCP server connection failed"));
            if (it->timeout) it->timeout->deleteLater();
        }
        m_pending.clear();
        setSessionState(SessionState::Failed);
        emit connectionFailed(QStringLiteral("MCP server connection failed"));
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
            Q_UNUSED(result)
            if (!error.isEmpty()) {
                emit connectionFailed(QStringLiteral("initialize failed: ")
                    + error.value("message").toString());
                setSessionState(SessionState::Failed);
                return;
            }
            // Announce the client, then pull the tool list.
            sendNotification(QStringLiteral("notifications/initialized"), {});
            refreshTools();
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

quint64 McpClient::listTools() {
    refreshTools();   // fire-and-forget refresh (id is not needed by callers)
    return 0;
}

quint64 McpClient::callTool(const QString& tool, const QJsonObject& args,
                            std::function<void(bool, const QString&,
                                               const QString&)> handler) {
    const quint64 id = sendRequest(QStringLiteral("tools/call"),
        QJsonObject{{"name", tool}, {"arguments", args}}, nullptr);
    if (id == 0) {
        // Request could not be sent (transport down).
        if (handler) handler(false, QString(),
            QStringLiteral("MCP server not connected"));
        return 0;
    }
    m_pending[id].toolHandler = std::move(handler);
    return id;
}

void McpClient::cancelRequest(quint64 id) {
    if (m_pending.contains(id)) {
        sendNotification(QStringLiteral("notifications/cancelled"),
                         QJsonObject{{"requestId", static_cast<qint64>(id)}});
        if (auto it = m_pending.find(id); it != m_pending.end()) {
            if (it->toolHandler)
                it->toolHandler(false, QString(), QStringLiteral("cancelled"));
            if (it->timeout) it->timeout->deleteLater();
            m_pending.erase(it);
        }
    }
}

quint64 McpClient::sendRequest(const QString& method, const QJsonObject& params,
                               std::function<void(const QJsonObject&,
                                                  const QJsonObject&)> handler) {
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
            pending.toolHandler(false, QString(),
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
        pending.toolHandler(false, QString(), error);
    emit diagnostic(error);
}

void McpClient::setSessionState(SessionState state) {
    if (m_sessionState == state) return;
    m_sessionState = state;
}

} // namespace NexusAI
