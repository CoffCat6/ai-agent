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
// Ingest AND search keep their embedding network IO outside the mutex -
// under the lock only state snapshots and pure in-memory similarity work
// remain, so GUI-thread readers never wait behind a slow embedding call.
class RagService : public QObject {
    Q_OBJECT
public:
    explicit RagService(QObject* parent = nullptr);

    void setEmbedder(DocumentRetriever::EmbedFn fn);
    void setBatchEmbedder(DocumentRetriever::BatchEmbedFn fn);

    // Chunk + embed + store a document. Returns the number of chunks kept,
    // 0 when the input is empty/unusable and -1 when the document exceeds
    // MaxChunksPerDocument. Re-ingesting an existing source name replaces it.
    int ingestDocument(const QString& sourceName, const QString& content);

    // Top-k passages across all documents, sorted by score
    QJsonArray search(const QString& query, int k = 4) const;

    QJsonArray documents() const; // [{name, chunks, chars, pseudo}]
    // Chunk metadata of one document ordered by chunk_index (detail view);
    // empty when the document is unknown.
    QJsonArray documentChunks(const QString& sourceName) const;
    bool removeDocument(const QString& sourceName);
    void clear();
    bool isEmpty() const;
    int documentCount() const; // total chunks across documents

    static constexpr int MaxChunksPerDocument = 512;

signals:
    void changed();

private:
    struct Document {
        QString name;
        int chars{0};
        std::unique_ptr<DocumentRetriever> retriever;
    };

    void dropDocumentLocked(const QString& sourceName);

    DocumentRetriever::EmbedFn m_embedFn;
    DocumentRetriever::BatchEmbedFn m_batchEmbedFn;
    std::vector<Document> m_docs; // move-only elements -> std::vector
    mutable QMutex m_mutex; // ingest/remove/search may run on different threads
};

} // namespace NexusAI
