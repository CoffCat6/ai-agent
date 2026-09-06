#pragma once

#include "VectorStore.h"
#include "KnowledgeChunk.h"

#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <cmath>

namespace NexusAI {

// Document retriever - chunks documents and retrieves via vector similarity.
//
// Chunking is character-based (with overlap) and prefers sentence
// boundaries, so CJK text without spaces chunks correctly; the previous
// whitespace splitter collapsed a whole Chinese document into one chunk,
// which made embedding requests time out and destroyed retrieval quality.
//
// Embedding consistency: cosine similarity is only meaningful between
// vectors of the same dimension, so one document is embedded in exactly one
// mode: either every chunk gets a real embedding from a single batch call,
// or a failed/incomplete batch switches the whole document to the local
// pseudo embedding. At query time the query vector is regenerated in the
// store's mode (dimension check) so search stays self-consistent even when
// the embedder availability changed between import and query.
class DocumentRetriever {
public:
    using EmbedFn = std::function<QVector<float>(const QString&)>;
    using BatchEmbedFn = std::function<QVector<QVector<float>>(const QStringList&)>;

    explicit DocumentRetriever(EmbedFn embedFn = {})
        : m_embedFn(std::move(embedFn)) {}

    void setEmbedFunction(EmbedFn fn) { m_embedFn = std::move(fn); }
    void setBatchEmbedFunction(BatchEmbedFn fn) { m_batchEmbedFn = std::move(fn); }

    // Add a document: chunk it and store embeddings. Returns false when the
    // content produced no usable chunks.
    bool addDocument(const QString& content, const QJsonObject& metadata) {
        const QStringList chunks = chunkDocument(content, ChunkSize, ChunkOverlap);
        if (chunks.isEmpty()) return false;

        // One batch call for the whole document; a missing/failed/incomplete
        // result falls back to the pseudo embedding for EVERY chunk so the
        // store never mixes vector dimensions.
        QVector<QVector<float>> vectors;
        if (m_batchEmbedFn) vectors = m_batchEmbedFn(chunks);
        bool pseudo = vectors.size() != chunks.size();
        if (!pseudo) {
            for (const auto& v : vectors) {
                if (v.isEmpty()) { pseudo = true; break; }
            }
        }

        m_embeddingDim = pseudo ? PseudoEmbedDim : vectors.first().size();
        m_usesPseudo = pseudo;

        for (int i = 0; i < chunks.size(); ++i) {
            QJsonObject chunkMeta = metadata;
            chunkMeta["chunk_index"] = i;
            chunkMeta["chunk_total"] = chunks.size();
            chunkMeta["text"] = chunks[i];
            const QVector<float> vec = pseudo ? pseudoEmbed(chunks[i]) : vectors.at(i);
            m_store.insert(vec, chunkMeta);
        }
        return true;
    }

    // Rebuild a document from persisted chunks (Batch 6): no chunking, no
    // embedding network IO. The vectors are inserted exactly as stored, so a
    // restored real-mode store is bit-consistent with its ingested state and
    // a pseudo-mode store stays searchable offline.
    bool addPrecomputedDocument(const QString& sourceName,
                                const QVector<KnowledgeChunk>& chunks) {
        if (chunks.isEmpty()) return false;
        m_usesPseudo = chunks.first().pseudo;
        m_embeddingDim = chunks.first().dim;
        for (int i = 0; i < chunks.size(); ++i) {
            const KnowledgeChunk& chunk = chunks.at(i);
            if (chunk.embedding.isEmpty()
                    || (!m_usesPseudo && chunk.embedding.size() != m_embeddingDim))
                return false;
            QJsonObject chunkMeta;
            chunkMeta["source"] = sourceName;
            chunkMeta["chunk_index"] = chunk.index;
            chunkMeta["chunk_total"] = chunk.total;
            chunkMeta["text"] = chunk.text;
            m_store.insert(chunk.embedding, chunkMeta);
        }
        return true;
    }

    // Search for relevant document chunks
    QJsonArray search(const QString& query, int k = 5) const {
        if (m_store.size() == 0) return {};
        // A pseudo-mode store never uses the real vector: skip the (network)
        // embed call entirely in that mode.
        QVector<float> realVec = (m_embedFn && !m_usesPseudo)
            ? m_embedFn(query) : QVector<float>{};
        return search(realVec, pseudoEmbed(query), k);
    }

