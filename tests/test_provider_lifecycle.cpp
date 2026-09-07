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
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "infrastructure/llm/AnthropicService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/OpenAIService.h"

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
        socket->write("HTTP/1.1 " + QByteArray::number(script.status) + " OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Content-Length: " + QByteArray::number(script.body.size()) + "\r\n"
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
