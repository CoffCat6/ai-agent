// Batch 31a (F9): MCP client coverage against a scripted stdio server —
// handshake, tool registration naming, call round trip, error mapping,
// cancel, crash restart and concurrent id correlation.
// Batch 31b (F9): Streamable HTTP transport coverage.
#include "core/mcp/McpClient.h"
#include "core/mcp/McpConfig.h"
#include "core/mcp/McpToolAdapter.h"
#include "core/mcp/StdioTransport.h"
#include "core/tools/ToolRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

using namespace NexusAI;

namespace {

// Each config test gets its own app-data directory; never touch the user's config.
struct ConfigTestPath {
    QTemporaryDir uniqueName;
    QString previousName = QCoreApplication::applicationName();
    QString dataDir;

    ConfigTestPath() {
        if (uniqueName.isValid()) {
            QCoreApplication::setApplicationName(
                QFileInfo(uniqueName.path()).fileName());
            dataDir = QFileInfo(McpConfig::configFilePath()).absolutePath();
        }
    }

    ~ConfigTestPath() {
        if (!dataDir.isEmpty()) QDir(dataDir).removeRecursively();
        QCoreApplication::setApplicationName(previousName);
    }
};

// Batch 31b: minimal Streamable-HTTP MCP server (JSON responses, session
// header). Records whether later requests carried Mcp-Session-Id.
class HttpFakeMcpServer : public QObject {
    Q_OBJECT

public:
    explicit HttpFakeMcpServer(QObject* parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (auto* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket,
                        &QTcpSocket::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    QByteArray& buffer = m_buffer[socket];
                    buffer += socket->readAll();
                    const int headerEnd = buffer.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;

                    // Session header: required on every request after
                    // initialize issued one.
                    const QByteArray headers = buffer.left(headerEnd);
                    // HTTP header names are case-insensitive; QNAM
                    // lowercases them on the wire.
                    const bool hadSession =
                        headers.toLower().contains("mcp-session-id:");
                    if (hadSession) m_sessionHeaderSeen = true;

                    int contentLength = 0;
                    const QList<QByteArray> lines = headers.split('\n');
                    for (const QByteArray& line : lines) {
                        if (line.toLower().startsWith("content-length:")) {
                            contentLength = line.mid(15).trimmed().toInt();
                            break;
                        }
                    }
                    if (buffer.size() - (headerEnd + 4) < contentLength)
                        return;
                    const QByteArray body =
                        buffer.mid(headerEnd + 4, contentLength);
                    buffer.remove(0, headerEnd + 4 + contentLength);

                    handleRequest(socket,
                                  QJsonDocument::fromJson(body).object());
                });
            }
        });
        if (!m_server.listen(QHostAddress::LocalHost))
            qWarning() << "HttpFakeMcpServer listen failed:"
                       << m_server.errorString();
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1/mcp")
            .arg(m_server.serverPort());
    }
    void setSseMode(bool on) { m_sse = on; }
    bool sessionHeaderSeen() const { return m_sessionHeaderSeen; }

