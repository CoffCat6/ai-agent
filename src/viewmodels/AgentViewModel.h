#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <functional>

#include "core/agent/AgentState.h"
#include "TraceModel.h"

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
    QML_SINGLETON
    QML_NAMED_ELEMENT(AgentVM)

    // ── State ──────────────────────────────────────────
    Q_PROPERTY(int state READ stateAsInt NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged FINAL)
    // Named AgentState values for QML bindings: the core enum is a plain
    // C++ enum class without QML registration, so these constants keep
    // state comparisons in QML free of magic numbers.
    Q_PROPERTY(int stateIdle READ stateIdle CONSTANT FINAL)
    Q_PROPERTY(int stateThinking READ stateThinking CONSTANT FINAL)
    Q_PROPERTY(int stateAwaitingToolConfirm READ stateAwaitingToolConfirm CONSTANT FINAL)
    Q_PROPERTY(int stateExecutingTool READ stateExecutingTool CONSTANT FINAL)
    Q_PROPERTY(int stateSummarizing READ stateSummarizing CONSTANT FINAL)
    Q_PROPERTY(int stateCompleted READ stateCompleted CONSTANT FINAL)
    Q_PROPERTY(int stateError READ stateError CONSTANT FINAL)
    // Thinking / AwaitingToolConfirm / ExecutingTool / Summarizing
    Q_PROPERTY(bool stateBusy READ stateBusy NOTIFY stateChanged FINAL)

    // ── Trace Model ───────────────────────────────────
    // Incremental QAbstractListModel: appended/updated rows are signaled as
    // such, so the QML ListView no longer rebuilds every delegate on each
    // agent update (see TraceModel).
    Q_PROPERTY(QAbstractListModel* traceModel READ traceModel NOTIFY traceChanged FINAL)
    Q_PROPERTY(int traceCount READ traceCount NOTIFY traceChanged FINAL)
    // thinking / tool_call / tool_result / context nodes (fold toggle badge)
    Q_PROPERTY(int intermediateCount READ intermediateCount NOTIFY traceChanged FINAL)

    // ── Response ──────────────────────────────────────
    Q_PROPERTY(QString finalResponse READ finalResponse NOTIFY responseReady FINAL)

    // ── Input ─────────────────────────────────────────
    Q_PROPERTY(QString draftText READ draftText WRITE setDraftText NOTIFY draftTextChanged FINAL)

    // ── Context Panel ─────────────────────────────────
    Q_PROPERTY(bool isPanelVisible READ isPanelVisible WRITE setIsPanelVisible NOTIFY panelVisibleChanged FINAL)
    // User-resizable width of the context panel (persisted)
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth NOTIFY panelWidthChanged FINAL)
    Q_PROPERTY(double tokenUsageRate READ tokenUsageRate NOTIFY tokenUsageChanged FINAL)
    Q_PROPERTY(int usedTokens READ usedTokens NOTIFY tokenUsageChanged FINAL)
    // Follows the live agent's context limit when attached (needs refresh
    // notifications, hence not CONSTANT).
    Q_PROPERTY(int maxTokens READ maxTokens NOTIFY tokenUsageChanged FINAL)

    // ── Sidebar ───────────────────────────────────────
    Q_PROPERTY(bool isSidebarExpanded READ isSidebarExpanded WRITE setIsSidebarExpanded NOTIFY sidebarExpandedChanged FINAL)
    // User-resizable width of the sidebar (persisted)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged FINAL)
    Q_PROPERTY(QVariantList sessionList READ sessionList NOTIFY sessionListChanged FINAL)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionIdChanged FINAL)

    // ── Tool Registry ─────────────────────────────────
    Q_PROPERTY(QVariantList toolRegistryModel READ toolRegistryModel CONSTANT FINAL)

    // ── Knowledge Base ────────────────────────────────
    Q_PROPERTY(QVariantList knowledgeDocuments READ knowledgeDocuments NOTIFY knowledgeChanged FINAL)
    Q_PROPERTY(bool knowledgeBusy READ knowledgeBusy NOTIFY knowledgeBusyChanged FINAL)
    Q_PROPERTY(int knowledgeEmbedDone READ knowledgeEmbedDone NOTIFY knowledgeEmbedProgressChanged)
    Q_PROPERTY(int knowledgeEmbedTotal READ knowledgeEmbedTotal NOTIFY knowledgeEmbedProgressChanged)

    // ── Pipeline mode ─────────────────────────────────
    Q_PROPERTY(bool liveMode READ liveMode NOTIFY liveModeChanged FINAL)

