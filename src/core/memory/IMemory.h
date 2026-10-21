#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace NexusAI {

class IMemory {
public:
    virtual ~IMemory() = default;

    // 返回新行主键（失败 0）：调用方靠它定位这一行，MAX(id) 在插入失败时会拿到上一条。
    virtual qint64 saveMessage(const QString& sessionId, const QJsonObject& msg) = 0;
    virtual QJsonArray loadMessages(const QString& sessionId, int limit = 50) = 0;

    virtual int deleteMessagesFrom(const QString& sessionId, qint64 fromId) {
        Q_UNUSED(sessionId); Q_UNUSED(fromId);
        return 0;
    }
    virtual qint64 lastUserMessageId(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return 0;
    }
    // 原子截末轮：消息行和其后的轨迹节点一起删，一个事务。
    virtual bool truncateLastTurn(const QString& sessionId, bool removeUser) {
        Q_UNUSED(sessionId); Q_UNUSED(removeUser);
        return false;
    }

    // docs/21：把第 answerRank 条回答及之前的历史复制成一个新会话（失败空串）。
    virtual QString forkSessionAtAnswer(const QString& sourceSessionId, int answerRank,
                                        const QString& title) {
        Q_UNUSED(sourceSessionId); Q_UNUSED(answerRank); Q_UNUSED(title);
        return {};
    }

    virtual qint64 saveEpisodic(const QString& sessionId, const QJsonObject& episode) = 0;
    virtual QJsonArray searchEpisodic(const QString& query, int limit = 10) = 0;
    // Semantic retrieval via cosine similarity against stored embedding BLOBs.
    // Default no-op; SqliteMemory implements with in-memory cosine rerank.
    virtual QJsonArray searchEpisodicSemantic(const QVector<float>& queryEmbedding, int limit = 10) {
        Q_UNUSED(queryEmbedding); Q_UNUSED(limit);
        return {};
    }
    // Asynchronously update the embedding BLOB of an existing episode.
    virtual bool updateEpisodicEmbedding(qint64 id, const QByteArray& blob) {
        Q_UNUSED(id); Q_UNUSED(blob);
        return false;
    }

    virtual void clear(const QString& sessionId) = 0;

    // Session bookkeeping (optional capability; default no-ops)
    virtual void beginSession(const QString& sessionId, const QString& title) {
        Q_UNUSED(sessionId); Q_UNUSED(title);
    }
    virtual void touchSession(const QString& sessionId) {
        Q_UNUSED(sessionId);
    }
    virtual QJsonArray listSessions(int limit = 50, bool includeArchived = false) {
        Q_UNUSED(limit); Q_UNUSED(includeArchived);
        return {};
    }
    // Remove a session together with its messages and episodes
    virtual void deleteSession(const QString& sessionId) {
        Q_UNUSED(sessionId);
    }
    // docs/23 G7: sessions are soft-deleted (messages/trace stay for the
    // undo window) and hard-purged after the retention window. Defaults
    // keep alternate IMemory backends compiling without the feature.
    virtual bool restoreSession(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return false;
    }
    virtual int purgeDeletedSessions(qint64 deletedBefore) {
        Q_UNUSED(deletedBefore);
        return 0;
    }
    // docs/23 D1: absolute path of the backing SQLite file (empty for
    // backends without one) — the backup feature keys off it.
    virtual QString databasePath() const { return {}; }
    // docs/24 R1: swap the backing store for a verified snapshot. Only
    // meaningful for file-backed implementations; default = unsupported.
    virtual bool restoreFromBackup(const QString& backupPath) {
        Q_UNUSED(backupPath);
        return false;
    }
    virtual void renameSession(const QString& sessionId, const QString& title) {
        Q_UNUSED(sessionId); Q_UNUSED(title);
    }
    virtual bool setSessionPinned(const QString& id, bool pinned) {
        Q_UNUSED(id); Q_UNUSED(pinned); return false;
    }
    virtual bool setSessionArchived(const QString& id, bool archived) {
        Q_UNUSED(id); Q_UNUSED(archived); return false;
    }

