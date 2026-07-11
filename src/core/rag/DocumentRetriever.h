#pragma once

#include "VectorStore.h"

#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <cmath>

namespace NexusAI {

// Document retriever - chunks documents and retrieves via vector similarity
class DocumentRetriever {
public:
    using EmbedFn = std::function<QVector<float>(const QString&)>;

    explicit DocumentRetriever(EmbedFn embedFn = {})
        : m_embedFn(std::move(embedFn)) {}

    void setEmbedFunction(EmbedFn fn) { m_embedFn = std::move(fn); }

    // Add a document: chunks it and stores embeddings
    void addDocument(const QString& content, const QJsonObject& metadata) {
        auto chunks = chunkDocument(content, DefaultChunkSize);
        for (int i = 0; i < chunks.size(); ++i) {
            QJsonObject chunkMeta = metadata;
            chunkMeta["chunk_index"] = i;
            chunkMeta["chunk_total"] = chunks.size();
            chunkMeta["text"] = chunks[i];

            QVector<float> embedding;
            if (m_embedFn) {
                embedding = m_embedFn(chunks[i]);
            } else {
                // Fallback: simple hash-based pseudo-embedding
                embedding = pseudoEmbed(chunks[i]);
            }

            m_store.insert(embedding, chunkMeta);
        }
    }

    // Search for relevant document chunks
    QJsonArray search(const QString& query, int k = 5) const {
        QVector<float> queryEmbedding;
        if (m_embedFn) {
            queryEmbedding = m_embedFn(query);
        } else {
            queryEmbedding = pseudoEmbed(query);
        }

        auto results = m_store.search(queryEmbedding, k);
        QJsonArray arr;
        for (const auto& obj : results) {
            arr.append(obj);
        }
        return arr;
    }

    int documentCount() const { return m_store.size(); }
    void clear() { m_store.clear(); }

    static constexpr int DefaultChunkSize = 512; // ~512 chars per chunk

private:
    // Split text into chunks by approximate token count
    static QStringList chunkDocument(const QString& text, int chunkSize) {
        QStringList chunks;
        // Simple word-based chunking
        QStringList words = text.split(' ');
        int currentSize = 0;
        QString currentChunk;

        for (const QString& word : words) {
            if (currentSize + word.length() + 1 > chunkSize && !currentChunk.isEmpty()) {
                chunks.append(currentChunk.trimmed());
                currentChunk.clear();
                currentSize = 0;
            }
            currentChunk += word + " ";
            currentSize += word.length() + 1;
        }

        if (!currentChunk.trimmed().isEmpty()) {
            chunks.append(currentChunk.trimmed());
        }

        return chunks;
    }

    // Simple pseudo-embedding for fallback (no real semantic meaning)
    static QVector<float> pseudoEmbed(const QString& text) {
        QVector<float> vec(128, 0.0f);
        for (int i = 0; i < text.length(); ++i) {
            QChar ch = text.at(i);
            int idx = (ch.unicode() % 128);
            vec[idx] += 1.0f;
        }
        // Normalize
        float norm = 0.0f;
        for (float v : vec) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0) {
            for (float& v : vec) v /= norm;
        }
        return vec;
    }

    VectorStore m_store;
    EmbedFn m_embedFn;
};

} // namespace NexusAI
