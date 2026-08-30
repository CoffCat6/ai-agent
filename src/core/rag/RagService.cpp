#include "RagService.h"

#include <QJsonObject>
#include <algorithm>

namespace NexusAI {

RagService::RagService(QObject* parent) : QObject(parent) {}

void RagService::setEmbedder(DocumentRetriever::EmbedFn fn) {
    QMutexLocker lock(&m_mutex);
    m_embedFn = std::move(fn);
}

int RagService::ingestDocument(const QString& sourceName, const QString& content) {
    const QString trimmed = content.trimmed();
    if (sourceName.isEmpty() || trimmed.isEmpty()) return 0;

    QMutexLocker lock(&m_mutex);

    // Replace an existing document of the same name
    for (int i = 0; i < m_docs.size(); ++i) {
        if (m_docs[i].name == sourceName) {
            m_docs.erase(m_docs.begin() + i);
            break;
        }
    }

    auto retriever = std::make_unique<DocumentRetriever>();
    retriever->setEmbedFunction(m_embedFn);
    retriever->addDocument(trimmed, QJsonObject{{"source", sourceName}});

    Document doc;
    doc.name = sourceName;
    doc.chars = trimmed.size();
    doc.retriever = std::move(retriever);
    const int chunks = doc.retriever->documentCount();
    m_docs.push_back(std::move(doc));

    lock.unlock();
    emit changed();
    return chunks;
}

QJsonArray RagService::search(const QString& query, int k) const {
    QMutexLocker lock(&m_mutex);

    if (query.trimmed().isEmpty() || m_docs.empty()) return {};

    // Merge scored passages from every document, keep the global top-k
    QVector<QJsonObject> hits;
    for (const auto& doc : m_docs) {
        const QJsonArray results = doc.retriever->search(query, k);
        for (const auto& entry : results)
            hits.append(entry.toObject());
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
        out.append(entry);
    }
    return out;
}

bool RagService::removeDocument(const QString& sourceName) {
    QMutexLocker lock(&m_mutex);

    for (int i = 0; i < m_docs.size(); ++i) {
        if (m_docs[i].name == sourceName) {
            m_docs.erase(m_docs.begin() + i);
            lock.unlock();
            emit changed();
            return true;
        }
    }
    return false;
}

void RagService::clear() {
    QMutexLocker lock(&m_mutex);
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
