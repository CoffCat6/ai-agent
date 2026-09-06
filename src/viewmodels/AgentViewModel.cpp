#include "AgentViewModel.h"

#include "core/agent/AIAgent.h"
#include "core/tools/ToolRegistry.h"
#include "core/rag/RagService.h"
#include "core/utils/Logger.h"
#include "core/utils/TokenEstimator.h"

#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QUrl>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace NexusAI {

AgentViewModel* AgentViewModel::s_instance = nullptr;

AgentViewModel* AgentViewModel::instance() {
    if (!s_instance) s_instance = new AgentViewModel(nullptr);
    return s_instance;
}

AgentViewModel* AgentViewModel::create(QQmlEngine*, QJSEngine*) {
    // main.cpp keeps its own reference and wires dependencies; the engine
    // must not garbage-collect the singleton behind its back.
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

AgentViewModel::AgentViewModel(QObject* parent)
    : QObject(parent) {
    initToolRegistry();
    m_simTimer.setSingleShot(true);
    connect(&m_simTimer, &QTimer::timeout, this, &AgentViewModel::simulationStep);

    // Restore last UI layout (sidebar / context panel visibility + widths)
    const QSettings uiSettings("NexusAI", "NexusAI");
    m_isPanelVisible = uiSettings.value("ui/panelVisible", true).toBool();
    m_isSidebarExpanded = uiSettings.value("ui/sidebarExpanded", true).toBool();
    m_panelWidth = uiSettings.value("ui/panelWidth", m_panelWidth).toInt();
    m_sidebarWidth = uiSettings.value("ui/sidebarWidth", m_sidebarWidth).toInt();

    // Splitter drags update the width per mouse move; coalesce the writes
    m_uiPersistTimer.setSingleShot(true);
    m_uiPersistTimer.setInterval(400);
    connect(&m_uiPersistTimer, &QTimer::timeout, this,
            &AgentViewModel::persistPanelWidths);
}

AgentViewModel::~AgentViewModel() {
    if (m_uiPersistTimer.isActive()) {
        m_uiPersistTimer.stop();
        persistPanelWidths();
    }
}

void AgentViewModel::persistPanelWidths() {
    QSettings("NexusAI", "NexusAI").setValue("ui/sidebarWidth", m_sidebarWidth);
    QSettings("NexusAI", "NexusAI").setValue("ui/panelWidth", m_panelWidth);
}

// ── Real agent pipeline ──────────────────────────────────

void AgentViewModel::setAgent(AIAgent* agent) {
    if (m_agent == agent) return;
    // Detaching or rewiring mid-task (simulation-mode toggle) terminates the
    // active task cleanly - otherwise the running agent loses its UI bridge
    // and stays busy until restart.
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    if (m_agent) {
        disconnect(m_agent, nullptr, this, nullptr);
    }
    m_agent = agent;
    if (m_agent) {
        connectAgent(m_agent);
    } else {
        // Detached: fall back to simulation state
        setState(AgentState::Idle);
        m_trace.clearNodes();
        emit traceChanged();
        m_sessionList.clear();
        emit sessionListChanged();
        m_currentSessionId.clear();
        emit currentSessionIdChanged();
    }
    emit liveModeChanged();
}

void AgentViewModel::connectAgent(AIAgent* agent) {
    connect(agent, &AIAgent::stateChanged, this, [this](AgentState state) {
        setState(state);
    });

    connect(agent, &AIAgent::traceChanged, this, [this, agent]() {
        m_trace.applyDelta(agent->traceModel().toVariantList());
        emit traceChanged();
    });

    connect(agent, &AIAgent::responseReady, this, [this, agent](const QString& response) {
        m_finalResponse = response;
        emit responseReady();
    });

    connect(agent, &AIAgent::toolConfirmRequired, this,
            [this](const QString& id, const QString& name, const QJsonObject& args) {
        emit toolConfirmRequired(id, name, args.toVariantMap());
    });

    connect(agent, &AIAgent::errorOccurred, this, &AgentViewModel::errorOccurred);

    connect(agent, &AIAgent::tokensChanged, this, [this](int usedTokens) {
        m_usedTokens = usedTokens;
        emit tokenUsageChanged();
    });

    connect(agent, &AIAgent::contextLimitChanged, this, [this]() {
        emit tokenUsageChanged();
    });

    connect(agent, &AIAgent::sessionsChanged, this, [this]() {
        refreshSessions();
    });

    connect(agent, &AIAgent::sessionChanged, this, [this](const QString& sessionId) {
        if (m_currentSessionId != sessionId) {
            m_currentSessionId = sessionId;
            emit currentSessionIdChanged();
        }
        refreshSessions();
    });

    // Initial sync with the agent's state
    m_state = agent->state();
    emit stateChanged();
    m_trace.applyDelta(agent->traceModel().toVariantList());
    emit traceChanged();
    m_finalResponse = agent->lastResponse();
    emit responseReady();
    m_usedTokens = TokenEstimator::estimateTokens(agent->messages());
    emit tokenUsageChanged();
    m_currentSessionId = agent->currentSessionId();
    emit currentSessionIdChanged();
    refreshSessions();
}

void AgentViewModel::refreshSessions() {
    const QVariantList sessions = m_agent
        ? m_agent->sessionList().toVariantList()
        : QVariantList{};
    if (m_sessionList == sessions) return;
    m_sessionList = sessions;
    emit sessionListChanged();
}

// ── Knowledge base (RAG) ─────────────────────────────────

void AgentViewModel::setRagService(RagService* rag) {
    if (m_rag == rag) return;
    if (m_rag) disconnect(m_rag, nullptr, this, nullptr);
    m_rag = rag;
    if (m_rag) {
        // changed() may be emitted from worker threads; the queued call to
        // refreshKnowledge() updates the property BEFORE the QML notify.
        // (Forwarding the signal straight to knowledgeChanged would make QML
        // re-read a stale list - documents imported fine but never showed up.)
        connect(m_rag, &RagService::changed, this, &AgentViewModel::refreshKnowledge);
    }
    refreshKnowledge();
}

void AgentViewModel::setKnowledgeContentProvider(std::function<QString(const QString&)> provider) {
    m_contentProvider = std::move(provider);
}

void AgentViewModel::refreshKnowledge() {
    const QVariantList docs = m_rag
        ? m_rag->documents().toVariantList() : QVariantList{};
    if (m_knowledgeDocuments == docs) return;
    m_knowledgeDocuments = docs;
    emit knowledgeChanged();
}

void AgentViewModel::importDocument(const QString& path) {
    if (!m_rag || m_knowledgeBusy) return;
    const QString cleanPath = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QFileInfo info(cleanPath);
    if (!info.isFile()) {
        emit errorOccurred(QStringLiteral("无法读取文件：%1").arg(cleanPath));
        return;
    }
    QFile file(cleanPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(QStringLiteral("无法打开文件：%1").arg(file.errorString()));
        return;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    const QString name = info.fileName();
    startIngestion(name, content, false);
}

void AgentViewModel::removeDocument(const QString& name) {
    if (m_rag && m_rag->removeDocument(name))
        emit documentRemoved(name);
}

void AgentViewModel::clearKnowledge() {
    if (!m_rag) return;
    m_rag->clear();
    emit knowledgeCleared();
}

QVariantList AgentViewModel::documentChunks(const QString& name) {
    return m_rag ? m_rag->documentChunks(name).toVariantList() : QVariantList{};
}

void AgentViewModel::reembedDocument(const QString& name) {
    if (!m_rag || m_knowledgeBusy) return;
    const QString content = m_contentProvider ? m_contentProvider(name) : QString();
    if (content.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("未找到文档内容，无法重新嵌入：%1").arg(name));
        return;
    }

    startIngestion(name, content, true);
}

void AgentViewModel::startIngestion(const QString& name, const QString& content, bool reembed) {
    setKnowledgeBusy(true);
    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, name, content, reembed]() {
        const int chunks = watcher->result();
        watcher->deleteLater();
        setKnowledgeBusy(false);
        if (reembed) {
            if (chunks > 0)
                emit noticeOccurred(QStringLiteral("已重新嵌入 %1（%2 段）").arg(name).arg(chunks));
            else
                emit errorOccurred(QStringLiteral("重新嵌入失败：%1").arg(name));
            return;
        }
        if (chunks > 0) {
            emit noticeOccurred(QStringLiteral("已导入 %1（%2 段）").arg(name).arg(chunks));
            emit documentImported(name, content);
        } else if (chunks == 0) {
            emit errorOccurred(QStringLiteral("导入失败（文件为空或无法分块）：%1").arg(name));
        } else {
            emit errorOccurred(QStringLiteral("文件过大（超过单文档 %1 段上限）：%2")
                .arg(RagService::MaxChunksPerDocument).arg(name));
        }
    });

    // Chunking + batch embedding can take a while (single network request);
    // run off the GUI thread. The worker lambda captures only values - never
    // `this` - so tearing the view model down mid-import cannot dangle; the
    // watcher (parented to this) delivers the outcome on the GUI thread,
    // where touching the busy flag and emitting signals is safe.
    RagService* rag = m_rag;
    watcher->setFuture(QtConcurrent::run([rag, name, content]() -> int {
        return rag ? rag->ingestDocument(name, content) : 0;
    }));
}