    // Search with pre-computed query vectors for both embedding modes. The
    // vector matching this store's mode is used; a real-mode store is only
    // scored with a real vector of its own dimension.
    //
    // Relevance gate (FIX-301): below-threshold passages are dropped, so
    // "no passages matched" is a reachable state. Real embeddings are gated
    // at RealMinRelevanceScore (placeholder - calibrate per embedding model
    // when vector persistence lands); pseudo-mode scores come from a
    // positional character hash and are meaningless beyond "at least some
    // overlap", so anything above zero passes.
    //
    // FIX-302: a missing (embedder offline) or dimension-drifted query
    // vector against a real-mode store makes every cosine similarity 0 -
    // returning the "top-k" of that would be arbitrary chunks, so the store
    // degrades to no-match instead.
    static constexpr double RealMinRelevanceScore = 0.15;

    QJsonArray search(const QVector<float>& realVec,
                      const QVector<float>& pseudoVec, int k = 5) const {
        if (m_store.size() == 0) return {};

        QJsonArray arr;
        if (m_usesPseudo) {
            for (const auto& obj : m_store.search(pseudoVec, k))
                if (obj.value("score").toDouble() > 0.0)
                    arr.append(obj);
            return arr;
        }

        if (realVec.isEmpty() || realVec.size() != m_embeddingDim)
            return {};
        for (const auto& obj : m_store.search(realVec, k))
            if (obj.value("score").toDouble() >= RealMinRelevanceScore)
                arr.append(obj);
        return arr;
    }

    // Persistence export (Batch 6): chunk text + the stored vector per
    // chunk, in insertion order — no re-embedding, bit-exact round trip.
    QVector<KnowledgeChunk> exportChunks() const {
        QVector<KnowledgeChunk> chunks;
        chunks.reserve(m_store.size());
        for (const auto& entry : m_store.entries()) {
            KnowledgeChunk chunk;
            chunk.index = entry.second.value("chunk_index").toInt();
            chunk.total = entry.second.value("chunk_total").toInt();
            chunk.text = entry.second.value("text").toString();
            chunk.embedding = entry.first;
            chunk.dim = entry.first.size();
            chunk.pseudo = m_usesPseudo;
            chunks.append(chunk);
        }
        return chunks;
    }

    int documentCount() const { return m_store.size(); }
    // All chunk metadata of this document ordered by chunk_index (detail view)
    QJsonArray chunks() const { return m_store.chunks(); }
    // True when this document was stored with the local pseudo embedding
    // (embedder was unavailable during import) - retrieval quality is
    // lexical at best and the UI should hint at it.
    bool usesPseudoEmbedding() const { return m_usesPseudo; }
    void clear() {
        m_store.clear();
        m_embeddingDim = 0;
        m_usesPseudo = false;
    }

    static constexpr int ChunkSize = 512;    // ~chars per chunk
    static constexpr int ChunkOverlap = 64;  // chars carried over between chunks
    static constexpr int PseudoEmbedDim = 128;

    // Dimension-stable fallback embedding (lexical hash; no real semantics,
    // but keeps search functional when the embedder is offline). Public so
    // RagService can compute the pseudo query vector once per search.
    static QVector<float> pseudoEmbed(const QString& text) {
        QVector<float> vec(PseudoEmbedDim, 0.0f);
        for (int i = 0; i < text.length(); ++i) {
            const int idx = (text.at(i).unicode() * 31 + i) % PseudoEmbedDim;
            vec[idx] += 1.0f;
        }
        float norm = 0.0f;
        for (float v : vec) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (float& v : vec) v /= norm;
        }
        return vec;
    }

private:
    // Character-level chunking with overlap, cutting at sentence boundaries
    // (paragraph breaks, CJK and latin punctuation) when one is available in
    // the back half of the window; hard cut otherwise.
    static QStringList chunkDocument(const QString& text, int chunkSize, int overlap) {
        QStringList chunks;
        const int n = text.length();
        int start = 0;

        while (start < n) {
            int end = qMin(start + chunkSize, n);
            if (end < n) {
                const int searchFrom = start + chunkSize / 2;
                int cut = -1;
                for (int i = end; i > searchFrom; --i) {
                    if (isSentenceBoundary(text.at(i - 1))) { cut = i; break; }
                }
                if (cut > 0) end = cut;
            }

            const QString chunk = text.mid(start, end - start).simplified();
            if (!chunk.isEmpty())
                chunks.append(chunk);

            if (end >= n) break;
            start = qMax(end - overlap, start + 1);
        }
        return chunks;
    }

    static bool isSentenceBoundary(QChar c) {
        return c == '\n' || c == '.' || c == '!' || c == '?' || c == ';' || c == ':'
            || c == u'。' || c == u'！' || c == u'？' || c == u'；' || c == u'…'
            || c == u'，' || c == u'、';
    }

    VectorStore m_store;
    EmbedFn m_embedFn;            // single-text embedding (queries)
    BatchEmbedFn m_batchEmbedFn;  // document chunks embedding (imports)
    int m_embeddingDim{0};
    bool m_usesPseudo{false};
};

} // namespace NexusAI
