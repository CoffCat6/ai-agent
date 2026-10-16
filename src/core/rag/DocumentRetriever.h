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

    // docs/25 E3：原先接受 EmbedFn 的构造/setEmbedFunction 只服务于已删除的
    // search(query) 单参重载（查询向量在检索器内部现算）。查询向量改由
    // RagService 每次检索时预计算后传入向量版 search，这里不再持有嵌入函数。
    DocumentRetriever() = default;

    void setBatchEmbedFunction(BatchEmbedFn fn) { m_batchEmbedFn = std::move(fn); }

    // Add a document: chunk it and store embeddings. Returns false when the
    // content produced no usable chunks.
    // docs/25: chunkPages 与 chunk 列表等长时逐 chunk 写入元数据 page 字段
    //（1 基 PDF 页码，由调用方按页边界映射）；空列表 = 无页码（文本文件、
    // 旧导入），page 记 -1。
    bool addDocument(const QString& content, const QJsonObject& metadata,
                     const QVariantList& chunkPages = QVariantList()) {
        return addChunkedDocument(
            chunkDocument(content, ChunkSize, ChunkOverlap), metadata, chunkPages);
    }

    // docs/25 B7: chunk-once ingest path. The PDF import already chunks the
    // content (it needs the chunk texts for page mapping), so re-chunking
    // inside addDocument doubled the chunking cost — and on oversize input
    // wasted a full pass before the MaxChunksPerDocument gate rejected it.
    // Semantics are identical to addDocument otherwise.
    bool addChunkedDocument(const QStringList& chunks, const QJsonObject& metadata,
                            const QVariantList& chunkPages = QVariantList()) {
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
            chunkMeta["page"] = i < chunkPages.size()
                ? chunkPages.at(i).toInt() : -1;
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
            chunkMeta["page"] = chunk.page;
            m_store.insert(chunk.embedding, chunkMeta);
        }
        return true;
    }

    // Search with pre-computed query vectors for both embedding modes. The
    // vector matching this store's mode is used; a real-mode store is only
    // scored with a real vector of its own dimension.
    //
    // docs/25 E3：已删除 search(query, k) 单参重载——它会在检索器内部隐式
    // 触发（网络）embed，且全仓无调用方；查询向量一律由上层（RagService）
    // 预计算后传入本重载，网络边界因此显式化。
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
            chunk.page = entry.second.value("page").toInt(-1);
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
    // 真实模式下，这个查询向量能否用于本库：**离线（空向量）与维度漂移
    // 都会让相似度全部算不出来**，检索注定为空。伪模式恒可用（本地哈希，
    // 不依赖网络）。上层据此区分"确实没有匹配"与"检索根本没跑起来"。
    bool acceptsQueryVector(const QVector<float>& realVec) const {
        if (m_usesPseudo) return true;
        return !realVec.isEmpty() && realVec.size() == m_embeddingDim;
    }
    void clear() {
        m_store.clear();
        m_embeddingDim = 0;
        m_usesPseudo = false;
    }

    static constexpr int ChunkSize = 512;    // ~chars per chunk
    static constexpr int ChunkOverlap = 64;  // chars carried over between chunks
    static constexpr int PseudoEmbedDim = 128;

    // docs/25: 公开给 RagService——页码映射需要先拿到 chunk 文本列表，
    // 再把结果经 addDocument 的 chunkPages 参数写回元数据。
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
    static bool isSentenceBoundary(QChar c) {
        return c == '\n' || c == '.' || c == '!' || c == '?' || c == ';' || c == ':'
            || c == u'。' || c == u'！' || c == u'？' || c == u'；' || c == u'…'
            || c == u'，' || c == u'、';
    }

    VectorStore m_store;
    BatchEmbedFn m_batchEmbedFn;    // document chunks embedding (imports)
    int m_embeddingDim{0};
    bool m_usesPseudo{false};
};

} // namespace NexusAI