void AgentViewModel::setKnowledgeBusy(bool busy) {
    if (m_knowledgeBusy == busy) return;
    m_knowledgeBusy = busy;
    emit knowledgeBusyChanged();
}

// ── Session / audit management ───────────────────────────

void AgentViewModel::deleteSession(const QString& sessionId) {
    if (m_agent) m_agent->deleteSession(sessionId);
}

void AgentViewModel::renameSession(const QString& sessionId, const QString& title) {
    if (m_agent) m_agent->renameSession(sessionId, title);
}

QVariantList AgentViewModel::auditLog(int limit) {
    return m_agent ? m_agent->auditLog(limit).toVariantList() : QVariantList{};
}

QString AgentViewModel::logFilePath() const {
    return Logger::logFilePath();
}

// ── State Management ─────────────────────────────────────

void AgentViewModel::setState(AgentState state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
}

QString AgentViewModel::stateText() const {
    switch (m_state) {
    case AgentState::Idle:               return "Ready";
    case AgentState::Thinking:           return "Thinking...";
    case AgentState::AwaitingToolConfirm:return "Awaiting Confirmation";
    case AgentState::ExecutingTool:      return "Executing Tool";
    case AgentState::Summarizing:        return "Summarizing";
    case AgentState::Completed:          return "Completed";
    case AgentState::Error:              return "Error";
    }
    return "Unknown";
}

