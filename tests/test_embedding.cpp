// OllamaEmbedding batching/progress coverage (Batch 8, FEAT-803).
// The pagination helper is pure; the request loop runs against a scripted
// local HTTP server so success, progress accounting and fail-fast behavior
// are all covered without a real Ollama instance.
#include "infrastructure/embed/OllamaEmbedding.h"
#include "infrastructure/embed/OpenAICompatibleEmbedding.h"
#include "infrastructure/embed/EmbedShutdown.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace NexusAI;

namespace {

// Minimal local server: answers every request with an /api/embed response
// carrying `dim`-dimensional vectors; counts requests.
class FakeEmbedServer : public QObject {
    Q_OBJECT
public:
    // delayMs: per-request artificial latency, used to keep a batched
    // embedding run open while the test mutates the client configuration.
    explicit FakeEmbedServer(int dim, int delayMs = 0, QObject* parent = nullptr)
        : QObject(parent), m_dim(dim), m_delayMs(delayMs) {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QTcpSocket::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    QByteArray& buffer = m_buffer[socket];
                    buffer += socket->readAll();
                    if (!requestComplete(buffer)) return;
                    const int bodyStart = buffer.indexOf("\r\n\r\n") + 4;
                    const QByteArray bodyBytes = buffer.mid(bodyStart);
                    const QStringList texts = parseInputs(bodyBytes);
                    // docs/22 F3: record which model each request carried.
                    m_modelsSeen.append(QJsonDocument::fromJson(bodyBytes)
                        .object().value(QStringLiteral("model")).toString());
                    buffer.clear();
                    ++m_requests;
                    // 任务 #22 取消用例：挂起模式——请求已完整接收但
                    // 不响应（连接保持），中断由客户端退出轮询 abort。
                    if (m_hang) return;
                    // 任务 #22 B1 用例：统一回 401（客户端错误）。
                    if (m_failAllUnauthorized) {
                        socket->write("HTTP/1.1 401 Unauthorized\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n\r\n");
                        socket->flush();
                        socket->disconnectFromHost();
                        return;
                    }
                    QJsonObject body;
                    // B4-B：第 2 个及以后的请求可切换返回维度（模拟模型
                    // 切换/服务异常导致的跨批异维）。
                    const int dim = (m_requests > 1 && m_dimAfterFirst >= 0)
                                        ? m_dimAfterFirst : m_dim;
                    if (m_returnSingleEmbedding && texts.size() == 1) {
                        // 单条 embed 响应形态：{"embedding": [...]}，用于
                        // 验证 OllamaEmbedding::embed() 解析。
                        QJsonArray vec;
                        for (int d = 0; d < dim; ++d)
                            vec.append(0.25);
                        body["embedding"] = vec;
                    } else {
                        QJsonArray embeddings;
                        const int count = texts.size() + m_countOffset;
                        for (int i = 0; i < count; ++i) {
                            QJsonArray vec;
                            for (int d = 0; d < dim; ++d)
                                vec.append(0.25);
                            if (m_indexEncoded) {
                                QJsonObject entry;
                                entry["index"] = i;
                                entry["embedding"] = vec;
                                embeddings.append(entry);
                            } else {
                                embeddings.append(vec);
                            }
                        }
                        body["embeddings"] = embeddings;
                    }
                    if (m_delayMs > 0)
                        QTest::qSleep(m_delayMs);
                    // docs/24 H5: optionally answer 500 for the first N
                    // requests (transport-error retry coverage).
                    if (m_requests <= m_failFirstRequests) {
                        socket->write("HTTP/1.1 500 Internal Server Error\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n\r\n");
                        socket->flush();
                        socket->disconnectFromHost();
                        return;
                    }
                    respond(socket, QJsonDocument(body).toJson(QJsonDocument::Compact));
                });
            }
        });
        if (!m_server.listen(QHostAddress::LocalHost))
            qWarning() << "FakeEmbedServer listen failed:" << m_server.errorString();
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    // docs/24 H5: fail the first N requests with HTTP 500.
    void setFailFirstRequests(int n) { m_failFirstRequests = n; }
    // 任务 #22 取消用例：收到请求后挂起不响应。
    void setHang(bool hang) { m_hang = hang; }
    // 任务 #22 B1 用例：所有请求回 401（客户端错误不重试）。
    void setFailAllUnauthorized(bool fail) { m_failAllUnauthorized = fail; }
    // 协议错误注入：data 条数相对 input 的偏移（负数=少几条）。
    void setCountOffset(int offset) { m_countOffset = offset; }
    // 单条 embed 响应形态开关：true 时返回 {"embedding": [...]} 而非
    // {"embeddings": [[...]]}，验证 OllamaEmbedding::embed() 解析。
    void setReturnSingleEmbedding(bool single) { m_returnSingleEmbedding = single; }
    // 索引编码开关：true 时 embeddings 数组元素为 {"index":i,"embedding":[...]}
    // 而非纯数组，验证 OllamaEmbedding::embedBatch() 对两种形态的容错。
    void setIndexEncoded(bool encoded) { m_indexEncoded = encoded; }
    // B4-B：从第 2 个请求起改用 dim 维向量（跨批异维注入）。
    void setDimAfterFirst(int dim) { m_dimAfterFirst = dim; }
    int requests() const { return m_requests; }
    const QStringList& modelsSeen() const { return m_modelsSeen; }

