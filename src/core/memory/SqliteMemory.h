#pragma once

#include "IMemory.h"
#include "core/rag/KnowledgeChunk.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>

namespace NexusAI {

class SqliteMemory : public IMemory {
    // Batch 16 (P3-9 follow-up): the session list's relative timestamps
    // ("刚刚"/"%1 分钟前"...) are UI-visible copy produced here, so this
    // non-QObject class needs tr() plumbing for lupdate/zh_CN.
    Q_DECLARE_TR_FUNCTIONS(SqliteMemory)

public:
    explicit SqliteMemory(const QString& dbPath = QString());
    ~SqliteMemory() override;

    void saveMessage(const QString& sessionId, const QJsonObject& msg) override;
    QJsonArray loadMessages(const QString& sessionId, int limit = 50) override;

    // Batch 17 (FEAT-1702): cross-session keyword search (trigram FTS5 with
    // a LIKE fallback for keywords shorter than 3 characters - the trigram
    // index cannot answer shorter queries).
    QVariantList searchMessages(const QString& keyword, int limit = 50) override;

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

    // ── Usage trail (跨会话用户操作与网络事件, Batch 12 前日志增强) ──
    // Structured usage events (Activity facade → this sink). Retained under
    // the 30-day / 10000-row policy enforced by pruneUsage at startup.
    void addUsageEvent(const QString& category, const QString& action,
                       const QString& target, quint64 requestId,
                       const QString& detail);
    QJsonArray loadUsageEvents(int limit = 500) const;
    // Delete rows older than keepDays OR beyond maxRows (newest kept).
    // Returns the number of rows removed.
    int pruneUsage(int keepDays = 30, int maxRows = 10000);

private:
    void initDatabase();
    void createTables();
    void migrateKnowledgeSchema();
    void migrateMessagesFts();

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_connName; // unique per instance: several may coexist (tests)
};

} // namespace NexusAI
