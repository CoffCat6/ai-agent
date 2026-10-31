#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <QVariantList>
#include <QVariantMap>

#include "core/agent/AgentState.h"
#include "TraceModel.h"

namespace NexusAI {

class AIAgent;

// One open session tab (Batch 29, F7): a dedicated AIAgent instance plus the
// conversation state belonging to it (trace / response / draft / session id /
// token usage). Parallel tabs each run their own
// controller, so a task running in a background tab keeps executing and
// updating its own trace while the user works in another tab.
//
// Tab management, the global session list and the knowledge base stay in
// AgentViewModel; this class is deliberately dumb about the tab concept.
class ChatSessionController : public QObject {
    Q_OBJECT

public:
    explicit ChatSessionController(QObject* parent = nullptr);
    ~ChatSessionController() override;

    // Attach the real pipeline. owned=true (factory-built tab agents) makes
    // the controller delete the agent on destruction; the shared template
    // agent from AgentViewModel::setAgent is attached with owned=false.
    void attachAgent(AIAgent* agent, bool owned);
    AIAgent* agent() const { return m_agent; }

    AgentState state() const { return m_state; }
    QString stateText() const;
    bool isBusy() const;
    TraceModel* trace() { return &m_trace; }
    int traceCount() const { return m_trace.count(); }
    int intermediateCount() const { return m_trace.intermediateCount(); }
    // 对话区左侧导航导轨的数据源；构造代价 O(n) 且只在 trace 变化时被读。
    QVariantList userMessageIndex() const { return m_trace.userMessageIndex(); }
    // docs/20 §3.1/§3.2：该类型的最后一行（供消息级操作按钮定位末条）。
    int lastIndexOfType(const QString& type) const { return m_trace.lastIndexOfType(type); }
    // docs/21：该类型的第几行（1 起，0=不命中），分叉锚点换算用。
    int rankOfTypeAt(int row, const QString& type) const
    { return m_trace.rankOfTypeAt(row, type); }
    QString finalResponse() const { return m_finalResponse; }
    QString draftText() const { return m_draftText; }
    void setDraftText(const QString& text);
    QVariantList draftAttachments() const { return m_draftAttachments; }
    void setDraftAttachments(const QVariantList& attachments);
    // Private to the application layer; the facade strips credentials
    // before exposing this configuration snapshot to QML.
    QVariantMap modelProfile;
    QString selectedModel;
    QVariant selectedReasoning;
    bool modelSelected = false;
    // docs/20 §3.3：本标签的输入历史环（进程内保留最近 50 条，不持久化）。
    // historyPrev/historyNext 返回对应历史条目；无更多历史时返回空串。
    // 正在编辑（非原始态）时由 QML 侧提前退出，不进入浏览。
    QString historyPrev();
    QString historyNext();
    // 复位浏览游标到最新（用户编辑后再次开始浏览时调用）。
    void resetHistoryCursor();
    QString currentSessionId() const { return m_currentSessionId; }
    QVariantList todoList() const { return m_todoList; }
    // G4: this session's file checkpoints (newest first).
    QVariantList fileCheckpoints() const { return m_fileCheckpoints; }
    // G4: undo all of this session's changes to the file that owns the id.
    bool undoFileCheckpoint(qint64 id);
    // Review: accept the file the id belongs to (no more undo for it).
    bool acceptFileCheckpoint(qint64 id);
    // Review: accept every pending file; returns the count.
    int acceptAllFileCheckpoints();
    // Review: revert every pending file. Returns
    // {reverted, skipped, failed[]} (see AIAgent::undoAllFileCheckpoints).
    QVariantMap undoAllFileCheckpoints();
    QJsonObject fileCheckpointDetail(qint64 id) const;
    // docs/18: 本标签的项目归属（转发自 agent；无 agent 时为空 = 本地）。
    // 归属按标签持有，AgentViewModel 的派生属性与沙箱镜像都从这里读。
    QString activeProjectId() const;
    QString activeProjectRoot() const;
    int usedTokens() const { return m_usedTokens; }
    // 本会话累计消耗（input+output）；agent 尚未挂接时为 0。
    qint64 sessionTokensUsed() const { return m_sessionTokensUsed; }
    int maxTokens() const;

    // ── 提示词预设（会话级系统提示覆盖）────────────────
    // 套用的预设按标签持有；与权限模式同理，全局设置（设置页的
    // agent/systemPromptOverride）只作用于**尚未套用预设**的标签，套用后
    // 预设接管系统提示，清除则回落到当时的全局值。
    QString promptPresetId() const { return m_promptPresetId; }
    QString promptPresetName() const { return m_promptPresetName; }
    void applyPromptPreset(const QString& id, const QString& name, const QString& prompt);
    // 清除预设：系统提示回落到全局值（m_globalSystemPrompt）。
    void clearPromptPreset();
    // 预设被编辑（文本变化）时把新文本推给已套用的标签；删除则不动——
    // 已套用的会话保持原提示（设置页对用户的既有承诺）。
    void syncPromptPreset(const QVariantList& presets);
    // 全局系统提示下发：记下并以它作为"清除预设"的回落值；只在没有套用
    // 预设时才真正写到 agent 上。
    void applyGlobalSystemPrompt(const QString& prompt);