    // G4: workspace file checkpoints. write_file/edit_file hand the original
    // bytes and the at-write diff back to the agent, which persists them here
    // (GUI thread only — the DB connection is thread-bound). `truncated`
    // means only the path was recorded (too large / unreadable); such rows
    // are audit-only and cannot be undone. Returns the new row id (0 =
    // failure).
    struct FileCheckpointInput {
        QString path;
        QByteArray beforeContent;
        bool existed{false};
        bool truncated{false};
        QString diff;
        bool diffTruncated{false};
        int addedLines{0};
        int removedLines{0};
        QString afterHash;
    };
    virtual qint64 addFileCheckpoint(const QString& sessionId,
                                     const FileCheckpointInput& input,
                                     quint64 requestId = 0) {
        Q_UNUSED(sessionId); Q_UNUSED(input); Q_UNUSED(requestId);
        return 0;
    }
    // Metadata only (newest first), no file content or diff text.
    virtual QJsonArray listFileCheckpoints(const QString& sessionId, int limit = 200) {
        Q_UNUSED(sessionId); Q_UNUSED(limit);
        return {};
    }
    // One entry per touched file (newest active write first). `id` is the
    // newest not-yet-undone row (or the newest row when all are undone), so
    // the UI updates one row per file while undo still steps through the
    // per-write history. Adds: changeCount, undoneCount, activeCount,
    // firstAt, updatedAt, latestId, firstId.
    virtual QJsonArray listFileCheckpointFiles(const QString& sessionId,
                                               int limit = 200) {
        Q_UNUSED(sessionId); Q_UNUSED(limit);
        return {};
    }
    // Full detail for one checkpoint (diff, stats, hash); empty when unknown.
    virtual QJsonObject fileCheckpointDetail(qint64 id, const QString& sessionId) {
        Q_UNUSED(id); Q_UNUSED(sessionId);
        return {};
    }
    // Loads one not-yet-undone checkpoint's content, scoped to its session.
    virtual bool loadFileCheckpointContent(qint64 id, const QString& sessionId,
                                           QByteArray* beforeContent, bool* existed,
                                           bool* truncated, QString* path) {
        Q_UNUSED(id); Q_UNUSED(sessionId); Q_UNUSED(beforeContent);
        Q_UNUSED(existed); Q_UNUSED(truncated); Q_UNUSED(path);
        return false;
    }
    // Marks a checkpoint undone and drops its stored content (the row stays
    // as history). Returns false when the row is unknown/already undone.
    virtual bool markFileCheckpointUndone(qint64 id, const QString& sessionId,
                                          qint64 undoneAtMs) {
        Q_UNUSED(id); Q_UNUSED(sessionId); Q_UNUSED(undoneAtMs);
        return false;
    }
    // Session-level undo: loads the earliest not-yet-undone write for one
    // path — the state before the session first touched the file.
    virtual bool loadFileCheckpointGroupContent(
        const QString& sessionId, const QString& path,
        QByteArray* beforeContent, bool* existed, bool* truncated,
        QString* storedPath) {
        Q_UNUSED(sessionId); Q_UNUSED(path); Q_UNUSED(beforeContent);
        Q_UNUSED(existed); Q_UNUSED(truncated); Q_UNUSED(storedPath);
        return false;
    }
    // Marks every not-yet-undone write for one path undone and drops their
    // payloads. Returns false when no active write remains.
    virtual bool markFileCheckpointGroupUndone(const QString& sessionId,
                                               const QString& path,
                                               qint64 undoneAtMs) {
        Q_UNUSED(sessionId); Q_UNUSED(path); Q_UNUSED(undoneAtMs);
        return false;
    }
    // Drops checkpoints older than the cutoff (session purge is separate).
    virtual int pruneFileCheckpoints(qint64 olderThanMs) {
        Q_UNUSED(olderThanMs);
        return 0;
    }

    // F1: the todo_write task list, persisted per session so reopening a
    // session restores its own list instead of starting empty. The tool
    // always sends the FULL list, so saveSessionTodos REPLACES the stored
    // list for that session (never appends). GUI thread only — the DB
    // connection is thread-bound.
    virtual bool saveSessionTodos(const QString& sessionId,
                                  const QJsonArray& todos) {
        Q_UNUSED(sessionId); Q_UNUSED(todos);
        return false;
    }
    // The stored list for one session; empty when the session never used
    // todo_write (or is unknown).
    virtual QJsonArray loadSessionTodos(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return {};
    }

    // Audit trail for tool activity (optional capability; default no-ops).
    // events: tool_execute / tool_approved / tool_declined / tool_blocked /
    // unknown_tool / llm_request / task_complete (Batch 11).
    // Observability params (OBS-1102): requestId links every row of one
    // request; latency/tokens/model annotate llm_request and task_complete.
    virtual void logAudit(const QString& sessionId, const QString& event,
                          const QString& toolName, const QString& arguments,
                          bool success, const QString& detail,
                          quint64 requestId = 0, qint64 latencyMs = -1,
                          int tokens = 0, const QString& model = QString()) {
        Q_UNUSED(sessionId); Q_UNUSED(event); Q_UNUSED(toolName);
        Q_UNUSED(arguments); Q_UNUSED(success); Q_UNUSED(detail);
        Q_UNUSED(requestId); Q_UNUSED(latencyMs); Q_UNUSED(tokens); Q_UNUSED(model);
    }
    virtual QJsonArray auditLog(int limit = 100) {
        Q_UNUSED(limit);
        return {};
    }

