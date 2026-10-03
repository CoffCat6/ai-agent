// Provider request-state lifecycle tests (Batch 0 regression net).
//
// These tests drive the REAL chatComplete() path against a scripted local
// HTTP/SSE server on loopback, because the audited P0 lives in long-lived
// streaming state, not in the pure parsers: OpenAIService::m_toolCalls and
// AnthropicService::m_toolUses survive resetStreamState() and are re-emitted
// with the NEXT response of the SAME service instance (the deterministic
// "knowledge tool fires on ordinary questions" cross-talk bug).
//
//   openai/anthropic/ollamaToolStateClearedBetweenRequests (TEST-001):
//   tool-call stream, then a plain-text stream on the same instance - the
//   second response must be tool-free. Failed while derived tool state
//   leaked (red on the pre-fix baseline); green since Batch 1.
//
//   ollamaToolStateClearedBetweenRequests reassigns m_toolCalls on done=true,
//   so it passed even before the fix; it pins that contract so stream-state
//   refactors cannot regress it.
//
//   staleReplyDoesNotPolluteNewRequest (TEST-003): a superseded request that
//   finishes late must not splice its bytes into the in-flight request's
//   stream state (red on the pre-fix baseline; green since Batch 1).
#include <QtTest>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkProxy>
#include "core/utils/Logger.h"
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/utils/Activity.h"
#include "infrastructure/llm/AnthropicService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/OpenAIResponsesService.h"
#include "infrastructure/llm/OpenAIService.h"
#include "core/tools/NetworkFetchTool.h"

#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrentRun>

using namespace NexusAI;

namespace {

// ── Scripted loopback HTTP server ─────────────────────────
// Each queued script carries a `match` substring located in the full request
// (request line + headers + body); the first matching, not-yet-bound script
// answers the request. Matching by content - not by accept order - keeps the
// mapping deterministic even when two connections are established in
// parallel (their completion order races).
//
// A "hold" script keeps the connection open so a test can release (and
// close) it later, emulating a slow/stale reply that outlives its request.
//
// The server NEVER blocks inside a socket callback and responds only after
// the client's full request arrived: QNAM coordinates POST uploads through
// the same thread that runs this server, so blocking in an accept handler
// (or closing before the request is fully sent) deadlocks or RSTs the
// exchange.
class FakeSseServer : public QObject {
    Q_OBJECT
public:
    struct Script {
        QByteArray body;
        bool hold = false;
        QByteArray match;
        int status = 200;   // HTTP status for the response head (retry tests)
        // Batch 21: extra response headers (e.g. Location for 3xx tests).
        QByteArray extraHeaders{};
    };

    explicit FakeSseServer(QObject* parent = nullptr) : QObject(parent) {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QTcpSocket::deleteLater);
                m_buffers.insert(socket, QByteArray());
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    onBytes(socket);
                });
            }
        });
        QObject::connect(&m_server, &QTcpServer::acceptError, this,
                         [this](QAbstractSocket::SocketError error) {
            qWarning() << "FakeSseServer accept error:" << error;
        });
        if (!m_server.listen(QHostAddress::LocalHost))
            qWarning() << "FakeSseServer listen failed:" << m_server.errorString();
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    void enqueue(const Script& script) { m_scripts.append(script); }

    int heldCount() const { return m_held.size(); }

    // Batch 15: flattened (whitespace-stripped) body of the most recent
    // complete request - for request-level sampling-param assertions.
    QByteArray lastFlatBody() const { return m_lastFlat; }

    // Respond to a held connection; releasedClosed(index) turns true once the
    // client finished processing the reply and closed its side (server sent
    // Connection: close, so the client's QNAM always closes afterwards). This
    // gives tests a deterministic "reply fully handled" barrier that works
    // both for a silent (fixed) and an emitting (buggy) reply handler.
    void releaseHeld(int index) {
        if (!m_held.contains(index)) return;
        QTcpSocket* socket = m_held.take(index);
        QObject::connect(socket, &QTcpSocket::disconnected, this, [this, index]() {
            m_releasedClosed.insert(index);
        });
        respond(socket, m_scripts.at(index));
    }

    bool releasedClosed(int index) const { return m_releasedClosed.contains(index); }

