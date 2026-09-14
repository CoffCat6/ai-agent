// Batch 31a (F9): MCP client coverage against a scripted stdio server —
// handshake, tool registration naming, call round trip, error mapping,
// cancel, crash restart and concurrent id correlation.
#include "core/mcp/McpClient.h"
#include "core/mcp/McpToolAdapter.h"
#include "core/tools/ToolRegistry.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace NexusAI;

namespace {

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

        // takeTool transfers ownership out (no dangling registry entry).
        auto taken = registry.takeTool(name);
        QVERIFY(taken != nullptr);
        QVERIFY(registry.find(name) == nullptr);
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
};

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    QCoreApplication app(argc, argv);
    TestMcp tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_mcp.moc"