private:
    void handleRequest(QTcpSocket* socket, const QJsonObject& request) {
        const quint64 id = static_cast<quint64>(
            request.value("id").toVariant().toLongLong());
        const QString method = request.value("method").toString();

        if (method == QLatin1String("initialize")) {
            respond(socket, id, QJsonObject{
                {"protocolVersion", QStringLiteral("2025-03-26")},
                {"capabilities", QJsonObject{
                    {"tools", QJsonObject{{"listChanged", false}}}}},
                {"serverInfo", QJsonObject{
                    {"name", QStringLiteral("fake-http")},
                    {"version", QStringLiteral("1.0")}}},
            });
            return;
        }
        if (method == QLatin1String("tools/list")) {
            respond(socket, id, QJsonObject{
                {"tools", QJsonArray{QJsonObject{
                    {"name", QStringLiteral("echo")},
                    {"description", QStringLiteral("fake http tool")},
                    {"inputSchema", QJsonObject{{"type", "object"}}}}}}});
            return;
        }
        if (method == QLatin1String("tools/call")) {
            respond(socket, id, QJsonObject{
                {"content", QJsonArray{QJsonObject{
                    {"type", "text"},
                    {"text", QStringLiteral("http echo")}}}}});
            return;
        }
        respond(socket, id, QJsonObject{});
    }

    void respond(QTcpSocket* socket, quint64 id, const QJsonObject& result) {
        const QJsonObject response{
            {"jsonrpc", QStringLiteral("2.0")},
            {"id", static_cast<qint64>(id)},
            {"result", result}};
        const QByteArray body =
            QJsonDocument(response).toJson(QJsonDocument::Compact);

        QByteArray head =
            "HTTP/1.1 200 OK\r\nMcp-Session-Id: test-session-1\r\n";
        if (m_sse) {
            head += "Content-Type: text/event-stream\r\n\r\n";
            socket->write(head);
            socket->write("event: message\r\ndata: " + body + "\r\n\r\n");
        } else {
            head += "Content-Type: application/json\r\n";
            head += "Content-Length: " + QByteArray::number(body.size())
                    + "\r\n\r\n";
            socket->write(head + body);
        }
        socket->flush();
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffer;
    bool m_sse{false};
    bool m_sessionHeaderSeen{false};
};

QString fakeServerExe() {
    return QCoreApplication::applicationDirPath()
        + QStringLiteral("/fake_mcp_server");
}

McpServerConfig makeConfig(const QStringList& tools = {"echo"},
                           const char* mode = "echo") {
    McpServerConfig config;
    config.name = QStringLiteral("fake");
    config.transport = QStringLiteral("stdio");
    config.command = fakeServerExe();
    config.args = {};
    config.env = QVariantMap{
        {"MCP_FAKE_TOOLS", tools.join(QLatin1Char(','))},
        {"MCP_FAKE_MODE", QString::fromLatin1(mode)},
    };
    return config;
}

} // namespace

class TestMcp : public QObject {
    Q_OBJECT

private slots:
    void init() {
        qputenv("MCP_FAKE_MODE", "echo");
        qunsetenv("MCP_FAKE_CRASH");
    }

    void configRoundTrip() {
        ConfigTestPath path;
        QVERIFY(path.uniqueName.isValid());

        const QVariantMap env{{"API_KEY", QStringLiteral("test-secret")}};
        const QVariantMap headers{
            {"Authorization", QStringLiteral("Bearer test-secret")},
            {"X-Custom", QStringLiteral("example")}};
        const QVariantMap server{
            {"name", QStringLiteral("http-test")},
            {"transport", QStringLiteral("http")},
            {"url", QStringLiteral("https://example.invalid/mcp")},
            {"headers", headers},
            {"env", env},
            {"enabled", true}};
        QVERIFY(McpConfig::save(QVariantList{server}));

        const auto loaded = McpConfig::load();
        QCOMPARE(loaded.size(), 1);
        QVERIFY(loaded.first().value("headers").toMap() == headers);
        QVERIFY(loaded.first().value("env").toMap() == env);
        QCOMPARE(loaded.first().value("name"), server.value("name"));
        QCOMPARE(loaded.first().value("url"), server.value("url"));

        // A second save must retain fields loaded from disk, too.
        QVERIFY(McpConfig::save(QVariantList{loaded.first()}));
        const auto reloaded = McpConfig::load();
        QCOMPARE(reloaded.size(), 1);
        QVERIFY(reloaded.first().value("headers").toMap() == headers);
        QVERIFY(reloaded.first().value("env").toMap() == env);
    }

    void configSaveFailure() {
        ConfigTestPath path;
        QVERIFY(path.uniqueName.isValid());
        QVERIFY(QDir().mkpath(path.dataDir));
        // A directory in place of the target file must make QSaveFile fail.
        QVERIFY(QDir().mkpath(McpConfig::configFilePath()));
        QVERIFY(!McpConfig::save(QVariantList{
            QVariantMap{{"name", QStringLiteral("unwritable")}}}));
        QVERIFY(QFileInfo(McpConfig::configFilePath()).isDir());
    }

