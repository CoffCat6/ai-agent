#include "RagService.h"

#include <QFileInfo>
#include <QJsonObject>
#include <algorithm>

namespace NexusAI {

RagService::RagService(QObject* parent) : QObject(parent) {}

void RagService::setEmbedder(DocumentRetriever::EmbedFn fn) {
    QMutexLocker lock(&m_mutex);
    m_embedFn = std::move(fn);
    m_queryCache.clear();   // query vectors belong to the previous embedder
    m_queryCacheOrder.clear();
}

void RagService::setBatchEmbedder(DocumentRetriever::BatchEmbedFn fn) {
    QMutexLocker lock(&m_mutex);
    m_batchEmbedFn = std::move(fn);
}

bool RagService::hasDocumentLocked(const QString& sourceName) const {
    for (const auto& doc : m_docs) {
        if (doc.name == sourceName) return true;
    }
    return false;
}

bool RagService::hasDocument(const QString& sourceName) const {
    QMutexLocker lock(&m_mutex);
    return hasDocumentLocked(sourceName);
}

bool RagService::containsContentHash(const QString& contentHash) const {
    if (contentHash.isEmpty()) return false;
    QMutexLocker lock(&m_mutex);
    for (const auto& doc : m_docs) {
        if (doc.contentHash == contentHash) return true;
    }
    return false;
}

QString RagService::uniqueDocumentName(const QString& baseName) const {
    QMutexLocker lock(&m_mutex);
    if (!hasDocumentLocked(baseName)) return baseName;

    const QFileInfo info(baseName);
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty()
        ? QString() : QStringLiteral(".") + info.suffix();
    for (int i = 2; i < 1000; ++i) {
        const QString candidate =
            QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(suffix);
        if (!hasDocumentLocked(candidate)) return candidate;
    }
    return baseName;
}

void RagService::dropDocumentLocked(const QString& sourceName) {
    for (int i = 0; i < static_cast<int>(m_docs.size()); ++i) {
        if (m_docs[i].name == sourceName) {
            m_docs.erase(m_docs.begin() + i);
            break;
        }
    }
}

std::unique_ptr<DocumentRetriever> RagService::buildRetriever(
        const QString& sourceName, const QString& content, int* chunksOut) const {
    *chunksOut = 0;
    const QString trimmed = content.trimmed();
    if (sourceName.isEmpty() || trimmed.isEmpty()) return nullptr;

    // Upper bound before any embedding work: reject pathologically large
    // inputs instead of spending minutes (or exhausting memory) on them.
    const int estimatedChunks = trimmed.size()
        / (DocumentRetriever::ChunkSize - DocumentRetriever::ChunkOverlap) + 1;
    if (estimatedChunks > MaxChunksPerDocument) {
        *chunksOut = -1;
        return nullptr;
    }

    // Build the retriever (embedding = network IO) WITHOUT holding the
    // mutex; search/documents stay usable during a long import.
    auto retriever = std::make_unique<DocumentRetriever>();
    {
        QMutexLocker lock(&m_mutex);
        retriever->setEmbedFunction(m_embedFn);
        retriever->setBatchEmbedFunction(m_batchEmbedFn);
    }

    if (!retriever->addDocument(trimmed, QJsonObject{{"source", sourceName}}))
        return nullptr;
    *chunksOut = retriever->documentCount();
    return retriever;
}

int RagService::ingestDocument(const QString& sourceName, const QString& content) {
    int chunks = 0;
    auto retriever = buildRetriever(sourceName, content, &chunks);
    if (!retriever) return chunks;

    // Export before the retriever is moved into the document store.
    const QVector<KnowledgeChunk> persisted = retriever->exportChunks();
    {
        QMutexLocker lock(&m_mutex);
        dropDocumentLocked(sourceName); // replace same-name doc (incl. races)
        Document doc;
        doc.name = sourceName;
        doc.chars = content.trimmed().size();
        doc.contentHash = knowledgeContentHash(content);
        doc.retriever = std::move(retriever);
        m_docs.push_back(std::move(doc));
    }

    // UI refresh signal: the knowledge list (and reembed flow) hangs off
    // changed() - it must accompany EVERY document mutation (lost once in
    // the Batch 6 refactor, which blanked the panel on import).
    emit changed();
    // Persistence export (may run on a worker thread; consumers queue it).
    emit documentPersisted(sourceName, content, persisted);
    return chunks;
}

int RagService::ingestRestoredDocument(const QString& sourceName, const QString& content,
                                       quint64 generation) {
    int chunks = 0;
    auto retriever = buildRetriever(sourceName, content, &chunks);
    if (!retriever) return chunks;

    const QVector<KnowledgeChunk> persisted = retriever->exportChunks();
    {
        QMutexLocker lock(&m_mutex);
        // The embedding round-trip above took a while: the user may have
        // cleared/removed documents in the meantime. Superseded restores
        // must not re-add what the user deleted.
        if (generation != m_restoreGeneration) return -2;
        dropDocumentLocked(sourceName);
        Document doc;
        doc.name = sourceName;
        doc.chars = content.trimmed().size();
        doc.contentHash = knowledgeContentHash(content);
        doc.retriever = std::move(retriever);
        m_docs.push_back(std::move(doc));
    }

    emit changed();   // UI refresh (see ingestDocument)
    emit documentPersisted(sourceName, content, persisted);
    return chunks;
}

bool RagService::restorePersistedDocument(const QString& sourceName,
                                          const QVector<KnowledgeChunk>& chunks,
                                          quint64 generation) {
    if (chunks.isEmpty()) return false;

    auto retriever = std::make_unique<DocumentRetriever>();
    {
        QMutexLocker lock(&m_mutex);
        retriever->setEmbedFunction(m_embedFn);
        retriever->setBatchEmbedFunction(m_batchEmbedFn);
    }
    if (!retriever->addPrecomputedDocument(sourceName, chunks)) return false;

    {
        QMutexLocker lock(&m_mutex);
        if (generation != m_restoreGeneration) return false;
        dropDocumentLocked(sourceName);
        Document doc;
        doc.name = sourceName;
        doc.chars = 0;
        for (const KnowledgeChunk& chunk : chunks) doc.chars += chunk.text.size();
        doc.retriever = std::move(retriever);
        m_docs.push_back(std::move(doc));
    }
    emit changed();
    return true;
}

quint64 RagService::restoreGeneration() const {
    QMutexLocker lock(&m_mutex);
    return m_restoreGeneration;
}

QVector<KnowledgeChunk> RagService::exportPersistedChunks(const QString& sourceName) const {
    QMutexLocker lock(&m_mutex);
    for (const auto& doc : m_docs) {
        if (doc.name == sourceName)
            return doc.retriever->exportChunks();
    }
    return {};
}

QJsonArray RagService::search(const QString& query, int k) const {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return {};

    {
        QMutexLocker lock(&m_mutex);
        if (m_docs.empty()) return {};
    }

    // ONE real embedding per search, shared by every real-mode document
    // (instead of one round-trip per document); the pseudo variant is local
    // and always available. Repeated queries (follow-up questions, the
    // agent retrying a similar probe) reuse the cached vector (KB-605).
    QVector<float> realVec;
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_queryCache.constFind(trimmed);
        if (it != m_queryCache.constEnd())
            realVec = it.value();
    }
    if (realVec.isEmpty()) {
        // Embed outside the lock: the network round-trip must never block
        // documents()/documentChunks() on the GUI thread.
        DocumentRetriever::EmbedFn embedFn;
        {
            QMutexLocker lock(&m_mutex);
            embedFn = m_embedFn;
        }
        if (embedFn) realVec = embedFn(trimmed);

        if (!realVec.isEmpty()) {
            QMutexLocker lock(&m_mutex);
            if (!m_queryCache.contains(trimmed)) {
                if (m_queryCache.size() >= MaxQueryCacheEntries
                        && !m_queryCacheOrder.isEmpty())
                    m_queryCache.remove(m_queryCacheOrder.takeFirst());
                m_queryCache.insert(trimmed, realVec);
                m_queryCacheOrder.enqueue(trimmed);
            }
        }
    }
    return finishSearch(trimmed, realVec, k);
}