private:
    void onBytes(QTcpSocket* socket) {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        if (!requestComplete(buffer)) return;
        if (m_boundSockets.contains(socket)) return;

        // Match keys are written whitespace-free; the client serializes its
        // body indented (QJsonDocument::toJson defaults to Indented), so the
        // match runs against a whitespace-stripped copy.
        const QByteArray flat = flatten(buffer);
        m_lastFlat = flat;

        int scriptIndex = -1;
        for (int i = 0; i < m_scripts.size(); ++i) {
            if (m_boundScripts.contains(i)) continue;
            if (flat.contains(m_scripts.at(i).match)) {
                scriptIndex = i;
                break;
            }
        }
        if (scriptIndex < 0) return;   // no script matches (yet)

        m_boundSockets.insert(socket);
        m_boundScripts.insert(scriptIndex);
        const Script script = m_scripts.at(scriptIndex);
        if (script.hold) {
            m_held.insert(scriptIndex, socket);
            return;
        }
        respond(socket, script);
    }

    static QByteArray flatten(const QByteArray& raw) {
        QByteArray out;
        out.reserve(raw.size());
        for (char c : raw) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            out.append(c);
        }
        return out;
    }

    static bool requestComplete(const QByteArray& buffer) {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return false;
        const QByteArray headers = buffer.left(headerEnd);
        int contentLength = 0;
        const QList<QByteArray> lines = headers.split('\n');
        for (const QByteArray& line : lines) {
            if (line.toLower().startsWith("content-length:")) {
                contentLength = line.mid(15).trimmed().toInt();
                break;
            }
        }
        return buffer.size() - (headerEnd + 4) >= contentLength;
    }

    void respond(QTcpSocket* socket, const Script& script) {
        // The full request was already consumed by onBytes(), so closing
        // after the response cannot RST unread inbound data.
        const char* reason = "OK";
        if (script.status >= 300 && script.status < 400) reason = "Found";
        else if (script.status >= 400) reason = "Error";
        socket->write("HTTP/1.1 " + QByteArray::number(script.status) + " "
                          + reason + "\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Content-Length: " + QByteArray::number(script.body.size()) + "\r\n"
                      + script.extraHeaders +
                      "Connection: close\r\n"
                      "\r\n" + script.body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QVector<Script> m_scripts;
    QSet<QTcpSocket*> m_boundSockets;
    QSet<int> m_boundScripts;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QHash<int, QTcpSocket*> m_held;
    QSet<int> m_releasedClosed;
    QByteArray m_lastFlat;
};

QJsonArray userMessages(const QString& text) {
    QJsonArray messages;
    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = text;
    messages.append(msg);
    return messages;
}

// Request-body match keys: the compact JSON body embeds the user message
// verbatim, and "hi" cannot match a body that only contains "hi2".
constexpr auto kMatchA = "\"content\":\"hi\"";
constexpr auto kMatchB = "\"content\":\"hi2\"";

// OpenAI-compatible SSE: one tool call, then finish, then [DONE]
QByteArray openaiToolBody() {
    return R"BODY(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"search_knowledge_base","arguments":"{\"query\":\"孙曼\"}"}}]}}]}

data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]}

data: [DONE]

)BODY";
}

QByteArray openaiTextBody() {
    return R"BODY(data: {"choices":[{"delta":{"content":"Hello"}}]}

data: [DONE]

)BODY";
}

// Anthropic Messages API SSE: tool_use block assembled from deltas
QByteArray anthropicToolBody() {
    return R"BODY(data: {"type":"message_start","message":{"usage":{"input_tokens":10}}}

data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_1","name":"search_knowledge_base"}}

data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"query\":\"孙曼\"}"}}

data: {"type":"message_stop"}

)BODY";
}

QByteArray anthropicTextBody() {
    return R"BODY(data: {"type":"message_start","message":{"usage":{"input_tokens":10}}}

data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}

data: {"type":"message_delta","usage":{"output_tokens":1}}

data: {"type":"message_stop"}

)BODY";
}

// Ollama NDJSON: the done=true line carries tool calls / usage
QByteArray ollamaToolBody() {
    return QByteArrayLiteral(
        "{\"message\":{\"content\":\"\",\"tool_calls\":[{\"function\":"
        "{\"name\":\"search_knowledge_base\",\"arguments\":{\"query\":\"孙曼\"}}}]},"
        "\"done\":true}\n");
}

QByteArray ollamaTextBody() {
    return QByteArrayLiteral("{\"message\":{\"content\":\"Hello\"},\"done\":true}\n");
}

