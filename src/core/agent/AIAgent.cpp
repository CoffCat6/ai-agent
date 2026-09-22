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
// An interim assistant answer this long counts as the final result when the
// model keeps re-issuing an identical tool call instead of stopping.
constexpr int kMinFinalAnswerChars = 40;
// Completed tool rounds kept verbatim in the history; older rounds are
// collapsed to a note. A long tail of tool_call/tool messages reads like an
// "always call tools" pattern and makes weak models fire the tool on every
// new message, greetings included.
constexpr int kKeepToolRounds = 2;

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
        "You are NexusAI, a desktop AI assistant running on the user's machine (OS: %1).\n"
        "\n"
        "Tool policy - choose the RIGHT tool, then stop:\n"
        "- Questions about the user's imported documents or knowledge base (keywords "
        "like knowledge base/docs/imported/according to...): call search_knowledge_base "
        "and answer from its passages. NEVER answer those by reading files from disk - "
        "the knowledge base lives inside the app, not as files on disk.\n"
        "- General knowledge, coding questions, small talk: answer directly WITHOUT "
        "any tool call.\n"
        "- Fetching a web page or any live online content: call http_fetch with the "
        "absolute URL. This is the only supported path for HTTP requests.\n"
        "- read_file / list_dir / shell_exec: only for tasks that explicitly target "
        "this local workspace. Do NOT use shell_exec to fetch web pages (python/"
        "curl/wget/Invoke-WebRequest) - nested quotes break on Windows; use "
        "http_fetch instead.\n"
        "\n"
        "Step budget: you have a HARD limit of %2 tool-use steps per request. One "
        "search usually suffices. When the collected information answers the question, "
        "stop calling tools and reply with the final answer immediately - do not keep "
        "exploring to be thorough.\n"
        "File writes and shell commands require user approval - if the user declines, "
        "adapt your plan. Never pretend to have run a tool.\n"
        "Base every tool call on the CURRENT user message and write fresh arguments "
        "for it; never repeat a previous call verbatim unless the user is asking the "
        "same question again.\n"
        "Judge each message on its own: if the current message is a greeting, small "
        "talk, or needs no fresh information, reply directly - earlier tool usage in "
        "this conversation does not make a tool call appropriate now.\n"
        "Always reply in the same language the user writes in."
    ).arg(QSysInfo::productType()).arg(AIAgent::kMaxIterations);
}

} // namespace

AIAgent::AIAgent(QObject* parent) : QObject(parent) {
    m_streamTimer.setSingleShot(true);
    m_streamTimer.setInterval(kStreamFlushMs);
    connect(&m_streamTimer, &QTimer::timeout, this, &AIAgent::flushStreamNode);

    // REL-1001: hard wall-clock budget per task
    m_taskTimeoutTimer.setSingleShot(true);
    connect(&m_taskTimeoutTimer, &QTimer::timeout, this, &AIAgent::onTaskTimeout);
}

void AIAgent::setTaskTimeoutMs(int ms) {
    m_taskTimeoutMs = qMax(0, ms);
}

// Fired while m_active: seal whatever is in flight (pending / awaiting
// confirmation / executing) and converge into a final response so the UI
// never waits forever on a wedged provider or tool.
void AIAgent::onTaskTimeout() {
    if (!m_active) return;
    qWarning() << "Task timed out after" << m_taskTimeoutMs << "ms - sealing";
    stop();   // seals pending/confirm/executing calls and cancels the service
    finishTask(QStringLiteral(
        "The task timed out and was stopped. The work completed so far is "
        "summarized above - ask a follow-up to continue."));
}

AIAgent::~AIAgent() = default;

