#pragma once

#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QPair>
#include <algorithm>
#include <cmath>

namespace NexusAI {

// In-memory vector store with cosine similarity search
class VectorStore {
public:
    struct Entry {
        QVector<float> embedding;
        QJsonObject metadata;
    };

    void insert(const QVector<float>& embedding, const QJsonObject& metadata) {
        m_entries.append({embedding, metadata});
    }

    // Search for top-k most similar entries by cosine similarity
    QVector<QJsonObject> search(const QVector<float>& query, int k) const {
        if (m_entries.isEmpty() || query.isEmpty())
            return {};

        // Compute similarities
        QVector<QPair<double, int>> scored;
        scored.reserve(m_entries.size());
        for (int i = 0; i < m_entries.size(); ++i) {
            double sim = cosineSimilarity(query, m_entries[i].embedding);
            scored.append({sim, i});
        }

        // docs/25 B6: partial_sort maintains only the sorted top-k prefix —
        // k is tiny compared to the entry count (the 4096-chunk total
        // budget), so the full O(n log n) sort of every scored chunk was
        // wasted work. Degenerates gracefully: count == 0 is a no-op,
        // count == size is a plain full sort.
        const int count = qMin(k, scored.size());
        std::partial_sort(scored.begin(), scored.begin() + count, scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        // Return top-k
        QVector<QJsonObject> results;
        for (int i = 0; i < count; ++i) {
            QJsonObject result = m_entries[scored[i].second].metadata;
            result["score"] = scored[i].first;
            results.append(result);
        }
        return results;
    }

    // All chunk metadata ordered by chunk_index (document detail view)
    QJsonArray chunks() const {
        QVector<QJsonObject> metas;
        metas.reserve(m_entries.size());
        for (const auto& entry : m_entries)
            metas.append(entry.metadata);
        std::sort(metas.begin(), metas.end(),
            [](const QJsonObject& a, const QJsonObject& b) {
                return a.value("chunk_index").toInt() < b.value("chunk_index").toInt();
            });
        QJsonArray arr;
        for (const auto& meta : metas)
            arr.append(meta);
        return arr;
    }

    // Stored entries in insertion order (persistence export, Batch 6)
    QVector<QPair<QVector<float>, QJsonObject>> entries() const {
        QVector<QPair<QVector<float>, QJsonObject>> out;
        out.reserve(m_entries.size());
        for (const auto& entry : m_entries)
            out.append({entry.embedding, entry.metadata});
        return out;
    }

    int size() const { return m_entries.size(); }
    void clear() { m_entries.clear(); }

private:
    static double cosineSimilarity(const QVector<float>& a, const QVector<float>& b) {
        if (a.size() != b.size() || a.isEmpty())
            return 0.0;

        double dot = 0.0, normA = 0.0, normB = 0.0;
        for (int i = 0; i < a.size(); ++i) {
            dot += a[i] * b[i];
            normA += a[i] * a[i];
            normB += b[i] * b[i];
        }

        double denom = std::sqrt(normA) * std::sqrt(normB);
        if (denom == 0.0) return 0.0;
        return dot / denom;
    }

    QVector<Entry> m_entries;
};

} // namespace NexusAI