    // Trace view persistence (optional capability; default no-ops). Trace
    // nodes rebuild the visual timeline (thinking / tool calls / results)
    // when a session is resumed - plain message history only keeps the
    // user/assistant text. `order` fixes the timeline position on first
    // insert and is ignored on upsert (status/title updates).
    virtual void saveTraceNode(const QString& sessionId, const QJsonObject& node, int order) {
        Q_UNUSED(sessionId); Q_UNUSED(node); Q_UNUSED(order);
    }
    virtual QJsonArray loadTraceNodes(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return {};
    }

    // docs/17 A0: 项目级会话（optional capability; default no-ops）。
    // 项目 = 导入的文件夹；删除项目时会话保留（project_id 置 NULL）。
    virtual QString createProject(const QString& name, const QString& rootPath) {
        Q_UNUSED(name); Q_UNUSED(rootPath);
        return {};
    }
    virtual void removeProject(const QString& projectId) {
        Q_UNUSED(projectId);
    }
    // P2: 项目重命名（显示名；root_path 不变）。
    virtual void renameProject(const QString& projectId, const QString& name) {
        Q_UNUSED(projectId); Q_UNUSED(name);
    }
    virtual QVariantList listProjects() {
        return {};
    }
    // 会话归属：projectId 为空串表示脱离项目（置 NULL）。
    virtual void setSessionProject(const QString& sessionId, const QString& projectId) {
        Q_UNUSED(sessionId); Q_UNUSED(projectId);
    }

    // Batch 17 (FEAT-1702): cross-session keyword search over the message
    // history (optional capability; default no-ops). Batch 22
    // (search-aggregate): results are aggregated PER SESSION - each row is
    // a map {sessionId, sessionTitle, hitCount, latestTimestamp, archived,
    // bestSnippet, bestRole, firstHitMessageId}, ordered by latestTimestamp
    // (the session's newest hit) first; `limit` caps the number of
    // SESSIONS. bestSnippet is single-line, markdown-stripped and wraps the
    // keyword in U+0001/U+0002 control-character highlight markers.
    // firstHitMessageId (qint64) is the messages.id of that session's
    // newest matching message (0 when unavailable); archived mirrors the
    // session soft-archive flag. Empty/blank keywords return an empty list.
    virtual QVariantList searchMessages(const QString& keyword, int limit = 50, bool includeArchived = false) {
        Q_UNUSED(keyword); Q_UNUSED(limit); Q_UNUSED(includeArchived);
        return {};
    }

    // M1: long-term memory management (optional capability; default no-ops).
    // listEpisodic enumerates every stored episode, newest first — needed
    // because searchEpisodic requires a non-empty keyword and therefore
    // cannot list. deleteEpisodic forgets one episode by id; deleteAllEpisodic
    // wipes the store and returns the number of rows removed.
    virtual QJsonArray listEpisodic(int limit = 200) {
        Q_UNUSED(limit);
        return {};
    }
    virtual bool deleteEpisodic(qint64 id) {
        Q_UNUSED(id);
        return false;
    }
    virtual int deleteAllEpisodic() {
        return 0;
    }

    // Usage activity timeline (optional capability; default no-ops).
    // loadUsageEvents enumerates recent usage_log rows, newest first
    // (id DESC); `limit` caps the number of rows returned. Each item is
    // a map with:
    //   timestamp  qint64 (epoch ms)
    //   category   "nexus.user" / "nexus.agent" / "nexus.network"
    //   action     short event name (e.g. "session.create", "llm_result")
    //   target     target id/name (may be empty)
    //   requestId  double (0 when uncorrelated)
    //   detail     one-line metadata (may be empty)
    // Privacy contract (Activity.h): metadata only — action, target
    // id/name, sizes, ids; never prompt text, document content or API
    // keys.
    virtual QJsonArray loadUsageEvents(int limit = 500) const {
        Q_UNUSED(limit);
        return {};
    }

    // M1 (stats): aggregate token/cost statistics from the audit trail
    // (optional capability; default empty). The task_complete audit rows are
    // the only place real token counts land. Returns a map with
    // {totalCalls, totalTokens, avgLatencyMs, byModel: [...], byDay: [...]}.
    virtual QVariantMap tokenStats(int recentDays = 30) {
        Q_UNUSED(recentDays);
        return {};
    }