bool AgentViewModel::isBusy() const {
    return m_state == AgentState::Thinking
        || m_state == AgentState::ExecutingTool
        || m_state == AgentState::AwaitingToolConfirm
        || m_state == AgentState::Summarizing;
}

// ── Draft Text ───────────────────────────────────────────

void AgentViewModel::setDraftText(const QString& text) {
    if (m_draftText == text) return;
    m_draftText = text;
    emit draftTextChanged();
}

// ── Panel ────────────────────────────────────────────────

void AgentViewModel::setIsPanelVisible(bool visible) {
    if (m_isPanelVisible == visible) return;
    m_isPanelVisible = visible;
    QSettings("NexusAI", "NexusAI").setValue("ui/panelVisible", visible);
    emit panelVisibleChanged();
}

void AgentViewModel::setPanelWidth(int width) {
    // Generous clamp; the QML drag handler applies the window-aware limit
    const int clamped = qBound(220, width, 640);
    if (m_panelWidth == clamped) return;
    m_panelWidth = clamped;
    m_uiPersistTimer.start();
    emit panelWidthChanged();
}

void AgentViewModel::togglePanel() {
    setIsPanelVisible(!m_isPanelVisible);
}

// ── Sidebar ───────────────────────────────────────────────

void AgentViewModel::setIsSidebarExpanded(bool expanded) {
    if (m_isSidebarExpanded == expanded) return;
    m_isSidebarExpanded = expanded;
    QSettings("NexusAI", "NexusAI").setValue("ui/sidebarExpanded", expanded);
    emit sidebarExpandedChanged();
}

