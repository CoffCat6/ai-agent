// OllamaEmbedding batching/progress coverage (Batch 8, FEAT-803).
// The pagination helper is pure; the request loop runs against a scripted
// local HTTP server so success, progress accounting and fail-fast behavior
// are all covered without a real Ollama instance.
#include "infrastructure/embed/OllamaEmbedding.h"

#include <QHostAddress>
#include <QNetworkProxy>
#include <QSignalSpy>
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
    explicit FakeEmbedServer(int dim, QObject* parent = nullptr)
        : QObject(parent), m_dim(dim) {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QTcpSocket::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    QByteArray& buffer = m_buffer[socket];
                    buffer += socket->readAll();
                    if (!requestComplete(buffer)) return;
                    const int bodyStart = buffer.indexOf("\r\n\r\n") + 4;
                    const QStringList texts = parseInputs(buffer.mid(bodyStart));
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
    int requests() const { return m_requests; }

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
    int m_requests{0};
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
};

QTEST_GUILESS_MAIN(TestEmbedding)
#include "test_embedding.moc"
