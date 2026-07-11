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

        // Sort by similarity descending
        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        // Return top-k
        QVector<QJsonObject> results;
        int count = qMin(k, scored.size());
        for (int i = 0; i < count; ++i) {
            QJsonObject result = m_entries[scored[i].second].metadata;
            result["score"] = scored[i].first;
            results.append(result);
        }
        return results;
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
