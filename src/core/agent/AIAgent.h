#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include "AgentState.h"
#include "LLMTypes.h"
#include "PermissionMode.h"

namespace NexusAI {

// Forward declarations
class IAIService;
class ITool;
class ToolRegistry;
class IMemory;
class EpisodicService;

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
    void setEpisodicService(EpisodicService* service);

    // Token budget for the LLM context window; history is compressed to fit
    // before each request. 0 disables compression.
    void setContextLimit(int tokens);
    int contextLimit() const { return m_contextLimit; }

    // docs/16 §8.3: 实时上下文构成（按 role 拆分的 token 桶 + tools schema
    // 预留），供 TopBar 容量占比浮层展示。
    QVariantMap contextComposition() const;

    // 会话级权限模式（变更确认/自动编辑/完全访问）。决定确认级工具是否
    // 逐次人工确认；任务进行中切换立即对下一个工具调用生效。内存态，
    // 不持久化——新会话由调用方注入全局默认（SettingsService）。
    void setPermissionMode(PermissionMode mode);
    PermissionMode permissionMode() const { return m_permissionMode; }

    // Batch 22 (F8): user-editable system prompt; empty (after trim) clears
    // the override and falls back to the built-in prompt.
    void setSystemPromptOverride(const QString& prompt);

    // REL-1001: hard wall-clock budget for one startTask. On expiry the
    // task is sealed (pending/confirm/executing calls) and converges into a
    // final response. 0 disables the timeout.
    void setTaskTimeoutMs(int ms);
    int taskTimeoutMs() const { return m_taskTimeoutMs; }

    AgentState state() const { return m_state; }
    bool isActive() const { return m_active; }
    QString currentModel() const;   // configured model identity (audit rows)
    QJsonArray messages() const { return m_messages; }
    QJsonArray traceModel() const { return m_traceModel; }
    QString lastResponse() const { return m_lastResponse; }
    QString currentSessionId() const { return m_sessionId; }

    // docs/17 A0: 活动项目（全局单活动项目，AgentViewModel 随切换推送）。
    // 用途：① 新建会话在 beginSession 后标记归属；② P0 项目上下文——
    // name/rootPath 进入系统提示，工具把相对路径锚定到 rootPath。没有
    // 这两项，模型不知道项目根在哪，读文件只能靠猜路径。空 id = 本地。
    void setActiveProject(const QString& id, const QString& name,
                          const QString& rootPath) {
        m_activeProjectId = id;
        m_activeProjectName = name;
        m_activeProjectRoot = rootPath;
    }
    // docs/18: 只读访问器。归属已改为**按标签**持有（每个 tab 一个 agent），
    // AgentViewModel 的 activeProjectId 由这些 getter 派生，不再有全局状态。
    QString activeProjectId() const { return m_activeProjectId; }
    QString activeProjectName() const { return m_activeProjectName; }
    QString activeProjectRoot() const { return m_activeProjectRoot; }
    QJsonArray sessionList() const;
    QJsonArray auditLog(int limit = 100) const;
    // docs/16 §8: read-only access for aggregate query wrappers.
    IMemory* memory() const { return m_memory; }

    // Base prompt (override or built-in) plus the project context block.
    // Public so tests can assert the injected project root.
    QString effectiveSystemPrompt() const;

public slots:
    // Batch 30 (F6): images are compressed data URLs ("data:image/jpeg;
    // base64,..."); files are {name, content} text attachments folded into
    // the request text (the persisted message keeps them as a field).
    void startTask(const QString& userMessage, const QStringList& images = {},
                   const QJsonArray& files = {});
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
    // docs/16 §12.4: 配额超限警告 (quotaType ∈ dailyToken/dailyCost/
    // monthlyToken/monthlyCost, used=已消耗, quota=上限) 与单次请求费用变化。
    void quotaWarning(const QString& quotaType, qint64 used, qint64 quota);
    void costChanged(qint64 costCents);

private:
    void setState(AgentState state);
    QString projectContextBlock() const;
    QString appendTraceNode(const QString& type, const QString& title,
                            const QString& status, const QJsonObject& extra = {});
    void updateLastNodeStatus(const QString& status);
    void updateLastNodeTitle(const QString& title);
    bool updateTraceNodeStatusById(const QString& id, const QString& status);
    void notifyTokens();

