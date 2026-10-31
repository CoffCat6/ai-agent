#include "viewmodels/McpManager.h"

#include "core/mcp/McpConfig.h"
#include "core/mcp/McpConfigTransfer.h"
#include "core/mcp/McpToolAdapter.h"
#include "core/tools/ToolRegistry.h"
#include "core/utils/Activity.h"

#include <QLoggingCategory>
#include <QCoreApplication>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(lcMcp, "nexus.mcp")

namespace NexusAI {


McpManager* McpManager::s_instance = nullptr;

McpManager* McpManager::instance() {
    if (!s_instance) s_instance = new McpManager(nullptr);
    return s_instance;
}

McpManager* McpManager::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

McpManager::McpManager(QObject* parent)
    : QObject(parent) {
    // docs/24 H9: 应用退出时同步回收在用与已退役的 client——延迟退役的
    // singleShot 依赖事件循环，退出后不会触发，stdio 子进程（QProcess）
    // 将永久残留。client 析构内的 reaping 负责关闭子进程。
    if (qApp) {
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            const auto reapAll = [this](QVector<McpClient*>& list) {
                for (McpClient* c : std::as_const(list)) {
                    c->disconnectFromServer();
                    delete c;   // 析构内 reaps 子进程（每个至多阻塞 2s）
                }
                list.clear();
            };
            reapAll(m_clients);
            reapAll(m_retired);
        });
    }
}

void McpManager::loadAndConnectAll() {
    const auto configs = McpConfig::load();
    for (const QVariantMap& server : configs) {
        m_configs.append(server);
        if (!server.value("enabled").toBool()) continue;
        McpServerConfig config;
        config.name = server.value("name").toString();
        config.transport = server.value("transport").toString();
        config.command = server.value("command").toString();
        for (const QVariant& arg : server.value("args").toList())
            config.args << arg.toString();
        config.env = server.value("env").toMap();
        config.url = server.value("url").toString();
        config.headers = server.value("headers").toMap();
        auto* client = new McpClient(config, this);
        m_clients.append(client);
        connectClient(client);
        client->connectToServer();
    }
    refreshServers();
}

void McpManager::applyRestoredConfigs(const QVariantList& servers) {
    for (McpClient* client : std::as_const(m_clients))
        teardownClient(client);
    m_clients.clear();
    m_configs = servers;
    refreshServers();
}

void McpManager::teardownClient(McpClient* client) {
    QObject::disconnect(client, nullptr, this, nullptr);
    for (const auto& tool : client->tools())
        ToolRegistry::instance().takeTool(
            McpToolAdapter::registryNameFor(client->serverName(), tool.name));
    client->disconnectFromServer();
    retireClient(client);
}

// docs/24 H9: keep a removed (disconnected) client alive past the maximum
// in-flight execute window (35s call + 3s cancel grace) — the adapter's
// pool-thread invokeMethod(client, ...) then always hits a live object
// (docs/22 F1's shared_ptr covers the adapter; this covers the client).
void McpManager::retireClient(McpClient* client) {
    // Track the retirement so the aboutToQuit hook can reap clients whose
    // delayed delete never fired (app exited inside the window).
    m_retired.append(client);
    QTimer::singleShot(40000, client, [this, client]() {
        m_retired.removeAll(client);
        client->deleteLater();
    });
}

McpClient* McpManager::clientFor(const QString& name) const {
    for (McpClient* client : m_clients)
        if (client->serverName() == name) return client;
    return nullptr;
}

