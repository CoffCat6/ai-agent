#pragma once

#include <QString>
#include <QVector>
#include <QCryptographicHash>
#include <QByteArray>

namespace NexusAI {

// One persisted knowledge-base chunk: the shared record between RagService
// (producer, in memory) and SqliteMemory (consumer, on disk). Kept in its
// own header so the two never need to include each other.
struct KnowledgeChunk {
    int index{0};
    int total{0};
    QString text;
    QVector<float> embedding; // pseudo-mode chunks persist their (stable) vector too
    int dim{0};
    bool pseudo{false};
};

// Stable content fingerprint for persistence versioning: a document is only
// restored from stored chunks when its content hash AND the embedding model
// that produced them both still match.
inline QString knowledgeContentHash(const QString& content) {
    return QString::fromUtf8(QCryptographicHash::hash(
        content.toUtf8(), QCryptographicHash::Sha256).toHex());
}

// Decide whether persisted chunks may be restored as-is. Stale in either of
// two cases: the stored content drifted from its hash, or the chunks were
// produced by a different embedding model (their vectors live in another
// space - restoring them would return confident nonsense).
inline bool persistedChunksUsable(const QString& storedHash, const QString& storedModel,
                                  const QString& content, const QString& currentModel) {
    if (storedHash.isEmpty() || storedModel.isEmpty()) return false;
    if (storedHash != knowledgeContentHash(content)) return false;
    return storedModel == currentModel;
}

} // namespace NexusAI

Q_DECLARE_METATYPE(NexusAI::KnowledgeChunk)
Q_DECLARE_METATYPE(QVector<NexusAI::KnowledgeChunk>)
