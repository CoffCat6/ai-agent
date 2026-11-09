// FakeMcpServer (Batch 31a test infrastructure): a minimal MCP stdio server
// for test_mcp. Behavior is driven by environment variables:
//   MCP_FAKE_TOOLS   JSON array of tool names to advertise (default: ["echo"])
//   MCP_FAKE_MODE    echo | error | delay  (tools/call behavior)
//   MCP_FAKE_DELAY   delay seconds for the delay mode (default 2)
//   MCP_FAKE_PROMPTS docs/46: prompts to advertise, ';'-separated; an
//                    entry "name:a,b" declares required args a and b
//                    (default: unset — no prompts capability declared)
//   MCP_FAKE_RESOURCES docs/48: resources to advertise, ';'-separated; an
//                    entry "uri|name|mime" (default: unset — no resources
//                    capability declared)
//   MCP_FAKE_RESOURCE_BYTES docs/48: resources/read payload size in bytes
//                    (default 0 — echo the uri instead)
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

// docs/46: parse MCP_FAKE_PROMPTS entries. "alpha;beta:lang,tone" →
// alpha without arguments + beta requiring lang and tone (';' separates
// prompts, ',' their args). An empty variable means the server does NOT
// declare the prompts capability at all (client must not send prompts/list).
struct FakePrompt {
    QString name;
    QStringList args;
};

QList<FakePrompt> fakePrompts() {
    QList<FakePrompt> out;
    const QStringList entries =
        qEnvironmentVariable("MCP_FAKE_PROMPTS").split(';', Qt::SkipEmptyParts);
    for (QString entry : entries) {
        entry = entry.trimmed();
        const int colon = entry.indexOf(':');
        FakePrompt prompt;
        prompt.name = colon < 0 ? entry : entry.left(colon).trimmed();
        if (colon >= 0) {
            for (QString arg : entry.mid(colon + 1).split(',', Qt::SkipEmptyParts))
                prompt.args << arg.trimmed();
        }
        if (!prompt.name.isEmpty())
            out.append(prompt);
    }
    return out;
}

// docs/48: parse MCP_FAKE_RESOURCES entries. "mem://a|Alpha;text/plain;..."
// → uri|name|mime, ';'-separated. Empty variable = the server does NOT
// declare the resources capability (client must not send resources/list).
struct FakeResource {
    QString uri;
    QString name;
    QString mime;
};

QList<FakeResource> fakeResources() {
    QList<FakeResource> out;
    const QStringList entries =
        qEnvironmentVariable("MCP_FAKE_RESOURCES").split(';', Qt::SkipEmptyParts);
    for (QString entry : entries) {
        entry = entry.trimmed();
        const QStringList fields = entry.split('|');
        if (fields.size() < 2) continue;
        FakeResource r;
        r.uri = fields.at(0).trimmed();
        r.name = fields.at(1).trimmed();
        r.mime = fields.size() > 2 ? fields.at(2).trimmed()
                                   : QStringLiteral("text/plain");
        if (!r.uri.isEmpty() && !r.name.isEmpty())
            out.append(r);
    }
    return out;
}

QJsonArray advertisedResources() {
    QJsonArray resources;
    for (const FakeResource& r : fakeResources()) {
        resources.append(QJsonObject{
            {"uri", r.uri},
            {"name", r.name},
            {"title", QStringLiteral("Fake %1").arg(r.name)},
            {"description",
             QStringLiteral("fake resource %1 (test server)").arg(r.name)},
            {"mimeType", r.mime}});
    }
    return resources;
}

void handleResourcesRead(quint64 id, const QJsonObject& params) {
    const QString uri = params.value("uri").toString();
    // MCP_FAKE_RESOURCE_BYTES > 0 → emit a payload of that size (over-limit
    // rejection test); otherwise echo the uri back as the content.
    const int bytes = qEnvironmentVariableIntValue("MCP_FAKE_RESOURCE_BYTES",
                                                   nullptr);
    const QString text = bytes > 0
        ? QString(bytes, QLatin1Char('x'))
        : QStringLiteral("resource %1 content").arg(uri);
    const QJsonObject content{
        {"uri", uri},
        {"mimeType", QStringLiteral("text/plain")},
        {"text", text}};
    sendResult(id, QJsonObject{
        {"contents", QJsonArray{content}}});
}

QJsonArray advertisedPrompts() {
    QJsonArray prompts;
    for (const FakePrompt& prompt : fakePrompts()) {
        QJsonArray arguments;
        for (const QString& arg : prompt.args) {
            arguments.append(QJsonObject{
                {"name", arg},
                {"description",
                 QStringLiteral("fake argument %1").arg(arg)},
                {"required", true}});
        }
        prompts.append(QJsonObject{
            {"name", prompt.name},
            {"title", QStringLiteral("Fake %1").arg(prompt.name)},
            {"description",
             QStringLiteral("fake prompt %1 (test server)").arg(prompt.name)},
            {"arguments", arguments}});
    }
    return prompts;
}

void handlePromptsGet(quint64 id, const QJsonObject& params) {
    const QString name = params.value("name").toString();
    // Echo the arguments back as JSON text: the round-trip test asserts the
    // client-provided argument values actually reached the server.
    const QJsonObject args = params.value("arguments").toObject();
    const QString argsJson = QString::fromUtf8(
        QJsonDocument(args).toJson(QJsonDocument::Compact));
    const QJsonObject textBlock{
        {"type", QStringLiteral("text")},
        {"text", QStringLiteral("prompt %1 with args: %2").arg(name, argsJson)}};
    const QJsonObject message{
        {"role", QStringLiteral("user")},
        {"content", textBlock}};
    sendResult(id, QJsonObject{
        {"description", QStringLiteral("fake prompt %1").arg(name)},
        {"messages", QJsonArray{message}}});
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
        QJsonObject capabilities{
            {"tools", QJsonObject{{"listChanged", false}}}};
        // Only declare the prompts/resources capability when the test asked
        // for them — mirrors real servers that implement a subset.
        if (!fakePrompts().isEmpty())
            capabilities["prompts"] = QJsonObject{{"listChanged", false}};
        if (!fakeResources().isEmpty())
            capabilities["resources"] = QJsonObject{{"listChanged", false}};
        sendResult(id, QJsonObject{
            {"protocolVersion", QStringLiteral("2025-03-26")},
            {"capabilities", capabilities},
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
    if (method == QLatin1String("prompts/list")) {
        sendResult(id, QJsonObject{{"prompts", advertisedPrompts()}});
        return;
    }
    if (method == QLatin1String("prompts/get")) {
        handlePromptsGet(id, request.value("params").toObject());
        return;
    }
    if (method == QLatin1String("resources/list")) {
        sendResult(id, QJsonObject{{"resources", advertisedResources()}});
        return;
    }
    if (method == QLatin1String("resources/read")) {
        handleResourcesRead(id, request.value("params").toObject());
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