// OpenAI Responses API SSE：命名事件靠 data 里的 "type" 判别（不读 event: 行）。
// 工具调用分三段：output_item.added 给 name/call_id，arguments 增量分片，最后
// output_item.done 带完整值（要能折叠掉已累积的分片而不是拼两遍）。
// 用 QByteArrayLiteral 拼接而非裸字符串字面量：与文件里其他脚本一致，也避开
// 多个 raw string 混用时 moc 解析不到后面测试类的问题。
QByteArray responsesToolBody() {
    return QByteArrayLiteral(
        "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\",\"status\":\"in_progress\"}}\n\n"
        "data: {\"type\":\"response.output_item.added\",\"output_index\":0,\"item\":{\"type\":\"function_call\","
        "\"id\":\"fc_1\",\"call_id\":\"call_1\",\"name\":\"search_knowledge_base\",\"arguments\":\"\"}}\n\n"
        "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_1\",\"output_index\":0,"
        "\"delta\":\"{\\\"query\\\":\\\"孙\"}\n\n"
        "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_1\",\"output_index\":0,"
        "\"delta\":\"曼\\\"}\"}\n\n"
        "data: {\"type\":\"response.output_item.done\",\"output_index\":0,\"item\":{\"type\":\"function_call\","
        "\"id\":\"fc_1\",\"call_id\":\"call_1\",\"name\":\"search_knowledge_base\","
        "\"arguments\":\"{\\\"query\\\":\\\"孙曼\\\"}\"}}\n\n"
        "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\",\"status\":\"completed\","
        "\"usage\":{\"input_tokens\":120,\"input_tokens_details\":{\"cached_tokens\":40},"
        "\"output_tokens\":30,\"output_tokens_details\":{\"reasoning_tokens\":10},\"total_tokens\":150}}}\n\n"
        "data: [DONE]\n\n");
}

// 第二个请求的脚本：纯文本 + 终止事件，用来验证第一次的 function_call 没有漏进
// 第二次的响应（同一条 stream 状态机跨请求复用）。
QByteArray responsesTextBody() {
    return QByteArrayLiteral(
        "data: {\"type\":\"response.output_text.delta\",\"item_id\":\"msg_1\",\"output_index\":0,"
        "\"delta\":\"Hello\"}\n\n"
        "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_2\",\"status\":\"completed\","
        "\"usage\":{\"input_tokens\":5,\"output_tokens\":1,\"total_tokens\":6}}}\n\n"
        "data: [DONE]\n\n");
}

} // namespace

class TestProviderLifecycle : public QObject {
    Q_OBJECT

    static LLMResponse takeResponse(const QSignalSpy& spy, int row) {
        return spy.at(row).at(0).value<LLMResponse>();
    }

private slots:
    void initTestCase() {
        qRegisterMetaType<LLMResponse>("NexusAI::LLMResponse");
        // Never route loopback traffic through a system proxy (clash etc.)
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        // Feed the ring buffer for the Activity/metadata assertions
        Logger::install();
        Logger::setMinLevel(QtInfoMsg);
    }

    // Isolation probe: raw QNetworkAccessManager GET vs. FakeSseServer.
    void serverServesPlainHttp() {
        FakeSseServer server;
        server.enqueue({ollamaTextBody(), false, "/probe"});

        QNetworkAccessManager nam;
        QNetworkReply* reply = nam.get(QNetworkRequest(QUrl(server.baseUrl() + "/probe")));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY2(finished.wait(5000), "probe reply never finished");
        QVERIFY(reply->error() == QNetworkReply::NoError);
        QVERIFY(reply->readAll().contains("Hello"));
        reply->deleteLater();
    }

