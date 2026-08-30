#include "AIAgent.h"

#include "infrastructure/llm/IAIService.h"
#include "core/tools/ToolRegistry.h"
#include "core/memory/IMemory.h"
#include "core/utils/TokenEstimator.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QUuid>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>

namespace NexusAI {

namespace {

constexpr int kMaxTraceChars = 1500;      // cap for tool args/results shown in UI
constexpr int kMaxToolResultChars = 6000; // cap fed back to the LLM
constexpr int kStreamFlushMs = 100;       // trace flush throttle while streaming

QString truncateText(const QString& text, int maxChars) {
    if (text.size() <= maxChars) return text;
    return text.left(maxChars)
        + QStringLiteral("\n...[truncated %1 chars]").arg(text.size() - maxChars);
}

QString argsToString(const QJsonObject& args) {
    return QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
}

QString systemPrompt() {
    return QStringLiteral(
        "You are NexusAI, a desktop AI assistant running on the user's machine (OS: %1). "
        "You can call tools to read files, write files, list directories and run shell commands. "
        "Use tools whenever they are needed to fulfill the request; never pretend to have run a tool. "
        "File writes and shell commands require user approval - if the user declines, adapt your plan. "
        "When the task is done, reply with a concise final answer without calling more tools. "
        "Always reply in the same language the user writes in."
    ).arg(QSysInfo::productType());
}

} // namespace

AIAgent::AIAgent(QObject* parent) : QObject(parent) {
    m_streamTimer.setSingleShot(true);
    m_streamTimer.setInterval(kStreamFlushMs);
    connect(&m_streamTimer, &QTimer::timeout, this, &AIAgent::flushStreamNode);
}

AIAgent::~AIAgent() = default;

void AIAgent::setLLMService(IAIService* service) {
    if (m_llmService == service) return; // re-wiring the same service would duplicate connections
    if (m_llmService) {
        disconnect(m_llmService, nullptr, this, nullptr);
    }
    m_llmService = service;
    if (m_llmService) {
        connect(m_llmService, &IAIService::responseReady, this, &AIAgent::onLLMResponse);
        connect(m_llmService, &IAIService::errorOccurred, this, &AIAgent::onLLMError);
        connect(m_llmService, &IAIService::streamingChunk, this, &AIAgent::onStreamingChunk);
    }
}

void AIAgent::setToolRegistry(ToolRegistry* registry) {
    m_toolRegistry = registry;
}

void AIAgent::setMemory(IMemory* memory) {
    m_memory = memory;
}

void AIAgent::setContextLimit(int tokens) {
    const int clamped = qBound(1024, tokens, 1048576);
    if (m_contextLimit == clamped) return;
    m_contextLimit = clamped;
    emit contextLimitChanged();
}

// ── Trace helpers ─────────────────────────────────────────

void AIAgent::setState(AgentState state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(m_state);
}

QString AIAgent::appendTraceNode(const QString& type, const QString& title,
                                 const QString& status, const QJsonObject& extra) {
    QJsonObject node;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node["id"] = id;
    node["type"] = type;
    node["title"] = title;
    node["status"] = status;
    node["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();
    node["args"] = extra.value("args").toString();
    node["result"] = extra.value("result").toString();

    m_traceModel.append(node);
    emit traceChanged();
    return id;
}

void AIAgent::updateLastNodeStatus(const QString& status) {
    if (m_traceModel.isEmpty()) return;
    QJsonObject node = m_traceModel.last().toObject();
    node["status"] = status;
    m_traceModel.replace(m_traceModel.size() - 1, node);
    emit traceChanged();
}

void AIAgent::updateLastNodeTitle(const QString& title) {
    if (m_traceModel.isEmpty()) return;
    QJsonObject node = m_traceModel.last().toObject();
    node["title"] = title;
    m_traceModel.replace(m_traceModel.size() - 1, node);
    emit traceChanged();
}

bool AIAgent::updateTraceNodeStatusById(const QString& id, const QString& status) {
    for (int i = 0; i < m_traceModel.size(); ++i) {
        QJsonObject node = m_traceModel.at(i).toObject();
        if (node.value("id").toString() == id) {
            node["status"] = status;
            m_traceModel.replace(i, node);
            emit traceChanged();
            return true;
        }
    }
    return false;
}

void AIAgent::notifyTokens() {
    emit tokensChanged(TokenEstimator::estimateTokens(m_messages));
}

// ── Session management ────────────────────────────────────

QJsonArray AIAgent::sessionList() const {
    return m_memory ? m_memory->listSessions(50) : QJsonArray();
}

QJsonArray AIAgent::auditLog(int limit) const {
    return m_memory ? m_memory->auditLog(limit) : QJsonArray();
}

void AIAgent::deleteSession(const QString& sessionId) {
    if (!m_memory || sessionId.isEmpty()) return;
    m_memory->deleteSession(sessionId);
    if (sessionId == m_sessionId) resetSession();
    emit sessionsChanged();
}

void AIAgent::renameSession(const QString& sessionId, const QString& title) {
    const QString trimmed = title.trimmed();
    if (!m_memory || sessionId.isEmpty() || trimmed.isEmpty()) return;
    m_memory->renameSession(sessionId, trimmed.left(80));
    emit sessionsChanged();
}

void AIAgent::startTask(const QString& userMessage) {
    if (m_active) return;

    const bool fresh = m_sessionId.isEmpty();
    if (fresh) {
        m_messages = QJsonArray();
        m_traceModel = QJsonArray();
        m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = systemPrompt();
        m_messages.append(sysMsg);

        if (m_memory) m_memory->beginSession(m_sessionId, userMessage.left(60));
        emit sessionChanged(m_sessionId);
        emit sessionsChanged();
    }

    m_iteration = 0;
    m_pendingToolCalls.clear();
    m_currentConfirmCall = ToolCallInfo{};
    m_confirmNodeId.clear();
    m_lastResponse.clear();
    clearStreamState();

    ++m_epoch; // invalidate results from any previous task

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userMessage;
    m_messages.append(userMsg);
    if (m_memory) m_memory->saveMessage(m_sessionId, userMsg);

    appendTraceNode("user_message", userMessage, "completed");
    notifyTokens();

    m_active = true;
    processIteration();
}

void AIAgent::resetSession() {
    const bool wasActive = m_active;
    m_active = false;
    ++m_epoch;
    m_pendingToolCalls.clear();
    m_currentConfirmCall = ToolCallInfo{};
    m_confirmNodeId.clear();
    clearStreamState();
    if (m_llmService && wasActive) m_llmService->cancel();

    m_sessionId.clear();
    m_messages = QJsonArray();
    m_traceModel = QJsonArray();
    m_lastResponse.clear();
    m_iteration = 0;

    setState(AgentState::Idle);
    emit traceChanged();
    emit sessionChanged(QString());
}

void AIAgent::resumeSession(const QString& sessionId) {
    if (m_active || sessionId.isEmpty()) return;

    m_sessionId = sessionId;
    m_messages = QJsonArray();

    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt();
    m_messages.append(sysMsg);

    // Persisted history contains clean user/assistant pairs
    if (m_memory) {
        const QJsonArray history = m_memory->loadMessages(sessionId, 200);
        for (const auto& entry : history) {
            const QJsonObject msg = entry.toObject();
            const QString role = msg.value("role").toString();
            if (role != "user" && role != "assistant") continue;
            if (role == "assistant" && msg.value("content").toString().isEmpty()) continue;
            m_messages.append(msg);
        }
    }

    // Rebuild the trace view from history
    m_traceModel = QJsonArray();
    m_lastResponse.clear();
    for (const auto& entry : m_messages) {
        const QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();
        if (role == "user") {
            appendTraceNode("user_message", msg.value("content").toString(), "completed");
        } else if (role == "assistant") {
            const QString content = msg.value("content").toString();
            appendTraceNode("summary", content, "completed");
            m_lastResponse = content;
        }
    }

    m_iteration = 0;
    m_pendingToolCalls.clear();
    m_currentConfirmCall = ToolCallInfo{};
    m_confirmNodeId.clear();
    clearStreamState();

    setState(AgentState::Idle);
    emit traceChanged();
    emit sessionChanged(m_sessionId);
}

// ── Task lifecycle ────────────────────────────────────────

void AIAgent::processIteration() {
    if (!m_active) return;

    if (!m_llmService) {
        failTask("No LLM service configured");
        return;
    }

    if (m_iteration >= m_maxIterations) {
        appendTraceNode("error",
                        QStringLiteral("Reached max iterations (%1)").arg(m_maxIterations),
                        "failed");
        finishTask(QStringLiteral(
            "I could not finish the task within %1 reasoning steps. "
            "Please rephrase the request or continue the conversation.")
            .arg(m_maxIterations));
        return;
    }

    ++m_iteration;
    applyContextBudget();
    m_thinkingNodeId = appendTraceNode("thinking",
                                       QStringLiteral("Reasoning (step %1)").arg(m_iteration),
                                       "running");
    setState(AgentState::Thinking);

    // Build tool definitions for the LLM; dangerous tools are never exposed.
    QJsonArray toolsArray;
    if (m_toolRegistry) {
        for (const auto& def : m_toolRegistry->listAll()) {
            if (def.level == ToolLevel::Dangerous) continue;
            QJsonObject toolObj;
            toolObj["type"] = "function";
            QJsonObject fn;
            fn["name"] = def.name;
            fn["description"] = def.description;
            fn["parameters"] = def.parameters;
            toolObj["function"] = fn;
            toolsArray.append(toolObj);
        }
    }

    m_llmService->chatComplete(m_messages, toolsArray);
}

// Compress the persisted history when it exceeds the context budget, so a
// long session cannot silently blow past the model's context window.
void AIAgent::applyContextBudget() {
    if (m_contextLimit <= 0) return;
    if (TokenEstimator::estimateTokens(m_messages) <= m_contextLimit) return;

    const int before = m_messages.size();
    m_messages = TokenEstimator::compressToFit(m_messages, m_contextLimit);
    if (m_messages.size() == before) return;

    notifyTokens();
    appendTraceNode("context",
        QStringLiteral("History compressed: %1 → %2 messages to fit the %3 token budget")
            .arg(before).arg(m_messages.size()).arg(m_contextLimit),
        "completed");
}

void AIAgent::onLLMResponse(const LLMResponse& response) {
    if (!m_active) return;

    m_streamTimer.stop();
    if (!m_thinkingNodeId.isEmpty()) {
        updateTraceNodeStatusById(m_thinkingNodeId, "completed");
        m_thinkingNodeId.clear();
    }

    if (!response.success) {
        if (m_streamNodeActive) {
            updateLastNodeStatus("failed");
            m_streamNodeActive = false;
        }
        m_streamText.clear();
        failTask(response.error.isEmpty() ? QStringLiteral("LLM request failed") : response.error);
        return;
    }

    // Append the assistant message in OpenAI wire format, including tool_calls.
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    if (!response.content.isEmpty()) {
        assistantMsg["content"] = response.content;
    } else {
        assistantMsg["content"] = QJsonValue(); // null when only tool calls
    }

    QVector<ToolCallInfo> calls;
    if (!response.toolCalls.isEmpty()) {
        QJsonArray callsArr;
        int index = 0;
        for (const auto& call : response.toolCalls) {
            ToolCallInfo normalized = call;
            if (normalized.id.isEmpty())
                normalized.id = QStringLiteral("call_%1_%2").arg(m_iteration).arg(index);
            calls.append(normalized);

            QJsonObject c;
            c["id"] = normalized.id;
            c["type"] = "function";
            QJsonObject fn;
            fn["name"] = normalized.name;
            fn["arguments"] = argsToString(normalized.arguments);
            c["function"] = fn;
            callsArr.append(c);
            ++index;
        }
        assistantMsg["tool_calls"] = callsArr;
    }
    m_messages.append(assistantMsg);
    notifyTokens();

    if (calls.isEmpty()) {
        // Final answer. If a stream node is showing, finalize it in place
        // instead of appending a duplicate summary node.
        const bool streamed = m_streamNodeActive;
        if (streamed) {
            updateLastNodeTitle(response.content);
            updateLastNodeStatus("completed");
            m_streamNodeActive = false;
        }
        m_streamText.clear();
        finishTask(response.content, streamed);
        return;
    }

    // Tool calls follow: the streamed text (if any) is interim assistant text.
    if (m_streamNodeActive) {
        updateLastNodeStatus("completed");
        m_streamNodeActive = false;
    }
    m_streamText.clear();

    m_pendingToolCalls = calls;
    processNextToolCall();
}

void AIAgent::onLLMError(const QString& error) {
    if (!m_active) return;
    failTask(error);
}

void AIAgent::stop() {
    if (!m_active) return;
    m_active = false;
    ++m_epoch;
    m_pendingToolCalls.clear();

    if (!m_confirmNodeId.isEmpty())
        updateTraceNodeStatusById(m_confirmNodeId, "cancelled");
    m_currentConfirmCall = ToolCallInfo{};
    m_confirmNodeId.clear();

    if (m_llmService) m_llmService->cancel();

    if (!m_thinkingNodeId.isEmpty()) {
        updateTraceNodeStatusById(m_thinkingNodeId, "completed");
        m_thinkingNodeId.clear();
    }
    if (m_streamNodeActive) {
        // Keep the partial text that already streamed in
        updateLastNodeStatus("completed");
        m_streamNodeActive = false;
    }
    m_streamTimer.stop();

    setState(AgentState::Idle);
    if (m_memory && !m_sessionId.isEmpty()) m_memory->touchSession(m_sessionId);
}

// ── Streaming display ─────────────────────────────────────

void AIAgent::onStreamingChunk(const QString& chunk) {
    if (!m_active || chunk.isEmpty()) return;

    if (!m_streamNodeActive) {
        appendTraceNode("summary", QString(), "running");
        m_streamNodeActive = true;
        m_streamText.clear();
    }
    m_streamText += chunk;

    // Throttle trace updates to keep the UI smooth
    if (!m_streamTimer.isActive())
        m_streamTimer.start();
}

void AIAgent::flushStreamNode() {
    if (!m_streamNodeActive) return;
    updateLastNodeTitle(m_streamText);
}

void AIAgent::clearStreamState() {
    m_streamTimer.stop();
    m_streamText.clear();
    m_streamNodeActive = false;
    m_thinkingNodeId.clear();
}

// ── Tool execution pipeline ───────────────────────────────

void AIAgent::processNextToolCall() {
    if (!m_active) return;

    while (!m_pendingToolCalls.isEmpty()) {
        const ToolCallInfo call = m_pendingToolCalls.takeFirst();
        ITool* tool = m_toolRegistry ? m_toolRegistry->find(call.name) : nullptr;

        if (!tool) {
            appendTraceNode("tool_call", call.name, "failed",
                            QJsonObject{{"args", argsToString(call.arguments)}});
            appendToolResultMessage(call,
                QStringLiteral("Error: unknown tool '%1'").arg(call.name));
            if (m_memory)
                m_memory->logAudit(m_sessionId, "unknown_tool", call.name,
                                   argsToString(call.arguments), false,
                                   QStringLiteral("unregistered tool"));
            continue;
        }

        const ToolDefinition def = tool->definition();

        if (def.level == ToolLevel::Dangerous) {
            appendTraceNode("tool_call", call.name, "failed",
                            QJsonObject{{"args", argsToString(call.arguments)}});
            appendToolResultMessage(call,
                QStringLiteral("Error: tool '%1' is blocked by sandbox policy").arg(call.name));
            if (m_memory)
                m_memory->logAudit(m_sessionId, "tool_blocked", call.name,
                                   argsToString(call.arguments), false,
                                   QStringLiteral("sandbox policy"));
            continue;
        }

        if (def.level == ToolLevel::RequiresConfirm) {
            // Suspend the loop and ask the user. The confirmation handle is
            // the trace node id, matching what the QML timeline buttons send.
            m_confirmNodeId = appendTraceNode("tool_call", call.name, "waiting_confirm",
                            QJsonObject{{"args", argsToString(call.arguments)}});
            m_currentConfirmCall = call;
            setState(AgentState::AwaitingToolConfirm);
            emit toolConfirmRequired(m_confirmNodeId, call.name, call.arguments);
            return; // resumes in approveToolCall()
        }

        dispatchToolExecution(call, QString());
        return; // resumes in finishToolExecution()
    }

    // All tool calls handled -> feed results back to the LLM.
    processIteration();
}

// ── Tool execution (worker thread) ────────────────────────

void AIAgent::dispatchToolExecution(const ToolCallInfo& call, const QString& traceNodeId) {
    QString nodeId = traceNodeId;
    if (nodeId.isEmpty()) {
        nodeId = appendTraceNode("tool_call", call.name, "running",
                                 QJsonObject{{"args", argsToString(call.arguments)}});
    } else {
        updateTraceNodeStatusById(nodeId, "running");
    }
    setState(AgentState::ExecutingTool);

    ITool* tool = m_toolRegistry ? m_toolRegistry->find(call.name) : nullptr;
    const quint64 epoch = m_epoch;

    auto* watcher = new QFutureWatcher<ToolResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, call, nodeId, epoch]() {
        const ToolResult result = watcher->result();
        watcher->deleteLater();
        if (epoch != m_epoch) {
            // Task was stopped/reset while the tool ran: keep the trace
            // consistent but do not resume the (dead) loop.
            updateTraceNodeStatusById(nodeId, "cancelled");
            return;
        }
        finishToolExecution(call, nodeId, result);
    });

