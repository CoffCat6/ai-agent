#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTimer>

#include "core/agent/AgentState.h"

namespace NexusAI {

// Main ViewModel for the Agent chat interface.
// Bridges C++ core logic with QML UI.
// Includes simulation mode for MVP demonstration without a real LLM backend.
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
    Q_PROPERTY(int maxTokens READ maxTokens CONSTANT FINAL)

    // ── Sidebar ───────────────────────────────────────
    Q_PROPERTY(bool isSidebarExpanded READ isSidebarExpanded WRITE setIsSidebarExpanded NOTIFY sidebarExpandedChanged FINAL)

    // ── Tool Registry ─────────────────────────────────
    Q_PROPERTY(QVariantList toolRegistryModel READ toolRegistryModel CONSTANT FINAL)

public:
    explicit AgentViewModel(QObject* parent = nullptr);
    ~AgentViewModel() override;

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

    // Token management
    double tokenUsageRate() const;
    int usedTokens() const { return m_usedTokens; }
    int maxTokens() const { return m_maxTokens; }

    // Tool registry
    QVariantList toolRegistryModel() const { return m_toolRegistryModel; }

    // ── QML-invokable actions ─────────────────────────
    Q_INVOKABLE void startTask(const QString& input);
    Q_INVOKABLE void approveToolCall(const QString& id, bool approved);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void togglePanel();
    Q_INVOKABLE void toggleSidebar();

signals:
    void stateChanged();
    void traceChanged();
    void responseReady();
    void draftTextChanged();
    void panelVisibleChanged();
    void sidebarExpandedChanged();
    void tokenUsageChanged();
    void toolConfirmRequired(const QString& id, const QString& name, const QVariantMap& args);

private:
    void setState(AgentState state);
    void appendTraceNode(const QString& type, const QString& title,
                         const QString& status, const QString& args = "",
                         const QString& result = "");
    void updateLastNodeStatus(const QString& status);
    void initToolRegistry();
    void startSimulation();
    void simulationStep();

    // ── State ──────────────────────────────────────────
    AgentState m_state{AgentState::Idle};
    QVariantList m_traceModel;
    QString m_finalResponse;
    QString m_draftText;

    // ── Panel ──────────────────────────────────────────
    bool m_isPanelVisible{true};

    // ── Sidebar ────────────────────────────────────────
    bool m_isSidebarExpanded{true};

    // ── Token ─────────────────────────────────────────
    int m_usedTokens{0};
    int m_maxTokens{8192};

    // ── Tool Registry ──────────────────────────────────
    QVariantList m_toolRegistryModel;

    // ── Simulation ────────────────────────────────────
    QTimer m_simTimer;
    int m_simStep{0};
    QString m_simInput;
    QString m_pendingToolCallId;
};

} // namespace NexusAI
