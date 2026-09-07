#pragma once

#include "IMemory.h"
#include "core/rag/KnowledgeChunk.h"

#include <QSqlDatabase>
#include <QString>

namespace NexusAI {

class SqliteMemory : public IMemory {
public:
    explicit SqliteMemory(const QString& dbPath = QString());
    ~SqliteMemory() override;

    void saveMessage(const QString& sessionId, const QJsonObject& msg) override;
    QJsonArray loadMessages(const QString& sessionId, int limit = 50) override;

    // Episodic memory (FTS5) — currently NOT wired into the agent.
    // Decision recorded in docs/5-完善计划.md Batch 7 (MEM-703): 搁置,
    // pending a summarization-strategy design; the future integration shape
    // is a session summary injected into the system prompt.
    void saveEpisodic(const QString& sessionId, const QJsonObject& episode) override;
    QJsonArray searchEpisodic(const QString& query, int limit = 10) override;

    void clear(const QString& sessionId) override;

    // Session bookkeeping
    void beginSession(const QString& sessionId, const QString& title) override;
    void touchSession(const QString& sessionId) override;
    QJsonArray listSessions(int limit = 50) override;
    void deleteSession(const QString& sessionId) override;
    void renameSession(const QString& sessionId, const QString& title) override;

    // Audit trail
    void logAudit(const QString& sessionId, const QString& event,
                  const QString& toolName, const QString& arguments,
                  bool success, const QString& detail,
                  quint64 requestId = 0, qint64 latencyMs = -1,
                  int tokens = 0, const QString& model = QString()) override;
    QJsonArray auditLog(int limit = 100) override;

    // Knowledge-base persistence (v2): documents keep their content hash and
    // the embedding model that produced the chunks, so startup restore can
    // skip re-embedding entirely when nothing changed (Batch 6).
    struct KnowledgeDocRecord {
        QString name;
        QString content;
        QString contentHash;
        QString embeddingModel;
    };

    void upsertKnowledgeDoc(const QString& name, const QString& content,
                            const QString& contentHash, const QString& embeddingModel);
    bool removeKnowledgeDoc(const QString& name);
    void clearKnowledgeDocs();
    QString loadKnowledgeDoc(const QString& name) const; // empty when unknown
    QVector<KnowledgeDocRecord> loadKnowledgeDocRecords() const;
    // Transactional chunk replacement (delete + insert for one document)
    void replaceKnowledgeChunks(const QString& name,
                                const QVector<KnowledgeChunk>& chunks);
    QVector<KnowledgeChunk> loadKnowledgeChunks(const QString& name) const;

    // Trace view persistence (IMemory optional capability)
    void saveTraceNode(const QString& sessionId, const QJsonObject& node, int order) override;
    QJsonArray loadTraceNodes(const QString& sessionId) override;

private:
    void initDatabase();
    void createTables();
    void migrateKnowledgeSchema();

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_connName; // unique per instance: several may coexist (tests)
};

} // namespace NexusAI