void McpManager::connectClient(McpClient* client) {
    // Tools register on the first successful handshake; crash restarts reuse
    // the already-registered adapters (same client, fresh transport).
    connect(client, &McpClient::connected, this, [this, client]() {
        const auto& tools = client->tools();
        // T14 (REQ-T14-05): per-server tool count cap (≤32).
        int registered = 0;
        for (const auto& tool : tools) {
            if (registered >= kMaxToolsPerServer) {
                qCWarning(lcMcp) << "Server" << client->serverName()
                                 << "exceeded per-server tool limit"
                                 << kMaxToolsPerServer << "- skipping"
                                 << (tools.size() - registered) << "tools";
                break;
            }
            ToolRegistry::instance().registerTool(
                std::make_unique<McpToolAdapter>(
                    client, client->serverName(), tool.name,
                    tool.description, tool.inputSchema));
            ++registered;
        }
        // T14 (REQ-T14-05): global MCP tool count cap (≤64).
        const QStringList allTools = ToolRegistry::instance().toolNamesForModel();
        int globalMcpTools = 0;
        for (const QString& n : allTools)
            if (n.startsWith(QStringLiteral("mcp__"))) ++globalMcpTools;
        if (globalMcpTools > kMaxGlobalMcpTools) {
            qCWarning(lcMcp) << "Global MCP tool count" << globalMcpTools
                             << "exceeds limit" << kMaxGlobalMcpTools;
        }
        refreshServers();
    });
    connect(client, &McpClient::disconnected, this, [this, client]() {
        for (const auto& tool : client->tools())
            ToolRegistry::instance().takeTool(
                McpToolAdapter::registryNameFor(client->serverName(), tool.name));
        refreshServers();
    });
    connect(client, &McpClient::connectionFailed, this,
            [this](const QString& error) {
        if (m_lastError != error) {
            m_lastError = error;
            emit lastErrorChanged(error);
        }
        refreshServers();
    });
    // Transport diagnostics (FailedToStart, server stderr, exit codes) flow
    // through `diagnostic`; mirror them to the log so users can see *why* a
    // connection failed even when the UI shows the summarized lastError.
    connect(client, &McpClient::diagnostic, this, [client](const QString& error) {
        qWarning().noquote()
            << "MCP server" << client->serverName() << ':' << error;
    });
    connect(client, &McpClient::toolsChanged, this,
            [this]() { refreshServers(); });
}

QVariantList McpManager::servers() const {
    QVariantList out;
    for (const QVariant& entry : m_configs) {
        const QVariantMap config = entry.toMap();
        const McpClient* client = clientFor(config.value("name").toString());

        QString endpoint;
        if (config.value("transport").toString() == QLatin1String("http"))
            endpoint = config.value("url").toString();
        else
            endpoint = config.value("command").toString()
                + QLatin1Char(' ') + config.value("args").toStringList()
                      .join(QLatin1Char(' '));

        QVariantMap item;
        item["name"] = config.value("name");
        item["transport"] = config.value("transport");
        item["endpoint"] = endpoint.trimmed();
        item["enabled"] = client != nullptr
            || config.value("enabled").toBool();
        item["state"] = static_cast<int>(
            client ? client->sessionState()
                   : McpClient::SessionState::Disconnected);
        item["toolCount"] = client ? client->tools().size() : 0;
        // 工具名预览：连接成功后卡片上直接看得到这台服务器贡献了哪些工具，
        // 不必去工具页逐个对来源。未连接的服务器给空列表。
        QStringList toolNames;
        if (client) {
            const auto tools = client->tools();
            toolNames.reserve(tools.size());
            for (const auto& tool : tools) toolNames.append(tool.name);
        }
        item["tools"] = toolNames;
        // 编辑回填：设置页需要原始字段，而不是拼好的 endpoint。
        item["command"] = config.value("command");
        item["args"] = config.value("args");
        item["url"] = config.value("url");
        item["env"] = config.value("env");
        item["headers"] = config.value("headers");
        out.append(item);
    }
    return out;
}

void McpManager::refreshServers() {
    emit serversChanged();
}

