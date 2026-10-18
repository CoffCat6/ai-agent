#pragma once

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
    virtual QVariantMap tokenStatsDetailed(int recentDays = 30) {
        Q_UNUSED(recentDays);
        return {};
    }

    // docs/16 §4.4: 按来源聚合统计
    virtual QVariantList tokenStatsBySource(int recentDays = 30) {
        Q_UNUSED(recentDays);
        return {};
    }

    // docs/16 §4.4: 成本核算统计
    virtual QVariantMap costStats(int recentDays = 30) {
        Q_UNUSED(recentDays);
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