void AIAgent::setLLMService(IAIService* service) {
    if (m_llmService == service) return; // re-wiring the same service would duplicate connections
    // Switching providers mid-task (settings profile change) terminates the
    // active task cleanly - otherwise the in-flight response loses its
    // receiver and the agent is stuck until restart.
    if (m_active) stop();
    if (m_llmService) {
        disconnect(m_llmService, nullptr, this, nullptr);
    }
    m_llmService = service;
    if (m_llmService) {
        connect(m_llmService, &IAIService::requestStarted, this, &AIAgent::onRequestStarted);
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
    const int clamped = qBound(MinContextTokens, tokens, MaxContextTokens);
    if (m_contextLimit == clamped) return;
    m_contextLimit = clamped;
    emit contextLimitChanged();
}

void AIAgent::setSystemPromptOverride(const QString& prompt) {
    const QString trimmed = prompt.trimmed();
    if (m_systemPromptOverride == trimmed) return;
    m_systemPromptOverride = trimmed;
}

QString AIAgent::effectiveSystemPrompt() const {
    return m_systemPromptOverride.isEmpty() ? systemPrompt() : m_systemPromptOverride;
}

// ── Trace helpers ─────────────────────────────────────────

void AIAgent::persistTraceNode(const QJsonObject& node, int order) {
    if (m_memory && !m_sessionId.isEmpty())
        m_memory->saveTraceNode(m_sessionId, node, order);
}

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
    if (extra.contains("images"))
        node["images"] = extra.value("images");

    m_traceModel.append(node);
    persistTraceNode(node, m_traceModel.size() - 1);
    emit traceChanged();
    return id;
}

void AIAgent::updateLastNodeStatus(const QString& status) {
    if (m_traceModel.isEmpty()) return;
    QJsonObject node = m_traceModel.last().toObject();
    node["status"] = status;
    m_traceModel.replace(m_traceModel.size() - 1, node);
    persistTraceNode(node, m_traceModel.size() - 1);
    emit traceChanged();
}

void AIAgent::updateLastNodeTitle(const QString& title) {
    if (m_traceModel.isEmpty()) return;
    QJsonObject node = m_traceModel.last().toObject();
    node["title"] = title;
    m_traceModel.replace(m_traceModel.size() - 1, node);
    persistTraceNode(node, m_traceModel.size() - 1);
    emit traceChanged();
}

