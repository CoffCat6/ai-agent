#pragma once

#include "core/mcp/McpClient.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVector>

namespace NexusAI {

// Batch 31b: runtime manager for the configured MCP servers — owns the
// McpClient instances, bridges their tools into the ToolRegistry, persists
// edits through McpConfig and exposes the live state to the settings page
// (QML singleton McpManager).
//
// Startup wiring (main.cpp) collapses to loadAndConnectAll().
class McpManager : public QObject {
    Q_OBJECT
    QML_SINGLETON
public:
    // T14 (REQ-T14-05): tool count safety caps.
    static constexpr int kMaxToolsPerServer = 32;
    static constexpr int kMaxGlobalMcpTools = 64;
    QML_NAMED_ELEMENT(McpManager)

    // [{name, transport, endpoint, enabled, state, toolCount,
    //   command, args, url, env, headers}]
    // 后五项供设置页「编辑」回填：它们不在卡片上直接展示，但改动必须
    // 能在原有条目上继续编辑，否则用户只能删了重建（工具会掉线重连）。
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged FINAL)
    // Human-readable diagnostics surfaced by the settings page.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)

public:
    explicit McpManager(QObject* parent);
    static McpManager* instance();
    static McpManager* create(QQmlEngine*, QJSEngine*);

    // Startup: read mcp_servers.json and connect every enabled server.
    void loadAndConnectAll();
    // Called only after a backup has been persisted successfully. Imported
    // servers remain disabled until explicitly enabled again.
    void applyRestoredConfigs(const QVariantList& servers);

    QVariantList servers() const;
    QString lastError() const { return m_lastError; }

    // map: {name, transport:"stdio"|"http", command, args, url,
    //       env, headers, enabled}
    // Returns false (and leaves the list untouched) on an empty name or a
    // duplicate — the page shows its own field-level message, while the
    // manager keeps lastError for the programmatic path.
    Q_INVOKABLE bool addServer(const QVariantMap& server);
    // Replaces the entry named `originalName` (renames are allowed when the
    // new name is free). The live client is rebuilt so command/url/env/
    // headers changes take effect without deleting the server first.
    Q_INVOKABLE bool updateServer(const QString& originalName,
                                  const QVariantMap& server);
    Q_INVOKABLE void removeServer(const QString& name);
    Q_INVOKABLE void setEnabled(const QString& name, bool enabled);
    Q_INVOKABLE void reconnect(const QString& name);

signals:
    void serversChanged();
    void lastErrorChanged(const QString& error);

private:
    McpClient* clientFor(const QString& name) const;
    McpClient* ensureClient(const QVariantMap& server);
    void connectClient(McpClient* client);
    // docs/24 H9: delayed destruction for removed clients (40s > the
    // in-flight execute ceiling) so pool-thread invokeMethod always hits
    // a live object.
    void retireClient(McpClient* client);
    void persist();
    void refreshServers();

    QVector<McpClient*> m_clients;
    // docs/24 H9: removed clients awaiting their 40s delayed destruction;
    // reaped synchronously at application quit.
    QVector<McpClient*> m_retired;
    QVariantList m_configs;      // persisted shape (McpConfig::save)
    QString m_lastError;

    static McpManager* s_instance;
};

} // namespace NexusAI
