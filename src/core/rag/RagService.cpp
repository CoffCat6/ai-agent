#include "RagService.h"

#include <QJsonObject>
#include <algorithm>

namespace NexusAI {

RagService::RagService(QObject* parent) : QObject(parent) {}

void RagService::setEmbedder(DocumentRetriever::EmbedFn fn) {
    QMutexLocker lock(&m_mutex);
    m_embedFn = std::move(fn);
}

void RagService::setBatchEmbedder(DocumentRetriever::BatchEmbedFn fn) {
    QMutexLocker lock(&m_mutex);
    m_batchEmbedFn = std::move(fn);
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

    {
        QMutexLocker lock(&m_mutex);
        dropDocumentLocked(sourceName); // replace same-name doc (incl. races)
        Document doc;
        doc.name = sourceName;
        doc.chars = content.trimmed().size();
        doc.retriever = std::move(retriever);
        m_docs.push_back(std::move(doc));
    }
    emit changed();
    return chunks;
}

int RagService::ingestRestoredDocument(const QString& sourceName, const QString& content,
                                       quint64 generation) {
    int chunks = 0;
    auto retriever = buildRetriever(sourceName, content, &chunks);
    if (!retriever) return chunks;

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
        doc.retriever = std::move(retriever);
        m_docs.push_back(std::move(doc));
    }
    emit changed();
    return chunks;
}

quint64 RagService::restoreGeneration() const {
    QMutexLocker lock(&m_mutex);
    return m_restoreGeneration;
}

QJsonArray RagService::search(const QString& query, int k) const {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return {};

    // Snapshot the embedder under the lock, then release it before any
    // network IO: the embedding round-trip must never block documents() /
    // documentChunks(), which run on the GUI thread while the retrieval
    // tool executes on a worker thread.
    DocumentRetriever::EmbedFn embedFn;
    {
        QMutexLocker lock(&m_mutex);
        if (m_docs.empty()) return {};
        embedFn = m_embedFn;
    }

    // ONE real embedding per search, shared by every real-mode document
    // (instead of one round-trip per document); the pseudo variant is local
    // and always available.
    const QVector<float> realVec = embedFn ? embedFn(trimmed) : QVector<float>{};
    const QVector<float> pseudoVec = DocumentRetriever::pseudoEmbed(trimmed);

    // Merge scored passages from every document, keep the global top-k.
    // Each document's retriever picks the query vector matching its own
    // embedding mode, so scores are only compared within one mode -
    // acceptable when a pseudo-mode document coexists with real-mode ones.
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
