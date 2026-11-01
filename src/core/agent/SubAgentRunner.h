#pragma once

#include <QString>
#include <QVector>
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

    static SubAgentReport run(IAIService* service, const SubAgentRequest& request);
};

} // namespace NexusAI
