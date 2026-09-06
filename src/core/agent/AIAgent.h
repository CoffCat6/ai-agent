#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include "AgentState.h"
#include "LLMTypes.h"

namespace NexusAI {

// Forward declarations
class IAIService;
class ToolRegistry;
class IMemory;

// The agent core: runs the ReAct loop
//   user message -> LLM -> (tool calls -> results -> LLM)* -> final answer
// Safe tools execute immediately; RequiresConfirm tools suspend the loop
// until the user approves via approveToolCall(); Dangerous tools are never
// exposed to the LLM and are rejected if hallucinated.
//
// Tools run on a worker thread (QtConcurrent); the loop resumes on the GUI
// thread when the result arrives. A monotonic epoch token discards results
// that outlive the task that dispatched them (stop/reset/startTask).
//
// Streaming: content deltas are surfaced as a live "summary" trace node
// (throttled) and finalized when the complete response arrives.
// Context: before each LLM call the history is compressed to fit
// contextLimit() tokens (system prompt + omission marker + recent suffix).
// Sessions: each conversation gets a UUID; history persists via IMemory
// and can be resumed with resumeSession().
class AIAgent : public QObject {
    Q_OBJECT
public:
    // Hard per-request tool-use step budget (shared with the system prompt)
    static constexpr int kMaxIterations = 10;
    // Hard per-request cap on ACTUAL tool executions (one response can
    // announce arbitrarily many calls; kMaxIterations alone does not bound
    // the total work).
    static constexpr int kMaxToolExecutions = 16;

    // Bounds for the context-window budget (setContextLimit clamps to these;
    // SettingsService applies the same window in its editor)
    static constexpr int MinContextTokens = 2048;
    static constexpr int MaxContextTokens = 1048576;

    explicit AIAgent(QObject* parent = nullptr);
    ~AIAgent() override;

    void setLLMService(IAIService* service);
    void setToolRegistry(ToolRegistry* registry);
    void setMemory(IMemory* memory);

    // Token budget for the LLM context window; history is compressed to fit
    // before each request. 0 disables compression.
    void setContextLimit(int tokens);
    int contextLimit() const { return m_contextLimit; }

    AgentState state() const { return m_state; }
    bool isActive() const { return m_active; }
    QJsonArray messages() const { return m_messages; }
    QJsonArray traceModel() const { return m_traceModel; }
    QString lastResponse() const { return m_lastResponse; }
    QString currentSessionId() const { return m_sessionId; }
    QJsonArray sessionList() const;
    QJsonArray auditLog(int limit = 100) const;

public slots:
    void startTask(const QString& userMessage);
    void approveToolCall(const QString& id, bool approved);
    void stop();                                    // abort generation, keep trace
    void resetSession();                            // clear everything, fresh session
    void resumeSession(const QString& sessionId);   // load history + trace from memory
    void cancel() { resetSession(); }               // legacy alias
    void deleteSession(const QString& sessionId);   // remove persisted session
    void renameSession(const QString& sessionId, const QString& title);

signals:
    void stateChanged(AgentState state);
    void traceChanged();
    void responseReady(const QString& response);
    void toolConfirmRequired(const QString& id, const QString& name, const QJsonObject& args);
    void errorOccurred(const QString& error);
    void tokensChanged(int usedTokens);
    void contextLimitChanged();
    void sessionChanged(const QString& sessionId);
    void sessionsChanged();

private:
    void setState(AgentState state);
    QString appendTraceNode(const QString& type, const QString& title,
                            const QString& status, const QJsonObject& extra = {});
    void updateLastNodeStatus(const QString& status);
    void updateLastNodeTitle(const QString& title);
    bool updateTraceNodeStatusById(const QString& id, const QString& status);
    void notifyTokens();

