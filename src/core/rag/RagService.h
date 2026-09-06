#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QMutex>
#include <QQueue>
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
//
// Persistence (Batch 6): after every successful ingestion the service emits
// documentPersisted() (chunks included) so the wiring layer can store them;
// restorePersistedDocument() rebuilds a document from stored chunks without
// any embedding IO. The embedding-model name acts as the version identity:
// chunks from a different model are never restored.
class RagService : public QObject {
    Q_OBJECT
public:
    explicit RagService(QObject* parent = nullptr);

    void setEmbedder(DocumentRetriever::EmbedFn fn);
    void setBatchEmbedder(DocumentRetriever::BatchEmbedFn fn);

    // Identity of the embedding model currently backing the embedder; used
    // to version persisted chunk vectors.
    void setEmbeddingModelName(const QString& model) { m_embeddingModel = model; }
    QString embeddingModelName() const { return m_embeddingModel; }

    // Chunk + embed + store a document. Returns the number of chunks kept,
    // 0 when the input is empty/unusable and -1 when the document exceeds
    // MaxChunksPerDocument. Re-ingesting an existing source name replaces it.
    int ingestDocument(const QString& sourceName, const QString& content);

    // Background-restore ingestion: bind to the restore generation the task
    // started with. clear()/removeDocument() bump the generation, so a user
    // wiping the base mid-restore makes the remaining restore steps no-ops
    // (-2) instead of re-adding deleted documents. Same result codes as
    // ingestDocument.
    int ingestRestoredDocument(const QString& sourceName, const QString& content,
                               quint64 generation);

    // Rebuild a document from persisted chunks - no chunking, no embedding
    // IO (Batch 6). Binds to the restore generation like
    // ingestRestoredDocument; false when the chunks are unusable or the
    // restore was superseded.
    bool restorePersistedDocument(const QString& sourceName,
                                  const QVector<KnowledgeChunk>& chunks,
                                  quint64 generation);

    // Persistence export of one stored document (empty when unknown).
    QVector<KnowledgeChunk> exportPersistedChunks(const QString& sourceName) const;

    // Current restore generation (snapshot before starting a restore task).
    quint64 restoreGeneration() const;

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
    // Emitted (possibly from a worker thread) after a successful ingest so
    // the wiring layer can persist content hash + chunks.
    void documentPersisted(const QString& name, const QString& content,
                           const QVector<KnowledgeChunk>& chunks);

private:
    struct Document {
        QString name;
        int chars{0};
        std::unique_ptr<DocumentRetriever> retriever;
    };

    void dropDocumentLocked(const QString& sourceName);
    // Chunk + embed outside the lock; nullptr with *chunksOut = 0 (empty) or
    // -1 (oversize). Shared by ingestDocument / ingestRestoredDocument.
    std::unique_ptr<DocumentRetriever> buildRetriever(const QString& sourceName,
                                                      const QString& content,
                                                      int* chunksOut) const;
    // Merged top-k across documents (embedding already done outside the lock).
    QJsonArray finishSearch(const QString& trimmed, const QVector<float>& realVec,
                            int k) const;

    DocumentRetriever::EmbedFn m_embedFn;
    DocumentRetriever::BatchEmbedFn m_batchEmbedFn;
    QString m_embeddingModel;
    std::vector<Document> m_docs; // move-only elements -> std::vector
    mutable QMutex m_mutex; // ingest/remove/search may run on different threads
    quint64 m_restoreGeneration{0}; // bumped by clear()/removeDocument()

    // Query-embedding cache (KB-605): one network round-trip per unique
    // query text; invalidated when the embedder changes. Keyed by text.
    mutable QHash<QString, QVector<float>> m_queryCache;
    mutable QQueue<QString> m_queryCacheOrder;
    static constexpr int MaxQueryCacheEntries = 32;
};

} // namespace NexusAI