bool McpManager::addServer(const QVariantMap& server) {
    const QString name = server.value("name").toString().trimmed();
    if (name.isEmpty()) return false;
    if (clientFor(name) || std::any_of(m_configs.cbegin(), m_configs.cend(),
            [&name](const QVariant& entry) {
                return entry.toMap().value("name").toString() == name;
            })) {
        // 旧实现静默 return，设置页点「添加」毫无反馈；现在让调用方
        // 知道失败，页面据此给出字段级提示。
        m_lastError = tr("已存在名为「%1」的服务器").arg(name);
        emit lastErrorChanged(m_lastError);
        return false;
    }

    QVariantMap normalized = server;
    normalized["name"] = name;
    if (!normalized.contains("enabled"))
        normalized["enabled"] = true;

    m_configs.append(normalized);
    if (!McpConfig::save(m_configs)) {
        qCWarning(lcMcp) << "Failed to save MCP server configuration";
        m_lastError = tr("保存 MCP 服务器配置失败");
        emit lastErrorChanged(m_lastError);
    }
    Activity::usage(Activity::Kind::User, QStringLiteral("mcp.server.add"), name);

    if (!normalized.value("enabled").toBool()) {
        refreshServers();
        return true;
    }

    // Live-connect stdio servers added at runtime (HTTP included — same
    // factory switch as loadAndConnectAll).
    McpServerConfig config;
    config.name = name;
    config.transport = normalized.value("transport").toString();
    config.command = normalized.value("command").toString();
    for (const QVariant& arg : normalized.value("args").toList())
        config.args << arg.toString();
    config.env = normalized.value("env").toMap();
    config.url = normalized.value("url").toString();
    config.headers = normalized.value("headers").toMap();

    auto* client = new McpClient(config, this);
    m_clients.append(client);
    connectClient(client);
    client->connectToServer();
    refreshServers();
    return true;
}

bool McpManager::updateServer(const QString& originalName,
                              const QVariantMap& server) {
    int index = -1;
    for (int i = 0; i < m_configs.size(); ++i) {
        if (m_configs.at(i).toMap().value("name").toString() == originalName) {
            index = i;
            break;
        }
    }
    if (index < 0) return false;

    const QString name = server.value("name").toString().trimmed();
    if (name.isEmpty()) return false;
    if (name != originalName && clientFor(name)) {
        m_lastError = tr("已存在名为「%1」的服务器").arg(name);
        emit lastErrorChanged(m_lastError);
        return false;
    }
    for (int i = 0; i < m_configs.size(); ++i) {
        if (i != index
                && m_configs.at(i).toMap().value("name").toString() == name) {
            m_lastError = tr("已存在名为「%1」的服务器").arg(name);
            emit lastErrorChanged(m_lastError);
            return false;
        }
    }

    QVariantMap normalized = server;
    normalized["name"] = name;
    // 编辑不改变启用状态：开关留在卡片上，弹窗只改连接参数。
    normalized["enabled"] = m_configs.at(index).toMap()
        .value("enabled", true).toBool();

    // 先持久化再拆连接：写盘失败时保持现有客户端继续工作。
    QVariantList next = m_configs;
    next[index] = normalized;
    if (!McpConfig::save(next)) {
        qCWarning(lcMcp) << "Failed to save MCP server configuration";
        m_lastError = tr("保存 MCP 服务器配置失败");
        emit lastErrorChanged(m_lastError);
        return false;
    }
    m_configs = next;

    // 名称、命令、URL、env、headers 任一变化都要重建客户端：McpClient
    // 在构造时拍下 config，没有热更新接口。旧名的工具先注销，避免
    // rename 后注册表里残留 mcp__旧名__* 。
    if (McpClient* client = clientFor(originalName)) {
        for (const auto& tool : client->tools())
            ToolRegistry::instance().takeTool(
                McpToolAdapter::registryNameFor(client->serverName(), tool.name));
        client->disconnectFromServer();
        m_clients.removeAll(client);
        retireClient(client);
    }

    Activity::usage(Activity::Kind::User, QStringLiteral("mcp.server.update"), name);

    if (normalized.value("enabled").toBool()) {
        McpServerConfig config;
        config.name = name;
        config.transport = normalized.value("transport").toString();
        config.command = normalized.value("command").toString();
        for (const QVariant& arg : normalized.value("args").toList())
            config.args << arg.toString();
        config.env = normalized.value("env").toMap();
        config.url = normalized.value("url").toString();
        config.headers = normalized.value("headers").toMap();

        auto* fresh = new McpClient(config, this);
        m_clients.append(fresh);
        connectClient(fresh);
        fresh->connectToServer();
    }
    refreshServers();
    return true;
}

