#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTimer>

#include "core/agent/AgentState.h"

namespace NexusAI {

class AIAgent;
class RagService;

// Main ViewModel for the Agent chat interface.
// Bridges C++ core logic with QML UI.
// When a real AIAgent is attached via setAgent(), all actions are delegated
// to it (real LLM pipeline). Otherwise the built-in simulation runs, which
// allows UI demonstration without a configured backend.
class AgentViewModel : public QObject {
    Q_OBJECT

    // ── State ──────────────────────────────────────────
    Q_PROPERTY(int state READ stateAsInt NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged FINAL)

    // ── Trace Model ───────────────────────────────────
    Q_PROPERTY(QVariantList traceModel READ traceModel NOTIFY traceChanged FINAL)

    // ── Response ──────────────────────────────────────
    Q_PROPERTY(QString finalResponse READ finalResponse NOTIFY responseReady FINAL)

    // ── Input ─────────────────────────────────────────
    Q_PROPERTY(QString draftText READ draftText WRITE setDraftText NOTIFY draftTextChanged FINAL)

    // ── Context Panel ─────────────────────────────────
    Q_PROPERTY(bool isPanelVisible READ isPanelVisible WRITE setIsPanelVisible NOTIFY panelVisibleChanged FINAL)
    Q_PROPERTY(double tokenUsageRate READ tokenUsageRate NOTIFY tokenUsageChanged FINAL)
    Q_PROPERTY(int usedTokens READ usedTokens NOTIFY tokenUsageChanged FINAL)
    // Follows the live agent's context limit when attached (needs refresh
    // notifications, hence not CONSTANT).
    Q_PROPERTY(int maxTokens READ maxTokens NOTIFY tokenUsageChanged FINAL)

    // ── Sidebar ───────────────────────────────────────
    Q_PROPERTY(bool isSidebarExpanded READ isSidebarExpanded WRITE setIsSidebarExpanded NOTIFY sidebarExpandedChanged FINAL)
    Q_PROPERTY(QVariantList sessionList READ sessionList NOTIFY sessionListChanged FINAL)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionIdChanged FINAL)

    // ── Tool Registry ─────────────────────────────────
    Q_PROPERTY(QVariantList toolRegistryModel READ toolRegistryModel CONSTANT FINAL)

    // ── Knowledge Base ────────────────────────────────
    Q_PROPERTY(QVariantList knowledgeDocuments READ knowledgeDocuments NOTIFY knowledgeChanged FINAL)

    // ── Pipeline mode ─────────────────────────────────
    Q_PROPERTY(bool liveMode READ liveMode NOTIFY liveModeChanged FINAL)

public:
    explicit AgentViewModel(QObject* parent = nullptr);
    ~AgentViewModel() override;

    // Attach the real agent pipeline. Takes no ownership (agent outlives the VM).
    void setAgent(AIAgent* agent);
    // Attach the knowledge-base service (optional). Takes no ownership.
    void setRagService(RagService* rag);
    bool liveMode() const { return m_agent != nullptr; }

    // State accessors
    int stateAsInt() const { return static_cast<int>(m_state); }
    AgentState state() const { return m_state; }
    QString stateText() const;

    // Trace model
    QVariantList traceModel() const { return m_traceModel; }

    // Response
    QString finalResponse() const { return m_finalResponse; }

    // Draft text
    QString draftText() const { return m_draftText; }
    void setDraftText(const QString& text);

    // Panel
    bool isPanelVisible() const { return m_isPanelVisible; }
    void setIsPanelVisible(bool visible);

    // Sidebar
    bool isSidebarExpanded() const { return m_isSidebarExpanded; }
    void setIsSidebarExpanded(bool expanded);

    // Sessions
    QVariantList sessionList() const { return m_sessionList; }
    QString currentSessionId() const { return m_currentSessionId; }

    // Token management
    double tokenUsageRate() const;
    int usedTokens() const { return m_usedTokens; }
    int maxTokens() const;

    // Tool registry
    QVariantList toolRegistryModel() const { return m_toolRegistryModel; }

    // Knowledge base
    QVariantList knowledgeDocuments() const { return m_knowledgeDocuments; }

    // ── QML-invokable actions ─────────────────────────
    Q_INVOKABLE void startTask(const QString& input);
    Q_INVOKABLE void approveToolCall(const QString& id, bool approved);
    Q_INVOKABLE void stop();                 // abort generation, keep trace
    Q_INVOKABLE void newChat();              // reset to a fresh session
    Q_INVOKABLE void loadSession(const QString& sessionId);
    Q_INVOKABLE void deleteSession(const QString& sessionId);
    Q_INVOKABLE void renameSession(const QString& sessionId, const QString& title);
    Q_INVOKABLE void cancel();               // alias of newChat
    Q_INVOKABLE void togglePanel();
    Q_INVOKABLE void toggleSidebar();
    Q_INVOKABLE void copyToClipboard(const QString& text);

    // Knowledge base management (file import happens on a worker thread;
    // knowledgeChanged notifies on completion)
    Q_INVOKABLE void importDocument(const QString& path);
    Q_INVOKABLE void removeDocument(const QString& name);
    Q_INVOKABLE void clearKnowledge();
    Q_INVOKABLE QVariantList auditLog(int limit = 200);

signals:
    void stateChanged();
    void traceChanged();
    void responseReady();
    void draftTextChanged();
    void panelVisibleChanged();
    void sidebarExpandedChanged();
    void tokenUsageChanged();
    void sessionListChanged();
    void currentSessionIdChanged();
    void knowledgeChanged();
    void liveModeChanged();
    void toolConfirmRequired(const QString& id, const QString& name, const QVariantMap& args);
    void errorOccurred(const QString& error);

private:
    void setState(AgentState state);
    void appendTraceNode(const QString& type, const QString& title,
                         const QString& status, const QString& args = "",
                         const QString& result = "");
    void updateLastNodeStatus(const QString& status);
    void initToolRegistry();
    void startSimulation();
    void simulationStep();
    void connectAgent(AIAgent* agent);
    void refreshSessions();
    void refreshKnowledge();
    bool isBusy() const;

    // ── State ──────────────────────────────────────────
    AIAgent* m_agent{nullptr};
    RagService* m_rag{nullptr};
    AgentState m_state{AgentState::Idle};
    QVariantList m_traceModel;
    QString m_finalResponse;
    QString m_draftText;

    // ── Panel ──────────────────────────────────────────
    bool m_isPanelVisible{true};

    // ── Sidebar ────────────────────────────────────────
    bool m_isSidebarExpanded{true};
    QVariantList m_sessionList;
    QString m_currentSessionId;

    // ── Token ─────────────────────────────────────────
    int m_usedTokens{0};
    int m_maxTokens{8192};

    // ── Tool Registry ──────────────────────────────────
    QVariantList m_toolRegistryModel;

    // ── Knowledge Base ────────────────────────────────
    QVariantList m_knowledgeDocuments;

    // ── Simulation ────────────────────────────────────
    QTimer m_simTimer;
    int m_simStep{0};
    QString m_simInput;
    QString m_pendingToolCallId;
};

} // namespace NexusAI
