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

    // docs/21：会话是线性的一串消息；分叉 = 复制前史成新会话。
    // 契约与语义见 IMemory.h。
    qint64 saveMessage(const QString& sessionId, const QJsonObject& msg) override;
    QJsonArray loadMessages(const QString& sessionId, int limit = 50) override;
    int deleteMessagesFrom(const QString& sessionId, qint64 fromId) override;
    qint64 lastUserMessageId(const QString& sessionId) override;
    bool truncateLastTurn(const QString& sessionId, bool removeUser) override;
    QString forkSessionAtAnswer(const QString& sourceSessionId, int answerRank,
                                const QString& title) override;
    bool setSessionPinned(const QString& id, bool pinned) override;
    bool setSessionArchived(const QString& id, bool archived) override;

    // Batch 17 (FEAT-1702): cross-session keyword search (trigram FTS5 with
    // a LIKE fallback for keywords shorter than 3 characters - the trigram
    // index cannot answer shorter queries). Batch 22 (search-aggregate):
    // results aggregate PER SESSION - one row per session
    // {sessionId, sessionTitle, hitCount, latestTimestamp, archived,
    // bestSnippet, bestRole, firstHitMessageId}, newest hit first; `limit`
    // caps the number of SESSIONS (default 50). bestSnippet is single-line,
    // markdown-stripped and wraps the keyword in U+0001/U+0002
    // control-character highlight markers (review fix m-1: plain brackets
    // collide with bracketed text like "arr[0]"). firstHitMessageId (A1)
    // is the messages.id of the session's newest matching message — the
    // sidebar jump target. The sub-trigram LIKE fallback keeps its Batch 21
    // (B4) guard: past a message-count ceiling it returns a single notice
    // row (sessionId="__search_notice__").
    QVariantList searchMessages(const QString& keyword, int limit = 50, bool includeArchived = false) override;

    // Episodic memory (FTS5) — currently NOT wired into the agent.
    // Decision recorded in docs/5-完善计划.md Batch 7 (MEM-703): 搁置,
    // pending a summarization-strategy design; the future integration shape
    // is a session summary injected into the system prompt.
    qint64 saveEpisodic(const QString& sessionId, const QJsonObject& episode) override;
    QJsonArray searchEpisodic(const QString& query, int limit = 10) override;
    QJsonArray searchEpisodicSemantic(const QVector<float>& queryEmbedding, int limit = 10) override;
    bool updateEpisodicEmbedding(qint64 id, const QByteArray& blob) override;

    // M1: long-term memory management (browse / forget / wipe).
    QJsonArray listEpisodic(int limit = 200) override;
    bool deleteEpisodic(qint64 id) override;
    int deleteAllEpisodic() override;

    // M1 (stats): token/cost aggregation from audit_log task_complete rows.
    QVariantMap tokenStats(int recentDays = 30) override;

    // docs/16 §4.4: 分类 token 明细与聚合查询（token_usage 表）。
    // Batch 6 T1: fromTs/toTs 范围语义（左闭右开 [fromTs, toTs)，任一
    // > 0 生效、均为 0 回退 recentDays）与 byDay 行上限语义（批次六
    // 审查 M1：回退窗口 31 行 / 范围模式 366 行，超出截断最近 N 天）
    // 见 IMemory.h。
    void logTokenUsage(const QVariantMap& record) override;
    QVariantMap tokenStatsDetailed(int recentDays = 30,
                                   qint64 fromTs = 0, qint64 toTs = 0) override;
    QVariantList tokenStatsBySource(int recentDays = 30,
                                    qint64 fromTs = 0, qint64 toTs = 0) override;
    QVariantMap costStats(int recentDays = 30,
                          qint64 fromTs = 0, qint64 toTs = 0) override;
    QVariantMap quotaStatus() override;
    QVariantMap sessionTokenBreakdown(const QString& sessionId) override;
    // Batch 6 T1: token_usage 明细分页（{rows, total}，timestamp DESC），
    // 契约见 IMemory.h。
    QVariantMap tokenUsageRecords(qint64 fromTs = 0, qint64 toTs = 0,
                                  int limit = 50, int offset = 0) override;

    void clear(const QString& sessionId) override;

    // Session bookkeeping
    void beginSession(const QString& sessionId, const QString& title) override;
    void touchSession(const QString& sessionId) override;
    QJsonArray listSessions(int limit = 50, bool includeArchived = false) override;
    void deleteSession(const QString& sessionId) override;
    // docs/23 G7: soft-delete support. deleteSession marks deleted_at
    // (messages/trace stay for the undo window); restoreSession unmarks;
    // purgeDeletedSessions hard-cascades rows whose deleted_at is older
    // than the cutoff. Returns the number of sessions purged.
    bool restoreSession(const QString& sessionId) override;
    int purgeDeletedSessions(qint64 deletedBefore) override;
    void renameSession(const QString& sessionId, const QString& title) override;

    // G4: file checkpoints (see IMemory.h). Per-session stored content is
    // capped; over-budget rows degrade to path-only (truncated=1).
    qint64 addFileCheckpoint(const QString& sessionId, const QString& path,
                             const QByteArray& beforeContent, bool existed,
                             bool truncated, quint64 requestId = 0) override;
    QJsonArray listFileCheckpoints(const QString& sessionId, int limit = 200) override;
    bool loadFileCheckpointContent(qint64 id, const QString& sessionId,
                                   QByteArray* beforeContent, bool* existed,
                                   bool* truncated, QString* path) override;
    bool markFileCheckpointUndone(qint64 id, const QString& sessionId,
                                  qint64 undoneAtMs) override;
    int pruneFileCheckpoints(qint64 olderThanMs) override;

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
    // loadUsageEvents is promoted to the IMemory interface (usage timeline
    // page): the signature must stay identical to the virtual, including
    // the trailing const, or it would hide instead of override.
    void addUsageEvent(const QString& category, const QString& action,
                       const QString& target, quint64 requestId,
                       const QString& detail);
    QJsonArray loadUsageEvents(int limit = 500) const override;
    // Delete rows older than keepDays OR beyond maxRows (newest kept).
    // Returns the number of rows removed.
    int pruneUsage(int keepDays = 30, int maxRows = 10000);
    // docs/23 G3: time-based retention for the audit tables that previously
    // grew unbounded. keepDays <= 0 = keep forever. Returns rows removed.
    int pruneAudit(int keepDays);
    int pruneTokenUsage(int keepDays);

    // ── docs/23 D1: database backup ─────────────────────────────
    // The SQLite file this instance is bound to (default: the app-data
    // nexusai_memory.db).
    QString databasePath() const { return m_dbPath; }
    // <app-data>/backups — the automatic/manual backup directory.
    static QString defaultBackupsDir();
    // Write a consistent snapshot of dbPath via VACUUM INTO from a
    // short-lived self-contained connection (WAL-safe: the read transaction
    // never blocks a concurrent GUI-thread writer). Self-contained on
    // purpose — usable from a worker thread without touching any
    // GUI-thread object (docs/22 exit-race lesson).
    static bool createBackupFile(const QString& dbPath, const QString& targetPath);
    // Remove oldest auto_*.db snapshots beyond keepCount (manual_* files
    // are never touched). Returns files removed.
    static int rotateAutoBackups(const QString& backupsDir, int keepCount);
    // Read-only peek into a backup file: {sessions, messages, knowledgeDocs,
    // newestTimestamp, error?} — powers the restore preview dialog (D2).
    static QVariantMap inspectBackupFile(const QString& backupPath);
    // docs/24 R1: swap the live database file for a verified snapshot.
    // Caller guarantees no other open connection to the file (UI closes all
    // tabs first). Every failure leaves the original database in place and
    // the connection usable; returns false in that case.
    bool restoreFromBackup(const QString& backupPath);