private:
    static bool requestComplete(const QByteArray& buffer) {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return false;
        int contentLength = 0;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        for (const QByteArray& line : lines) {
            if (line.toLower().startsWith("content-length:")) {
                contentLength = line.mid(15).trimmed().toInt();
                break;
            }
        }
        return buffer.size() - (headerEnd + 4) >= contentLength;
    }

    static QStringList parseInputs(const QByteArray& body) {
        // 兼容两种请求形态：batch 用 {"input": ["a","b",...]}，
        // 单条 embed 用 {"prompt": "..."}（OllamaEmbedding::embed 的格式）。
        QStringList texts;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonValue input = doc.object().value("input");
        if (input.isArray()) {
            for (const auto& item : input.toArray())
                texts.append(item.toString());
        } else {
            const QJsonValue prompt = doc.object().value("prompt");
            if (prompt.isString())
                texts.append(prompt.toString());
        }
        return texts;
    }

    void respond(QTcpSocket* socket, const QByteArray& body) {
        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                      "Connection: close\r\n"
                      "\r\n" + body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffer;
    int m_dim;
    int m_delayMs{0};
    int m_failFirstRequests{0};
    bool m_hang{false};
    bool m_failAllUnauthorized{false};
    int m_countOffset{0};
    int m_dimAfterFirst{-1};
    bool m_indexEncoded{false};
    bool m_returnSingleEmbedding{false};
    int m_requests{0};
    QStringList m_modelsSeen;
};

QStringList makeTexts(int n) {
    QStringList texts;
    for (int i = 0; i < n; ++i)
        texts.append(QStringLiteral("text %1").arg(i));
    return texts;
}

// B4-B：捕获告警文本，用于断言维度失败的错误信息包含 expected/actual
//（失败语义本身由返回空容器保证；这里只验证错误可读）。
QStringList g_warningMessages;
QtMessageHandler g_previousHandler = nullptr;

void captureWarningHandler(QtMsgType type, const QMessageLogContext& ctx,
                           const QString& msg) {
    if (type == QtWarningMsg || type == QtCriticalMsg)
        g_warningMessages.append(msg);
    if (g_previousHandler) g_previousHandler(type, ctx, msg);
}

class ScopedWarningCapture {
public:
    ScopedWarningCapture() {
        g_warningMessages.clear();
        g_previousHandler = qInstallMessageHandler(&captureWarningHandler);
    }
    ~ScopedWarningCapture() { qInstallMessageHandler(g_previousHandler); }
    QStringList messages() const { return g_warningMessages; }
};