    // Tools are stateless; running them here keeps shell_exec / large file
    // reads from freezing the GUI thread.
    watcher->setFuture(QtConcurrent::run([tool, call]() -> ToolResult {
        return tool ? tool->execute(call.arguments)
                    : ToolResult::fail(QStringLiteral("Tool not found"));
    }));
}

void AIAgent::finishToolExecution(const ToolCallInfo& call, const QString& traceNodeId,
                                  const ToolResult& result) {
    updateTraceNodeStatusById(traceNodeId, result.success ? "success" : "failed");

    const QString output = result.success ? result.output : result.error;
    appendTraceNode("tool_result", call.name,
                    result.success ? "success" : "failed",
                    QJsonObject{{"result", truncateText(output, kMaxTraceChars)}});

    appendToolResultMessage(call, truncateText(
        result.success ? result.output
                       : QStringLiteral("Error: %1").arg(result.error),
        kMaxToolResultChars));

    if (m_memory)
        m_memory->logAudit(m_sessionId, "tool_execute", call.name,
                           argsToString(call.arguments), result.success,
                           truncateText(result.success ? result.output : result.error, 500));

    processNextToolCall();
}

void AIAgent::appendToolResultMessage(const ToolCallInfo& call, const QString& content) {
    QJsonObject toolMsg;
    toolMsg["role"] = "tool";
    toolMsg["tool_call_id"] = call.id;
    toolMsg["content"] = content;
    m_messages.append(toolMsg);
    notifyTokens();
}