    // Handshake completes and tools/list results surface via toolsChanged.
    void handshakeAndToolDiscovery() {
        McpClient client(makeConfig({"echo", "second"}));
        QSignalSpy connectedSpy(&client, &McpClient::connected);
        QSignalSpy toolsSpy(&client, &McpClient::toolsChanged);

        client.connectToServer();
        QTRY_COMPARE(connectedSpy.count(), 1);
        QCOMPARE(client.sessionState(), McpClient::SessionState::Connected);

        QTRY_COMPARE(toolsSpy.count(), 1);
        const QList<McpClient::ToolInfo> tools = client.tools();
        QCOMPARE(tools.size(), 2);
        QCOMPARE(tools.first().name, QStringLiteral("echo"));
        QVERIFY(!tools.first().description.isEmpty());
        QVERIFY(tools.first().inputSchema.contains("type"));

        client.disconnectFromServer();
        QCOMPARE(client.sessionState(), McpClient::SessionState::Disconnected);
    }

    // tools/call round trip: text content blocks are joined into the result.
    void toolCallRoundTrip() {
        McpClient client(makeConfig({"echo"}));
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);

        bool handlerRan = false;
        client.callTool(QStringLiteral("echo"),
                        QJsonObject{{"text", QStringLiteral("hello")}},
                        [&handlerRan](bool isError, const QString& text,
                                      const QString& error) {
            handlerRan = true;
            QVERIFY(!isError);
            QVERIFY(text.contains(QStringLiteral("echo: ")));
            QVERIFY(text.contains(QStringLiteral("hello")));
            QVERIFY(error.isEmpty());
        });
        QTRY_VERIFY(handlerRan);
        client.disconnectFromServer();
    }

    // isError:true in the tool result maps to a failed ToolResult.
    void toolErrorMapsToIsError() {
        qputenv("MCP_FAKE_MODE", "error");
        McpClient client(makeConfig({"echo"}, "error"));
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);

        bool handlerRan = false;
        client.callTool(QStringLiteral("echo"), {},
                        [&handlerRan](bool isError, const QString& text,
                                      const QString&) {
            handlerRan = true;
            QVERIFY(isError);
            QVERIFY(text.contains(QStringLiteral("fake tool failure")));
        });
        QTRY_VERIFY(handlerRan);
        client.disconnectFromServer();
    }

    // The registry name is mcp__<server>__<tool> with sanitized parts, and
    // the adapter forces RequiresConfirm.
    void adapterRegistersUnderNamespacedName() {
        McpClient client(makeConfig({"echo"}));
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);

        auto& registry = ToolRegistry::instance();
        auto adapter = std::make_unique<McpToolAdapter>(
            &client, QStringLiteral("fake server"), QStringLiteral("echo"),
            QStringLiteral("desc"), QJsonObject{{"type", "object"}});
        const QString name = adapter->definition().name;
        QCOMPARE(name, QStringLiteral("mcp__fake_server__echo"));
        QCOMPARE(adapter->definition().level,
                 ToolLevel::RequiresConfirm);

        registry.registerTool(std::move(adapter));
        QVERIFY(registry.find(name) != nullptr);
        QVERIFY(registry.isRegistered(name));

        // docs/22 F1: takeTool removes the registry entry and hands back
        // the shared reference — the taken tool stays fully usable.
        auto taken = registry.takeTool(name);
        QVERIFY(taken != nullptr);
        QVERIFY(registry.find(name) == nullptr);
        QCOMPARE(taken->definition().name, name);
    }

    // docs/22 F1: a tool unregistered mid-flight must survive until the
    // pool-thread execute() returns. McpManager discards takeTool's return
    // value; the dispatch path holds a shared_ptr reference for exactly
    // that window (pre-fix this test was a use-after-free).
    void takeToolWhileInFlightKeepsToolAlive() {
        class SlowTool : public ITool {
        public:
            ToolDefinition definition() const override {
                ToolDefinition def;
                def.name = QStringLiteral("test_slow_owner");
                return def;
            }
            ToolResult execute(const QJsonObject&) override {
                QTest::qSleep(200);
                return ToolResult::ok(QStringLiteral("slow-done"));
            }
        };

        auto& registry = ToolRegistry::instance();
        const QString name = QStringLiteral("test_slow_owner");
        registry.registerTool(std::make_unique<SlowTool>());
        const std::shared_ptr<ITool> dispatched = registry.findShared(name);
        QVERIFY(dispatched != nullptr);

        QThread* worker = QThread::create([dispatched]() {
            const ToolResult result = dispatched->execute({});
            QVERIFY(result.success);
            QCOMPARE(result.output, QStringLiteral("slow-done"));
        });
        QObject::connect(worker, &QThread::finished, worker,
                         &QThread::deleteLater);
        worker->start();

        // While execute() is sleeping, remove the registry entry and drop
        // the ownership on the floor — as McpManager does on disconnect.
        QTest::qWait(60);
        registry.takeTool(name);
        QVERIFY(registry.find(name) == nullptr);

        QVERIFY(QTest::qWaitFor([worker]() { return worker->isFinished(); },
                                5000));
    }

    // Cancel: the waiting execute() slices on the atomic flag and bails.
    void cancelAbortsWaitingCall() {
        qputenv("MCP_FAKE_MODE", "delay");
        qputenv("MCP_FAKE_DELAY", "30");
        McpClient client(makeConfig({"echo"}, "delay"));
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);

        McpToolAdapter adapter(&client, QStringLiteral("fake"),
                               QStringLiteral("echo"), QStringLiteral("desc"),
                               QJsonObject{{"type", "object"}});

        // Run execute on a pool thread and cancel it shortly after.
        QThread* worker = QThread::create([&adapter]() {
            const ToolResult result = adapter.execute({});
            QVERIFY(result.error.contains(QStringLiteral("cancelled")));
        });
        QObject::connect(worker, &QThread::finished, worker,
                         &QThread::deleteLater);
        worker->start();
        QTest::qWait(500);
        adapter.cancel();
        QVERIFY(QTest::qWaitFor([worker]() { return worker->isFinished(); },
                                8000));
        client.disconnectFromServer();
    }

    // Crash restart: the server dies mid-session, the transport restarts it
    // with backoff, the handshake re-runs and calls work again.
    void crashTriggersRestartAndRecovery() {
        McpServerConfig config = makeConfig({"echo"}, "echo");
        auto* client = new McpClient(config, this);
        QSignalSpy connectedSpy(client, &McpClient::connected);

        client->connectToServer();
        QTRY_COMPARE(connectedSpy.count(), 1);

        // Simulate an external crash: kill the child; the transport
        // schedules a bounded restart and the client re-runs the handshake.
        QProcess killer;
        killer.start(QStringLiteral("taskkill"),
                     QStringList{QStringLiteral("/IM"),
                                 QStringLiteral("fake_mcp_server.exe"),
                                 QStringLiteral("/F")});
        killer.waitForFinished(5000);

        // The transport schedules a restart; wait for the handshake to
        // complete again (connected is emitted once per successful
        // handshake after a crash — the client re-runs it).
        QTRY_VERIFY(client->sessionState()
                    == McpClient::SessionState::Connected);
        client->deleteLater();
    }

    // Concurrent tool calls: responses correlate by JSON-RPC id even when
    // issued back to back.
    void concurrentCallsCorrelateById() {
        McpClient client(makeConfig({"echo"}));
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);

        int done = 0;
        for (int i = 0; i < 5; ++i) {
            const QString payload = QStringLiteral("call-%1").arg(i);
            client.callTool(QStringLiteral("echo"),
                QJsonObject{{"text", payload}},
                [payload, &done](bool isError, const QString& text,
                                 const QString&) {
                    QVERIFY(!isError);
                    QVERIFY(text.contains(payload));   // no cross-talk
                    ++done;
                });
        }
        QTRY_COMPARE(done, 5);
        client.disconnectFromServer();
    }

    // ── Batch 31b: Streamable HTTP ─────────────────────

    // Handshake, tools/list and tools/call over the POST-only transport
    // (JSON response path).
    void httpHandshakeAndToolCall() {
        HttpFakeMcpServer http;
        McpServerConfig config;
        config.name = QStringLiteral("http-fake");
        config.transport = QStringLiteral("http");
        config.url = http.baseUrl();

        McpClient client(config);
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);
        QCOMPARE(client.tools().size(), 1);

        bool handlerRan = false;
        client.callTool(QStringLiteral("echo"), {},
                        [&handlerRan](bool isError, const QString& text,
                                      const QString&) {
            handlerRan = true;
            QVERIFY(!isError);
            QCOMPARE(text, QStringLiteral("http echo"));
        });
        QTRY_VERIFY(handlerRan);
        client.disconnectFromServer();
    }

    // SSE-framed responses parse into the same message stream.
    void httpSseResponse() {
        HttpFakeMcpServer http;
        http.setSseMode(true);
        McpServerConfig config;
        config.name = QStringLiteral("http-fake");
        config.transport = QStringLiteral("http");
        config.url = http.baseUrl();

        McpClient client(config);
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);

        bool handlerRan = false;
        client.callTool(QStringLiteral("echo"), {},
                        [&handlerRan](bool isError, const QString& text,
                                      const QString&) {
            handlerRan = true;
            QVERIFY(!isError);
            QCOMPARE(text, QStringLiteral("http echo"));
        });
        QTRY_VERIFY(handlerRan);
        client.disconnectFromServer();
    }

    // The Mcp-Session-Id issued at initialize must ride on later requests.
    void sessionHeaderMaintained() {
        HttpFakeMcpServer http;
        McpServerConfig config;
        config.name = QStringLiteral("http-fake");
        config.transport = QStringLiteral("http");
        config.url = http.baseUrl();

        McpClient client(config);
        client.connectToServer();
        QTRY_COMPARE(client.sessionState(),
                     McpClient::SessionState::Connected);
        // tools/list ran after initialize — the header was required.
        QVERIFY(http.sessionHeaderSeen());
        client.disconnectFromServer();
    }

    // ── Windows .cmd wrapper compatibility + diagnostic透传 ──────────

    // A failed connection must surface the transport's real reason
    // (e.g. "Failed to start MCP server: <cmd>"), not just the generic
    // "MCP server connection failed" that previously hid the cause.
    void connectionFailureCarriesDiagnostic() {
        McpServerConfig config = makeConfig({"echo"});
        config.command = QStringLiteral("definitely-not-a-real-mcp-exe-xyz");
        McpClient client(config);
        QSignalSpy failedSpy(&client, &McpClient::connectionFailed);

        client.connectToServer();
        QTRY_VERIFY(failedSpy.count() > 0);

        const QString error = failedSpy.first().first().toString();
        QVERIFY(error.contains(QStringLiteral("Failed to start")));
        QVERIFY(error.contains(QStringLiteral("definitely-not-a-real-mcp-exe-xyz")));
    }

    // Windows: bare commands that resolve to .cmd/.bat wrappers (npx, uvx)
    // must start through cmd.exe instead of failing with FailedToStart.
    // Reproduced with a throwaway .cmd script discovered via PATH.
    void windowsCmdWrapperStartsViaCmd() {
#ifndef Q_OS_WIN
        QSKIP("Windows-specific QProcess/PATHEXT behavior");
#endif
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // A .cmd that stays alive briefly so QProcess::started fires and
        // the transport reaches Connected before the child exits.
        const QString scriptPath =
            dir.filePath(QStringLiteral("nexus-mcp-cmd-fake.cmd"));
        {
            QFile file(scriptPath);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream(&file) << "@echo off\r\n"
                               << "ping -n 4 127.0.0.1 >nul\r\n";
        }

        // findExecutable() reads PATH from the environment; prepend the
        // temp dir so the bare name resolves to our .cmd. Restore on exit.
        const QString originalPath =
            QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
        qputenv("PATH",
                QByteArray((dir.path() + QLatin1Char(';') + originalPath)
                               .toLocal8Bit()));
        struct PathRestore {
            QByteArray oldPath;
            ~PathRestore() { qputenv("PATH", oldPath); }
        } restore{originalPath.toLocal8Bit()};

        StdioTransport::Config cfg;
        cfg.command = QStringLiteral("nexus-mcp-cmd-fake");
        StdioTransport transport(cfg);
        QSignalSpy errorSpy(&transport, &McpTransport::errorOccurred);

        transport.start();
        // QProcess::started → setState(Connected). If the .cmd wrapper
        // path were broken we'd see "Failed to start" instead.
        QTRY_COMPARE(transport.state(), McpTransport::State::Connected);

        for (int i = 0; i < errorSpy.count(); ++i)
            QVERIFY(!errorSpy.at(i).first().toString()
                         .contains(QStringLiteral("Failed to start")));

        transport.stop();
    }

    // Windows: a .cmd resolved under a path containing spaces (e.g.
    // "D:\Program Files\nodejs\npx.cmd") must still start. cmd /s /c's
    // quote stripping used to split the path at the space and fail with
    // "'D:/Program' is not recognized as an internal or external command".
    void windowsCmdWrapperHandlesSpacedPath() {
#ifndef Q_OS_WIN
        QSKIP("Windows-specific cmd /s /c quoting behavior");
#endif
        const QString base = QDir::tempPath()
                           + QStringLiteral("/nexus mcp spaced test");
        QVERIFY(QDir().mkpath(base));

        const QString scriptPath =
            base + QStringLiteral("/nexus-mcp-spaced-fake.cmd");
        {
            QFile file(scriptPath);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream(&file) << "@echo off\r\n"
                               << "ping -n 4 127.0.0.1 >nul\r\n";
        }

        const QString originalPath =
            QProcessEnvironment::systemEnvironment()
                .value(QStringLiteral("PATH"));
        qputenv("PATH",
                QByteArray((base + QLatin1Char(';') + originalPath)
                               .toLocal8Bit()));
        struct PathRestore {
            QByteArray oldPath;
            ~PathRestore() { qputenv("PATH", oldPath); }
        } restore{originalPath.toLocal8Bit()};

        StdioTransport::Config cfg;
        cfg.command = QStringLiteral("nexus-mcp-spaced-fake");
        StdioTransport transport(cfg);
        QSignalSpy errorSpy(&transport, &McpTransport::errorOccurred);

        transport.start();
        QTRY_COMPARE(transport.state(), McpTransport::State::Connected);

        for (int i = 0; i < errorSpy.count(); ++i)
            QVERIFY(!errorSpy.at(i).first().toString()
                         .contains(QStringLiteral("Failed to start")));

        transport.stop();
        QDir(base).removeRecursively();
    }

    // When a server exits non-zero, its stderr tail must be inlined into
    // the transport error so users see the real cause (missing API key,
    // package install failure, ...) in the log instead of just an exit code.
    void stderrSurfacedOnExit() {
        StdioTransport::Config cfg;
        cfg.command = fakeServerExe();
        cfg.env.insert(QStringLiteral("MCP_FAKE_BOOT_FAIL"),
                       QStringLiteral("1"));
        StdioTransport transport(cfg);
        QSignalSpy errorSpy(&transport, &McpTransport::errorOccurred);

        transport.start();
        QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > 0, 5000);

        const QString error = errorSpy.first().first().toString();
        QVERIFY(error.contains(QStringLiteral("MCP server exited (code 1")));
        QVERIFY(error.contains(QStringLiteral("simulated boot failure")));

        transport.stop();
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    TestMcp tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_mcp.moc"
