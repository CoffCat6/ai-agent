// FakeMcpServer (Batch 31a test infrastructure): a minimal MCP stdio server
// for test_mcp. Behavior is driven by environment variables:
//   MCP_FAKE_TOOLS   JSON array of tool names to advertise (default: ["echo"])
//   MCP_FAKE_MODE    echo | error | delay  (tools/call behavior)
//   MCP_FAKE_DELAY   delay seconds for the delay mode (default 2)
// Speaks newline-delimited JSON-RPC 2.0 over stdin/stdout; stderr is logs.
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIODevice>
#include <QTextStream>
#include <QTimer>

namespace {

QTextStream qin(stdin, QIODevice::ReadOnly);
QTextStream qerr(stderr, QIODevice::WriteOnly);
int handledRequests = 0;

void sendLine(const QJsonObject& message) {
    // C stdio + explicit flush: the static QTextStream(stdout) built before
    // QCoreApplication proved unreliable when several fake instances ran in
    // one test process (responses never reached the client).
    const QByteArray line =
        QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    fwrite(line.constData(), 1, line.size(), stdout);
    fflush(stdout);
}

void sendResult(quint64 id, const QJsonObject& result) {
    QJsonObject response{
        {"jsonrpc", QStringLiteral("2.0")},
        {"id", static_cast<qint64>(id)},
        {"result", result}};
    sendLine(response);
}

void sendTextResult(quint64 id, const QString& text, bool isError = false) {
    QJsonObject result;
    result["content"] = QJsonArray{QJsonObject{
        {"type", "text"}, {"text", text}}};
    if (isError)
        result["isError"] = true;
    sendResult(id, result);
}

void sendError(quint64 id, int code, const QString& message) {
    QJsonObject response{
        {"jsonrpc", QStringLiteral("2.0")},
        {"id", static_cast<qint64>(id)},
        {"error", QJsonObject{{"code", code}, {"message", message}}}};
    sendLine(response);
}

QJsonArray advertisedTools() {
    QJsonArray tools;
    const QStringList names =
        qEnvironmentVariable("MCP_FAKE_TOOLS", QStringLiteral("echo"))
            .split(',', Qt::SkipEmptyParts);
    for (const QString& name : names) {
        tools.append(QJsonObject{
            {"name", name.trimmed()},
            {"description",
             QStringLiteral("fake tool %1 (test server)").arg(name.trimmed())},
            {"inputSchema", QJsonObject{
                {"type", "object"},
                {"properties", QJsonObject{
                    {"text", QJsonObject{{"type", "string"}}}}}}},
        });
    }
    return tools;
}

void handleToolsCall(quint64 id, const QJsonObject& params) {
    const QString mode =
        qEnvironmentVariable("MCP_FAKE_MODE", QStringLiteral("echo"));
    const QString argumentText =
        params.value("arguments").toObject().value("text").toString();

    if (mode == QLatin1String("delay")) {
        const int seconds =
            qEnvironmentVariableIntValue("MCP_FAKE_DELAY", nullptr);
        // Answer after a delay on the event loop (request stays pending).
        QTimer::singleShot(seconds * 1000, [id, argumentText]() {
            sendTextResult(id, QStringLiteral("delayed: %1").arg(argumentText));
        });
        return;
    }
    if (mode == QLatin1String("flood")) {
        // docs/24 H2: dump 12 MiB of newline-less bytes — the client's
        // stdout line-buffer cap must kill this server instead of growing
        // the buffer without bound.
        const QByteArray chunk(1024 * 1024, 'x');
        for (int i = 0; i < 12; ++i) {
            fwrite(chunk.constData(), 1, chunk.size(), stdout);
            fflush(stdout);
        }
        // Stay alive; the client is expected to terminate us.
        return;
    }
    if (mode == QLatin1String("error")) {
        sendTextResult(id, QStringLiteral("fake tool failure"), true);
        return;
    }
    if (mode == QLatin1String("rpc_error")) {
        sendError(id, -32603, QStringLiteral("fake JSON-RPC error"));
        return;
    }
    // echo (default): reflect the arguments back as JSON text.
    sendTextResult(id, QStringLiteral("echo: %1")
        .arg(QString::fromUtf8(QJsonDocument(
            params.value("arguments").toObject())
            .toJson(QJsonDocument::Compact))));
}

void handleRequest(const QJsonObject& request) {
    const quint64 id = static_cast<quint64>(
        request.value("id").toVariant().toLongLong());
    const QString method = request.value("method").toString();

    if (method == QLatin1String("initialize")) {
        sendResult(id, QJsonObject{
            {"protocolVersion", QStringLiteral("2025-03-26")},
            {"capabilities", QJsonObject{
                {"tools", QJsonObject{{"listChanged", false}}}}},
            {"serverInfo", QJsonObject{
                {"name", QStringLiteral("fake-mcp")},
                {"version", QStringLiteral("1.0")}}},
        });
        return;
    }
    if (method == QLatin1String("tools/list")) {
        sendResult(id, QJsonObject{{"tools", advertisedTools()}});
        return;
    }
    if (method == QLatin1String("ping")) {
        sendResult(id, QJsonObject{});
        return;
    }
    if (method == QLatin1String("tools/call")) {
        handleToolsCall(id, request.value("params").toObject());
        return;
    }
    sendError(id, -32601, QStringLiteral("method not found: ") + method);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    // Test hook: simulate a server that fails at boot (writes stderr then
    // exits with non-zero), mirroring real MCP servers that misconfigure
    // themselves (missing API key, package install failure, ...).
    if (qEnvironmentVariableIsSet("MCP_FAKE_BOOT_FAIL")) {
        qerr << "simulated boot failure\n";
        return 1;
    }

    const int crashAfter =
        qEnvironmentVariableIntValue("MCP_FAKE_CRASH", nullptr);

    auto onLine = [&app, crashAfter](const QString& line) {
        if (line.trimmed().isEmpty()) return;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject()) return;
        const QJsonObject request = doc.object();
        if (request.contains("id"))
            handleRequest(request);
        ++handledRequests;
        if (crashAfter > 0 && handledRequests >= crashAfter) {
            qerr << "fake mcp server crashing on purpose\n";
            app.exit(1);
        }
    };

    // stdin readiness via QSocketNotifier is awkward for Windows pipes in a
    // test helper; use a polling timer instead (small and sufficient here).
    // docs/24 H9: exit on stdin EOF after having served requests — when the
    // parent test process dies, an orphaned server must release its
    // inherited handles instead of polling forever.
    int eofRounds = 0;
    auto* timer = new QTimer(&app);
    timer->setInterval(15);
    QObject::connect(timer, &QTimer::timeout, [&onLine, &eofRounds]() {
        if (qin.atEnd()) {
            if (handledRequests > 0 && ++eofRounds > 3)
                QCoreApplication::exit(0);
            return;
        }
        eofRounds = 0;
        while (!qin.atEnd()) {
            const QString line = qin.readLine();
            onLine(line);
        }
    });
    timer->start();

    return app.exec();
}
