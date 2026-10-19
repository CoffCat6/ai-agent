// H1: HTTP proxy behavior.
//
// Covers the three modes at the factory level, a real request routed through
// a local HTTP proxy (including Proxy-Authorization), readable failure when
// the proxy is dead, and the SettingsService probe the UI button calls.

#include "core/utils/NetworkProxyConfig.h"
#include "viewmodels/SettingsService.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

using namespace NexusAI;

namespace {

class ProxySettingsGuard {
public:
    ProxySettingsGuard() {
        QSettings s(QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
        m_mode = s.value("network/proxyMode");
        m_host = s.value("network/proxyHost");
        m_port = s.value("network/proxyPort");
        m_auth = s.value("network/proxyAuth");
    }
    ~ProxySettingsGuard() {
        QSettings s(QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
        m_mode.isValid() ? s.setValue("network/proxyMode", m_mode)
                         : s.remove("network/proxyMode");
        m_host.isValid() ? s.setValue("network/proxyHost", m_host)
                         : s.remove("network/proxyHost");
        m_port.isValid() ? s.setValue("network/proxyPort", m_port)
                         : s.remove("network/proxyPort");
        m_auth.isValid() ? s.setValue("network/proxyAuth", m_auth)
                         : s.remove("network/proxyAuth");
    }
private:
    QVariant m_mode, m_host, m_port, m_auth;
};

struct ProxyState {
    QString buffer;
    bool sawAbsoluteUrl{false};
    bool sawAuth{false};
    int requests{0};
};

// Minimal HTTP proxy: requires Proxy-Authorization, then answers 200 "ok"
// without ever resolving the target host. Returns the actual port.
quint16 startFakeProxy(QTcpServer* server, QSharedPointer<ProxyState> state) {
    if (!server->listen(QHostAddress::LocalHost, 0)) return 0;
    QObject::connect(server, &QTcpServer::newConnection, server,
                     [server, state]() {
        while (QTcpSocket* socket = server->nextPendingConnection()) {
            auto* buffer = new QByteArray;
            QObject::connect(socket, &QObject::destroyed, socket,
                             [buffer]() { delete buffer; });
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, buffer, state]() {
                buffer->append(socket->readAll());
                int end = 0;
                while ((end = buffer->indexOf("\r\n\r\n")) >= 0) {
                    const QByteArray head = buffer->left(end);
                    buffer->remove(0, end + 4);
                    ++state->requests;
                    const QByteArray requestLine = head.left(head.indexOf("\r\n"));
                    if (requestLine.contains("example.invalid"))
                        state->sawAbsoluteUrl = true;
                    const bool hasAuth = head.toLower().contains(
                        "\r\nproxy-authorization:");
                    state->sawAuth = state->sawAuth || hasAuth;
                    if (hasAuth) {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                                      "Connection: keep-alive\r\n\r\nok");
                    } else {
                        // 407 first: exercises the credential retry path.
                        socket->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                                      "Proxy-Authenticate: Basic realm=\"test\"\r\n"
                                      "Content-Length: 0\r\nConnection: keep-alive\r\n\r\n");
                    }
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket,
                             &QObject::deleteLater);
        }
    });
    return server->serverPort();
}

quint16 unusedLoopbackPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

NetworkProxyConfig manualConfig(const QString& host, int port,
                                const QString& user = {},
                                const QString& password = {}) {
    NetworkProxyConfig config;
    config.mode = NetworkProxyConfig::Mode::Manual;
    config.host = host;
    config.port = port;
    config.username = user;
    config.password = password;
    return config;
}

} // namespace