public:
    // No default constructor on purpose: a default-constructible
    // QML_SINGLETON makes the engine prefer Constructor over the create()
    // factory (qqmlprivate's construction-mode check), yielding a second,
    // un-injected instance.
    explicit AgentViewModel(QObject* parent);
    ~AgentViewModel() override;

    // QML singleton plumbing: main.cpp owns the instance (dependency
    // injection happens there); the engine picks the same object up through
    // create() when loading the UI.
    static AgentViewModel* instance();

    // FEAT-803: batch-embedding progress (wired from OllamaEmbedding)
    int knowledgeEmbedDone() const { return m_embedDone; }
    int knowledgeEmbedTotal() const { return m_embedTotal; }
    void setEmbedProgress(int done, int total);
    static AgentViewModel* create(QQmlEngine*, QJSEngine*);

    // Attach the real agent pipeline. Takes no ownership (agent outlives the VM).
    void setAgent(AIAgent* agent);
    // Attach the knowledge-base service (optional). Takes no ownership.
    void setRagService(RagService* rag);
    // Provide persisted knowledge document content (used by re-embedding).
    // Called on the GUI thread; return empty when the document is unknown.
    void setKnowledgeContentProvider(std::function<QString(const QString&)> provider);
    bool liveMode() const { return m_agent != nullptr; }

    // State accessors
    int stateAsInt() const { return static_cast<int>(m_state); }
    AgentState state() const { return m_state; }
    QString stateText() const;

    // Named enum constants for QML (see the Q_PROPERTY block above)
    int stateIdle() const { return static_cast<int>(AgentState::Idle); }
    int stateThinking() const { return static_cast<int>(AgentState::Thinking); }
    int stateAwaitingToolConfirm() const { return static_cast<int>(AgentState::AwaitingToolConfirm); }
    int stateExecutingTool() const { return static_cast<int>(AgentState::ExecutingTool); }
    int stateSummarizing() const { return static_cast<int>(AgentState::Summarizing); }
    int stateCompleted() const { return static_cast<int>(AgentState::Completed); }
    int stateError() const { return static_cast<int>(AgentState::Error); }
    bool stateBusy() const {
        return m_state >= AgentState::Thinking && m_state <= AgentState::Summarizing;
    }

    // Trace model
    QAbstractListModel* traceModel() { return &m_trace; }
    int traceCount() const { return m_trace.count(); }
    int intermediateCount() const { return m_trace.intermediateCount(); }

    // Response
    QString finalResponse() const { return m_finalResponse; }

    // Draft text
    QString draftText() const { return m_draftText; }
    void setDraftText(const QString& text);

    // Panel
    bool isPanelVisible() const { return m_isPanelVisible; }
    void setIsPanelVisible(bool visible);
    int panelWidth() const { return m_panelWidth; }
    void setPanelWidth(int width);

    // Sidebar
    bool isSidebarExpanded() const { return m_isSidebarExpanded; }
    void setIsSidebarExpanded(bool expanded);
    int sidebarWidth() const { return m_sidebarWidth; }
    void setSidebarWidth(int width);

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
    bool knowledgeBusy() const { return m_knowledgeBusy; }

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
    // Chunk metadata of one imported document (document detail dialog)
    Q_INVOKABLE QVariantList documentChunks(const QString& name);
    // Re-run embedding for a degraded (pseudo-mode) document, e.g. after the
    // embedding service came back online.
    Q_INVOKABLE void reembedDocument(const QString& name);
    Q_INVOKABLE QVariantList auditLog(int limit = 200);
    Q_INVOKABLE QString logFilePath() const;   // runtime log file path (for the audit dialog)

signals:
    void stateChanged();
    void traceChanged();
    void responseReady();
    void draftTextChanged();
    void panelVisibleChanged();
    void panelWidthChanged();
    void sidebarExpandedChanged();
    void sidebarWidthChanged();
    void tokenUsageChanged();
    void sessionListChanged();
    void currentSessionIdChanged();
    void knowledgeChanged();
    void knowledgeBusyChanged();
    void knowledgeEmbedProgressChanged();
    void liveModeChanged();
    void toolConfirmRequired(const QString& id, const QString& name, const QVariantMap& args);
    void errorOccurred(const QString& error);
    // Non-error user feedback (import finished, etc.)
    void noticeOccurred(const QString& notice);
    // Emitted after a successful knowledge-base mutation, for persistence
    void documentRemoved(const QString& name);
    void knowledgeCleared();

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
    void setKnowledgeBusy(bool busy);

    // Runs ingestDocument on a worker thread; the outcome (busy flag, signals)
    // is handled back on the GUI thread via a QFutureWatcher.
    void startIngestion(const QString& name, const QString& content, bool reembed);
    void persistPanelWidths();
    bool isBusy() const;

    // ── State ──────────────────────────────────────────
    AIAgent* m_agent{nullptr};
    RagService* m_rag{nullptr};
    std::function<QString(const QString&)> m_contentProvider;
    AgentState m_state{AgentState::Idle};
    TraceModel m_trace;
    QString m_finalResponse;
    QString m_draftText;

    // ── Panel ──────────────────────────────────────────
    bool m_isPanelVisible{true};
    int m_panelWidth{300};

    // ── Sidebar ────────────────────────────────────────
    bool m_isSidebarExpanded{true};
    int m_sidebarWidth{260};
    // Debounces QSettings writes while the user drags a splitter
    QTimer m_uiPersistTimer;
    QVariantList m_sessionList;
    QString m_currentSessionId;

    // ── Token ─────────────────────────────────────────
    int m_usedTokens{0};
    int m_maxTokens{8192};

    // ── Tool Registry ──────────────────────────────────
    QVariantList m_toolRegistryModel;

    // ── Knowledge Base ────────────────────────────────
    QVariantList m_knowledgeDocuments;
    bool m_knowledgeBusy{false};
    int m_embedDone{0};
    int m_embedTotal{0};

    // ── Simulation ────────────────────────────────────
    QTimer m_simTimer;
    int m_simStep{0};
    QString m_simInput;
    QString m_pendingToolCallId;

    static AgentViewModel* s_instance;
};

} // namespace NexusAI
