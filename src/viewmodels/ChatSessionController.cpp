#include "viewmodels/ChatSessionController.h"

#include "core/agent/AIAgent.h"
#include "core/utils/Activity.h"
#include "core/utils/TokenEstimator.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace NexusAI {

ChatSessionController::ChatSessionController(QObject* parent)
    : QObject(parent) {
    m_simTimer.setSingleShot(true);
    connect(&m_simTimer, &QTimer::timeout, this, &ChatSessionController::simulationStep);
}

ChatSessionController::~ChatSessionController() {
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    // Factory-built agents belong to this controller; the shared template
    // agent (AgentViewModel::setAgent) is owned by the caller.
    if (m_agent && m_ownedAgent)
        m_agent->deleteLater();
}

void ChatSessionController::attachAgent(AIAgent* agent, bool owned) {
    if (m_agent == agent && m_ownedAgent == owned) return;
    // Rewiring mid-task (simulation-mode toggle) terminates the active task
    // cleanly — the running agent would otherwise lose its bridge and stay
    // busy forever.
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    if (m_agent) {
        disconnect(m_agent, nullptr, this, nullptr);
        if (m_ownedAgent)
            m_agent->deleteLater();
    }
    m_agent = agent;
    m_ownedAgent = owned;
    if (m_agent) {
        connectAgent(m_agent);
    } else {
        // Detached: fall back to simulation state.
        setState(AgentState::Idle);
        m_trace.clearNodes();
        emit traceChanged();
        m_currentSessionId.clear();
        emit currentSessionIdChanged();
    }
}

void ChatSessionController::connectAgent(AIAgent* agent) {
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

    connect(agent, &AIAgent::errorOccurred, this, &ChatSessionController::errorOccurred);

    connect(agent, &AIAgent::tokensChanged, this, [this](int usedTokens) {
        m_usedTokens = usedTokens;
        emit tokenUsageChanged();
    });

    connect(agent, &AIAgent::contextLimitChanged, this, [this]() {
        emit contextLimitChanged();
        emit tokenUsageChanged();
    });

    // Session history is global (same SQLite file for every tab); the facade
    // refreshes the shared list whenever any tab reports a change.
    connect(agent, &AIAgent::sessionsChanged, this,
            &ChatSessionController::sessionHistoryChanged);

    connect(agent, &AIAgent::sessionChanged, this, [this](const QString& sessionId) {
        if (m_currentSessionId != sessionId) {
            m_currentSessionId = sessionId;
            emit currentSessionIdChanged();
        }
        emit sessionHistoryChanged();
    });

    // Initial sync with the agent's state.
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
    emit sessionHistoryChanged();
}

QString ChatSessionController::stateText() const {
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

bool ChatSessionController::isBusy() const {
    return m_state == AgentState::Thinking
        || m_state == AgentState::ExecutingTool
        || m_state == AgentState::AwaitingToolConfirm
        || m_state == AgentState::Summarizing;
}

int ChatSessionController::maxTokens() const {
    return m_agent ? m_agent->contextLimit() : 8192;
}

void ChatSessionController::setDraftText(const QString& text) {
    if (m_draftText == text) return;
    m_draftText = text;
    emit draftTextChanged();
}

void ChatSessionController::setState(AgentState state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
}

void ChatSessionController::appendTraceNode(const QString& type, const QString& title,
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

void ChatSessionController::updateLastNodeStatus(const QString& status) {
    m_trace.updateLastNode({{"status", status}});
    emit traceChanged();
}

// ── QML actions ──────────────────────────────────────────

void ChatSessionController::startTask(const QString& input, const QStringList& images,
                                      const QJsonArray& files) {
    if (isBusy()) return;
    Activity::usage(Activity::Kind::User, QStringLiteral("task.start"),
                    input.left(80));

    m_finalResponse.clear();
    emit responseReady();

    if (m_agent) {
        m_agent->startTask(input, images, files);
        return;
    }

    // Simulation fallback.
    m_trace.clearNodes();
    emit traceChanged();
    m_simStep = 0;
    m_simInput = input;
    m_usedTokens = 0;
    emit tokenUsageChanged();

    QJsonObject userNode;
    userNode["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    userNode["type"] = "user_message";
    userNode["title"] = input;
    userNode["status"] = "completed";
    userNode["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    userNode["args"] = QString();
    userNode["result"] = QString();
    if (!images.isEmpty())
        userNode["images"] = QJsonArray::fromStringList(images);
    if (!files.isEmpty())
        userNode["files"] = files;
    m_trace.appendNode(userNode.toVariantMap());
    emit traceChanged();

    m_usedTokens = input.length() / 4;
    emit tokenUsageChanged();
    setState(AgentState::Thinking);
    startSimulation();
}

void ChatSessionController::approveToolCall(const QString& id, bool approved) {
    Activity::usage(Activity::Kind::User,
                    approved ? QStringLiteral("tool.approve")
                             : QStringLiteral("tool.decline"),
                    id);
    if (m_agent) {
        m_agent->approveToolCall(id, approved);
        return;
    }

    // Simulation path.
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

void ChatSessionController::stop() {
    Activity::usage(Activity::Kind::User, QStringLiteral("task.stop"));
    if (m_agent) {
        m_agent->stop();
        return;
    }

    // Simulation path.
    m_simTimer.stop();
    if (m_trace.count() > 0
            && m_trace.nodeAt(m_trace.count() - 1).value("status").toString() == "running")
        updateLastNodeStatus("cancelled");
    setState(AgentState::Idle);
}

void ChatSessionController::newChat() {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.new"));
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

void ChatSessionController::loadSession(const QString& sessionId) {
    if (!m_agent || isBusy()) return;
    Activity::usage(Activity::Kind::User, QStringLiteral("session.resume"),
                    sessionId);
    m_agent->resumeSession(sessionId);
    m_finalResponse = m_agent->lastResponse();
    emit responseReady();
}

void ChatSessionController::deleteSession(const QString& sessionId) {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.delete"),
                    sessionId);
    if (m_agent) m_agent->deleteSession(sessionId);
}

void ChatSessionController::renameSession(const QString& sessionId, const QString& title) {
    Activity::usage(Activity::Kind::User, QStringLiteral("session.rename"),
                    sessionId);
    if (m_agent) m_agent->renameSession(sessionId, title);
}

QVariantList ChatSessionController::auditLog(int limit) const {
    return m_agent ? m_agent->auditLog(limit).toVariantList() : QVariantList{};
}

// ── Simulation (fallback when no agent attached) ─────────

void ChatSessionController::startSimulation() {
    m_simStep = 0;
    m_simTimer.start(1500);
}

void ChatSessionController::simulationStep() {
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