void McpManager::removeServer(const QString& name) {
    if (McpClient* client = clientFor(name)) {
        client->disconnectFromServer();
        // Unregister the tools; discarding the shared_ptr return is safe —
        // in-flight executes hold their own reference (docs/22 F1).
        for (const auto& tool : client->tools())
            ToolRegistry::instance().takeTool(
                McpToolAdapter::registryNameFor(client->serverName(), tool.name));
        m_clients.removeAll(client);
        retireClient(client);
    }
    for (int i = 0; i < m_configs.size(); ++i) {
        if (m_configs.at(i).toMap().value("name").toString() == name) {
            m_configs.removeAt(i);
            break;
        }
    }
    if (!McpConfig::save(m_configs)) {
        qCWarning(lcMcp) << "Failed to save MCP server configuration";
        m_lastError = tr("保存 MCP 服务器配置失败");
        emit lastErrorChanged(m_lastError);
    }
    Activity::usage(Activity::Kind::User, QStringLiteral("mcp.server.remove"),
                    name);
    refreshServers();
}

void McpManager::setEnabled(const QString& name, bool enabled) {
    McpClient* client = clientFor(name);
    for (int i = 0; i < m_configs.size(); ++i) {
        QVariantMap config = m_configs.at(i).toMap();
        if (config.value("name").toString() != name) continue;
        config["enabled"] = enabled;
        m_configs[i] = config;
        break;
    }
    if (!McpConfig::save(m_configs)) {
        qCWarning(lcMcp) << "Failed to save MCP server configuration";
        m_lastError = tr("保存 MCP 服务器配置失败");
        emit lastErrorChanged(m_lastError);
    }
    Activity::usage(Activity::Kind::User,
                    enabled ? QStringLiteral("mcp.server.enable")
                            : QStringLiteral("mcp.server.disable"), name);

    if (!enabled && client) {
        // Disable = unregister tools and drop the connection; the config
        // entry stays for re-enabling.
        for (const auto& tool : client->tools())
            ToolRegistry::instance().takeTool(
                McpToolAdapter::registryNameFor(client->serverName(), tool.name));
        client->disconnectFromServer();
        m_clients.removeAll(client);
        retireClient(client);
    } else if (enabled && !client) {
        // Re-enable: build the client from the persisted config entry.
        for (const QVariant& entry : m_configs) {
            const QVariantMap config = entry.toMap();
            if (config.value("name").toString() != name) continue;

            McpServerConfig mc;
            mc.name = name;
            mc.transport = config.value("transport").toString();
            mc.command = config.value("command").toString();
            for (const QVariant& arg : config.value("args").toList())
                mc.args << arg.toString();
            mc.env = config.value("env").toMap();
            mc.url = config.value("url").toString();
            mc.headers = config.value("headers").toMap();

            auto* fresh = new McpClient(mc, this);
            m_clients.append(fresh);
            connectClient(fresh);
            fresh->connectToServer();
            break;
        }
    }
    refreshServers();
}

void McpManager::reconnect(const QString& name) {
    if (McpClient* client = clientFor(name)) {
        client->disconnectFromServer();
        client->connectToServer();
    }
}

// ── docs/31 N7：配置导入/导出 ─────────────────────────────

QVariantMap McpManager::exportServers(const QString& path) {
    QVector<QVariantMap> servers;
    for (const QVariant& entry : std::as_const(m_configs)) {
        const QVariantMap config = entry.toMap();
        if (!config.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            servers.append(config);
    }
    if (servers.isEmpty()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("path"), path},
                {QStringLiteral("error"), tr("还没有可导出的 MCP 服务器")}};
    }

    QString error;
    if (!McpConfigTransfer::writeExport(path, servers, &error)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("path"), path},
                {QStringLiteral("error"), error}};
    }
    Activity::usage(Activity::Kind::User, QStringLiteral("mcp.config.export"),
                    QString::number(servers.size()));
    int secretsOmitted = 0;
    for (const QVariantMap& server : std::as_const(servers)) {
        if (!server.value(QStringLiteral("env")).toMap().isEmpty()
            || !server.value(QStringLiteral("headers")).toMap().isEmpty())
            ++secretsOmitted;
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("path"), path},
            {QStringLiteral("count"), servers.size()},
            {QStringLiteral("secretsOmitted"), secretsOmitted}};
}

