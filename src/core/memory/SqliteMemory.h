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

    // docs/20 §3.1/§3.2（FORK-701）：按主键阈值删除，供重新生成/编辑重发。
    int deleteMessagesFrom(const QString& sessionId, qint64 fromId) override;
    // docs/20 §3.1（FORK-701）：会话内最后一条 user 消息的主键。
    qint64 lastUserMessageId(const QString& sessionId) override;
    bool truncateLastTurn(const QString& sessionId, bool removeUser) override;
    bool setSessionPinned(const QString& id, bool pinned) override;
    bool setSessionArchived(const QString& id, bool archived) override;

    // Batch 17 (FEAT-1702): cross-session keyword search (trigram FTS5 with
    // a LIKE fallback for keywords shorter than 3 characters - the trigram
    // index cannot answer shorter queries).
    QVariantList searchMessages(const QString& keyword, int limit = 50, bool includeArchived = false) override;

    // Episodic memory (FTS5) — currently NOT wired into the agent.
    // Decision recorded in docs/5-完善计划.md Batch 7 (MEM-703): 搁置,
    // pending a summarization-strategy design; the future integration shape
    // is a session summary injected into the system prompt.
    void saveEpisodic(const QString& sessionId, const QJsonObject& episode) override;
    QJsonArray searchEpisodic(const QString& query, int limit = 10) override;

    // M1: long-term memory management (browse / forget / wipe).
    QJsonArray listEpisodic(int limit = 200) override;
    bool deleteEpisodic(qint64 id) override;
    int deleteAllEpisodic() override;

    // M1 (stats): token/cost aggregation from audit_log task_complete rows.
    QVariantMap tokenStats(int recentDays = 30) override;

    // docs/16 §4.4: 分类 token 明细与聚合查询（token_usage 表）。
    void logTokenUsage(const QVariantMap& record) override;
    QVariantMap tokenStatsDetailed(int recentDays = 30) override;
    QVariantList tokenStatsBySource(int recentDays = 30) override;
    QVariantMap costStats(int recentDays = 30) override;
    QVariantMap quotaStatus() override;
    QVariantMap sessionTokenBreakdown(const QString& sessionId) override;

    void clear(const QString& sessionId) override;

    // Session bookkeeping
    void beginSession(const QString& sessionId, const QString& title) override;
    void touchSession(const QString& sessionId) override;
    QJsonArray listSessions(int limit = 50, bool includeArchived = false) override;
    void deleteSession(const QString& sessionId) override;
    void renameSession(const QString& sessionId, const QString& title) override;

    // docs/17 A0: 项目级会话。projects 表 + sessions.project_id（NULL=本地）。
    // 删除项目时会话保留（project_id 置 NULL），不级联删除。
    QString createProject(const QString& name, const QString& rootPath) override;
    void removeProject(const QString& projectId) override;
    void renameProject(const QString& projectId, const QString& name) override;
    QVariantList listProjects() override;
    void setSessionProject(const QString& sessionId, const QString& projectId) override;

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

#ifdef NEXUSAI_TESTING
    // Test-only: direct access for constructing deterministic fixtures
    // (controlled timestamps, bulk inserts). Not part of the runtime API.
    QSqlDatabase databaseForTest() const { return m_db; }
#endif

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
    // docs/17 A0: sessions.project_id 列补齐（受控 ALTER，幂等）。
    void migrateSessionProjectColumn();
    void migrateKnowledgeSchema();
    // trace_nodes.reasoning：思考正文列，老库补列（新库由建表 DDL 带上）
    void migrateTraceNodeReasoningColumn();
    // backfillRows（docs/23 G1）：仅 user_version==0（老库升级/新库首建）
    // 时执行 O(messages) 的 FTS 回填；稳态启动由触发器保证同步，跳过。
    void migrateMessagesFts(bool backfillRows);
    // L3: upgrade episodic_fts to the trigram tokenizer (matches messages_fts).
    // Idempotent: no-op when the table already uses trigram.
    void migrateEpisodicFts();
    // docs/16 §4.2: 首次启动预置 11 个模型的公开定价（INSERT OR IGNORE，
    // 不覆盖用户自定义）。刷新 PricingCalculator 缓存在 initDatabase 末尾。
    void seedModelPricing();

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_connName; // unique per instance: several may coexist (tests)
};

} // namespace NexusAI
