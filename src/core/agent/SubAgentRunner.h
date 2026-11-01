#pragma once

#include <QString>
#include <QThreadStorage>
#include <QVector>
#include <functional>
#include <memory>

#include "core/tools/ITool.h"

namespace NexusAI {

class IAIService;

// One read-only tool handed to a sub-agent: the definition (schema shown to
// the sub-model) plus the strong reference that keeps the tool object alive
// for the whole run even if the registry entry is removed mid-flight. The
// snapshot is taken on the GUI thread (ToolRegistry has no internal locking);
// the runner only touches this vector, never the registry itself.
struct SubAgentToolEntry {
    ToolDefinition def;
    std::shared_ptr<ITool> tool;
};

// docs/32 P2: one live progress event from a running sub-agent. Emitted on
// the pool thread that runs the sub-agent loop; receivers marshal onto the
// GUI thread themselves.
struct SubAgentProgress {
    int step{0};        // 1-based LLM round
    int maxSteps{0};
    QString phase;      // "thinking" (about to call the model) / "tools" (round's tool calls executed)
    QString detail;     // e.g. executed tool names for the "tools" phase
};

using SubAgentProgressHook = std::function<void(const SubAgentProgress&)>;

// docs/32 P2: thread-local hand-off from the dispatcher (AIAgent) to the
// spawning tool. AIAgent cannot pass per-call context through the fixed
// ITool::execute signature, and SubAgentTool is a process-wide object shared
// by parallel calls and tabs — a member sink would clobber. The hook is
// installed inside the QtConcurrent lambda around tool->execute(); sub-agent
// work (including nested tool calls) stays on that same thread, so the hook
// is visible for exactly the one run. Multiple parallel spawn_agent calls
// run on different pool threads and never see each other's hook.
class SubAgentProgressRouter {
public:
    explicit SubAgentProgressRouter(SubAgentProgressHook hook) {
        s_hook.localData() = std::move(hook);
    }
    ~SubAgentProgressRouter() { s_hook.localData() = nullptr; }
    SubAgentProgressRouter(const SubAgentProgressRouter&) = delete;
    SubAgentProgressRouter& operator=(const SubAgentProgressRouter&) = delete;

    // The hook installed on the CURRENT thread (null outside a dispatch).
    static const SubAgentProgressHook& current() { return s_hook.localData(); }

private:
    static inline QThreadStorage<SubAgentProgressHook> s_hook;
};

// P1 (docs/32): parameters for one sub-agent run.
struct SubAgentRequest {
    QString goal;        // required, self-contained task description
    QString context;     // optional background/constraints from the primary agent
    QString hint;        // "explore" / "research" (empty = explore); shapes the
                         // system prompt, not the tool set
    QVector<SubAgentToolEntry> tools;   // read-only snapshot (see above)
    int maxSteps{12};                    // LLM round-trips, incl. the forced final
    qint64 timeoutMs{300000};            // whole-run wall-clock budget
    ITool::Cancellation cancelled;       // shared with the dispatching tool; a
                                         // stop() of the primary agent flips it
    SubAgentProgressHook progress;       // optional; docs/32 P2 live progress
};

struct SubAgentReport {
    enum class Stop {
        Completed,   // final answer produced
        Cancelled,   // cancellation flag observed
        TimedOut,    // wall-clock budget exhausted
        Budget,      // step budget exhausted without an answer
        Error        // LLM/service failure
    };

    bool ok{false};
    Stop stop{Stop::Error};
    QString text;        // final report (Completed) or a readable failure reason
    int steps{0};        // LLM round-trips actually used
    int tokensUsed{0};   // sum of the per-response token counts, when reported
    // docs/33 E1: full transcript of the run (goal + every assistant/tool
    // message), written to the app-data artifacts directory. Empty when the
    // run had no tool rounds (nothing to preserve) or the disk write failed.
    // The primary agent can inspect it with file_read — the sandbox read
    // allowlist covers AppDataLocation.
    QString artifactPath;
};

// Minimal read-only sub-agent loop (docs/32 §3.2). Deliberately NOT a full
// AIAgent: no TraceModel, no SQLite (IMemory is GUI-thread-bound and tools
// run on pool threads), no confirmation flow — the tool set is filtered to
// Safe+readOnly upstream, so nothing can ask for approval.
//
// Threading: run() is called on the pool thread executing spawn_agent and
// drives its own QEventLoop there — the IAIService (created by the caller's
// factory, parentless, owned by the run) needs an event loop for
// QNetworkAccessManager, and the sandbox ExecutionScope pinned by the
// primary agent's dispatch stays active on this same thread for the nested
// tool calls. run() blocks until the report is ready; poll-based aborts
// (cancel flag, deadline) bound the wait.
class SubAgentRunner {
public:
    // Per-tool-result cap inside the sub-history: bounds a single huge read
    // from filling the sub-context. The final report is NOT truncated here —
    // the primary agent applies its own kMaxToolResultChars when the result
    // lands in the main history.
    static constexpr int kSubToolResultMaxChars = 6000;
    // Evidence artifacts (docs/33 E1): per-file byte cap and retention.
    static constexpr int kArtifactMaxBytes = 256 * 1024;
    static constexpr int kArtifactRetentionDays = 7;

    static SubAgentReport run(IAIService* service, const SubAgentRequest& request);

    // Redirect the evidence artifacts root (default: AppData/subagents).
    // Tests only; call with an empty string to restore the default.
    static void setArtifactsRootOverride(const QString& dir);

private:
    // The loop proper; run() wraps it with artifact pruning/writing and hands
    // out the final transcript for the evidence dump.
    static SubAgentReport runImpl(IAIService* service, const SubAgentRequest& request,
                                  QJsonArray& messages);
};

} // namespace NexusAI
