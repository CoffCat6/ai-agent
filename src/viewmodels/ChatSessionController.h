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

    QTimer m_simTimer;
    int m_simStep{0};
    QString m_simInput;
    QString m_pendingToolCallId;
};

} // namespace NexusAI
