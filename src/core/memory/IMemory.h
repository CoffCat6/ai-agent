#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>

namespace NexusAI {

class IMemory {
public:
    virtual ~IMemory() = default;

    // ── 会话消息（线性）+ 分叉为新会话（docs/21）─────────────────
    // 一个会话就是一条按主键单调递增的对话线。"从这里分叉"不在原会话里动刀：它把
    // 那次回答及之前的历史**复制**成一个新会话（forkSessionAtAnswer），原会话一个字
    // 都不改，天然可回退，也不需要 head/parent/兄弟节点这套持久结构。
    // 答案不另存版本：重新生成 / 编辑重发就是截断这一轮再重发（truncateLastTurn），
    // 与 docs/20 FORK-701 的既有语义一致。
    //
    // 返回新行主键（AUTOINCREMENT 单调），失败返回 0。调用方必须用返回值定位这一行，
    // 不能靠 MAX(id) 反猜——插入失败时会拿到上一条消息的 id。
    virtual qint64 saveMessage(const QString& sessionId, const QJsonObject& msg) = 0;
    // 该会话最近 limit 条消息，按对话顺序正序。每行带 id：分叉锚点与截断阈值都靠它。
    virtual QJsonArray loadMessages(const QString& sessionId, int limit = 50) = 0;

    // docs/20 §3.1/§3.2（FORK-701）：按主键阈值删除（AUTOINCREMENT 保证单调），
    // messages_fts 影子表经 AFTER DELETE 触发器自动同步。audit_log 不动。
    virtual int deleteMessagesFrom(const QString& sessionId, qint64 fromId) {
        Q_UNUSED(sessionId); Q_UNUSED(fromId);
        return 0;
    }
    // docs/20 §3.1（FORK-701）：会话内最后一条 user 消息的主键，供截断定位阈值。
    virtual qint64 lastUserMessageId(const QString& sessionId) {
        Q_UNUSED(sessionId);
        return 0;
    }
    // 原子截末轮：删掉该 user 消息之后的消息行**以及**其后的持久化轨迹节点，一个
    // 事务。轨迹节点必须一起删——上一版只删消息行，被丢弃的过程在重开会话时整段
    // 复活。removeUser=true 时连提问一起删（编辑重发），false 时保留它当重发锚点。
    virtual bool truncateLastTurn(const QString& sessionId, bool removeUser) {
        Q_UNUSED(sessionId); Q_UNUSED(removeUser);
        return false;
    }

    // docs/21 分叉为新会话：把源会话**第 answerRank 条回答及其之前**的历史复制到
    // 一个新会话，返回新会话 id；失败返回空串。**源会话完全不参与**——这就是
    // "在一次回答结束后，由此开始一个新分支"：副本以那条回答结尾，用户在新会话里
    // 接着问。分叉点是一条回答（assistant 行）而不是提问：回答必须一起复制，
    // 否则新会话会停在半句话上。
    // 定位按"第几条回答"而不是消息主键：线性会话里第 n 条 summary 节点恒对应第
    // n 条 assistant 行（一轮回答各写一条，截断成对删、分叉成对复制），所以升级前
    // 那些 trace_nodes.message_id 为空甚至为脏值的会话同样能分叉，不需要数据迁移。
    // 时间线切到那条回答所在轮次的末尾（下一条提问之前的最后一个节点）。
    virtual QString forkSessionAtAnswer(const QString& sourceSessionId, int answerRank,
                                        const QString& title) {
        Q_UNUSED(sourceSessionId); Q_UNUSED(answerRank); Q_UNUSED(title);
        return {};
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
    // docs/21：节点可通过 node["messageId"] 记下它归属的消息主键（落在
    // trace_nodes.message_id 列），仅作诊断用。分叉与截末轮都不读这一列——它们按
    // 类型和 node_order 定位，所以升级前那些填着脏值的旧行不会把操作带偏。
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
