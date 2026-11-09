#pragma once

#include "core/mcp/McpTransport.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>

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

    // MCP prompts（docs/46）：服务器预制的提示词模板。arguments 是模板的
    // 填参槽位（prompts/get 需要按名提供）；与 ToolInfo 平行，不走 ITool。
    struct PromptArgument {
        QString name;
        QString description;
        bool required{false};
    };
    struct PromptInfo {
        QString name;
        QString title;
        QString description;
        QList<PromptArgument> arguments;
    };

    // MCP resources（docs/48）：服务器暴露的远程数据（数据库表、文档页等）。
    // uri 是读取主键（resources/read 按 uri，不是 name）；templates/subscribe
    // 不在本批范围。
    struct ResourceInfo {
        QString uri;
        QString name;
        QString title;
        QString description;
        QString mimeType;
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

    // 服务器声明的 prompts 能力下才非空（capabilities.prompts，握手时解析）。
    // 单服务器上限 kMaxPrompts：与工具的 per-server 上限同一量纲，防止单台
    // 服务器把补全列表撑爆；分页（nextCursor）拉满上限即停。
    static constexpr int kMaxPrompts = 32;
    QList<PromptInfo> prompts() const { return m_prompts; }

    // 服务器声明的 resources 能力下才非空（capabilities.resources，握手时
    // 解析）；上限与分页口径同 prompts。
    static constexpr int kMaxResources = 32;
    QList<ResourceInfo> resources() const { return m_resources; }


    // Sends tools/call; handler runs on the GUI thread exactly once:
    // handler(isError, joinedText, errorText).
    quint64 callTool(const QString& tool, const QJsonObject& args,
                     std::function<void(bool isError, const QString& text,
                                        const QString& error)> handler);
    // Sends prompts/get; handler runs on the GUI thread exactly once:
    // handler(ok, joinedText, errorText)。ok=false 时 text 为空。复用
    // tools/call 的内容块拼接（toolHandler 路径），prompts/get 结果没有
    // isError 标记，普通成功即 ok=true。
    quint64 promptsGet(const QString& name, const QJsonObject& args,
                       std::function<void(bool ok, const QString& text,
                                          const QString& error)> handler);
    // Sends resources/read by uri; handler semantics 同 promptsGet。
    // contents[] 中 text 项按换行拼接；blob（base64 二进制）记占位符
    // 不进上下文；空结果按失败处理。
    quint64 resourcesRead(const QString& uri,
                          std::function<void(bool ok, const QString& text,
                                             const QString& error)> handler);
    // Aborts an in-flight request and tells the server (best effort).
    void cancelRequest(quint64 id);

signals:
    void connected();                // handshake + initial tools/list done
    void disconnected();
    void connectionFailed(const QString& error);
    void toolsChanged();
    void promptsChanged();
    void resourcesChanged();
    void toolResultArrived(quint64 requestId, bool isError,
                           const QString& text, const QString& error);
    void diagnostic(const QString& message);   // transport/stderr diagnostics

private:
    void onTransportStateChanged(McpTransport::State state);
    void onMessage(const QJsonObject& message);
    void runHandshake();
    void refreshTools();
    void refreshPrompts();
    void fetchPromptsPage(const QString& cursor,
                          std::shared_ptr<QList<PromptInfo>> acc);
    void refreshResources();
    void fetchResourcesPage(const QString& cursor,
                            std::shared_ptr<QList<ResourceInfo>> acc);
    quint64 sendRequest(const QString& method, const QJsonObject& params,
                        std::function<void(const QJsonObject& result,
                                           const QJsonObject& error)> handler,
                        std::function<void(bool isError, const QString& text,
                                           const QString& error)> toolHandler = nullptr);
    void sendNotification(const QString& method, const QJsonObject& params);
    void failRequest(quint64 id, const QString& error);
    void setSessionState(SessionState state);

    McpServerConfig m_config;
    std::unique_ptr<McpTransport> m_transport;
    SessionState m_sessionState{SessionState::Disconnected};
    quint64 m_nextId{1};
    QList<ToolInfo> m_tools;
    QString m_lastDiagnostic;   // most recent transport error (for connectionFailed detail)

    // Prompts（docs/46）：能力协商 + 分页拉取状态。busy/stale 处理
    // list_changed 在一次分页拉取中途再到的竞态——拉完后若已过期则重拉。
    bool m_promptsCapability{false};
    bool m_promptsBusy{false};
    bool m_promptsStale{false};
    QList<PromptInfo> m_prompts;

    // Resources（docs/48）：状态机同 prompts。
    bool m_resourcesCapability{false};
    bool m_resourcesBusy{false};
    bool m_resourcesStale{false};
    QList<ResourceInfo> m_resources;

    struct PendingRequest {
        std::function<void(const QJsonObject& result, const QJsonObject& error)> handler;
        std::function<void(bool isError, const QString& text, const QString& error)> toolHandler;
        QTimer* timeout{nullptr};
    };
    QHash<quint64, PendingRequest> m_pending;

    static constexpr int RequestTimeoutMs = 30000;
};

} // namespace NexusAI