    // docs/16 §4.4: 记录单次 token 使用明细（分类统计）
    virtual void logTokenUsage(const QVariantMap& record) {
        Q_UNUSED(record);
    }

    // docs/16 §4.4: 详细聚合统计（含方向/来源拆分）
    // Batch 6 T1: fromTs/toTs（epoch ms，本地时区，口径对齐写入侧
    // QDateTime::toMSecsSinceEpoch）任一 > 0 时进入范围模式：窗口为
    // [fromTs, toTs)（左闭右开；fromTs <= 0 不设下界、toTs <= 0 不设
    // 上界，fromTs == toTs 必为空范围），recentDays 被忽略——「今日」=
    // 本地自然日 0 点 ~ 次日 0 点，由调用方构造后传入；两者均 <= 0 时
    // 回退既有 recentDays 回溯窗口（recentDays <= 0 按 30）。
    // byDay 行上限（批次六审查 M1）：recentDays 回退窗口最多 31 行、
    // 范围模式最多 366 行，超出按 day DESC 截断最近 N 天；范围跨多年
    // 超出 366 天属已知边界（降采样演进方向见 docs/25 批次六遗留技术债）。
    virtual QVariantMap tokenStatsDetailed(int recentDays = 30,
                                           qint64 fromTs = 0, qint64 toTs = 0) {
        Q_UNUSED(recentDays); Q_UNUSED(fromTs); Q_UNUSED(toTs);
        return {};
    }

    // docs/16 §4.4: 按来源聚合统计（范围语义同 tokenStatsDetailed）
    virtual QVariantList tokenStatsBySource(int recentDays = 30,
                                            qint64 fromTs = 0, qint64 toTs = 0) {
        Q_UNUSED(recentDays); Q_UNUSED(fromTs); Q_UNUSED(toTs);
        return {};
    }

    // docs/16 §4.4: 成本核算统计（范围语义同 tokenStatsDetailed；
    // projectedMonthlyCents 固定按当月推算，不受范围参数影响；
    // byDay 行上限语义同 tokenStatsDetailed：回退窗口 31 行 / 范围
    // 模式 366 行，超出截断最近 N 天）
    virtual QVariantMap costStats(int recentDays = 30,
                                  qint64 fromTs = 0, qint64 toTs = 0) {
        Q_UNUSED(recentDays); Q_UNUSED(fromTs); Q_UNUSED(toTs);
        return {};
    }

    // Batch 6 T1: token_usage 明细分页查询（每次 LLM 请求一行）。
    // 范围：fromTs > 0 加下界 timestamp >= fromTs，toTs > 0 加上界
    // timestamp < toTs（左闭右开，与聚合接口一致）；均为 0 = 全部记录
    // （明细查询没有 recentDays 回退概念）。
    // 返回 {rows: QVariantList, total: int}：
    //   - rows 按 timestamp DESC（同刻按 id DESC 稳定排序）分页返回，
    //     每行为驼峰键 QVariantMap {requestId, sessionId, model,
    //     timestamp, inputTokens, outputTokens, cacheHitTokens,
    //     latencyMs, costCents}；
    //   - total = 范围内总行数（与分页无关，供翻页 UI 计算页数）；
    //     offset 超出总数时 rows 为空、total 仍正确；
    //   - offset 分页的并发写入语义（批次六审查 m2）：翻页期间若有
    //     新行写入（新行 timestamp 更大、插入队首），后续 offset 窗口
    //     整体右移，已展示行可能重复出现于下一页；UI 可通过 refresh
    //     重置翻页规避（长期演进：锚定翻页，见 docs/25 批次六遗留技术债）。
    // limit 为每页行数（默认 50，钳制到 [1, 500]），offset < 0 归 0。
    virtual QVariantMap tokenUsageRecords(qint64 fromTs = 0, qint64 toTs = 0,
                                          int limit = 50, int offset = 0) {
        Q_UNUSED(fromTs); Q_UNUSED(toTs); Q_UNUSED(limit); Q_UNUSED(offset);
        return {};
    }

    // docs/16 §4.4: 配额状态查询
    virtual QVariantMap quotaStatus() {
        return {};
    }

    // docs/16 §8.3: 会话级 token 细节聚合（TopBar 悬停浮层）。对单个会话的
    // token_usage 行做方向/来源拆分汇总；无记录时 totalCalls 为 0。
    virtual QVariantMap sessionTokenBreakdown(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return {};
    }
};

} // namespace NexusAI
