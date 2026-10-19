#include "NetworkProxyConfig.h"

#include <QMutexLocker>

namespace NexusAI {

NetworkProxyConfig::Mode NetworkProxyConfig::modeFromString(const QString& value) {
    if (value == QLatin1String("manual")) return Mode::Manual;
    if (value == QLatin1String("off")) return Mode::Off;
    return Mode::System;
}

QString NetworkProxyConfig::modeToString(Mode mode) {
    switch (mode) {
        case Mode::Manual: return QStringLiteral("manual");
        case Mode::Off:    return QStringLiteral("off");
        case Mode::System: break;
    }
    return QStringLiteral("system");
}

bool NetworkProxyConfig::manualUsable() const {
    return !host.trimmed().isEmpty() && port > 0 && port <= 65535;
}

void ConfigurableProxyFactory::setConfig(const NetworkProxyConfig& config) {
    QMutexLocker locker(&m_mutex);
    m_config = config;
}

NetworkProxyConfig ConfigurableProxyFactory::config() const {
    QMutexLocker locker(&m_mutex);
    return m_config;
}

bool ConfigurableProxyFactory::isLoopbackHost(const QString& host) {
    const QString h = host.trimmed().toLower();
    return h == QLatin1String("localhost")
        || h == QLatin1String("127.0.0.1")
        || h == QLatin1String("::1")
        || h == QLatin1String("[::1]")
        || h.endsWith(QLatin1String(".localhost"));
}

QVector<QNetworkProxy> ConfigurableProxyFactory::queryProxy(
    const QNetworkProxyQuery& query) {
    if (isLoopbackHost(query.url().host()))
        return {QNetworkProxy(QNetworkProxy::NoProxy)};

    const NetworkProxyConfig config = this->config();
    switch (config.mode) {
        case NetworkProxyConfig::Mode::Off:
            return {QNetworkProxy(QNetworkProxy::NoProxy)};

        case NetworkProxyConfig::Mode::Manual: {
            if (!config.manualUsable())
                return {QNetworkProxy(QNetworkProxy::NoProxy)};
            QNetworkProxy proxy(QNetworkProxy::HttpProxy, config.host.trimmed(),
                                static_cast<quint16>(config.port));
            if (!config.username.isEmpty()) {
                proxy.setUser(config.username);
                proxy.setPassword(config.password);
            }
            return {proxy};
        }

        case NetworkProxyConfig::Mode::System:
            break;
    }

    const auto proxies = systemProxyForQuery(query);
    if (proxies.isEmpty())
        return {QNetworkProxy(QNetworkProxy::NoProxy)};
    return proxies;
}

} // namespace NexusAI