    // Isolation probe: POST with the exact headers the LLM services use.
    void serverServesServiceStylePost() {
        FakeSseServer server;
        server.enqueue({ollamaTextBody(), false, "/api/chat"});

        QNetworkRequest request;
        request.setUrl(QUrl(server.baseUrl() + "/api/chat"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(300000);

        QNetworkAccessManager nam;
        QNetworkReply* reply = nam.post(request, QByteArrayLiteral("{\"messages\":[]}"));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY2(finished.wait(5000), "POST probe reply never finished");
        QVERIFY(reply->error() == QNetworkReply::NoError);
        QVERIFY(reply->readAll().contains("Hello"));
        reply->deleteLater();
    }

    // TEST-001 (OpenAI): request A calls a tool; request B on the SAME
    // service instance is plain text. B's response must be tool-free.
    void openaiToolStateClearedBetweenRequests() {
        FakeSseServer server;
        OpenAIService service;
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);

        server.enqueue({openaiToolBody(), false, kMatchA});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response for request A");
        const LLMResponse first = takeResponse(spy, 0);
        QVERIFY(first.success);
        QCOMPARE(first.toolCalls.size(), 1);
        QCOMPARE(first.toolCalls.first().name, QStringLiteral("search_knowledge_base"));

        server.enqueue({openaiTextBody(), false, kMatchB});
        service.chatComplete(userMessages(QStringLiteral("hi2")), {});
        QVERIFY2(spy.wait(5000), "no response for request B");
        const LLMResponse second = takeResponse(spy, 1);
        QVERIFY(second.success);
        QCOMPARE(second.content, QStringLiteral("Hello"));
        QVERIFY2(second.toolCalls.isEmpty(),
                 "request A's tool call leaked into request B's response");
    }

    // TEST-001 (OpenAI Responses): 同一实例上先工具调用、后纯文本，第二次的
    // 响应不能带上第一次的 function_call；顺带钉住 usage 映射与 done 事件对
    // 已累积参数分片的折叠。
    void responsesToolStateClearedBetweenRequests() {
        FakeSseServer server;
        OpenAIResponsesService service;
        service.setApiKey(QStringLiteral("test-key"));
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);

        server.enqueue({responsesToolBody(), false, kMatchA});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response for request A");
        const LLMResponse first = takeResponse(spy, 0);
        QVERIFY2(first.success, qPrintable(first.error));
        QCOMPARE(first.toolCalls.size(), 1);
        QCOMPARE(first.toolCalls.first().name, QStringLiteral("search_knowledge_base"));
        // id 必须是 call_id：下一轮按它回传 function_call_output
        QCOMPARE(first.toolCalls.first().id, QStringLiteral("call_1"));
        QCOMPARE(first.toolCalls.first().arguments.value(QStringLiteral("query")).toString(),
                 QString::fromUtf8("孙曼"));
        QCOMPARE(first.promptTokens, 120);
        QCOMPARE(first.completionTokens, 30);
        QCOMPARE(first.cachedTokens, 40);
        QCOMPARE(first.tokensUsed, 150);
        // Responses 的 input_tokens 已含缓存，另两个桶必须留空（AIAgent 会相加）
        QCOMPARE(first.cacheReadTokens, 0);
        QCOMPARE(first.cacheCreationTokens, 0);

        server.enqueue({responsesTextBody(), false, kMatchB});
        service.chatComplete(userMessages(QStringLiteral("hi2")), {});
        QVERIFY2(spy.wait(5000), "no response for request B");
        const LLMResponse second = takeResponse(spy, 1);
        QVERIFY(second.success);
        QCOMPARE(second.content, QStringLiteral("Hello"));
        QVERIFY2(second.toolCalls.isEmpty(),
                 "request A's function_call leaked into request B's response");
    }

    // 有些兼容网关不发 output_item.added，只在 done 里给完整 item：name/call_id
    // 必须能从 done 单独成槽，否则工具调用会被静默丢掉。
    void responsesToolCallFromDoneItemOnly() {
        FakeSseServer server;
        OpenAIResponsesService service;
        service.setApiKey(QStringLiteral("test-key"));
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);

