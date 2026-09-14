#include "viewmodels/McpManager.h"

#include "core/mcp/McpConfig.h"
#include "core/mcp/McpToolAdapter.h"
#include "core/tools/ToolRegistry.h"
#include "core/utils/Activity.h"

namespace NexusAI {

namespace {
constexpr int StateConnected = 2;    // McpClient::SessionState::Connected
constexpr int StateFailed = 3;
} // namespace

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
    : QObject(parent) {}

void McpManager::loadAndConnectAll() {
    const auto configs = McpConfig::load();
    for (const QVariantMap& server : configs) {
        m_configs.append(server);
        if (!server.value("enabled").toBool()) continue;
        if (server.value("transport").toString() == QLatin1String("http"))
            continue;   // 31b HTTP clients are created the same way below

        McpServerConfig config;
        config.name = server.value("name").toString();
        config.transport = QStringLiteral("stdio");
        config.command = server.value("command").toString();
        for (const QVariant& arg : server.value("args").toList())
            config.args << arg.toString();
        config.env = server.value("env").toMap();
        auto* client = new McpClient(config, this);
        m_clients.append(client);
        connectClient(client);
        client->connectToServer();
    }
    refreshServers();
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
        for (const auto& tool : client->tools())
            ToolRegistry::instance().registerTool(
                std::make_unique<McpToolAdapter>(
                    client, client->serverName(), tool.name,
                    tool.description, tool.inputSchema));
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
        out.append(item);
    }
    return out;
}

void McpManager::refreshServers() {
    emit serversChanged();
}

void McpManager::addServer(const QVariantMap& server) {
    const QString name = server.value("name").toString().trimmed();
    if (name.isEmpty() || clientFor(name)) return;

    QVariantMap normalized = server;
    normalized["name"] = name;
    if (!normalized.contains("enabled"))
        normalized["enabled"] = true;

    m_configs.append(normalized);
    McpConfig::save(m_configs);
    Activity::usage(Activity::Kind::User, QStringLiteral("mcp.server.add"), name);

    if (!normalized.value("enabled").toBool()) {
        refreshServers();
        return;
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
}

void McpManager::removeServer(const QString& name) {
    if (McpClient* client = clientFor(name)) {
        client->disconnectFromServer();
        // takeTool first (disconnected already did), then drop the client.
        for (const auto& tool : client->tools())
            ToolRegistry::instance().takeTool(
                McpToolAdapter::registryNameFor(client->serverName(), tool.name));
        m_clients.removeAll(client);
        client->deleteLater();
    }
    for (int i = 0; i < m_configs.size(); ++i) {
        if (m_configs.at(i).toMap().value("name").toString() == name) {
            m_configs.removeAt(i);
            break;
        }
    }
    McpConfig::save(m_configs);
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
    McpConfig::save(m_configs);
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
        client->deleteLater();
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

} // namespace NexusAI
