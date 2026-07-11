#include "AgentViewModel.h"

#include <QDateTime>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace NexusAI {

AgentViewModel::AgentViewModel(QObject* parent)
    : QObject(parent) {
    initToolRegistry();
    m_simTimer.setSingleShot(true);
    connect(&m_simTimer, &QTimer::timeout, this, &AgentViewModel::simulationStep);
}

AgentViewModel::~AgentViewModel() = default;

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
    emit panelVisibleChanged();
}

void AgentViewModel::togglePanel() {
    setIsPanelVisible(!m_isPanelVisible);
}

// ── Sidebar ───────────────────────────────────────────────

void AgentViewModel::setIsSidebarExpanded(bool expanded) {
    if (m_isSidebarExpanded == expanded) return;
    m_isSidebarExpanded = expanded;
    emit sidebarExpandedChanged();
}

void AgentViewModel::toggleSidebar() {
    setIsSidebarExpanded(!m_isSidebarExpanded);
}

// ── Token ────────────────────────────────────────────────

double AgentViewModel::tokenUsageRate() const {
    if (m_maxTokens == 0) return 0.0;
    return static_cast<double>(m_usedTokens) / static_cast<double>(m_maxTokens);
}

// ── Tool Registry Init ────────────────────────────────────

void AgentViewModel::initToolRegistry() {
    // Safe tools (green)
    {
        QVariantMap tool;
        tool["name"] = "read_file";
        tool["level"] = "safe";
        tool["description"] = "Read file contents";
        m_toolRegistryModel.append(tool);
    }
    {
        QVariantMap tool;
        tool["name"] = "web_search";
        tool["level"] = "safe";
        tool["description"] = "Search the web";
        m_toolRegistryModel.append(tool);
    }
    // Tools requiring confirmation (yellow)
    {
        QVariantMap tool;
        tool["name"] = "write_file";
        tool["level"] = "requires_confirm";
        tool["description"] = "Write to file";
        m_toolRegistryModel.append(tool);
    }
    {
        QVariantMap tool;
        tool["name"] = "shell_exec";
        tool["level"] = "requires_confirm";
        tool["description"] = "Execute shell command";
        m_toolRegistryModel.append(tool);
    }
    // Dangerous tools (red - blocked by sandbox)
    {
        QVariantMap tool;
        tool["name"] = "rm_rf";
        tool["level"] = "dangerous";
        tool["description"] = "Delete files recursively";
        m_toolRegistryModel.append(tool);
    }
}

// ── Trace Node Helpers ───────────────────────────────────

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

    m_traceModel.append(node);
    emit traceChanged();
}

void AgentViewModel::updateLastNodeStatus(const QString& status) {
    if (m_traceModel.isEmpty()) return;
    QVariantMap lastNode = m_traceModel.last().toMap();
    lastNode["status"] = status;
    m_traceModel.replace(m_traceModel.size() - 1, lastNode);
    emit traceChanged();
}

// ── QML Actions ───────────────────────────────────────────

void AgentViewModel::startTask(const QString& input) {
    if (m_state == AgentState::Thinking ||
        m_state == AgentState::ExecutingTool ||
        m_state == AgentState::AwaitingToolConfirm) {
        return; // Already running
    }

    // Reset
    m_traceModel.clear();
    m_finalResponse.clear();
    m_simStep = 0;
    m_simInput = input;
    m_usedTokens = 0;

    // Add user message to trace
    appendTraceNode("user_message", input, "completed");

    // Update token usage estimate
    m_usedTokens = input.length() / 4;
    emit tokenUsageChanged();

    setState(AgentState::Thinking);
    startSimulation();
}

void AgentViewModel::startSimulation() {
    m_simStep = 0;
    m_simTimer.start(1500);
}

void AgentViewModel::simulationStep() {
    switch (m_simStep) {
    case 0:
        // Thinking: analyzing request
        appendTraceNode("thinking", "Analyzing request...", "running");
        m_simTimer.start(2000);
        break;

    case 1:
        // Thinking complete, planning
        updateLastNodeStatus("completed");
        appendTraceNode("thinking", "Planning tool calls...", "running");
        m_simTimer.start(1500);
        break;

    case 2:
        // Plan complete, first tool call (read_file - safe)
        updateLastNodeStatus("completed");
        appendTraceNode("tool_call", "read_file", "running",
            QString::fromUtf8(QJsonDocument(QJsonObject{
                {"path", "/home/user/document.txt"}
            }).toJson(QJsonDocument::Compact)));
        m_simTimer.start(1200);
        break;

    case 3:
        // Tool call complete, tool result
        updateLastNodeStatus("success");
        appendTraceNode("tool_result", "File contents loaded", "success",
            "", "Lorem ipsum dolor sit amet, consectetur adipiscing elit...");
        m_usedTokens += 250;
        emit tokenUsageChanged();
        m_simTimer.start(1500);
        break;

    case 4:
        // Second tool call (write_file - requires confirmation)
        appendTraceNode("tool_call", "write_file", "waiting_confirm",
            QString::fromUtf8(QJsonDocument(QJsonObject{
                {"path", "/home/user/output.txt"},
                {"content", "Processed output data..."}
            }).toJson(QJsonDocument::Compact)));

        // Extract the node ID for the pending confirmation
        if (!m_traceModel.isEmpty()) {
            QVariantMap lastNode = m_traceModel.last().toMap();
            m_pendingToolCallId = lastNode["id"].toString();

            QVariantMap args;
            args["path"] = "/home/user/output.txt";
            args["content"] = "Processed output data...";
            emit toolConfirmRequired(m_pendingToolCallId, "write_file", args);
        }

        setState(AgentState::AwaitingToolConfirm);
        // Don't start timer - wait for user approval
        break;

    case 5:
        // Synthesizing response
        appendTraceNode("thinking", "Synthesizing response...", "running");
        m_simTimer.start(2000);
        break;

    case 6:
        // Final response
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

void AgentViewModel::approveToolCall(const QString& id, bool approved) {
    // Find the tool call node in trace
    for (int i = 0; i < m_traceModel.size(); ++i) {
        QVariantMap node = m_traceModel.at(i).toMap();
        if (node["id"].toString() == id) {
            if (approved) {
                // Mark as running, then success
                node["status"] = "running";
                m_traceModel.replace(i, node);
                emit traceChanged();

                // Simulate tool execution
                QTimer::singleShot(1000, this, [this, i]() {
                    if (i < m_traceModel.size()) {
                        QVariantMap node = m_traceModel.at(i).toMap();
                        node["status"] = "success";
                        m_traceModel.replace(i, node);
                        emit traceChanged();
                    }

                    // Continue simulation
                    appendTraceNode("tool_result", "File written successfully", "success",
                        "", "Written 256 bytes to /home/user/output.txt");
                    m_usedTokens += 100;
                    emit tokenUsageChanged();

                    setState(AgentState::Thinking);
                    m_simTimer.start(1500);
                });
            } else {
                node["status"] = "cancelled";
                m_traceModel.replace(i, node);
                emit traceChanged();

                // Skip to final response
                m_finalResponse = "Tool call was rejected by the user. "
                                  "I cannot complete the task without writing the output file.";
                appendTraceNode("summary", m_finalResponse, "completed");
                setState(AgentState::Completed);
                emit responseReady();
            }
            break;
        }
    }
}

void AgentViewModel::cancel() {
    m_simTimer.stop();
    setState(AgentState::Idle);
    m_traceModel.clear();
    emit traceChanged();
}

} // namespace NexusAI
