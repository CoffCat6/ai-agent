#pragma once

#include "core/mcp/McpTransport.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>

namespace NexusAI {

struct McpServerConfig {
    QString name;
    QString transport;   // "stdio" | "http"
    QString command;
    QStringList args;
    QVariantMap env;
    QString url;
    QVariantMap headers;   // Batch 31b: static HTTP headers (e.g. Authorization)
    bool enabled{true};
};

// One MCP server session (Batch 31a): transport + JSON-RPC framing +
// initialize handshake + tools/list + tools/call with per-request timeout
// and GUI-thread callbacks.
//
// Concurrency: several session tabs (Batch 29) may call tools at once; every
// request carries a unique JSON-RPC id and the response is dispatched by id,
// so concurrent calls never cross wires. Requests are queued to the GUI
// thread where the transport lives.
class McpClient : public QObject {
    Q_OBJECT

public:
    struct ToolInfo {
        QString name;
        QString description;
        QJsonObject inputSchema;
    };

    explicit McpClient(const McpServerConfig& config, QObject* parent = nullptr);
    ~McpClient() override;

    void connectToServer();
    void disconnectFromServer();

    QString serverName() const { return m_config.name; }
    McpServerConfig config() const { return m_config; }

    // MCP-level session state (transport state + handshake progress).
    enum class SessionState { Disconnected, Connecting, Connected, Failed };
    SessionState sessionState() const { return m_sessionState; }

    QList<ToolInfo> tools() const { return m_tools; }

    quint64 listTools();
    // Sends tools/call; handler runs on the GUI thread exactly once:
    // handler(isError, joinedText, errorText).
    quint64 callTool(const QString& tool, const QJsonObject& args,
                     std::function<void(bool isError, const QString& text,
                                        const QString& error)> handler);
    // Aborts an in-flight request and tells the server (best effort).
    void cancelRequest(quint64 id);

signals:
    void connected();                // handshake + initial tools/list done
    void disconnected();
    void connectionFailed(const QString& error);
    void toolsChanged();
    void toolResultArrived(quint64 requestId, bool isError,
                           const QString& text, const QString& error);
    void diagnostic(const QString& message);   // transport/stderr diagnostics

private:
    void onTransportStateChanged(McpTransport::State state);
    void onMessage(const QJsonObject& message);
    void runHandshake();
    void refreshTools();
    quint64 sendRequest(const QString& method, const QJsonObject& params,
                        std::function<void(const QJsonObject& result,
                                           const QJsonObject& error)> handler);
    void sendNotification(const QString& method, const QJsonObject& params);
    void failRequest(quint64 id, const QString& error);
    void setSessionState(SessionState state);

    McpServerConfig m_config;
    std::unique_ptr<McpTransport> m_transport;
    SessionState m_sessionState{SessionState::Disconnected};
    quint64 m_nextId{1};
    QList<ToolInfo> m_tools;
    QString m_lastDiagnostic;   // most recent transport error (for connectionFailed detail)

    struct PendingRequest {
        std::function<void(const QJsonObject& result, const QJsonObject& error)> handler;
        std::function<void(bool isError, const QString& text, const QString& error)> toolHandler;
        QTimer* timeout{nullptr};
    };
    QHash<quint64, PendingRequest> m_pending;

    static constexpr int RequestTimeoutMs = 30000;
};

} // namespace NexusAI
