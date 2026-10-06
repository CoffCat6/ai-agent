#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>

namespace NexusAI {

class IMemory {
public:
    virtual ~IMemory() = default;

    virtual void saveMessage(const QString& sessionId, const QJsonObject& msg) = 0;
    virtual QJsonArray loadMessages(const QString& sessionId, int limit = 50) = 0;

    // docs/20 §3.1/§3.2（FORK-701）：重新生成/编辑重发的持久层支撑。删除
    // 会话中 id >= fromId 的全部消息（messages 表 AUTOINCREMENT 主键保证
    // 单调），FTS 影子表经 AFTER DELETE 触发器自动同步。audit_log 不动 ——
    // 重新生成是正常行为，保留完整审计链。可选能力，默认 no-op 返回 0。
    // 返回删除的行数。
    virtual int deleteMessagesFrom(const QString& sessionId, qint64 fromId) {
        Q_UNUSED(sessionId); Q_UNUSED(fromId);
        return 0;
    }

    // docs/20 §3.1（FORK-701）：会话内最后一条 user 消息的主键（AUTOINCREMENT
    // id），供重新生成定位删除阈值。空会话或无 user 消息时返回 0。可选能力，
    // 默认 no-op。
    virtual qint64 lastUserMessageId(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return 0;
    }

    virtual void saveEpisodic(const QString& sessionId, const QJsonObject& episode) = 0;
    virtual QJsonArray searchEpisodic(const QString& query, int limit = 10) = 0;

    virtual void clear(const QString& sessionId) = 0;

    // Session bookkeeping (optional capability; default no-ops)
    virtual void beginSession(const QString& sessionId, const QString& title) {
        Q_UNUSED(sessionId); Q_UNUSED(title);
    }
    virtual void touchSession(const QString& sessionId) {
        Q_UNUSED(sessionId);
    }
    virtual QJsonArray listSessions(int limit = 50) {
        Q_UNUSED(limit);
        return {};
    }
    // Remove a session together with its messages and episodes
    virtual void deleteSession(const QString& sessionId) {
        Q_UNUSED(sessionId);
    }
    virtual void renameSession(const QString& sessionId, const QString& title) {
        Q_UNUSED(sessionId); Q_UNUSED(title);
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
    // history (optional capability; default no-ops). Each result is a map
    // {sessionId, sessionTitle, messageId, snippet, role, timestamp}, newest
    // first. Empty/blank keywords return an empty list.
    virtual QVariantList searchMessages(const QString& keyword, int limit = 50) {
        Q_UNUSED(keyword); Q_UNUSED(limit);
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
