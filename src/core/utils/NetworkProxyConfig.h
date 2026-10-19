#pragma once

#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QMutex>
#include <QString>
#include <QVector>

namespace NexusAI {

// Snapshot of the user's proxy settings. Kept free of SettingsService so the
// factory can be unit-tested and queried from worker threads (http_fetch runs
// on the QtConcurrent pool).
struct NetworkProxyConfig {
    enum class Mode {
        System,   // follow the OS / environment configuration
        Manual,   // explicit HTTP proxy (host + port + optional credentials)
        Off       // never use a proxy
    };

    Mode mode{Mode::System};
    QString host;
    int port{0};
    QString username;
    QString password;

    static Mode modeFromString(const QString& value);
    static QString modeToString(Mode mode);
    bool manualUsable() const;
};

// Application-wide proxy factory. Installed once by main(); config changes
// reach it through setConfig() and affect every QNetworkAccessManager that
// keeps the default (LLM, embedding, web search, MCP HTTP, http_fetch).
//
// Loopback targets always bypass the proxy: local Ollama / gateways break
// when their traffic is routed through a proxy. In manual mode a proxy that
// is itself on loopback is fine - the check is on the target host.
class ConfigurableProxyFactory : public QNetworkProxyFactory {
public:
    ConfigurableProxyFactory() = default;

    void setConfig(const NetworkProxyConfig& config);
    NetworkProxyConfig config() const;

    QVector<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override;

    static bool isLoopbackHost(const QString& host);

private:
    mutable QMutex m_mutex;
    NetworkProxyConfig m_config;
};

} // namespace NexusAI