// Merged top-k across documents (embedding already done outside the lock).
QJsonArray RagService::finishSearch(const QString& trimmed, const QVector<float>& realVec,
                                    int k) const {
    const QVector<float> pseudoVec = DocumentRetriever::pseudoEmbed(trimmed);

    QVector<QJsonObject> hits;
    {
        QMutexLocker lock(&m_mutex);
        for (const auto& doc : m_docs) {
            const QJsonArray results =
                doc.retriever->search(realVec, pseudoVec, k);
            for (const auto& entry : results)
                hits.append(entry.toObject());
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const QJsonObject& a, const QJsonObject& b) {
                  return a.value("score").toDouble() > b.value("score").toDouble();
              });

    QJsonArray out;
    for (int i = 0; i < qMin(k, hits.size()); ++i)
        out.append(hits[i]);
    return out;
}

QJsonArray RagService::documents() const {
    QMutexLocker lock(&m_mutex);

    QJsonArray out;
    for (const auto& doc : m_docs) {
        QJsonObject entry;
        entry["name"] = doc.name;
        entry["chunks"] = doc.retriever->documentCount();
        entry["chars"] = doc.chars;
        entry["pseudo"] = doc.retriever->usesPseudoEmbedding();
        out.append(entry);
    }
    return out;
}

QJsonArray RagService::documentChunks(const QString& sourceName) const {
    QMutexLocker lock(&m_mutex);

    for (const auto& doc : m_docs) {
        if (doc.name == sourceName)
            return doc.retriever->chunks();
    }
    return {};
}

bool RagService::removeDocument(const QString& sourceName) {
    QMutexLocker lock(&m_mutex);

    for (int i = 0; i < static_cast<int>(m_docs.size()); ++i) {
        if (m_docs[i].name == sourceName) {
            m_docs.erase(m_docs.begin() + i);
            ++m_restoreGeneration;   // supersede in-flight restore steps
            lock.unlock();
            emit changed();
            return true;
        }
    }
    return false;
}

void RagService::clear() {
    QMutexLocker lock(&m_mutex);
    ++m_restoreGeneration;   // supersede in-flight restore steps
    m_docs.clear();
    lock.unlock();
    emit changed();
}

bool RagService::isEmpty() const {
    QMutexLocker lock(&m_mutex);
    return m_docs.empty();
}

int RagService::documentCount() const {
    QMutexLocker lock(&m_mutex);
    int total = 0;
    for (const auto& doc : m_docs)
        total += doc.retriever->documentCount();
    return total;
}

} // namespace NexusAI
