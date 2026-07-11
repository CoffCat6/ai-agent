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

    // Synchronous embedding call (blocks with QEventLoop)
    QVector<float> embed(const QString& text);

    // Asynchronous embedding call
    void embedAsync(const QString& text);

signals:
    void embeddingReady(const QVector<float>& embedding);
    void errorOccurred(const QString& error);

private:
    QString m_baseUrl{"http://localhost:11434"};
    QString m_model{"nomic-embed-text"};
    QNetworkAccessManager* m_nam;
};

} // namespace NexusAI