// OpenAI-compatible fake (task #16): answers /v1/embeddings with
// {"data": [{"index": i, "embedding": [...]}]}. Vector values are encoded
// from the index (vec[d] = index + 1 + d*0.25) so request-order correctness
// is assertable even when data comes back shuffled; the request path and
// Authorization header are recorded for the URL-joining/Bearer cases.
class FakeOpenAIEmbedServer : public QObject {
    Q_OBJECT
public:
    explicit FakeOpenAIEmbedServer(int dim, QObject* parent = nullptr)
        : QObject(parent), m_dim(dim) {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QTcpSocket::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    QByteArray& buffer = m_buffer[socket];
                    buffer += socket->readAll();
                    if (!requestComplete(buffer)) return;
                    const int headerEnd = buffer.indexOf("\r\n\r\n");
                    recordRequest(buffer.left(headerEnd),
                                  buffer.mid(headerEnd + 4));
                    buffer.clear();
                    ++m_requests;
                    // 任务 #22 取消用例：挂起模式——请求已完整接收但
                    // 不响应（连接保持），中断由客户端退出轮询 abort。
                    if (m_hang) return;
                    // 任务 #22 B1 用例：统一回 401（客户端错误）。
                    if (m_failAllUnauthorized) {
                        socket->write("HTTP/1.1 401 Unauthorized\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n\r\n");
                        socket->flush();
                        socket->disconnectFromHost();
                        return;
                    }
                    // docs/24 H5: fail the first N requests with HTTP 500
                    // (transport-error retry coverage).
                    if (m_requests <= m_failFirstRequests) {
                        socket->write("HTTP/1.1 500 Internal Server Error\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n\r\n");
                        socket->flush();
                        socket->disconnectFromHost();
                        return;
                    }
                    respond(socket);
                });
            }
        });
        if (!m_server.listen(QHostAddress::LocalHost))
            qWarning() << "FakeOpenAIEmbedServer listen failed:"
                       << m_server.errorString();
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    // docs/24 H5: fail the first N requests with HTTP 500.
    void setFailFirstRequests(int n) { m_failFirstRequests = n; }
    // 任务 #22 取消用例：收到请求后挂起不响应。
    void setHang(bool hang) { m_hang = hang; }
    // 任务 #22 B1 用例：所有请求回 401（客户端错误不重试）。
    void setFailAllUnauthorized(bool fail) { m_failAllUnauthorized = fail; }
    // 稳定乱序：data 按 index 降序返回（index 归位逻辑可断言）。
    void setReverseIndices(bool reverse) { m_reverseIndices = reverse; }
    // 协议错误注入：HTTP 200 但响应不写 data。
    void setDropData(bool drop) { m_dropData = drop; }
    // 协议错误注入：data 条数相对 input 的偏移（负数=少几条）。
    void setCountOffset(int offset) { m_countOffset = offset; }
    // B4-B：从第 2 个请求起改用 dim 维向量（跨批异维注入）。
    void setDimAfterFirst(int dim) { m_dimAfterFirst = dim; }
    int requests() const { return m_requests; }
    const QString& lastPath() const { return m_lastPath; }
    const QString& lastAuthorization() const { return m_lastAuthorization; }

private:
    static bool requestComplete(const QByteArray& buffer) {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return false;
        int contentLength = 0;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        for (const QByteArray& line : lines) {
            if (line.toLower().startsWith("content-length:")) {
                contentLength = line.mid(15).trimmed().toInt();
                break;
            }
        }
        return buffer.size() - (headerEnd + 4) >= contentLength;
    }

    // 请求行（"POST /v1/embeddings HTTP/1.1"）取路径；头里找 Authorization；
    // body 的 input 兼容单条（字符串）与批量（数组）两种形态。
    void recordRequest(const QByteArray& head, const QByteArray& bodyBytes) {
        const QByteArray requestLine = head.left(head.indexOf("\r\n"));
        const QList<QByteArray> parts = requestLine.split(' ');
        m_lastPath = parts.size() > 1 ? QString::fromLatin1(parts.at(1))
                                      : QString();
        m_lastAuthorization.clear();
        const QList<QByteArray> lines = head.split('\n');
        for (const QByteArray& line : lines) {
            if (line.toLower().startsWith("authorization:"))
                m_lastAuthorization = QString::fromLatin1(line.mid(14).trimmed());
        }
        const QJsonDocument doc = QJsonDocument::fromJson(bodyBytes);
        const QJsonValue input = doc.object().value("input");
        m_inputCount = input.isString() ? 1
                                         : static_cast<int>(input.toArray().size());
    }

    void respond(QTcpSocket* socket) {
        QJsonObject body;
        if (!m_dropData) {
            // B4-B：第 2 个及以后的请求可切换返回维度。
            const int dim = (m_requests > 1 && m_dimAfterFirst >= 0)
                                ? m_dimAfterFirst : m_dim;
            const int count = m_inputCount + m_countOffset;
            QJsonArray data;
            for (int i = 0; i < count; ++i) {
                // 乱序形态：data 数组顺序与 index 相反；向量值仍按真实
                // index 编码，客户端按 index 归位后顺序必须与请求一致。
                const int index = m_reverseIndices ? (count - 1 - i) : i;
                QJsonArray vec;
                for (int d = 0; d < dim; ++d)
                    vec.append(index + 1 + d * 0.25);
                QJsonObject item;
                item["index"] = index;
                item["embedding"] = vec;
                data.append(item);
            }
            body["data"] = data;
        }
        const QByteArray payload =
            QJsonDocument(body).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
                      "Connection: close\r\n"
                      "\r\n" + payload);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffer;
    int m_dim;
    int m_failFirstRequests{0};
    int m_countOffset{0};
    bool m_hang{false};
    bool m_failAllUnauthorized{false};
    int m_requests{0};
    int m_inputCount{0};
    int m_dimAfterFirst{-1};
    bool m_reverseIndices{false};
    bool m_dropData{false};
    QString m_lastPath;
    QString m_lastAuthorization;
};

} // namespace

