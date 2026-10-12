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
    // docs/25: pageTexts 非空（PDF 导入）时逐 chunk 映射页码到元数据。
    int ingestDocument(const QString& sourceName, const QString& content,
                       const QStringList& pageTexts = QStringList());
    static QVariantList mapChunksToPages(const QStringList& chunkTexts,
                                         const QStringList& pageTexts);

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

    // 检索结果 + 失败原因。search() 只返回 passages；工具层用 searchDetailed()
    // 区分"确实没有匹配"与"检索根本没跑起来"——后者在**真实向量**库上等价于
    // 嵌入服务（Ollama）离线：查询向量取不到，与任何真实向量的相似度都算不出来，
    // 于是按 DocumentRetriever 的 FIX-302 降级为 no-match。
    // 不暴露这个状态的话，工具只能回 "No passages matched"，让"服务没启动"
    // 看起来像"知识库里没有"——用户会以为文档白导了。
    struct SearchOutcome {
        QJsonArray passages;
        bool hasDocuments{false};          // 知识库非空
        bool embeddingUnavailable{false};  // 有真实向量文档，但查询向量取不到
    };
    SearchOutcome searchDetailed(const QString& query, int k = 4) const;

    QJsonArray documents() const; // [{name, chunks, chars, pseudo}]

    // FEAT-802: import-side duplicate/coexistence helpers
    bool hasDocument(const QString& sourceName) const;
    bool containsContentHash(const QString& contentHash) const;
    // "a.txt" exists → "a (2).txt", "a (3).txt", ... (display-name dedupe;
    // the DB primary key stays the unique display name)
    QString uniqueDocumentName(const QString& baseName) const;
    // docs/25: whether the named document was stored with the local pseudo
    // embedding (embedder unavailable during import) — batch import reports
    // this as the "degraded" status. Unknown name → false.
    bool usesPseudoEmbedding(const QString& sourceName) const;
    // Chunk metadata of one document ordered by chunk_index (detail view);
    // empty when the document is unknown.
    QJsonArray documentChunks(const QString& sourceName) const;
    bool removeDocument(const QString& sourceName);
    void clear();
    bool isEmpty() const;
    int documentCount() const; // total chunks across documents

    static constexpr int MaxChunksPerDocument = 512;
    static constexpr int MaxTotalChunks = 4096;

    // REL-1003: knowledge-base-wide chunk budget (ingest returns -3 past
    // it; removal frees budget). Configurable for tests; the shipped
    // default is MaxTotalChunks.
    void setMaxTotalChunks(int cap);
    int maxTotalChunks() const { return m_maxTotalChunks; }
    // docs/24 R2: invalidate every in-flight restore step — the database
    // restore flow bumps this before re-running the restore against the
    // swapped file.
    void bumpRestoreGeneration();

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
        QString contentHash;
        std::unique_ptr<DocumentRetriever> retriever;
    };

    int dropDocumentLocked(const QString& sourceName); // returns removed chunk count
    bool hasDocumentLocked(const QString& sourceName) const;
    int totalChunksLocked() const;
    // Chunk + embed outside the lock; nullptr with *chunksOut = 0 (empty) or
    // -1 (oversize). Shared by ingestDocument / ingestRestoredDocument.
    std::unique_ptr<DocumentRetriever> buildRetriever(const QString& sourceName,
                                                      const QString& content,
                                                      int* chunksOut,
                                                      const QVariantList& chunkPages = QVariantList()) const;
    // Merged top-k across documents (embedding already done outside the lock).
    QJsonArray finishSearch(const QString& trimmed, const QVector<float>& realVec,
                            int k) const;

    DocumentRetriever::EmbedFn m_embedFn;
    DocumentRetriever::BatchEmbedFn m_batchEmbedFn;
    QString m_embeddingModel;
    std::vector<Document> m_docs; // move-only elements -> std::vector
    mutable QMutex m_mutex; // ingest/remove/search may run on different threads
    quint64 m_restoreGeneration{0}; // bumped by clear()/removeDocument()
    int m_maxTotalChunks{MaxTotalChunks};

    // Query-embedding cache (KB-605): one network round-trip per unique
    // query text; invalidated when the embedder changes. Keyed by text.
    mutable QHash<QString, QVector<float>> m_queryCache;
    mutable QQueue<QString> m_queryCacheOrder;
    static constexpr int MaxQueryCacheEntries = 32;
};

} // namespace NexusAI