void AgentViewModel::setSidebarWidth(int width) {
    const int clamped = qBound(180, width, 560);
    if (m_sidebarWidth == clamped) return;
    m_sidebarWidth = clamped;
    m_uiPersistTimer.start();
    emit sidebarWidthChanged();
}

void AgentViewModel::toggleSidebar() {
    setIsSidebarExpanded(!m_isSidebarExpanded);
}

// ── Token ────────────────────────────────────────────────

double AgentViewModel::tokenUsageRate() const {
    if (m_maxTokens == 0) return 0.0;
    return static_cast<double>(m_usedTokens) / static_cast<double>(maxTokens());
}

int AgentViewModel::maxTokens() const {
    return m_agent ? m_agent->contextLimit() : m_maxTokens;
}

// ── Tool Registry Init ────────────────────────────────────

void AgentViewModel::initToolRegistry() {
    const auto defs = ToolRegistry::instance().listAll();
    for (const auto& def : defs) {
        QVariantMap tool;
        tool["name"] = def.name;
        tool["description"] = def.description;
        tool["level"] = def.level == ToolLevel::Safe ? "safe"
                      : def.level == ToolLevel::RequiresConfirm ? "requires_confirm"
                      : "dangerous";
        m_toolRegistryModel.append(tool);
    }
}

// ── Trace Node Helpers (simulation path) ─────────────────

