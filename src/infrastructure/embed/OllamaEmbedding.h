#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QNetworkAccessManager>

namespace NexusAI {

// Embedding service using Ollama's /api/embeddings endpoint
class OllamaEmbedding : public QObject {
    Q_OBJECT
public:
    explicit OllamaEmbedding(QObject* parent = nullptr);

    void setBaseUrl(const QString& url) { m_baseUrl = url; }
    void setModel(const QString& model) { m_model = model; }
    // Identity of the embedding model (persisted-chunk versioning, Batch 6)
    QString model() const { return m_model; }

    // Synchronous embedding call (blocks with QEventLoop)
    QVector<float> embed(const QString& text);

    // Synchronous batch embedding via /api/embed, paged into requests of
    // BatchSize texts (FEAT-803): progress is reported after every batch and
    // any failed batch aborts with an empty container so the caller falls
    // back wholesale. Must be called from a thread that may run an event
    // loop (document import runs on a QtConcurrent worker).
    static constexpr int BatchSize = 32;
    static QVector<QStringList> splitBatches(const QStringList& texts, int batchSize);
    QVector<QVector<float>> embedBatch(const QStringList& texts);

    // Asynchronous embedding call
    void embedAsync(const QString& text);

signals:
    void embeddingReady(const QVector<float>& embedding);
    void errorOccurred(const QString& error);
    // Texts embedded so far / total (after each successful batch)
    void embedProgress(int done, int total);

private:
    QString m_baseUrl{"http://localhost:11434"};
    QString m_model{"nomic-embed-text"};
    QNetworkAccessManager* m_nam;
};

} // namespace NexusAI
