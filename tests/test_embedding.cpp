// OllamaEmbedding batching/progress coverage (Batch 8, FEAT-803).
// The pagination helper is pure; the request loop runs against a scripted
// local HTTP server so success, progress accounting and fail-fast behavior
// are all covered without a real Ollama instance.
#include "infrastructure/embed/OllamaEmbedding.h"

#include <QHostAddress>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QThread>
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
                    QJsonArray embeddings;
                    for (int i = 0; i < texts.size(); ++i) {
                        QJsonArray vec;
                        for (int d = 0; d < m_dim; ++d)
                            vec.append(0.25);
                        embeddings.append(vec);
                    }
                    QJsonObject body;
                    body["embeddings"] = embeddings;
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
        // The request body is indented JSON: {"input": ["a", "b", ...]}
        QStringList texts;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonArray input = doc.object().value("input").toArray();
        for (const auto& item : input)
            texts.append(item.toString());
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
    int m_requests{0};
    QStringList m_modelsSeen;
};

QStringList makeTexts(int n) {
    QStringList texts;
    for (int i = 0; i < n; ++i)
        texts.append(QStringLiteral("text %1").arg(i));
    return texts;
}

} // namespace

class TestEmbedding : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
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
};

QTEST_GUILESS_MAIN(TestEmbedding)
#include "test_embedding.moc"
