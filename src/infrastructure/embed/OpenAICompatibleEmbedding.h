#pragma once

#include <QMutex>
#include <QObject>
#include <QVector>
#include <QString>


namespace NexusAI {

// Embedding service speaking the OpenAI-compatible /v1/embeddings protocol.
// One implementation covers OpenAI, SiliconFlow, Zhipu, LiteLLM proxies and
// Ollama's own compat endpoint. Deliberately mirrors OllamaEmbedding's
// method surface and behavior contracts (synchronous QEventLoop calls,
// mutex-snapshot configuration, paged batches, transport-only retry,
// embedProgress) so the wiring layer in main.cpp treats both backends
// uniformly — only the wire format differs.
class OpenAICompatibleEmbedding : public QObject {
    Q_OBJECT
public:
    explicit OpenAICompatibleEmbedding(QObject* parent = nullptr);

    // docs/22 F3: setters may run on the GUI thread while embed/embedBatch
    // run on worker threads (RAG tool, document import) — configuration is
    // mutex-guarded and each call snapshots it at entry, so a mid-flight
    // swap never tears a QString or changes the request underneath a call.
    void setBaseUrl(const QString& url) {
        QMutexLocker lock(&m_configMutex);
        m_baseUrl = url;
    }
    void setModel(const QString& model) {
        QMutexLocker lock(&m_configMutex);
        m_model = model;
    }
    void setApiKey(const QString& key) {
        QMutexLocker lock(&m_configMutex);
        m_apiKey = key;
    }
    // Identity of the embedding model for persisted-chunk versioning. The
    // "openai:" prefix keeps the two backends' vector spaces apart: the
    // persisted-chunk usability check (main.cpp persistedChunksUsable)
    // compares this string, so switching providers invalidates old chunks
    // and forces a re-embed instead of silently mixing incompatible
    // vectors.
    QString model() const {
        QMutexLocker lock(&m_configMutex);
        return QStringLiteral("openai:") + m_model;
    }

    // Synchronous embedding call (blocks with QEventLoop)
    QVector<float> embed(const QString& text);

    // Synchronous batch embedding, paged into requests of BatchSize texts
    // (FEAT-803): progress is reported after every batch and any failed
    // batch aborts with an empty container so the caller falls back
    // wholesale. Must be called from a thread that may run an event loop
    // (document import runs on a QtConcurrent worker).
    static constexpr int BatchSize = 32;
    static QVector<QStringList> splitBatches(const QStringList& texts, int batchSize);
    QVector<QVector<float>> embedBatch(const QStringList& texts);

signals:
    // Texts embedded so far / total (after each successful batch)
    void embedProgress(int done, int total);

private:
    // Build the endpoint URL from a user-supplied base: both
    // "https://api.openai.com" and "https://api.openai.com/v1" (plus
    // trailing-slash variants) must resolve to .../v1/embeddings.
    static QString embeddingsUrl(const QString& baseUrl);

    mutable QMutex m_configMutex;
    QString m_baseUrl{"https://api.openai.com"};
    QString m_model;
    QString m_apiKey;
};

} // namespace NexusAI