QVariantMap McpManager::previewServerImport(const QString& path) const {
    QVector<QVariantMap> current;
    for (const QVariant& entry : m_configs)
        current.append(entry.toMap());
    const McpConfigTransfer::ImportPreview preview =
        McpConfigTransfer::previewImportFile(path, current);

    QVariantList rows;
    for (const McpConfigTransfer::ImportEntry& entry : preview.entries) {
        const QVariantMap server = entry.server;
        const QString transport = server.value(QStringLiteral("transport")).toString();
        const QString endpoint = transport == QLatin1String("http")
            ? server.value(QStringLiteral("url")).toString()
            : server.value(QStringLiteral("command")).toString() + QLatin1Char(' ')
                  + server.value(QStringLiteral("args")).toStringList().join(QLatin1Char(' '));
        rows.append(QVariantMap{
            {QStringLiteral("name"), server.value(QStringLiteral("name"))},
            {QStringLiteral("transport"), transport},
            {QStringLiteral("endpoint"), endpoint.trimmed()},
            {QStringLiteral("conflict"), entry.conflict},
            {QStringLiteral("error"), entry.error}});
    }
    return {{QStringLiteral("ok"), preview.ok},
            {QStringLiteral("path"), path},
            {QStringLiteral("error"), preview.error},
            {QStringLiteral("total"), preview.total},
            {QStringLiteral("importable"), preview.importable},
            {QStringLiteral("conflictCount"), preview.conflictCount},
            {QStringLiteral("withSecrets"), preview.withSecrets},
            {QStringLiteral("servers"), rows}};
}

QVariantMap McpManager::importServers(const QString& path, const QString& policy) {
    QVector<QVariantMap> current;
    QSet<QString> currentNames;
    for (const QVariant& entry : std::as_const(m_configs)) {
        const QVariantMap config = entry.toMap();
        current.append(config);
        currentNames.insert(config.value(QStringLiteral("name")).toString());
    }
    const McpConfigTransfer::ImportPreview preview =
        McpConfigTransfer::previewImportFile(path, current);
    if (!preview.ok) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), preview.error}};
    }
    if (preview.importable == 0) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), tr("文件里没有可导入的服务器")}};
    }

    int added = 0, replaced = 0, skipped = 0;
    const QVector<QVariantMap> merged = McpConfigTransfer::applyImport(
        current, preview, McpConfigTransfer::policyFromName(policy),
        &added, &replaced, &skipped);

    QVariantList next;
    for (const QVariantMap& server : merged)
        next.append(server);
    if (!McpConfig::save(next)) {
        m_lastError = tr("保存 MCP 服务器配置失败");
        emit lastErrorChanged(m_lastError);
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), m_lastError}};
    }
    m_configs = next;

    // 导入一律禁用；被覆盖/删除的原有服务器若还连着，必须同步下线，
    // 否则界面显示"已禁用"但工具仍在注册表里。
    QSet<QString> enabledNames;
    for (const QVariantMap& server : merged) {
        if (server.value(QStringLiteral("enabled")).toBool())
            enabledNames.insert(server.value(QStringLiteral("name")).toString());
    }
    QVector<McpClient*> drop;
    for (McpClient* client : std::as_const(m_clients)) {
        if (!enabledNames.contains(client->serverName())) drop.append(client);
    }
    for (McpClient* client : drop) {
        m_clients.removeAll(client);
        teardownClient(client);
    }

    Activity::usage(Activity::Kind::User, QStringLiteral("mcp.config.import"),
                    QString::number(added + replaced));
    refreshServers();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("added"), added},
            {QStringLiteral("replaced"), replaced},
            {QStringLiteral("skipped"), skipped}};
}

} // namespace NexusAI