    void processIteration();
    void applyContextBudget(int reservedTokens);
    void onContextLimitChanged(int oldLimit, int newLimit);
    void restoreFullHistoryIfAvailable();
    int estimateReservedTokensForNextRequest() const;
    // docs/16 §7.2: 配额检查，返回 false 表示 strict 模式下拒绝请求。
    bool checkQuota();
    // docs/16 §5.2.2: 采集单次请求的 token 分类明细并持久化。
    void recordTokenUsage(const LLMResponse& response);
    void onTaskTimeout();
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
    void dispatchToolExecution(const ToolCallInfo& call, const QString& traceNodeId,
                               bool autoApproved = false);
    void finishToolExecution(const ToolCallInfo& call, const QString& traceNodeId,
                             const ToolResult& result, bool autoApproved = false);
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
    QString m_activeProjectId;   // docs/17 A0: 活动项目（空 = 本地）
    // P0 项目上下文：随 id 一起推送，进入系统提示（见 projectContextBlock）。
    QString m_activeProjectName;
    QString m_activeProjectRoot;
    QString m_lastResponse;

    QVector<ToolCallInfo> m_pendingToolCalls;
    ToolCallInfo m_currentConfirmCall;
    // Dispatched to the worker pool but not finished yet: removed from the
    // pending queue, so stop() must seal it separately.
    ToolCallInfo m_currentExecutingCall;
    // Batch 13 (SEC-1302): tools dispatched to the pool and not finished yet
    // (call id -> registry-owned tool, no ownership). stop() walks this to
    // invoke ITool::cancel() so side effects terminate instead of running to
    // completion; entries are removed in the watcher callback on both the
    // normal and the epoch-drop path. No ownership - never delete.
    QHash<QString, ITool*> m_inFlightTools;
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
    QString m_systemPromptOverride;   // empty = built-in prompt
    // Step budget exhausted: one final LLM call with tools withheld so the
    // gathered information turns into an answer instead of an error.
    bool m_forcedFinal{false};
    // Request-scoped control message injected into the forced-final call at
    // chatComplete time; never becomes part of the conversation history.
    QString m_forcedFinalPush;

    // REL-1001: task wall-clock budget
    QTimer m_taskTimeoutTimer;
    int m_taskTimeoutMs{300000};

    // Streaming display state
    QTimer m_streamTimer;
    QString m_streamText;
    bool m_streamNodeActive{false};
    QString m_thinkingNodeId;

    int m_contextLimit{8192};

    PermissionMode m_permissionMode{PermissionMode::ConfirmChanges};

    IAIService* m_llmService{nullptr};
    ToolRegistry* m_toolRegistry{nullptr};
    IMemory* m_memory{nullptr};
    EpisodicService* m_episodicService{nullptr};

    // Request the LLM service last dispatched for us (requestStarted echo);
    // responses carrying a different non-zero id are dropped (ARCH-402).
    quint64 m_activeRequestId{0};
    // Observability (Batch 11): per-request latency/token accounting and the
    // wall-clock start of the running task.
    qint64 m_requestStartMs{-1};
    qint64 m_lastLatencyMs{-1};
    int m_lastTokensUsed{0};
    qint64 m_taskStartMs{-1};
    // Process-global tool_call_id fallback counter + session-scoped set of
    // ids already in the history: agent-generated or provider-fallback ids
    // ("call_0") repeat across turns, and strict proxies reject duplicates.
    quint64 m_toolCallSeq{0};
    QSet<QString> m_usedToolCallIds;
};

} // namespace NexusAI