bool AIAgent::updateTraceNodeStatusById(const QString& id, const QString& status) {
    for (int i = 0; i < m_traceModel.size(); ++i) {
        QJsonObject node = m_traceModel.at(i).toObject();
        if (node.value("id").toString() == id) {
            node["status"] = status;
            m_traceModel.replace(i, node);
            persistTraceNode(node, i);
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

void AIAgent::startTask(const QString& userMessage, const QStringList& images,
                        const QJsonArray& files) {
    if (m_active) return;

    const bool fresh = m_sessionId.isEmpty();
    if (fresh) {
        m_messages = QJsonArray();
        m_traceModel = QJsonArray();
        m_usedToolCallIds.clear();   // fresh history: no recorded call ids
        m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = effectiveSystemPrompt();
        m_messages.append(sysMsg);

        if (m_memory) m_memory->beginSession(m_sessionId, userMessage.left(60));
        emit sessionChanged(m_sessionId);
        emit sessionsChanged();
    }

    m_iteration = 0;
    m_lastToolSignature.clear();
    m_repeatToolCount = 0;
    m_forcedFinal = false;
    m_pendingToolCalls.clear();
    m_currentConfirmCall = ToolCallInfo{};
    m_confirmNodeId.clear();
    m_lastResponse.clear();
    m_currentExecutingCall = ToolCallInfo{};
    m_toolExecutions = 0;
    m_forcedFinalPush.clear();
    clearStreamState();

    ++m_epoch; // invalidate results from any previous task

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userMessage;
    // Batch 30 (F6): images arrive as compressed data URLs (the viewmodel
    // downsizes before calling in — nexus_core must not link QtGui).
    if (!images.isEmpty()) {
        QJsonArray imageUrls;
        for (const QString& dataUrl : images)
            if (!dataUrl.isEmpty())
                imageUrls.append(dataUrl);
        if (!imageUrls.isEmpty()) {
            userMsg["images"] = imageUrls;
            userMsg["metadata"] = QJsonObject{{"images", imageUrls}};
        }
    }
    // Text attachments ride as a separate field; the request-time folding
    // into the message text happens in processIteration.
    if (!files.isEmpty()) {
        userMsg["files"] = files;
        QJsonObject metadata = userMsg.value("metadata").toObject();
        metadata["files"] = files;
        userMsg["metadata"] = metadata;
    }
    m_messages.append(userMsg);
    if (m_memory) m_memory->saveMessage(m_sessionId, userMsg);

    QJsonObject extra;
    if (userMsg.contains("images"))
        extra["images"] = userMsg.value("images");
    if (userMsg.contains("files"))
        extra["files"] = userMsg.value("files");
    appendTraceNode("user_message", userMessage, "completed", extra);
    notifyTokens();

    m_active = true;
    m_taskStartMs = QDateTime::currentMSecsSinceEpoch();
    if (m_taskTimeoutMs > 0) m_taskTimeoutTimer.start(m_taskTimeoutMs);
    processIteration();
}

void AIAgent::resetSession() {
    const bool wasActive = m_active;
    m_active = false;
    ++m_epoch;
    m_taskTimeoutTimer.stop();
    m_pendingToolCalls.clear();
    m_currentConfirmCall = ToolCallInfo{};
    m_currentExecutingCall = ToolCallInfo{};
    m_inFlightTools.clear();   // Batch 13: bookkeeping only; results are epoch-dropped
    m_toolExecutions = 0;
    m_forcedFinalPush.clear();
    m_confirmNodeId.clear();
    clearStreamState();
    if (m_llmService && wasActive) m_llmService->cancel();

    m_sessionId.clear();
    m_messages = QJsonArray();
    m_traceModel = QJsonArray();
    m_usedToolCallIds.clear();
    m_lastResponse.clear();
    m_iteration = 0;
    m_lastToolSignature.clear();
    m_repeatToolCount = 0;
    m_forcedFinal = false;

    setState(AgentState::Idle);
    emit traceChanged();
    emit sessionChanged(QString());
}

void AIAgent::resumeSession(const QString& sessionId) {
    if (m_active || sessionId.isEmpty()) return;

    m_sessionId = sessionId;
    m_messages = QJsonArray();
    m_usedToolCallIds.clear();   // resumed history carries no tool calls

    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = effectiveSystemPrompt();
    m_messages.append(sysMsg);

    // Persisted history contains clean user/assistant pairs.
    //
    // Resume contract (MEM-701): tool evidence is persisted in trace_nodes
    // for the UI timeline ONLY and is deliberately NOT re-injected into the
    // LLM context — resuming a session must be equivalent to continuing a
    // fresh conversation with the same visible text (guarded by the
    // resumeKeepsCleanHistoryAcrossTurns test).
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

    // Rebuild the trace view: prefer the persisted timeline (thinking /
    // tool calls / results); fall back to the user/assistant text summary
    // for sessions recorded before trace persistence existed.
    m_traceModel = QJsonArray();
    m_lastResponse.clear();
    const QJsonArray savedNodes = m_memory ? m_memory->loadTraceNodes(sessionId) : QJsonArray();
    if (!savedNodes.isEmpty()) {
        m_traceModel = savedNodes;
        for (int i = savedNodes.size() - 1; i >= 0; --i) {
            const QJsonObject node = savedNodes.at(i).toObject();
            if (node.value("type").toString() == "summary") {
                m_lastResponse = node.value("title").toString();
                break;
            }
        }
    } else {
        for (const auto& entry : m_messages) {
            const QJsonObject msg = entry.toObject();
            const QString role = msg.value("role").toString();
            if (role == "user") {
                QJsonObject userNode;
                userNode["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                userNode["type"] = "user_message";
                userNode["title"] = msg.value("content").toString();
                userNode["status"] = "completed";
                userNode["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                userNode["args"] = QString();
                userNode["result"] = QString();
                // Batch 30: images survive in message metadata (loadMessages
                // promotes them back to the top level).
                if (msg.contains("images"))
                    userNode["images"] = msg.value("images");
                m_traceModel.append(userNode);
                emit traceChanged();
            } else if (role == "assistant") {
                const QString content = msg.value("content").toString();
                appendTraceNode("summary", content, "completed");
                m_lastResponse = content;
            }
        }
    }

    m_iteration = 0;
    m_lastToolSignature.clear();
    m_repeatToolCount = 0;
    m_forcedFinal = false;
    m_pendingToolCalls.clear();
    m_currentConfirmCall = ToolCallInfo{};
    m_currentExecutingCall = ToolCallInfo{};
    m_toolExecutions = 0;
    m_forcedFinalPush.clear();
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
        if (m_forcedFinal) {
            // Even the forced no-tools call looped: nothing left to try.
            appendTraceNode("error",
                            QStringLiteral("Reached max iterations (%1)").arg(m_maxIterations),
                            "failed");
            finishTask(QStringLiteral(
                "I could not finish the task within %1 reasoning steps. "
                "Please rephrase the request or continue the conversation.")
                .arg(m_maxIterations));
            return;
        }
        // Budget exhausted: force ONE final call with tools withheld so the
        // information already gathered becomes an answer instead of an error.
        appendTraceNode("context",
            QStringLiteral("Step budget (%1) exhausted - requesting a final answer "
                           "from the information collected so far").arg(m_maxIterations),
            "completed");
        // The control message belongs to THIS request only: it is injected
        // at chatComplete time and never becomes part of the conversation
        // history (a leftover "Do NOT call any tools" user message would
        // otherwise ride along with every later turn).
        m_forcedFinalPush = QStringLiteral(
            "[System] Step budget exhausted. Do NOT call any tools. Based on ALL "
            "information gathered above, answer the user's last question now; "
            "state explicitly what you could and could not confirm.");
        m_forcedFinal = true;
    } else {
        ++m_iteration;
    }

    collapseOldToolRounds();

    // Build tool definitions for the LLM; dangerous tools are never exposed.
    // Withheld entirely on the forced final call. Built BEFORE the budget
    // pass so the schema's cost can be reserved (REL-1002).
    QJsonArray toolsArray;
    if (m_toolRegistry && !m_forcedFinal) {
        for (const auto& def : m_toolRegistry->listAll()) {
            // Batch 14: disabled tools (local-only mode) must not even reach
            // the wire - the acceptance criterion is that the request body
            // contains no schema for the gated tool at all.
            if (!m_toolRegistry->isToolEnabled(def.name)) continue;
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

    applyContextBudget(TokenEstimator::estimateToolsSchema(toolsArray));
    m_thinkingNodeId = appendTraceNode("thinking",
                                       QStringLiteral("Reasoning (step %1)").arg(m_iteration),
                                       "running");
    setState(AgentState::Thinking);

    QJsonArray requestMessages = m_messages;
    // Batch 30 (F6): fold text attachments into the request copy of the user
    // message — every provider gets them as plain text, while the persisted
    // message keeps them as a structured field. Images stay on the message
    // for the provider vision converters.
    for (int i = 0; i < requestMessages.size(); ++i) {
        QJsonObject msg = requestMessages.at(i).toObject();
        const QJsonArray files = msg.value("files").toArray();
        if (files.isEmpty()) continue;
        QString content = msg.value("content").toString();
        for (const auto& file : files) {
            const QJsonObject f = file.toObject();
            content += QStringLiteral("\n\n--- Attached file: %1 ---\n%2\n--- End of %1 ---")
                .arg(f.value("name").toString(), f.value("content").toString());
        }
        msg["content"] = content;
        msg.remove("files");
        requestMessages.replace(i, msg);
    }
    if (!m_forcedFinalPush.isEmpty()) {
        QJsonObject pushMsg;
        pushMsg["role"] = "user";
        pushMsg["content"] = m_forcedFinalPush;
        requestMessages.append(pushMsg);
    }
    m_llmService->chatComplete(requestMessages, toolsArray);
}

// Compress the persisted history when it exceeds the context budget, so a
// long session cannot silently blow past the model's context window.
// reservedTokens: the per-request extras (tools schema) that share the
// window but are not part of m_messages (REL-1002).
void AIAgent::applyContextBudget(int reservedTokens) {
    if (m_contextLimit <= 0) return;
    const int effective = qMax(0, m_contextLimit - reservedTokens);
    if (TokenEstimator::estimateTokens(m_messages) <= effective) return;

    const int before = m_messages.size();
    m_messages = TokenEstimator::compressToFit(m_messages, effective);
    if (m_messages.size() == before) return;

    notifyTokens();
    appendTraceNode("context",
        QStringLiteral("History compressed: %1 → %2 messages to fit the %3 token budget")
            .arg(before).arg(m_messages.size()).arg(m_contextLimit),
        "completed");
}

QString AIAgent::currentModel() const {
    return m_llmService ? m_llmService->model() : QString();
}

void AIAgent::onRequestStarted(quint64 requestId) {
    m_activeRequestId = requestId;
    m_requestStartMs = QDateTime::currentMSecsSinceEpoch();
    m_lastLatencyMs = -1;
    m_lastTokensUsed = 0;
    // OBS-1102: one audit row per LLM call, linkable by request_id
    if (m_memory && !m_sessionId.isEmpty()) {
        QString snippet;
        for (int i = m_messages.size() - 1; i >= 0; --i) {
            const QJsonObject msg = m_messages.at(i).toObject();
            if (msg.value("role").toString() == QLatin1String("user")) {
                snippet = msg.value("content").toString().left(120);
                break;
            }
        }
        m_memory->logAudit(m_sessionId, QStringLiteral("llm_request"),
                           currentModel(), snippet, true, QString(),
                           requestId, -1, 0, currentModel());
    }
}

void AIAgent::onLLMResponse(const LLMResponse& response) {
    if (!m_active) return;

    // The forced-final control message rode along with this request only.
    m_forcedFinalPush.clear();

    // A response echoing a superseded request's id carries results for a
    // task that no longer exists - drop it wholesale (ARCH-402).
    if (response.requestId != 0 && m_activeRequestId != 0
            && response.requestId != m_activeRequestId) {
        qWarning() << "Dropping response from superseded request"
                   << response.requestId << "(active:" << m_activeRequestId << ")";
        return;
    }

    // Batch 11: per-request latency/token accounting for the audit chain
    if (m_requestStartMs > 0)
        m_lastLatencyMs = QDateTime::currentMSecsSinceEpoch() - m_requestStartMs;
    m_lastTokensUsed = response.tokensUsed > 0
        ? response.tokensUsed
        : TokenEstimator::estimateTokens(m_messages);

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
        for (const auto& call : response.toolCalls) {
            ToolCallInfo normalized = call;
            // Empty provider ids and provider fallbacks that repeat across
            // turns ("call_0") are regenerated from a process-global
            // counter - duplicate tool_call_ids in one history are rejected
            // by strict OpenAI-compatible proxies (FIX-401).
            if (normalized.id.isEmpty() || m_usedToolCallIds.contains(normalized.id))
                normalized.id = QStringLiteral("call_%1").arg(++m_toolCallSeq);
            m_usedToolCallIds.insert(normalized.id);
            calls.append(normalized);

            QJsonObject c;
            c["id"] = normalized.id;
            c["type"] = "function";
            QJsonObject fn;
            fn["name"] = normalized.name;
            fn["arguments"] = argsToString(normalized.arguments);
            c["function"] = fn;
            callsArr.append(c);
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

    // On the forced final call the model must answer, not call tools. It
    // tried anyway: drop the calls and finish with whatever text it produced.
    if (m_forcedFinal) {
        sealUnansweredToolCalls(calls);
        m_streamText.clear();
        finishTask(response.content.isEmpty()
            ? QStringLiteral("Step budget exhausted before I could fully answer. "
                             "Here is what I found so far - please continue the "
                             "conversation for more detail.")
            : response.content);
        return;
    }

    // ── Loop terminator: identical back-to-back tool call rounds ──
    // Some models write the complete final answer AND then re-issue a tool
    // call they already ran (observed with OpenAI-compatible proxies).
    // The second repetition means the loop is stuck: finish with the answer
    // that is already on the table instead of burning the step budget.
    QString signature;
    for (const auto& c : calls)
        signature += c.name + QLatin1Char(':') + argsToString(c.arguments)
                   + QLatin1Char(';');
    const bool repeated = (signature == m_lastToolSignature);
    m_lastToolSignature = signature;
    if (repeated) ++m_repeatToolCount; else m_repeatToolCount = 0;

    if (m_repeatToolCount >= 2
            || (m_repeatToolCount == 1 && response.content.size() >= kMinFinalAnswerChars)) {
        sealUnansweredToolCalls(calls);
        const bool streamed = m_streamNodeActive;
        if (streamed) {
            updateLastNodeStatus("completed");
            m_streamNodeActive = false;
        }
        m_streamText.clear();
        finishTask(response.content.isEmpty()
            ? QStringLiteral("Step budget exhausted before I could fully answer. "
                             "Here is what I found so far - please continue the "
                             "conversation for more detail.")
            : response.content, streamed);
        return;
    }
    if (m_repeatToolCount == 1) {
        // Second identical call without a usable answer: skip the redundant
        // execution, acknowledge the repeats as tool results, and demand a
        // final answer.
        for (const auto& c : calls) {
            appendTraceNode("tool_call", c.name, "cancelled",
                            QJsonObject{{"args", argsToString(c.arguments)}});
            QJsonObject toolMsg;
            toolMsg["role"] = "tool";
            toolMsg["tool_call_id"] = c.id;
            toolMsg["content"] = QStringLiteral(
                "[Repeated call skipped] This exact call was already executed - its "
                "result is in the conversation above. Do NOT call it again. Reply "
                "with your final answer now.");
            m_messages.append(toolMsg);
        }
        processIteration();
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
    m_forcedFinalPush.clear();
    failTask(error);
}

void AIAgent::stop() {
    if (!m_active) return;
    m_active = false;
    ++m_epoch;
    m_taskTimeoutTimer.stop();
    // Batch 13 (SEC-1302): cooperatively cancel every tool already running on
    // the worker pool - sealing the history alone left side effects (file
    // writes, child processes) running to completion. cancel() only flips
    // atomic flags inside the tools (GUI thread contract, ITool::cancel);
    // after the epoch bump the late watcher callback drops the result.
    for (auto it = m_inFlightTools.cbegin(); it != m_inFlightTools.cend(); ++it)
        it.value()->cancel();
    m_inFlightTools.clear();
    // The assistant message may already be in the history announcing calls
    // that will now never run - seal ALL of them (still queued, awaiting
    // confirmation, and currently executing on the worker pool) or the next
    // turn inherits a dangling tool_calls sequence.
    QVector<ToolCallInfo> unanswered = m_pendingToolCalls;
    if (!m_currentConfirmCall.id.isEmpty())
        unanswered.append(m_currentConfirmCall);
    if (!m_currentExecutingCall.id.isEmpty())
        unanswered.append(m_currentExecutingCall);
    sealUnansweredToolCalls(unanswered,
        QStringLiteral("[Not executed] The task was stopped before this call "
                       "could run."));
    m_pendingToolCalls.clear();
    m_currentConfirmCall = ToolCallInfo{};
    m_currentExecutingCall = ToolCallInfo{};
    m_forcedFinalPush.clear();

    if (!m_confirmNodeId.isEmpty())
        updateTraceNodeStatusById(m_confirmNodeId, "cancelled");
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

// Collapse tool-call rounds older than the newest kKeepToolRounds into a
// single assistant note. Each round (assistant with tool_calls + its tool
// results) is replaced as a WHOLE so the wire format stays valid. This
// keeps the model from reading the conversation as an "always call tools"
// script - the pattern that made it fire search_knowledge_base on greetings.
void AIAgent::collapseOldToolRounds() {
    struct Round { int from; int to; };   // inclusive message range
    QVector<Round> rounds;
    for (int i = 1; i < m_messages.size(); ++i) {   // skip the system prompt
        const QJsonObject msg = m_messages.at(i).toObject();
        if (msg.value("role").toString() != QLatin1String("assistant")
                || msg.value("tool_calls").toArray().isEmpty())
            continue;
        int j = i + 1;
        while (j < m_messages.size()
               && m_messages.at(j).toObject().value("role").toString() == QLatin1String("tool"))
            ++j;
        rounds.append({i, j - 1});
        i = j - 1;
    }
    if (rounds.size() <= kKeepToolRounds) return;

    const int collapseCount = rounds.size() - kKeepToolRounds;
    // Replace back-to-front so the indices of the not-yet-collapsed rounds
    // stay valid.
    for (int r = collapseCount - 1; r >= 0; --r) {
        const Round round = rounds.at(r);
        QJsonObject note;
        note["role"] = "assistant";
        // Tool-neutral on purpose (FIX-504): naming "e.g. search_knowledge_base"
        // injected KB bias into every conversation, no matter which tools ran.
        note["content"] = QStringLiteral(
            "[Earlier tool calls were executed here; "
            "their results were already used in the answers that follow.]");
        for (int idx = round.to; idx >= round.from; --idx)
            m_messages.removeAt(idx);
        m_messages.insert(round.from, note);
    }
}

void AIAgent::sealUnansweredToolCalls(const QVector<ToolCallInfo>& calls,
                                      const QString& note) {
    if (calls.isEmpty()) return;
    for (const auto& c : calls) {
        QJsonObject toolMsg;
        toolMsg["role"] = "tool";
        toolMsg["tool_call_id"] = c.id;
        toolMsg["content"] = note;
        m_messages.append(toolMsg);
    }
}

void AIAgent::processNextToolCall() {
    if (!m_active) return;

    while (!m_pendingToolCalls.isEmpty()) {
        const ToolCallInfo call = m_pendingToolCalls.takeFirst();
        ITool* tool = m_toolRegistry ? m_toolRegistry->find(call.name) : nullptr;

        if (!tool) {
            appendTraceNode("tool_call", call.name, "failed",
                            QJsonObject{{"args", argsToString(call.arguments)}});
            // Naming the valid tools lets the model self-correct instead of
            // re-issuing the same hallucinated call until the step budget
            // runs out.
            appendToolResultMessage(call,
                QStringLiteral("Error: unknown tool '%1'. Available tools: %2")
                    .arg(call.name, m_toolRegistry
                         ? m_toolRegistry->toolNamesForModel().join(QStringLiteral(", "))
                         : QString()));
            if (m_memory)
                m_memory->logAudit(m_sessionId, "unknown_tool", call.name,
                                   argsToString(call.arguments), false,
                                   QStringLiteral("unregistered tool"),
                                   m_activeRequestId, m_lastLatencyMs,
                                   m_lastTokensUsed, currentModel());
            continue;
        }

        // Batch 14 (SEC-1401): a tool can be registered yet disabled (e.g.
        // search_knowledge_base under local-only mode). Reject stale/hallu-
        // cinated calls with a structured error so tool_call/tool pairing
        // stays intact - same shape as the unknown-tool path above.
        if (m_toolRegistry && !m_toolRegistry->isToolEnabled(call.name)) {
            appendTraceNode("tool_call", call.name, "failed",
                            QJsonObject{{"args", argsToString(call.arguments)}});
            appendToolResultMessage(call,
                QStringLiteral("Error: tool '%1' is currently disabled by the "
                               "local-only privacy mode").arg(call.name));
            if (m_memory)
                m_memory->logAudit(m_sessionId, "tool_disabled", call.name,
                                   argsToString(call.arguments), false,
                                   QStringLiteral("local-only mode"),
                                   m_activeRequestId, m_lastLatencyMs,
                                   m_lastTokensUsed, currentModel());
            continue;
        }

        const ToolDefinition def = tool->definition();

        if (def.level == ToolLevel::RequiresConfirm
                || def.level == ToolLevel::Dangerous) {
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
    // Redundant-call detection lives in onLLMResponse (round-level, before
    // dispatch) - by the time we get here the call is worth executing.

    if (m_toolExecutions >= kMaxToolExecutions) {
        // Execution budget exhausted: refuse this call, seal everything still
        // queued, and force one final no-tools answer from what was gathered.
        if (!traceNodeId.isEmpty())
            updateTraceNodeStatusById(traceNodeId, "cancelled");
        else
            appendTraceNode("tool_call", call.name, "cancelled",
                            QJsonObject{{"args", argsToString(call.arguments)}});
        appendTraceNode("context",
            QStringLiteral("Tool execution budget (%1) exhausted - forcing a "
                           "final answer").arg(kMaxToolExecutions),
            "completed");
        appendToolResultMessage(call,
            QStringLiteral("[Not executed] Tool execution budget exhausted."));
        for (const auto& queued : m_pendingToolCalls) {
            appendTraceNode("tool_call", queued.name, "cancelled",
                            QJsonObject{{"args", argsToString(queued.arguments)}});
            appendToolResultMessage(queued,
                QStringLiteral("[Not executed] Tool execution budget exhausted."));
        }
        m_pendingToolCalls.clear();
        m_forcedFinal = true;
        processIteration();
        return;
    }

    QString nodeId = traceNodeId;
    if (nodeId.isEmpty()) {
        nodeId = appendTraceNode("tool_call", call.name, "running",
                                 QJsonObject{{"args", argsToString(call.arguments)}});
    } else {
        updateTraceNodeStatusById(nodeId, "running");
    }
    setState(AgentState::ExecutingTool);
    ++m_toolExecutions;
    m_currentExecutingCall = call;

    ITool* tool = m_toolRegistry ? m_toolRegistry->find(call.name) : nullptr;
    const quint64 epoch = m_epoch;
    if (tool)
        m_inFlightTools.insert(call.id, tool);   // Batch 13: stop() reaches it

    auto* watcher = new QFutureWatcher<ToolResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, call, nodeId, epoch]() {
        m_inFlightTools.remove(call.id);   // both paths: done is done
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
    m_currentExecutingCall = ToolCallInfo{};
    updateTraceNodeStatusById(traceNodeId, result.success ? "success" : "failed");

    const QString output = result.success ? result.output : result.error;
    appendTraceNode("tool_result", call.name,
                    result.success ? "success" : "failed",
                    QJsonObject{{"result", truncateText(output, kMaxTraceChars)}});

    appendToolResultMessage(call, truncateText(
        result.success ? result.output
                       : QStringLiteral("Error: %1").arg(result.error),
        kMaxToolResultChars));

    if (m_memory) {
        // Batch 31a: surface the originating MCP server in the audit trail
        // for mcp__<server>__<tool> dynamic tools.
        QString auditDetail = truncateText(
            result.success ? result.output : result.error, 500);
        if (call.name.startsWith(QStringLiteral("mcp__"))) {
            const QStringList parts = call.name.split(QStringLiteral("__"));
            if (parts.size() >= 3) {
                const QJsonObject detail{
                    {"output", auditDetail},
                    {"mcp_server", parts.at(1)}};
                auditDetail = QString::fromUtf8(
                    QJsonDocument(detail).toJson(QJsonDocument::Compact));
            }
        }
        m_memory->logAudit(m_sessionId, "tool_execute", call.name,
                           argsToString(call.arguments), result.success,
                           auditDetail,
                           m_activeRequestId, m_lastLatencyMs,
                           m_lastTokensUsed, currentModel());
    }

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
                               QStringLiteral("user declined execution"),
                               m_activeRequestId, m_lastLatencyMs,
                               m_lastTokensUsed, currentModel());
        processNextToolCall();
        return;
    }

    if (m_memory)
        m_memory->logAudit(m_sessionId, "tool_approved", call.name,
                           argsToString(call.arguments), true,
                           QStringLiteral("user approved execution"),
                           m_activeRequestId, m_lastLatencyMs,
                           m_lastTokensUsed, currentModel());
    dispatchToolExecution(call, nodeId);
}

// ── Completion / failure ──────────────────────────────────

void AIAgent::finishTask(const QString& finalText, bool summaryTraced) {
    m_active = false;
    m_taskTimeoutTimer.stop();
    m_inFlightTools.clear();   // Batch 13: normally empty here; belt-and-braces

    // Batch 11: close the observability chain — one audit row carrying the
    // task-level latency/tokens, and a metrics trace node for the timeline.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 taskLatency = m_taskStartMs > 0 ? now - m_taskStartMs : -1;
    if (m_memory && !m_sessionId.isEmpty()) {
        m_memory->logAudit(m_sessionId, QStringLiteral("task_complete"),
                           QString(), QString(), true,
                           truncateText(finalText, 500),
                           m_activeRequestId, taskLatency,
                           m_lastTokensUsed, currentModel());
    }
    appendTraceNode("metrics",
                    QStringLiteral("Task %1s · last request %2 ms · tokens %3")
                        .arg(taskLatency / 1000.0, 0, 'f', 1)
                        .arg(m_lastLatencyMs)
                        .arg(m_lastTokensUsed),
                    "completed");
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
    m_taskTimeoutTimer.stop();
    m_inFlightTools.clear();   // Batch 13: belt-and-braces, mirrors finishTask
    appendTraceNode("error", error, "failed");
    setState(AgentState::Error);
    emit errorOccurred(error);
}

} // namespace NexusAI