class TestEmbedding : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    }

    // 任务 #22：退出标志是进程级全局状态——取消用例置位后若因断言失
    // 败提前返回，残留标志会短路后续所有用例的入口检查，这里在每个
    // 用例开始前兜底复位（用例结尾也各自复位，双保险）。
    void init() {
        resetEmbeddingShutdownForTesting();
    }

    // FEAT-803: batching boundaries
    void splitBatchesBoundaries() {
        QVERIFY(OllamaEmbedding::splitBatches({}, 32).isEmpty());

        const QStringList one = makeTexts(1);
        QCOMPARE(OllamaEmbedding::splitBatches(one, 32).size(), 1);

        const QStringList exact = makeTexts(32);
        QCOMPARE(OllamaEmbedding::splitBatches(exact, 32).size(), 1);

        const QStringList over = makeTexts(33);
        const auto batches = OllamaEmbedding::splitBatches(over, 32);
        QCOMPARE(batches.size(), 2);
        QCOMPARE(batches.at(0).size(), 32);
        QCOMPARE(batches.at(1).size(), 1);
        QCOMPARE(batches.at(0).first(), over.first());
        QCOMPARE(batches.at(1).first(), over.last());
    }

    // 33 texts → two paged requests, progress (32,33) then (33,33),
    // result order preserved.
    void pagedRequestsWithProgress() {
        FakeEmbedServer server(8);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QStringList texts = makeTexts(33);
        QSignalSpy progress(&embed, &OllamaEmbedding::embedProgress);

        const QVector<QVector<float>> result = embed.embedBatch(texts);
        QCOMPARE(result.size(), 33);
        QCOMPARE(result.first().size(), 8);
        QCOMPARE(server.requests(), 2);

        QCOMPARE(progress.count(), 2);
        QCOMPARE(progress.at(0).at(0).toInt(), 32);
        QCOMPARE(progress.at(0).at(1).toInt(), 33);
        QCOMPARE(progress.at(1).at(0).toInt(), 33);
        QCOMPARE(progress.at(1).at(1).toInt(), 33);
    }

    // Any failed batch aborts with an empty result (wholesale pseudo
    // fallback contract) — here the server is unreachable.
    void deadEndpointFailsFastWithEmptyResult() {
        OllamaEmbedding embed;
        embed.setBaseUrl(QStringLiteral("http://127.0.0.1:1"));

        QSignalSpy progress(&embed, &OllamaEmbedding::embedProgress);
        const QVector<QVector<float>> result =
            embed.embedBatch(makeTexts(OllamaEmbedding::BatchSize + 1));
        QVERIFY(result.isEmpty());
        QVERIFY(progress.isEmpty());   // nothing completed → nothing reported
    }

    // Batch 25 (G11): setBaseUrl redirects subsequent requests to the
    // configured endpoint (the settings-level switch never re-embeds, so a
    // live redirect must be enough for the next request to land there).
    void customEndpointReceivesRequests() {
        FakeEmbedServer first(8);
        FakeEmbedServer second(8);
        OllamaEmbedding embed;

        embed.setBaseUrl(first.baseUrl());
        QCOMPARE(embed.embedBatch(makeTexts(2)).size(), 2);
        QCOMPARE(first.requests(), 1);

        embed.setBaseUrl(second.baseUrl());
        QCOMPARE(embed.embedBatch(makeTexts(2)).size(), 2);
        QCOMPARE(first.requests(), 1);   // no new traffic to the old endpoint
        QCOMPARE(second.requests(), 1);
    }

    // docs/22 F3: setModel mid-import must not disturb the running
    // embedBatch — the call snapshots model/endpoint at entry; the swap
    // lands on the next call. No torn QString / crash across the swap is
    // the core contract (the race itself is not reproducible without TSAN).
    void setModelWhileInFlightUsesSnapshot() {
        FakeEmbedServer server(4, /*delayMs=*/25);
        OllamaEmbedding embedder;
        embedder.setBaseUrl(server.baseUrl());
        embedder.setModel(QStringLiteral("model-a"));

        const QStringList texts = makeTexts(96);   // 3 batches × ~25ms
        QVector<QVector<float>> result;
        QThread* worker = QThread::create([&embedder, &texts, &result]() {
            result = embedder.embedBatch(texts);
        });
        QObject::connect(worker, &QThread::finished, worker,
                         &QThread::deleteLater);
        worker->start();

        QTest::qWait(40);   // in-flight, inside batch 1 or 2
        embedder.setModel(QStringLiteral("model-b"));
        QVERIFY(QTest::qWaitFor([worker]() { return worker->isFinished(); },
                                30000));

        // The whole run completed against the entry snapshot.
        QCOMPARE(result.size(), texts.size());
        QCOMPARE(server.requests(), 3);
        QVERIFY(!server.modelsSeen().contains(QStringLiteral("model-b")));

        // The next call picks up the swapped model.
        QCOMPARE(embedder.embedBatch(makeTexts(1)).size(), 1);
        QCOMPARE(server.modelsSeen().last(), QStringLiteral("model-b"));
    }

    // docs/24 H5: one transport-error retry per batch — a single blip must
    // not degrade the whole document to pseudo embeddings permanently.
    void batchTransportErrorRetriedOnce() {
        FakeEmbedServer server(4);
        server.setFailFirstRequests(1);   // first request → HTTP 500

        OllamaEmbedding embedder;
        embedder.setBaseUrl(server.baseUrl());
        embedder.setModel(QStringLiteral("m"));

        QSignalSpy progress(&embedder, &OllamaEmbedding::embedProgress);
        // One batch of 2: request 1 fails, retry succeeds.
        const QVector<QVector<float>> result = embedder.embedBatch(makeTexts(2));
        QCOMPARE(result.size(), 2);
        QCOMPARE(server.requests(), 2);
        QCOMPARE(progress.count(), 1);   // progress only after success
    }

    // ── OpenAICompatibleEmbedding（外部向量模型，任务 #16）──

    // 单条 embed：OpenAI 格式响应解析 + model() 的 "openai:" 前缀标识
    // （切换后端时旧持久化 chunk 依此失效，向量空间不混用）。
    void openaiSingleEmbedParsesResponse() {
        FakeOpenAIEmbedServer server(4);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());
        embed.setModel(QStringLiteral("text-embedding-3-small"));
        QCOMPARE(embed.model(), QStringLiteral("openai:text-embedding-3-small"));

        const QVector<float> vec = embed.embed(QStringLiteral("hello"));
        QCOMPARE(vec.size(), 4);
        // fake 的向量按 index 编码：vec[d] = index + 1 + d*0.25（index=0）
        QCOMPARE(vec.at(0), 1.0f);
        QCOMPARE(vec.at(1), 1.25f);
        QCOMPARE(vec.at(3), 1.75f);
    }

    // Bearer 鉴权头 + URL 拼接容错：baseUrl 带/不带 /v1、带尾斜杠三种
    // 形态都必须命中 .../v1/embeddings。
    void openaiBearerHeaderAndUrlJoining() {
        FakeOpenAIEmbedServer server(2);
        OpenAICompatibleEmbedding embed;
        embed.setApiKey(QStringLiteral("sk-test-123"));

        // ① baseUrl 不带 /v1 → 拼 /v1/embeddings
        embed.setBaseUrl(server.baseUrl());
        QVERIFY(!embed.embed(QStringLiteral("a")).isEmpty());
        QCOMPARE(server.lastPath(), QStringLiteral("/v1/embeddings"));
        QCOMPARE(server.lastAuthorization(), QStringLiteral("Bearer sk-test-123"));

        // ② baseUrl 已带 /v1 → 不再重复拼（不能出现 /v1/v1/embeddings）
        embed.setBaseUrl(server.baseUrl() + QStringLiteral("/v1"));
        QVERIFY(!embed.embed(QStringLiteral("b")).isEmpty());
        QCOMPARE(server.lastPath(), QStringLiteral("/v1/embeddings"));

        // ③ 尾部多余斜杠 → 归一后拼接
        embed.setBaseUrl(server.baseUrl() + QStringLiteral("/"));
        QVERIFY(!embed.embed(QStringLiteral("c")).isEmpty());
        QCOMPARE(server.lastPath(), QStringLiteral("/v1/embeddings"));
        QCOMPARE(server.requests(), 3);
    }

    // key 为空：仍发请求（部分兼容网关无鉴权；让服务端 401 拒绝），
    // 且请求不带 Authorization 头。
    void openaiEmptyKeyStillSendsRequest() {
        FakeOpenAIEmbedServer server(2);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QVERIFY(!embed.embed(QStringLiteral("x")).isEmpty());
        QCOMPARE(server.lastAuthorization(), QString());
        QCOMPARE(server.requests(), 1);
    }

    // 批量：data 乱序返回（index 降序）时按 index 归位，结果顺序必须
    // 与请求一致（协议不保证服务端保序，实测部分网关乱序返回）。
    void openaiBatchShuffledIndicesReordered() {
        FakeOpenAIEmbedServer server(2);
        server.setReverseIndices(true);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        const QVector<QVector<float>> result = embed.embedBatch(makeTexts(3));
        QCOMPARE(result.size(), 3);
        QCOMPARE(result.at(0).at(0), 1.0f);   // 第 1 条输入 → index 0 向量
        QCOMPARE(result.at(1).at(0), 2.0f);   // 第 2 条输入 → index 1 向量
        QCOMPARE(result.at(2).at(0), 3.0f);   // 第 3 条输入 → index 2 向量
    }

    // 协议错误 fail-fast（对齐 OllamaEmbedding 语义，docs/24 H5）：HTTP
    // 200 但 data 缺失 / 数量不符 → 空容器，不重试。
    void openaiBatchProtocolErrorsFailFast() {
        {   // 响应缺 data
            FakeOpenAIEmbedServer server(2);
            server.setDropData(true);
            OpenAICompatibleEmbedding embed;
            embed.setBaseUrl(server.baseUrl());
            QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
            QCOMPARE(server.requests(), 1);   // fail-fast：无第二次请求
        }
        {   // data 数量与 input 不符
            FakeOpenAIEmbedServer server(2);
            server.setCountOffset(-1);
            OpenAICompatibleEmbedding embed;
            embed.setBaseUrl(server.baseUrl());
            QVERIFY(embed.embedBatch(makeTexts(3)).isEmpty());
        }
    }

    // docs/24 H5 对齐：批级传输错误重试一次（500ms 退避），单次瞬时故障
    // 不再把整篇文档永久降级为伪向量。
    void openaiBatchTransportErrorRetriedOnce() {
        FakeOpenAIEmbedServer server(2);
        server.setFailFirstRequests(1);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QSignalSpy progress(&embed, &OpenAICompatibleEmbedding::embedProgress);
        const QVector<QVector<float>> result = embed.embedBatch(makeTexts(2));
        QCOMPARE(result.size(), 2);
        QCOMPARE(server.requests(), 2);   // 一次失败 + 一次重试成功
        QCOMPARE(progress.count(), 1);    // 进度只在批成功后报告
    }

    // splitBatches 静态方法与 Ollama 版语义一致（FEAT-803 分页边界）。
    void openaiSplitBatchesBoundaries() {
        QVERIFY(OpenAICompatibleEmbedding::splitBatches({}, 32).isEmpty());
        const QStringList texts = makeTexts(33);
        const auto batches = OpenAICompatibleEmbedding::splitBatches(texts, 32);
        QCOMPARE(batches.size(), 2);
        QCOMPARE(batches.at(0).size(), 32);
        QCOMPARE(batches.at(1).size(), 1);
    }

    // ── 任务 #22：退出协作取消 + 4xx 不重试 ──

    // 入口短路：标志置位后 embed/embedBatch 不发任何请求直接返回空
    //（两个后端对称断言——排队中的调用在进程退出时不白跑）。
    void shutdownFlagShortCircuitsEntry() {
        requestEmbeddingShutdown();
        {
            FakeOpenAIEmbedServer server(2);
            OpenAICompatibleEmbedding embed;
            embed.setBaseUrl(server.baseUrl());
            QVERIFY(embed.embed(QStringLiteral("x")).isEmpty());
            QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
            QCOMPARE(server.requests(), 0);
        }
        {
            FakeEmbedServer server(2);
            OllamaEmbedding embed;
            embed.setBaseUrl(server.baseUrl());
            QVERIFY(embed.embed(QStringLiteral("x")).isEmpty());
            QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
            QCOMPARE(server.requests(), 0);
        }
        resetEmbeddingShutdownForTesting();
    }

    // 轮询中断（OpenAI 单条）：server 挂起 + 500ms 后置位退出标志 →
    // embed 在一个轮询周期（200ms）内返回空，而不是等 60s 传输超时
    //（这正是退出卡住修复 A 的核心路径）。
    void openaiShutdownInterruptsHangingEmbed() {
        FakeOpenAIEmbedServer server(2);
        server.setHang(true);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QTimer::singleShot(500, []() { requestEmbeddingShutdown(); });
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(embed.embed(QStringLiteral("hang")).isEmpty());
        QVERIFY(elapsed.elapsed() < 5000);
        QCOMPARE(server.requests(), 1);
        resetEmbeddingShutdownForTesting();
    }

    // 批量中断（OpenAI）：第一批挂起时置位 → 在途批被 abort、后续批
    // 不再发送（70 条 = 3 批，只应看到第 1 批的 1 个请求）。
    void openaiShutdownInterruptsHangingBatch() {
        FakeOpenAIEmbedServer server(2);
        server.setHang(true);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QTimer::singleShot(500, []() { requestEmbeddingShutdown(); });
        const QVector<QVector<float>> result = embed.embedBatch(makeTexts(70));
        QVERIFY(result.isEmpty());
        QCOMPARE(server.requests(), 1);
        resetEmbeddingShutdownForTesting();
    }

    // 轮询中断（Ollama 单条，对称验证；Ollama 超时 30s，修复后同样在
    // 一个轮询周期内返回空）。
    void ollamaShutdownInterruptsHangingEmbed() {
        FakeEmbedServer server(2);
        server.setHang(true);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QTimer::singleShot(500, []() { requestEmbeddingShutdown(); });
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(embed.embed(QStringLiteral("hang")).isEmpty());
        QVERIFY(elapsed.elapsed() < 5000);
        QCOMPARE(server.requests(), 1);
        resetEmbeddingShutdownForTesting();
    }

    // 4xx 不重试（OpenAI）：401 是客户端错误，同样的请求再发一次注定
    // 再失败——只应收到 1 次请求（修复前 attempt 循环会发满 2 次）。
    // 5xx 重试语义由 openaiBatchTransportErrorRetriedOnce 继续覆盖。
    void openaiClientErrorNotRetried() {
        FakeOpenAIEmbedServer server(2);
        server.setFailAllUnauthorized(true);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
        QCOMPARE(server.requests(), 1);
    }

    // 4xx 不重试（Ollama，对称验证；Ollama 典型 4xx 是 404 模型不存
    // 在，这里同样以 401 注入）。
    void ollamaClientErrorNotRetried() {
        FakeEmbedServer server(2);
        server.setFailAllUnauthorized(true);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
        QCOMPARE(server.requests(), 1);
    }

    // ── Ollama 对称测试（对齐 OpenAI 既有用例）──

    // 单条 embed：Ollama 单数响应形态 {"embedding": [...]} 解析。
    // 与 openaiSingleEmbedParsesResponse 对称——验证 OllamaEmbedding::embed()
    // 能正确解析单数响应，而非仅支持批量 {"embeddings": [...]} 形态。
    void ollamaSingleEmbedParsesResponse() {
        FakeEmbedServer server(4);
        server.setReturnSingleEmbedding(true);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());
        embed.setModel(QStringLiteral("nomic-embed-text"));

        const QVector<float> vec = embed.embed(QStringLiteral("hello"));
        QCOMPARE(vec.size(), 4);
        // FakeEmbedServer 固定回 0.25
        QCOMPARE(vec.at(0), 0.25f);
        QCOMPARE(vec.at(1), 0.25f);
        QCOMPARE(vec.at(3), 0.25f);
    }

    // 协议错误 fail-fast（对齐 openaiBatchProtocolErrorsFailFast）：
    // data 条数不匹配 / 索引编码形态（Ollama 不认）→ 空容器，不重试。
    void ollamaBatchProtocolErrorsFailFast() {
        {   // data 条数比 input 少一条 → size mismatch 短路
            FakeEmbedServer server(4);
            server.setCountOffset(-1);
            OllamaEmbedding embed;
            embed.setBaseUrl(server.baseUrl());
            QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
            QCOMPARE(server.requests(), 1);   // fail-fast：无重试
        }
        {   // 索引编码形态（{"index":i,"embedding":[...]}）→ 元素非数组
            // → toArray() 返回空 → vec.isEmpty() 短路
            FakeEmbedServer server(4);
            server.setIndexEncoded(true);
            OllamaEmbedding embed;
            embed.setBaseUrl(server.baseUrl());
            QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
            QCOMPARE(server.requests(), 1);
        }
    }

    // 轮询中断（Ollama 批量，对称于 openaiShutdownInterruptsHangingBatch）：
    // server 挂起 + 500ms 后置位退出标志 → 在途批被 abort、后续批不再发送。
    void ollamaShutdownInterruptsHangingBatch() {
        FakeEmbedServer server(2);
        server.setHang(true);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QTimer::singleShot(500, []() { requestEmbeddingShutdown(); });
        const QVector<QVector<float>> result = embed.embedBatch(makeTexts(70));
        QVERIFY(result.isEmpty());
        QCOMPARE(server.requests(), 1);
        resetEmbeddingShutdownForTesting();
    }

    // ── B4-B：嵌入维度一致性校验 ──

    // 两批返回不同维度（8 vs 4）→ 整次调用失败、结果为空的空容器
    //（不部分写入），错误日志同时给出 expected/actual。
    void ollamaBatchDimensionMismatchFailsWholesale() {
        FakeEmbedServer server(8);
        server.setDimAfterFirst(4);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        ScopedWarningCapture capture;
        const QVector<QVector<float>> result =
            embed.embedBatch(makeTexts(OllamaEmbedding::BatchSize + 1));
        QVERIFY2(result.isEmpty(), "mismatched dimension must abort the run");
        QCOMPARE(server.requests(), 2);   // 协议错误 fail-fast，无重试

        const QString log = capture.messages().join(QLatin1Char('\n'));
        QVERIFY2(log.contains(QStringLiteral("expected 8")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("actual 4")), qPrintable(log));
    }

    // 同一维度跨批正常：首批维度作为全 run 基准，结果完整返回。
    void ollamaBatchSameDimensionAcrossBatchesSucceeds() {
        FakeEmbedServer server(8);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        const QVector<QVector<float>> result =
            embed.embedBatch(makeTexts(OllamaEmbedding::BatchSize + 1));
        QCOMPARE(result.size(), OllamaEmbedding::BatchSize + 1);
        QCOMPARE(server.requests(), 2);
        for (const QVector<float>& vec : result)
            QCOMPARE(vec.size(), 8);
    }

    // 空向量视为失败（即使条数匹配）。
    void ollamaBatchEmptyVectorFails() {
        FakeEmbedServer server(0);   // 每条向量都是 []
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
        QCOMPARE(server.requests(), 1);
    }

    // 空输入批：不发请求，直接按失败返回。
    void ollamaEmptyInputBatchFails() {
        OllamaEmbedding embed;
        QVERIFY(embed.embedBatch({}).isEmpty());
    }

    // 单条调用同样要求维度 > 0。
    void ollamaSingleEmbedEmptyVectorFails() {
        FakeEmbedServer server(0);
        server.setReturnSingleEmbedding(true);
        OllamaEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QVERIFY(embed.embed(QStringLiteral("x")).isEmpty());
    }

    // OpenAI 后端对称：跨批异维 → 整次失败、结果为空、日志含具体维度。
    void openaiBatchDimensionMismatchFailsWholesale() {
        FakeOpenAIEmbedServer server(8);
        server.setDimAfterFirst(3);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        ScopedWarningCapture capture;
        const QVector<QVector<float>> result =
            embed.embedBatch(makeTexts(OpenAICompatibleEmbedding::BatchSize + 1));
        QVERIFY2(result.isEmpty(), "mismatched dimension must abort the run");
        QCOMPARE(server.requests(), 2);

        const QString log = capture.messages().join(QLatin1Char('\n'));
        QVERIFY2(log.contains(QStringLiteral("expected 8")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("actual 3")), qPrintable(log));
    }

    // OpenAI 同维度跨批正常返回。
    void openaiBatchSameDimensionAcrossBatchesSucceeds() {
        FakeOpenAIEmbedServer server(4);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        const QVector<QVector<float>> result =
            embed.embedBatch(makeTexts(OpenAICompatibleEmbedding::BatchSize + 1));
        QCOMPARE(result.size(), OpenAICompatibleEmbedding::BatchSize + 1);
        QCOMPARE(server.requests(), 2);
        for (const QVector<float>& vec : result)
            QCOMPARE(vec.size(), 4);
    }

    // OpenAI 空向量视为失败。
    void openaiBatchEmptyVectorFails() {
        FakeOpenAIEmbedServer server(0);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QVERIFY(embed.embedBatch(makeTexts(2)).isEmpty());
        QCOMPARE(server.requests(), 1);
    }

    // OpenAI 空输入批直接失败。
    void openaiEmptyInputBatchFails() {
        OpenAICompatibleEmbedding embed;
        QVERIFY(embed.embedBatch({}).isEmpty());
    }

    // OpenAI 单条调用同样要求维度 > 0。
    void openaiSingleEmbedEmptyVectorFails() {
        FakeOpenAIEmbedServer server(0);
        OpenAICompatibleEmbedding embed;
        embed.setBaseUrl(server.baseUrl());

        QVERIFY(embed.embed(QStringLiteral("x")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestEmbedding)
#include "test_embedding.moc"