private:
    // Batch 22 (search-aggregate): snippet post-processing shared by the
    // FTS and LIKE search paths. stripMarkdownDecorations removes the
    // decorations a raw message body carries (bold/italic/code spans,
    // links, images, headings, list bullets, fenced blocks); the ordering
    // is load-bearing - see the .cpp. buildSearchSnippet composes the full
    // pipeline: strip → flatten newlines → hit-centered window → inject
    // the U+0001/U+0002 control-character highlight marker LAST (stripping
    // markdown first is what keeps the link rule from eating the marked
    // keyword as link text; review fix M-1 also drops the underscore
    // bold/italic rules there to protect snake_case identifiers).
    static QString stripMarkdownDecorations(const QString& text);
    static QString buildSearchSnippet(const QString& raw, const QString& keyword);

    void initDatabase();
    void createTables();
    // docs/24 R1: shared by the constructor and restoreFromBackup — add,
    // open, PRAGMA setup, schema init. m_dbPath must be set already.
    bool openConnection();
    // docs/24 R1: restore-time integrity gate (read-only quick_check).
    static bool snapshotPassesQuickCheck(const QString& path);
    // docs/23 G7: 硬级联（messages/episodic/sessions/trace_nodes），
    // 由 purgeDeletedSessions 对到期软删会话逐个调用。
    // docs/25 B4: 返回全部四条 DELETE 是否成功（purge 事务据此回滚）。
    bool hardDeleteSessionRows(const QString& sessionId);
    // docs/17 A0: sessions.project_id 列补齐（受控 ALTER，幂等）。
    void migrateSessionProjectColumn();
    void migrateKnowledgeSchema();
    // docs/25: knowledge_chunks.page 列补齐（引用定位——chunk 页码；幂等，
    // 仅老库升级路径，新库由建表 DDL 带上）。
    void migrateKnowledgeChunkPageColumn();
    // trace_nodes.reasoning：思考正文列，老库补列（新库由建表 DDL 带上）
    void migrateTraceNodeReasoningColumn();
    // docs/21：会话模型定为"线性历史 + 分叉成新会话"。这一步把中间那版消息树
    // 拉平：按 head 的祖先链定序、删掉旁支上被放弃的重复答案行、清空 parent_id /
    // head_message_id，并回收遗留的 message_variants 表（答案不再存多版本）。
    // 没经历过树版本的库这些列/表本来就不存在，整段是 no-op，只落版本闸门。
    // 仅在 user_version < 3 时跑一次。
    void migrateConversationModel(int userVersion);
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