    void processIteration();
    void applyContextBudget();
    void onRequestStarted(quint64 requestId);
    void onLLMResponse(const LLMResponse& response);
    void onLLMError(const QString& error);
    void onStreamingChunk(const QString& chunk);
    void flushStreamNode();
    void clearStreamState();

    void processNextToolCall();
    // Append synthetic tool-result messages for calls the assistant already
    // announced but that will NOT be executed (loop terminator, forced
    // final, stop). Without them the history ends in dangling tool_calls -
    // an invalid OpenAI sequence that makes some proxies repeat the stale
    // call on the next user turn.
    void sealUnansweredToolCalls(const QVector<ToolCallInfo>& calls,
                                 const QString& note = QStringLiteral(
                                     "[Not executed] The task was completed from the "
                                     "answer above without running this call."));
    // Replace completed tool-call rounds older than the newest few with a
    // short assistant note (keeps the model from pattern-matching the
    // history into calling tools on every new message).
    void collapseOldToolRounds();
    // Persist one trace node (upsert keyed by node id) so resumed sessions
    // rebuild the full timeline, not just user/assistant text.
    void persistTraceNode(const QJsonObject& node, int order);
    // traceNodeId: existing tool_call node to flip to "running" (approval
    // path), or empty to append a fresh one (auto-execution path).
    void dispatchToolExecution(const ToolCallInfo& call, const QString& traceNodeId);
    void finishToolExecution(const ToolCallInfo& call, const QString& traceNodeId,
                             const ToolResult& result);
    void appendToolResultMessage(const ToolCallInfo& call, const QString& content);
    void finishTask(const QString& finalText, bool summaryTraced = false);
    void failTask(const QString& error);

    AgentState m_state{AgentState::Idle};
    QJsonArray m_messages;
    QJsonArray m_traceModel;
    int m_iteration{0};
    int m_maxIterations{kMaxIterations};
    bool m_active{false};
    quint64 m_epoch{0};   // bumped on start/stop/reset; stale tool results are dropped

    QString m_sessionId;
    QString m_lastResponse;

    QVector<ToolCallInfo> m_pendingToolCalls;
    ToolCallInfo m_currentConfirmCall;
    // Dispatched to the worker pool but not finished yet: removed from the
    // pending queue, so stop() must seal it separately.
    ToolCallInfo m_currentExecutingCall;
    QString m_confirmNodeId;  // trace node of the pending confirmation
    // Actual tool executions in the current request (kMaxToolExecutions cap;
    // kMaxIterations alone bounds LLM rounds, not the announced call count).
    int m_toolExecutions{0};

    // Loop guard (round-level, checked in onLLMResponse): an identical
    // back-to-back tool call round means the model is stuck; the second
    // repetition finishes the task with the answer it already wrote instead
    // of burning the step budget on redundant executions.
    QString m_lastToolSignature;
    int m_repeatToolCount{0};
    // Step budget exhausted: one final LLM call with tools withheld so the
    // gathered information turns into an answer instead of an error.
    bool m_forcedFinal{false};
    // Request-scoped control message injected into the forced-final call at
    // chatComplete time; never becomes part of the conversation history.
    QString m_forcedFinalPush;

    // Streaming display state
    QTimer m_streamTimer;
    QString m_streamText;
    bool m_streamNodeActive{false};
    QString m_thinkingNodeId;

    int m_contextLimit{8192};

    IAIService* m_llmService{nullptr};
    ToolRegistry* m_toolRegistry{nullptr};
    IMemory* m_memory{nullptr};

    // Request the LLM service last dispatched for us (requestStarted echo);
    // responses carrying a different non-zero id are dropped (ARCH-402).
    quint64 m_activeRequestId{0};
    // Process-global tool_call_id fallback counter + session-scoped set of
    // ids already in the history: agent-generated or provider-fallback ids
    // ("call_0") repeat across turns, and strict proxies reject duplicates.
    quint64 m_toolCallSeq{0};
    QSet<QString> m_usedToolCallIds;
};

} // namespace NexusAI
