#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "core/agent/AgentState.h"
#include "TraceModel.h"

namespace NexusAI {

class AIAgent;

// One open session tab (Batch 29, F7): a dedicated AIAgent instance plus the
// conversation state belonging to it (trace / response / draft / session id /
// token usage / simulation fallback). Parallel tabs each run their own
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
    QString finalResponse() const { return m_finalResponse; }
    QString draftText() const { return m_draftText; }
    void setDraftText(const QString& text);
    QString currentSessionId() const { return m_currentSessionId; }
    int usedTokens() const { return m_usedTokens; }
    int maxTokens() const;

    // 会话级权限模式（0=变更确认 1=自动编辑 2=完全访问）。QML 经
    // AgentViewModel 写入，转发给本 tab 的 agent；镜像成员在 agent 未挂接
    // （模拟模式）期间也保持可读。内存态，不持久化。
    int permissionMode() const { return m_permissionMode; }
    // 用户显式切换：标记 overridden，此后全局默认变更不再影响本 tab。
    void setPermissionMode(int mode);
    // 全局默认下发：只作用于尚未被用户显式切换过的 tab（含 VM 构造时
    // 尚未注入默认值的首个 tab）。
    void applyDefaultPermissionMode(int mode);

    // Batch 30 (F6): images are compressed data URLs from the viewmodel;
    // files are {name, content} text attachments (folded into the request
    // text by the agent).
    void startTask(const QString& input, const QStringList& images = {},
                   const QJsonArray& files = {});
    void approveToolCall(const QString& id, bool approved);
    void stop();
    void newChat();
    void loadSession(const QString& sessionId);
    void deleteSession(const QString& sessionId);
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
    void tokenUsageChanged();
    void contextLimitChanged();
    void currentSessionIdChanged();
    // Surfaced regardless of which tab is active: the confirm dialog is
    // global and approveToolCall routes back to the waiting controller.
    void toolConfirmRequired(const QString& id, const QString& name,
                             const QVariantMap& args);
    void errorOccurred(const QString& error);
    // Session data behind the global history list changed.
    void sessionHistoryChanged();
    // docs/16 §6/§7: forwarded from AIAgent so the UI can show per-request
    // cost and quota threshold warnings.
    void costChanged(qint64 costCents);
    void quotaWarning(const QString& quotaType, qint64 used, qint64 quota);
    // Session permission mode changed (AgentViewModel re-emits for the
    // active tab; SessionTabs reads it via tabModel for background tabs).
    void permissionModeChanged();

private:
    void setState(AgentState state);
    void connectAgent(AIAgent* agent);
    void appendTraceNode(const QString& type, const QString& title,
                         const QString& status, const QString& args = "",
                         const QString& result = "");
    void updateLastNodeStatus(const QString& status);
    void startSimulation();
    void simulationStep();

    AIAgent* m_agent{nullptr};
    bool m_ownedAgent{false};
    AgentState m_state{AgentState::Idle};
    TraceModel m_trace;
    QString m_finalResponse;
    QString m_draftText;
    QString m_currentSessionId;
    int m_usedTokens{0};
    int m_permissionMode{0};            // PermissionMode as int; 0 = ConfirmChanges
    bool m_permissionModeOverridden{false};

    QTimer m_simTimer;
    int m_simStep{0};
    QString m_simInput;
    QString m_pendingToolCallId;
};

} // namespace NexusAI