void AgentViewModel::appendTraceNode(const QString& type, const QString& title,
                                       const QString& status,
                                       const QString& args, const QString& result) {
    QVariantMap node;
    node["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node["type"] = type;
    node["title"] = title;
    node["status"] = status;
    node["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();
    node["args"] = args;
    node["result"] = result;

    m_trace.appendNode(node);
    emit traceChanged();
}

void AgentViewModel::updateLastNodeStatus(const QString& status) {
    m_trace.updateLastNode({{"status", status}});
    emit traceChanged();
}

// ── QML Actions ───────────────────────────────────────────

void AgentViewModel::startTask(const QString& input) {
    if (isBusy()) return;

    m_finalResponse.clear();
    emit responseReady();

    if (m_agent) {
        // Real LLM pipeline (agent rebuilds the trace itself)
        m_agent->startTask(input);
        return;
    }

    // Simulation fallback
    m_trace.clearNodes();
    emit traceChanged();
    m_simStep = 0;
    m_simInput = input;
    m_usedTokens = 0;
    emit tokenUsageChanged();
    appendTraceNode("user_message", input, "completed");
    m_usedTokens = input.length() / 4;
    emit tokenUsageChanged();
    setState(AgentState::Thinking);
    startSimulation();
}

void AgentViewModel::approveToolCall(const QString& id, bool approved) {
    if (m_agent) {
        m_agent->approveToolCall(id, approved);
        return;
    }

    // Simulation path
    const int row = m_trace.rowOfId(id);
    if (row < 0) return;

    if (approved) {
        m_trace.updateNode(row, {{"status", "running"}});
        emit traceChanged();

        QTimer::singleShot(1000, this, [this, row]() {
            if (row < m_trace.count()) {
                m_trace.updateNode(row, {{"status", "success"}});
                emit traceChanged();
            }

            appendTraceNode("tool_result", "File written successfully", "success",
                "", "Written 256 bytes to /home/user/output.txt");
            m_usedTokens += 100;
            emit tokenUsageChanged();

            setState(AgentState::Thinking);
            m_simTimer.start(1500);
        });
    } else {
        m_trace.updateNode(row, {{"status", "cancelled"}});
        emit traceChanged();

        m_finalResponse = "Tool call was rejected by the user. "
                          "I cannot complete the task without writing the output file.";
        appendTraceNode("summary", m_finalResponse, "completed");
        setState(AgentState::Completed);
        emit responseReady();
    }
}

void AgentViewModel::stop() {
    if (m_agent) {
        m_agent->stop();
        return;
    }

    // Simulation path
    m_simTimer.stop();
    if (m_trace.count() > 0
            && m_trace.nodeAt(m_trace.count() - 1).value("status").toString() == "running")
        updateLastNodeStatus("cancelled");
    setState(AgentState::Idle);
}

void AgentViewModel::newChat() {
    m_simTimer.stop();
    m_finalResponse.clear();
    emit responseReady();

    if (m_agent) {
        m_agent->resetSession();
        return;
    }

    setState(AgentState::Idle);
    m_trace.clearNodes();
    emit traceChanged();
    m_currentSessionId.clear();
    emit currentSessionIdChanged();
}

void AgentViewModel::loadSession(const QString& sessionId) {
    if (!m_agent || isBusy()) return;
    m_agent->resumeSession(sessionId);
    m_finalResponse = m_agent->lastResponse();
    emit responseReady();
}

void AgentViewModel::cancel() {
    newChat();
}

void AgentViewModel::copyToClipboard(const QString& text) {
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

// ── Simulation (fallback when no agent attached) ─────────

void AgentViewModel::startSimulation() {
    m_simStep = 0;
    m_simTimer.start(1500);
}

void AgentViewModel::simulationStep() {
    switch (m_simStep) {
    case 0:
        appendTraceNode("thinking", "Analyzing request...", "running");
        m_simTimer.start(2000);
        break;

    case 1:
        updateLastNodeStatus("completed");
        appendTraceNode("thinking", "Planning tool calls...", "running");
        m_simTimer.start(1500);
        break;

    case 2:
        updateLastNodeStatus("completed");
        appendTraceNode("tool_call", "read_file", "running",
            QString::fromUtf8(QJsonDocument(QJsonObject{
                {"path", "/home/user/document.txt"}
            }).toJson(QJsonDocument::Compact)));
        m_simTimer.start(1200);
        break;

    case 3:
        updateLastNodeStatus("success");
        appendTraceNode("tool_result", "File contents loaded", "success",
            "", "Lorem ipsum dolor sit amet, consectetur adipiscing elit...");
        m_usedTokens += 250;
        emit tokenUsageChanged();
        m_simTimer.start(1500);
        break;

    case 4:
        appendTraceNode("tool_call", "write_file", "waiting_confirm",
            QString::fromUtf8(QJsonDocument(QJsonObject{
                {"path", "/home/user/output.txt"},
                {"content", "Processed output data..."}
            }).toJson(QJsonDocument::Compact)));

        if (m_trace.count() > 0) {
            m_pendingToolCallId =
                m_trace.nodeAt(m_trace.count() - 1).value("id").toString();

            QVariantMap args;
            args["path"] = "/home/user/output.txt";
            args["content"] = "Processed output data...";
            emit toolConfirmRequired(m_pendingToolCallId, "write_file", args);
        }

        setState(AgentState::AwaitingToolConfirm);
        break;

    case 5:
        appendTraceNode("thinking", "Synthesizing response...", "running");
        m_simTimer.start(2000);
        break;

    case 6:
        updateLastNodeStatus("completed");
        m_finalResponse = QString("I've analyzed your request: \"%1\"\n\n"
            "I read the file `/home/user/document.txt` and successfully wrote "
            "the processed output to `/home/user/output.txt`.\n\n"
            "Here's a summary of what I did:\n"
            "1. Read the input document\n"
            "2. Processed the content\n"
            "3. Saved the results to the output file\n\n"
            "The task is now complete.").arg(m_simInput);

        appendTraceNode("summary", m_finalResponse, "completed");
        m_usedTokens += 500;
        emit tokenUsageChanged();
        setState(AgentState::Completed);
        emit responseReady();
        break;
    }

    m_simStep++;
}

} // namespace NexusAI