void AIAgent::approveToolCall(const QString& id, bool approved) {
    if (!m_active || m_currentConfirmCall.id.isEmpty() || id != m_confirmNodeId) return;

    const ToolCallInfo call = m_currentConfirmCall;
    const QString nodeId = m_confirmNodeId;
    m_currentConfirmCall = ToolCallInfo{};
    m_confirmNodeId.clear();

    if (!approved) {
        updateTraceNodeStatusById(nodeId, "cancelled");
        appendToolResultMessage(call,
            QStringLiteral("User declined execution of tool '%1'. Adapt your plan accordingly.").arg(call.name));
        if (m_memory)
            m_memory->logAudit(m_sessionId, "tool_declined", call.name,
                               argsToString(call.arguments), false,
                               QStringLiteral("user declined execution"));
        processNextToolCall();
        return;
    }

    if (m_memory)
        m_memory->logAudit(m_sessionId, "tool_approved", call.name,
                           argsToString(call.arguments), true,
                           QStringLiteral("user approved execution"));
    dispatchToolExecution(call, nodeId);
}

// ── Completion / failure ──────────────────────────────────

void AIAgent::finishTask(const QString& finalText, bool summaryTraced) {
    m_active = false;
    m_lastResponse = finalText;

    if (!finalText.isEmpty()) {
        if (!summaryTraced) {
            appendTraceNode("summary", finalText, "completed");
        }
        if (m_memory) {
            QJsonObject assistantMsg;
            assistantMsg["role"] = "assistant";
            assistantMsg["content"] = finalText;
            m_memory->saveMessage(m_sessionId, assistantMsg);
        }
    }

    if (m_memory && !m_sessionId.isEmpty()) m_memory->touchSession(m_sessionId);
    emit sessionsChanged();

    setState(AgentState::Summarizing);
    setState(AgentState::Completed);
    emit responseReady(finalText);
}

void AIAgent::failTask(const QString& error) {
    m_active = false;
    appendTraceNode("error", error, "failed");
    setState(AgentState::Error);
    emit errorOccurred(error);
}

} // namespace NexusAI