class TestNetworkProxy : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
        // Application-wide factory, exactly as main() installs it.
        m_factory = new ConfigurableProxyFactory;
        QNetworkProxyFactory::setApplicationProxyFactory(m_factory);
    }

    void cleanupTestCase() {
        QNetworkProxyFactory::setApplicationProxyFactory(nullptr);
        m_factory = nullptr;
    }

    void modeStringsRoundTrip() {
        for (const auto& pair : QList<QPair<NetworkProxyConfig::Mode, QString>>{
                 {NetworkProxyConfig::Mode::System, QStringLiteral("system")},
                 {NetworkProxyConfig::Mode::Manual, QStringLiteral("manual")},
                 {NetworkProxyConfig::Mode::Off, QStringLiteral("off")}}) {
            QCOMPARE(NetworkProxyConfig::modeToString(pair.first), pair.second);
            QCOMPARE(static_cast<int>(NetworkProxyConfig::modeFromString(pair.second)),
                     static_cast<int>(pair.first));
        }
        QCOMPARE(static_cast<int>(NetworkProxyConfig::modeFromString(QStringLiteral("bogus"))),
                 static_cast<int>(NetworkProxyConfig::Mode::System));
    }

    void factoryResolvesEachMode() {
        ConfigurableProxyFactory factory;
        const QUrl remote(QStringLiteral("https://api.example.com/v1/chat"));

        factory.setConfig(manualConfig(QStringLiteral("proxy.example"), 3128,
                                       QStringLiteral("alice"),
                                       QStringLiteral("secret")));
        auto manual = factory.queryProxy(QNetworkProxyQuery(remote));
        QCOMPARE(manual.size(), 1);
        QCOMPARE(manual.first().type(), QNetworkProxy::HttpProxy);
        QCOMPARE(manual.first().hostName(), QStringLiteral("proxy.example"));
        QCOMPARE(manual.first().port(), quint16(3128));
        QCOMPARE(manual.first().user(), QStringLiteral("alice"));
        QCOMPARE(manual.first().password(), QStringLiteral("secret"));

        // Local backends never take the proxy, even in manual mode.
        auto local = factory.queryProxy(
            QNetworkProxyQuery(QUrl(QStringLiteral("http://localhost:11434/api"))));
        QCOMPARE(local.first().type(), QNetworkProxy::NoProxy);

        NetworkProxyConfig off;
        off.mode = NetworkProxyConfig::Mode::Off;
        off.host = QStringLiteral("proxy.example");
        off.port = 3128;
        factory.setConfig(off);
        QCOMPARE(factory.queryProxy(QNetworkProxyQuery(remote)).first().type(),
                 QNetworkProxy::NoProxy);

        // Manual with no host/port must not invent a proxy.
        factory.setConfig(manualConfig(QString(), 0));
        QCOMPARE(factory.queryProxy(QNetworkProxyQuery(remote)).first().type(),
                 QNetworkProxy::NoProxy);

        NetworkProxyConfig system;
        system.mode = NetworkProxyConfig::Mode::System;
        factory.setConfig(system);
        QVERIFY(!factory.queryProxy(QNetworkProxyQuery(remote)).isEmpty());
    }

    void realRequestIsRoutedThroughProxyWithCredentials() {
        QSharedPointer<ProxyState> state(new ProxyState);
        QTcpServer proxy;
        const quint16 port = startFakeProxy(&proxy, state);
        QVERIFY(port != 0);

        m_factory->setConfig(manualConfig(QStringLiteral("127.0.0.1"), port,
                                          QStringLiteral("alice"),
                                          QStringLiteral("wonder")));

        QNetworkAccessManager nam;
        QNetworkRequest request(QUrl(QStringLiteral("http://example.invalid/probe")));
        request.setTransferTimeout(5000);
        QNetworkReply* reply = nam.get(request);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY2(finished.wait(6000), "request did not finish (proxy hang)");

        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(reply->readAll(), QByteArray("ok"));
        QVERIFY2(state->sawAbsoluteUrl,
                 "proxy did not receive an absolute-form request");
        QVERIFY2(state->sawAuth,
                 "Proxy-Authorization was not sent after the 407 challenge");
        reply->deleteLater();
    }

    void deadProxyFailsFastWithReadableError() {
        const quint16 port = unusedLoopbackPort();
        QVERIFY(port != 0);
        m_factory->setConfig(manualConfig(QStringLiteral("127.0.0.1"), port));

        QNetworkAccessManager nam;
        QNetworkRequest request(QUrl(QStringLiteral("http://example.invalid/probe")));
        request.setTransferTimeout(5000);
        QElapsedTimer clock;
        clock.start();
        QNetworkReply* reply = nam.get(request);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY2(finished.wait(6000), "dead proxy looked like a hang");

        QVERIFY2(clock.elapsed() < 5000, "failure was not fast");
        QVERIFY(reply->error() != QNetworkReply::NoError);
        QVERIFY(!reply->errorString().isEmpty());
        // A proxy failure - not a DNS failure of the target host.
        QVERIFY(reply->error() == QNetworkReply::ProxyConnectionRefusedError
                || reply->error() == QNetworkReply::ConnectionRefusedError
                || reply->error() == QNetworkReply::ProxyTimeoutError
                || reply->error() == QNetworkReply::TimeoutError);
        reply->deleteLater();
    }

    void settingsProbeReportsSuccessAndFailure() {
        ProxySettingsGuard guard;
        QSharedPointer<ProxyState> state(new ProxyState);
        QTcpServer proxy;
        const quint16 port = startFakeProxy(&proxy, state);
        QVERIFY(port != 0);

        SettingsService service(nullptr);
        service.setProxyMode(QStringLiteral("manual"));
        service.setProxyHost(QStringLiteral("127.0.0.1"));
        service.setProxyPort(port);
        service.setProxyUsername(QStringLiteral("alice"));
        service.setProxyPassword(QStringLiteral("wonder"));

        {
            QSignalSpy finished(&service, &SettingsService::proxyTestFinished);
            service.testProxyUrl(QStringLiteral("http://example.invalid/probe"));
            QVERIFY(service.proxyTesting());
            QVERIFY2(finished.wait(6000), "proxy probe did not finish");
            QCOMPARE(finished.count(), 1);
            QCOMPARE(finished.at(0).at(0).toBool(), true);
            QVERIFY2(finished.at(0).at(1).toString().contains(QStringLiteral("HTTP 200")),
                     qPrintable(finished.at(0).at(1).toString()));
            QVERIFY(!service.proxyTesting());
            QVERIFY(service.proxyTestOk());
        }

        proxy.close();
        {
            // A fresh instance/NAM: no keep-alive connection to the fake proxy
            // can mask the failure.
            SettingsService deadService(nullptr);
            deadService.setProxyMode(QStringLiteral("manual"));
            deadService.setProxyHost(QStringLiteral("127.0.0.1"));
            deadService.setProxyPort(unusedLoopbackPort());

            QSignalSpy finished(&deadService, &SettingsService::proxyTestFinished);
            deadService.testProxyUrl(QStringLiteral("http://example.invalid/probe"));
            QVERIFY2(finished.wait(6000), "dead proxy probe did not finish");
            QCOMPARE(finished.count(), 1);
            QCOMPARE(finished.at(0).at(0).toBool(), false);
            const QString message = finished.at(0).at(1).toString();
            QVERIFY2(message.startsWith(QStringLiteral("代理测试失败")),
                     qPrintable(message));
            QVERIFY(!deadService.proxyTestOk());
        }
    }

private:
    ConfigurableProxyFactory* m_factory{nullptr};
};

QTEST_GUILESS_MAIN(TestNetworkProxy)
#include "test_network_proxy.moc"
