#pragma once

#include <QObject>
#include <QJsonArray>
#include <QMutex>
#include <memory>
#include <vector>

#include "DocumentRetriever.h"

namespace NexusAI {

// Knowledge-base facade over DocumentRetriever: multi-document management
// with thread-safe ingest/search (the retrieval tool runs on a worker
// thread while QML calls arrive on the GUI thread) and a document listing
// for the management UI.
//
// Each document gets its own retriever so removal is O(1); embeddings come
// from the configured embedder (Ollama) with the retriever's local pseudo
// embedding as fallback when no embedder is set or the call fails.
class RagService : public QObject {
    Q_OBJECT
public:
    explicit RagService(QObject* parent = nullptr);

    void setEmbedder(DocumentRetriever::EmbedFn fn);

    // Chunk + embed + store a document; returns the number of chunks kept.
    // Re-ingesting an existing source name replaces it.
    int ingestDocument(const QString& sourceName, const QString& content);

    // Top-k passages across all documents, sorted by score
    QJsonArray search(const QString& query, int k = 4) const;

    QJsonArray documents() const; // [{name, chunks, chars}]
    bool removeDocument(const QString& sourceName);
    void clear();
    bool isEmpty() const;
    int documentCount() const; // total chunks across documents

signals:
    void changed();

private:
    struct Document {
        QString name;
        int chars{0};
        std::unique_ptr<DocumentRetriever> retriever;
    };

    DocumentRetriever::EmbedFn m_embedFn;
    std::vector<Document> m_docs; // move-only elements -> std::vector
    mutable QMutex m_mutex; // ingest/remove/search may run on different threads
};

} // namespace NexusAI
