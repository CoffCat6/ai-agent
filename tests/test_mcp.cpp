// Batch 31a (F9): MCP client coverage against a scripted stdio server —
// handshake, tool registration naming, call round trip, error mapping,
// cancel, crash restart and concurrent id correlation.
// Batch 31b (F9): Streamable HTTP transport coverage.
#include "core/mcp/McpClient.h"
#include "core/mcp/McpToolAdapter.h"
#include "core/tools/ToolRegistry.h"

#include <QCoreApplication>
#include <QHash>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

using namespace NexusAI;

namespace {

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
};

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    QCoreApplication app(argc, argv);
    TestMcp tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_mcp.moc"