    // 会话级权限模式（0=变更确认 1=自动编辑 2=完全访问）。QML 经
    // AgentViewModel 写入并转发给本 tab 的 agent。内存态，不持久化。
    int permissionMode() const { return m_permissionMode; }
    // 用户显式切换：标记 overridden，此后全局默认变更不再影响本 tab。
    void setPermissionMode(int mode);
    // 全局默认下发：只作用于尚未被用户显式切换过的 tab（含 VM 构造时
    // 尚未注入默认值的首个 tab）。
    void applyDefaultPermissionMode(int mode);

    // Batch 30 (F6): images are compressed data URLs from the viewmodel;
    // files are {name, content} text attachments (folded into the request
    // text by the agent).
    bool startTask(const QString& input, const QStringList& images = {},
                   const QJsonArray& files = {});
    void approveToolCall(const QString& id, bool approved);
    void stop();
    // docs/20 §3.1（FORK-701）：重新生成。busy 时拒绝。
    void regenerate();
    void newChat();
    void loadSession(const QString& sessionId);
    void deleteSession(const QString& sessionId);
    // docs/23 G7: undo a soft-deleted session.
    bool restoreSession(const QString& sessionId);
    void renameSession(const QString& sessionId, const QString& title);
    QVariantList auditLog(int limit) const;
    // docs/16 §8.3: live context composition of this tab's agent (TopBar
    // capacity popup). Empty map when no agent is attached yet.
    Q_INVOKABLE QVariantMap contextComposition() const;

    // docs/16 §8: QML-facing wrappers around IMemory aggregate queries.
    // The viewmodel (AgentVM) forwards these so UsageStatsPage can call
    // AgentVM.tokenStatsDetailed / costStats / quotaStatus directly.
    Q_INVOKABLE QVariantMap tokenStatsDetailed(int recentDays = 30) const;
    Q_INVOKABLE QVariantList tokenStatsBySource(int recentDays = 30) const;
    Q_INVOKABLE QVariantMap costStats(int recentDays = 30) const;
    Q_INVOKABLE QVariantMap quotaStatus() const;

signals:
    void stateChanged();
    void traceChanged();
    void responseReady();
    void draftTextChanged();
    void draftAttachmentsChanged();
    void tokenUsageChanged();
    void sessionTokensChanged();
    void contextLimitChanged();
    void currentSessionIdChanged();
    // Surfaced regardless of which tab is active: the confirm dialog is
    // global and approveToolCall routes back to the waiting controller.
    void toolConfirmRequired(const QString& id, const QString& name,
                             const QVariantMap& args, const QString& preview);
    void errorOccurred(const QString& error);
    // Session data behind the global history list changed.
    void sessionHistoryChanged();
    // docs/16 §6/§7: forwarded from AIAgent so the UI can show per-request
    // cost and quota threshold warnings.
    void costChanged(qint64 costCents);
    void quotaWarning(const QString& quotaType, qint64 used, qint64 quota);
    // docs/31 S4.1 N9: 接近配额上限（未超限）时转发一次。
    void quotaAlert(const QString& quotaType, qint64 used, qint64 quota);
    // Session permission mode changed (AgentViewModel re-emits for the
    // active tab; the sidebar reads it via sessionList for background sessions).
    void permissionModeChanged();
    // 套用的提示词预设变了（套用/清除/预设文本被编辑）。
    void promptPresetChanged();
    void todoListChanged(const QVariantList& todos);
    void fileCheckpointsChanged(const QVariantList& checkpoints);
    void episodicChanged();

private:
    void setState(AgentState state);
    void connectAgent(AIAgent* agent);

    AIAgent* m_agent{nullptr};
    bool m_ownedAgent{false};
    AgentState m_state{AgentState::Idle};
    TraceModel m_trace;
    QString m_finalResponse;
    QString m_draftText;
    QVariantList m_draftAttachments;
    QString m_currentSessionId;
    int m_usedTokens{0};
    qint64 m_sessionTokensUsed{0};   // AIAgent::sessionTokensUsed 镜像
    int m_permissionMode{0};            // PermissionMode as int; 0 = ConfirmChanges
    bool m_permissionModeOverridden{false};
    // 套用的提示词预设（空 id = 未套用，回落全局 systemPromptOverride）。
    // 名字随 id 冗余保存：预设被删除后已套用的会话仍能显示来源名。
    QString m_promptPresetId;
    QString m_promptPresetName;
    // 套用时的提示词正文。留在控制器里，换 agent（重挂载）时才能把同一段
    // 提示重新推给新 agent，而不必回查 Settings。
    QString m_promptPresetPrompt;
    // 设置页里配置的全局系统提示词，由 main.cpp 的 applySystemPrompt 下发。
    QString m_globalSystemPrompt;
    QVariantList m_todoList;
    QVariantList m_fileCheckpoints;


    // docs/20 §3.3：输入历史环。仅内存态（进程内），相邻重复不入队；
    // m_historyCursor 指向浏览位置（size() = 未浏览/最新之外）。
    static constexpr int kMaxHistory = 50;
    QStringList m_inputHistory;
    int m_historyCursor{0};
};

} // namespace NexusAI