        server.enqueue({QByteArrayLiteral(
            "data: {\"type\":\"response.output_item.done\",\"output_index\":0,"
            "\"item\":{\"type\":\"function_call\",\"id\":\"fc_9\",\"call_id\":\"call_9\","
            "\"name\":\"lookup\",\"arguments\":\"{\\\"q\\\":1}\"}}\n\n"
            "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n"
            "data: [DONE]\n\n"), false, "/responses"});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response for the done-only tool call");
        const LLMResponse parsed = takeResponse(spy, 0);
        QVERIFY2(parsed.success, qPrintable(parsed.error));
        QCOMPARE(parsed.toolCalls.size(), 1);
        QCOMPARE(parsed.toolCalls.first().id, QStringLiteral("call_9"));
        QCOMPARE(parsed.toolCalls.first().name, QStringLiteral("lookup"));
        QCOMPARE(parsed.toolCalls.first().arguments.value(QStringLiteral("q")).toInt(), 1);
    }

    // 流中 error 事件（顶层 code/message，不套 error 对象）必须转成一次
    // errorOccurred，而不是伪装成一个空答案。
    void responsesMidStreamErrorEmitsError() {
        FakeSseServer server;
        OpenAIResponsesService service;
        service.setApiKey(QStringLiteral("test-key"));
        service.setBaseUrl(server.baseUrl());
        QSignalSpy responseSpy(&service, &IAIService::responseReady);
        QSignalSpy errorSpy(&service, &IAIService::errorOccurred);

        server.enqueue({QByteArrayLiteral(
            "data: {\"type\":\"response.output_text.delta\",\"delta\":\"半句\"}\n\n"
            "data: {\"type\":\"error\",\"code\":\"server_error\","
            "\"message\":\"上游断了\"}\n\n"), false, "/responses"});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(errorSpy.wait(5000), "the stream error event never surfaced");
        QCOMPARE(responseSpy.count(), 0);
        QVERIFY(errorSpy.at(0).at(0).toString().contains(QStringLiteral("上游断了")));
    }

    // TEST-001 (Anthropic): same contract on the Messages API stream.
    void anthropicToolStateClearedBetweenRequests() {
        FakeSseServer server;
        AnthropicService service;
        service.setApiKey(QStringLiteral("test-key"));
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);

        server.enqueue({anthropicToolBody(), false, kMatchA});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response for request A");
        const LLMResponse first = takeResponse(spy, 0);
        QVERIFY(first.success);
        QCOMPARE(first.toolCalls.size(), 1);
        QCOMPARE(first.toolCalls.first().name, QStringLiteral("search_knowledge_base"));

        server.enqueue({anthropicTextBody(), false, kMatchB});
        service.chatComplete(userMessages(QStringLiteral("hi2")), {});
        QVERIFY2(spy.wait(5000), "no response for request B");
        const LLMResponse second = takeResponse(spy, 1);
        QVERIFY(second.success);
        QCOMPARE(second.content, QStringLiteral("Hello"));
        QVERIFY2(second.toolCalls.isEmpty(),
                 "request A's tool_use leaked into request B's response");
    }

    // Baseline (passed even before the fix): Ollama reassigns m_toolCalls on
    // done=true. Pins the contract so stream-state refactors cannot regress.
    void ollamaToolStateClearedBetweenRequests() {
        FakeSseServer server;
        OllamaService service;
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);

        server.enqueue({ollamaToolBody(), false, kMatchA});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response for request A");
        QCOMPARE(takeResponse(spy, 0).toolCalls.size(), 1);

        server.enqueue({ollamaTextBody(), false, kMatchB});
        service.chatComplete(userMessages(QStringLiteral("hi2")), {});
        QVERIFY2(spy.wait(5000), "no response for request B");
        const LLMResponse second = takeResponse(spy, 1);
        QCOMPARE(second.content, QStringLiteral("Hello"));
        QVERIFY2(second.toolCalls.isEmpty(),
                 "request A's tool call leaked into request B's response");
    }

    // Batch 15 (FEAT-1502): profile sampling params reach the wire only when
    // set - asserted against the flattened request body.
    void profileParamsAppearInRequestBody() {
        FakeSseServer server;
        OpenAIService service;
        service.setBaseUrl(server.baseUrl());
        service.setSamplingParams(0.3, QVariant());   // temperature only

        QSignalSpy spy(&service, &IAIService::responseReady);
        server.enqueue({openaiTextBody(), false, kMatchA});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response");

        const QByteArray body = server.lastFlatBody();
        QVERIFY2(body.contains("\"temperature\":0.3"),
                 "profile temperature missing from the request body");
        QVERIFY2(!body.contains("\"max_tokens\""),
                 "max_tokens present although unset");
    }

    void anthropicMaxTokensFromProfile() {
        FakeSseServer server;
        AnthropicService service;
        service.setApiKey(QStringLiteral("test-key"));
        service.setBaseUrl(server.baseUrl());
        service.setSamplingParams(QVariant(), 8192);   // max_tokens only

        QSignalSpy spy(&service, &IAIService::responseReady);
        server.enqueue({anthropicTextBody(), false, kMatchA});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response");

        QVERIFY2(server.lastFlatBody().contains("\"max_tokens\":8192"),
                 "profile maxOutputTokens not applied to the request body");
        QVERIFY2(!server.lastFlatBody().contains("\"temperature\""),
                 "temperature present although unset");
    }

    // Defaults: no sampling fields reach the wire; Anthropic keeps its
    // REQUIRED max_tokens at the historical 4096 fallback.
    void emptyParamsOmitted() {
        {
            FakeSseServer server;
            OpenAIService service;
            service.setBaseUrl(server.baseUrl());
            QSignalSpy spy(&service, &IAIService::responseReady);
            server.enqueue({openaiTextBody(), false, kMatchA});
            service.chatComplete(userMessages(QStringLiteral("hi")), {});
            QVERIFY2(spy.wait(5000), "no response (openai)");
            QVERIFY2(!server.lastFlatBody().contains("\"temperature\""),
                     "openai temperature leaked into the request body");
            QVERIFY2(!server.lastFlatBody().contains("\"max_tokens\""),
                     "openai max_tokens leaked into the request body");
        }
        {
            FakeSseServer server;
            AnthropicService service;
            service.setApiKey(QStringLiteral("test-key"));
            service.setBaseUrl(server.baseUrl());
            QSignalSpy spy(&service, &IAIService::responseReady);
            server.enqueue({anthropicTextBody(), false, kMatchA});
            service.chatComplete(userMessages(QStringLiteral("hi")), {});
            QVERIFY2(spy.wait(5000), "no response (anthropic)");
            QVERIFY2(server.lastFlatBody().contains("\"max_tokens\":4096"),
                     "anthropic 4096 fallback lost");
            QVERIFY2(!server.lastFlatBody().contains("\"temperature\""),
                     "anthropic temperature leaked into the request body");
        }
        {
            FakeSseServer server;
            OllamaService service;
            service.setBaseUrl(server.baseUrl());
            QSignalSpy spy(&service, &IAIService::responseReady);
            server.enqueue({ollamaTextBody(), false, kMatchA});
            service.chatComplete(userMessages(QStringLiteral("hi")), {});
            QVERIFY2(spy.wait(5000), "no response (ollama)");
            QVERIFY2(!server.lastFlatBody().contains("\"options\""),
                     "ollama options leaked into the request body");
        }
    }

    void ollamaSamplingParamsInRequestBody() {
        FakeSseServer server;
        OllamaService service;
        service.setBaseUrl(server.baseUrl());
        service.setSamplingParams(0.7, 4096);

        QSignalSpy spy(&service, &IAIService::responseReady);
        server.enqueue({ollamaTextBody(), false, kMatchA});
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response");

        const QByteArray body = server.lastFlatBody();
        QVERIFY2(body.contains("\"options\":{"),
                 "ollama options object missing from the request body");
        QVERIFY2(body.contains("\"temperature\":0.7"),
                 "options.temperature missing from the request body");
        QVERIFY2(body.contains("\"num_ctx\":4096"),
                 "options.num_ctx missing from the request body");
    }

    // ── Batch 20: http_fetch (FEAT-2001/2002) ─────────
    // The test server lives on 127.0.0.1, so the happy path needs the SSRF
    // escape hatch; the rejection tests run with defaults (guard on).

    void httpFetchHappyPath() {
        FakeSseServer server;
        NetworkFetchTool tool;
        tool.setAllowPrivateTargets(true);
        server.enqueue({QByteArrayLiteral(
                            "<html><body>hello fetch world</body></html>"),
                        false, "/fetch-ok"});

        const ToolResult r = tool.execute(QJsonObject{
            {"url", server.baseUrl() + QStringLiteral("/fetch-ok")}});
        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY2(r.output.contains(QStringLiteral("hello fetch world")),
                 qPrintable(r.output));
        QVERIFY2(!r.output.contains(QStringLiteral("<html>")), "HTML tags not stripped");
    }

    void httpFetchSsrfLoopbackRejected() {
        NetworkFetchTool tool;   // defaults: private targets rejected
        const ToolResult r = tool.execute(QJsonObject{
            {"url", QStringLiteral("http://127.0.0.1:9/nope")}});
        QVERIFY2(!r.success, "loopback fetch must be rejected by default");
        QVERIFY2(r.error.contains(QStringLiteral("rejected")), qPrintable(r.error));
    }

    // Literal private IP: QHostInfo short-circuits (no DNS), so the guard
    // fires before any connection - testable fully offline.
    void httpFetchPrivateNetworkRejected() {
        NetworkFetchTool tool;
        const ToolResult r = tool.execute(QJsonObject{
            {"url", QStringLiteral("http://10.255.0.1/nope")}});
        QVERIFY2(!r.success, "private-network fetch must be rejected by default");
        QVERIFY2(r.error.contains(QStringLiteral("rejected")), qPrintable(r.error));
    }

    void httpFetchTimeout() {
        FakeSseServer server;
        NetworkFetchTool tool;
        tool.setAllowPrivateTargets(true);
        server.enqueue({QByteArrayLiteral("late"), true, "/fetch-timeout"});   // held

        const ToolResult r = tool.execute(QJsonObject{
            {"url", server.baseUrl() + QStringLiteral("/fetch-timeout")},
            {"timeout_ms", 1000}});
        QVERIFY2(!r.success, "held request must time out");
        QVERIFY2(r.error.contains(QStringLiteral("timed out")), qPrintable(r.error));
    }

    void httpFetchSizeCapTruncates() {
        FakeSseServer server;
        NetworkFetchTool tool;
        tool.setAllowPrivateTargets(true);
        QByteArray big(2 * 1024 * 1024, 'a');   // 2 MB > the 1 MB wire cap
        server.enqueue({big, false, "/fetch-big"});

        const ToolResult r = tool.execute(QJsonObject{
            {"url", server.baseUrl() + QStringLiteral("/fetch-big")}});
        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY2(r.output.contains(QStringLiteral("[truncated: response exceeded")),
                 qPrintable(r.output));
        QVERIFY2(r.output.size() <= 6100, "result must respect the 6000-char layer");
    }

    // Cooperative cancellation (Batch 13 contract): cancel() flips the
    // atomic flag; the sliced execute aborts on the pool thread within a
    // slice and reports a cancelled failure.
    void httpFetchCancelledOnStop() {
        FakeSseServer server;
        NetworkFetchTool tool;
        tool.setAllowPrivateTargets(true);
        server.enqueue({QByteArrayLiteral("never"), true, "/fetch-held"});

        QElapsedTimer clock;
        clock.start();
        auto future = QtConcurrent::run([&tool, &server] {
            return tool.execute(QJsonObject{
                {"url", server.baseUrl() + QStringLiteral("/fetch-held")}});
        });
        QTest::qWait(300);              // let the request spin up
        tool.cancel();
        int waited = 0;
        while (!future.isFinished() && waited < 3000) {
            QTest::qWait(50);
            waited += 50;
        }
        QVERIFY2(future.isFinished(), "cancelled fetch did not return in time");
        QVERIFY2(clock.elapsed() < 2500, "cancel took longer than 2 s");
        const ToolResult r = future.result();
        QVERIFY2(!r.success, qPrintable(r.error));
        QVERIFY2(r.error.contains(QStringLiteral("cancelled")), qPrintable(r.error));
    }

    // Batch 21 (B1): a 3xx under ManualRedirectPolicy finishes with NoError
    // but no body; the tool must surface it as a failure and echo Location.
    void httpFetchRedirectSurfacedAsFailure() {
        FakeSseServer server;
        NetworkFetchTool tool;
        tool.setAllowPrivateTargets(true);
        FakeSseServer::Script script;
        script.match = "/fetch-moved";
        script.status = 302;
        script.body = QByteArrayLiteral("");
        script.extraHeaders =
            QByteArrayLiteral("Location: /new-home\r\n");
        server.enqueue(script);

        const ToolResult r = tool.execute(QJsonObject{
            {"url", server.baseUrl() + QStringLiteral("/fetch-moved")}});
        QVERIFY2(!r.success, "3xx must not be treated as success");
        QVERIFY2(r.error.contains(QStringLiteral("redirect (302)")), qPrintable(r.error));
        QVERIFY2(r.error.contains(QStringLiteral("/new-home")),
                 "Location must be echoed so the model can refetch");
    }

    // Batch 21 (C1): CGNAT, multicast and limited-broadcast ranges are also
    // internal/non-routable and must be rejected before connecting.
    void httpFetchAdditionalPrivateRangesRejected() {
        const QStringList hosts = {
            QStringLiteral("http://100.64.0.1/"),       // CGNAT
            QStringLiteral("http://100.127.255.254/"), // CGNAT far edge
            QStringLiteral("http://224.0.0.1/"),       // multicast
            QStringLiteral("http://255.255.255.255/"), // limited broadcast
        };
        for (const QString& url : hosts) {
            NetworkFetchTool tool;
            const ToolResult r = tool.execute(QJsonObject{{"url", url}});
            QVERIFY2(!r.success, qPrintable(url));
            QVERIFY2(r.error.contains(QStringLiteral("rejected")),
                     qPrintable(url + " -> " + r.error));
        }
        // No "outside the range" negative case here: connecting to such an
        // address would actually hit the network (SYN timeout) and stall the
        // suite; mask boundaries are covered by the four literal rejects.
    }

    // REL-1001: a transient server failure (HTTP 500) is retried once
    // transparently — the caller sees exactly one clean response and no
    // error signal.
    void transientServerErrorRetriesOnce() {
        FakeSseServer server;
        OpenAIService service;
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);
        QSignalSpy errors(&service, &IAIService::errorOccurred);

        server.enqueue({QByteArrayLiteral("{\"error\":{\"message\":\"server exploded\"}}"),
                        false, kMatchA, 500});   // first attempt fails
        server.enqueue({openaiTextBody(), false, kMatchA, 200});   // retry succeeds
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        QVERIFY2(spy.wait(5000), "no response after transient failure");
        QCOMPARE(errors.count(), 0);
        QCOMPARE(spy.count(), 1);
        const LLMResponse response = takeResponse(spy, 0);
        QCOMPARE(response.content, QStringLiteral("Hello"));
        QVERIFY(response.toolCalls.isEmpty());
    }

    // 日志增强 privacy red line: the network metadata events must never
    // contain request/response body content — endpoint, status, sizes and
    // ids only.
    void networkLogsAreMetadataOnly() {
        FakeSseServer server;
        OpenAIService service;
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);
        const quint64 rev0 = Logger::revision();

        const QString bodyMarker = QStringLiteral("SECRET-BODY-MARKER");
        server.enqueue({openaiTextBody(), false,
                        "\"content\":\"SECRET-BODY-MARKER\""});
        service.chatComplete(userMessages(bodyMarker), {});
        QVERIFY2(spy.wait(5000), "no response");

        const auto entries = Logger::entriesSince(rev0);
        QVERIFY2(!entries.isEmpty(), "no log entries captured");
        bool sawNetwork = false;
        for (const auto& e : entries) {
            QVERIFY2(!e.message.contains(bodyMarker),
                     qPrintable(QStringLiteral("body content leaked into log: %1")
                                    .arg(e.message)));
            if (e.category == QStringLiteral("nexus.network"))
                sawNetwork = true;
        }
        QVERIFY2(sawNetwork, "no network category events logged");
    }

    // TEST-003: request A is superseded by request B before it completes;
    // A's late bytes must never touch B's stream state nor swallow B's
    // response. The release of B's response is gated on the client having
    // fully handled (and closed) A's reply, so the outcome is deterministic:
    // buggy code emits A's stale result first and then drops B's; fixed code
    // discards A silently and delivers B.
    void staleReplyDoesNotPolluteNewRequest() {
        FakeSseServer server;
        OpenAIService service;
        service.setBaseUrl(server.baseUrl());
        QSignalSpy spy(&service, &IAIService::responseReady);

        server.enqueue({openaiToolBody(), true, kMatchA});   // held: request A
        server.enqueue({openaiTextBody(), true, kMatchB});   // held: request B
        service.chatComplete(userMessages(QStringLiteral("hi")), {});
        service.chatComplete(userMessages(QStringLiteral("hi2")), {});
        QTRY_COMPARE_WITH_TIMEOUT(server.heldCount(), 2, 5000);

        server.releaseHeld(0);      // A's body arrives late...
        // ...and must be FULLY processed (or silently discarded) before the
        // live request is answered.
        QTRY_COMPARE_WITH_TIMEOUT(server.releasedClosed(0), 1, 10000);
        server.releaseHeld(1);      // B's actual response

        QVERIFY2(spy.wait(5000), "no response for request B");
        QTest::qWait(200);          // no second (stale) emission expected
        QCOMPARE(spy.count(), 1);
        const LLMResponse response = takeResponse(spy, 0);
        QCOMPARE(response.content, QStringLiteral("Hello"));
        QVERIFY2(response.toolCalls.isEmpty(),
                 "the superseded request's tool call leaked into the live request");
    }
};

QTEST_GUILESS_MAIN(TestProviderLifecycle)
#include "test_provider_lifecycle.moc"
