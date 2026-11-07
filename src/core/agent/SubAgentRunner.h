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

// docs/34 P3.1: 一条修改提案——子代理只提「改哪里、怎么改」，不落盘。
// find 必须在其文件当前内容里恰好出现一次（锚点式），应用由主代理用
// edit_file/file_write 完成（沿用既有 RequiresConfirm 确认流）。
struct SubAgentProposalEdit {
    QString find;
    QString replace;
};

struct SubAgentProposal {
    QString path;       // resolved absolute path (report shows a project-relative form)
    QString rationale;  // one-line reason from the sub-agent
    QVector<SubAgentProposalEdit> edits;
};

// docs/34 P3.1: 提案的线程局部汇聚槽——与 SubAgentProgressRouter 同型：
// 子代理运行在同一线程上，ProposeEditTool 把校验通过的提案推进来，Runner
// 在 run() 结束时取走。NULL 表示当前不在子代理运行内（主代理若幻觉调用
// propose_edit，工具据此直接拒绝）。
class SubAgentProposalSink {
public:
    explicit SubAgentProposalSink(QVector<SubAgentProposal>* sink) {
        s_sink.localData() = sink;
    }
    ~SubAgentProposalSink() { s_sink.localData() = nullptr; }
    SubAgentProposalSink(const SubAgentProposalSink&) = delete;
    SubAgentProposalSink& operator=(const SubAgentProposalSink&) = delete;

    static QVector<SubAgentProposal>* current() { return s_sink.localData(); }

private:
    static inline QThreadStorage<QVector<SubAgentProposal>*> s_sink;
};

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

// docs/43 O1-T2: project rules (AGENTS.md / .nexus/rules.md) handed to the
// sub-agent run. AIAgent snapshots the block on the GUI thread (the rules
// cache is not thread-safe) and installs it on the dispatching pool thread;
// SubAgentTool copies it into the request. Same lifetime/threading contract
// as SubAgentProgressRouter above.
class SubAgentRulesRouter {
public:
    explicit SubAgentRulesRouter(QString rules) {
        s_rules.localData() = std::move(rules);
    }
    ~SubAgentRulesRouter() { s_rules.localData().clear(); }
    SubAgentRulesRouter(const SubAgentRulesRouter&) = delete;
    SubAgentRulesRouter& operator=(const SubAgentRulesRouter&) = delete;

    static QString current() { return s_rules.localData(); }

private:
    static inline QThreadStorage<QString> s_rules;
};

// docs/33 E2: a named role profile — the "hint" enum generalized into a
// configurable roster. name is the model-facing id in spawn_agent's "role"
// argument; prompt replaces the hint-based focus block in the sub-agent
// system prompt; tools is an optional whitelist on top of the read-only
// catalog (empty = the whole catalog). Built-in presets live in
// SubAgentTool::presetRoles(); user-defined ones come from the
// agent/subagentRoles settings blob via SubAgentTool::rolesFromVariant().
struct SubAgentRole {
    QString name;       // e.g. "explore"; must be non-empty
    QString prompt;     // role focus text for the system prompt
    QStringList tools;  // tool-name whitelist; empty = no restriction
    // docs/35 P4.6: 角色级模型配置。profileId 空 = 沿用活动 profile；指向
    // 已删除的 profile 时装配层回退到活动 profile。effort（low/medium/
    // high）非空时覆盖该 profile 的思考强度。
    QString profileId;
    QString effort;
    // 该角色默认是否后台运行。spawn_agent 省略 background 参数时按此值
    // 执行；模型显式传 false 仍可强制同步（模型决定优先于角色默认）。
    bool background{false};
};

// P1 (docs/32): parameters for one sub-agent run.
struct SubAgentRequest {
    QString goal;        // required, self-contained task description
    QString context;     // optional background/constraints from the primary agent
    QString hint;        // legacy preset id ("explore"/"research"); ignored when
                         // rolePrompt is set — kept so old callers still work
    QString rolePrompt;  // docs/33 E2: resolved SubAgentRole prompt; non-empty
                         // replaces the hint-based focus block
    // docs/43 O1-T2: project rules snapshot (already rendered with its
    // "Project rules (...)" header by AIAgent::projectRulesBlock); appended
    // verbatim to the sub-agent system prompt.
    QString projectRules;
    QVector<SubAgentToolEntry> tools;   // read-only snapshot (see above)
    // LLM round-trip budget incl. the forced final. 0 = 无预算（2026-11-01
    // 起不再是用户可配置限制，与主任务一致：模型自定何时收工）；内部仍设
    // kSubStepCap 防失控。>0 仅测试注入做确定性用例。
    int maxSteps{0};
    // 0 = no wall-clock budget (user may stop manually); >0 only from tests.
    qint64 timeoutMs{0};
    // docs/35 P4.1: model context window (tokens) shared by the sub-agent's
    // profile. 0 = unknown (profile resolution failed / caller has no value):
    // the runner compresses the request copy with kFallbackContextLimit instead
    // of sending an over-window request to the provider. The full transcript is
    // still kept for the evidence artifact and the next round.
    int contextLimit{0};
    ITool::Cancellation cancelled;       // shared with the dispatching tool; a
                                         // stop() of the primary agent flips it
    SubAgentProgressHook progress;       // optional; docs/32 P2 live progress
};

// docs/43 O2: one machine-readable conclusion parsed from the report's
// trailing "## FINDINGS" section. Parsing never affects the report text —
// a malformed section simply yields no findings.
struct SubAgentFinding {
    QString severity;   // critical | high | medium | low | info
    QString path;       // project-relative when the report named one
    int line{0};        // 0 = unspecified
    QString message;
};

// docs/43 O2: classified usage. Providers that do not report per-class counts
// leave every number at 0 and usageReported=false — the aggregate tokensUsed
// on the report stays the source of truth in that case.
struct SubAgentUsage {
    QString model;
    QString profileId;
    int inputTokens{0};
    int outputTokens{0};
    int cachedTokens{0};   // OpenAI cached_tokens + Anthropic cache_read, merged
    // B3: Anthropic 缓存分类原值。cacheRead 同时并入 cachedTokens 供 UI 展示，
    // cacheCreation 属于未命中的输入，入账时单独补进 input。
    int cacheReadTokens{0};
    int cacheCreationTokens{0};
    bool usageReported{false};
    // B9: provider 未上报 usage 时 Runner 用 TokenEstimator 回退估算 tokensUsed
    // 并置此标记；UI 以「≈ tokens N」区分估算值，绝不冒充真实计量。
    bool estimated{false};
    // B4: 跨 attempt 汇总时区分「部分上报」——reported 保持 OR（有任一上报即
    // 可展示分类），reportedCount/attemptCount 给出分母，partial 标记只有
    // 部分 attempt 提供了分类用量。单次 attempt 全量上报时 partial=false。
    int reportedCount{0};
    int attemptCount{0};
    bool partial{false};
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
    // docs/43 O2: machine-readable reason (completed / cancelled / timeout /
    // budget / repeated / service_error / empty_response / no_service /
    // invalid_goal). The Stop enum stays for existing UI/tests; stopCode is
    // what the primary agent and audit consumers read.
    QString stopCode{QStringLiteral("error")};
    // docs/43 O1-T3: transient failure (service/network error, timeout, empty
    // response) worth one automatic re-run. Budget/Cancelled and wiring
    // failures are never retryable.
    bool retryable{false};
    // Set by SubAgentTool's attempt loop (1-based). Several attempts mean the
    // steps/tokens below are the accumulated cost of the whole spawn call.
    int attempt{1};
    QString text;        // final report (Completed) or a readable failure reason
    int steps{0};        // LLM round-trips actually used
    int tokensUsed{0};   // sum of the per-response token counts, when reported
    // docs/43 O2: classified usage across every step and retry attempt.
    SubAgentUsage usage;
    // docs/43 O2: conclusions parsed from "## FINDINGS" (empty when absent).
    QVector<SubAgentFinding> findings;
    // docs/33 E1: full transcript of the run (goal + every assistant/tool
    // message), written to the app-data artifacts directory. Empty when the
    // run had no tool rounds (nothing to preserve) or the disk write failed.
    // The primary agent can inspect it with file_read — the sandbox read
    // allowlist covers AppDataLocation.
    QString artifactPath;
    // docs/34 P3.1: 未应用的修改提案（coder 角色经 propose_edit 产出）。
    // 报告文本里同时渲染成 `## PROPOSALS` 段；即使运行以 Budget/Error
    // 收场也照常返回，主代理可只应用已验证的部分。
    QVector<SubAgentProposal> proposals;
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
    // B8: contextLimit=0（未知 profile）时的保守回退窗口。与主代理
    // AIAgent::kDefaultContextTokens 同值：宁可多压缩一轮，也不把必然被
    // provider 拒绝的超窗请求发出去（失败重试只会白烧 token）。
    static constexpr int kFallbackContextLimit = 128000;
    // Per-tool-result cap inside the sub-history: bounds a single huge read
    // from filling the sub-context. The final report is NOT truncated here —
    // the primary agent applies its own kMaxToolResultChars when the result
    // lands in the main history.
    static constexpr int kSubToolResultMaxChars = 6000;
    // docs/35 P4.2: read-only tools inside one round run on the runner's own
    // small pool. Capped low because the primary agent may already be running
    // several sub-agents concurrently (3 sub-agents × 3 = 9 nested tasks).
    static constexpr int kSubToolParallelCap = 3;
    // maxSteps <= 0 时的内部安全上限（防失控派生；正常情况下模型自己收敛）。
    static constexpr int kSubStepCap = 1000;
    // Evidence artifacts (docs/33 E1): per-file byte cap and retention.
    static constexpr int kArtifactMaxBytes = 256 * 1024;
    static constexpr int kArtifactRetentionDays = 7;

    static SubAgentReport run(IAIService* service, const SubAgentRequest& request);

    // Redirect the evidence artifacts root (default: AppData/subagents).
    // Tests only; call with an empty string to restore the default.
    static void setArtifactsRootOverride(const QString& dir);

    // True when path is a readable file INSIDE the evidence artifacts root.
    // The UI opens evidence paths from trace/audit data with the system
    // handler; this check keeps a planted path from escaping the root.
    static bool isEvidencePath(const QString& path);

private:
    // The loop proper; run() wraps it with artifact pruning/writing and hands
    // out the final transcript for the evidence dump.
    static SubAgentReport runImpl(IAIService* service, const SubAgentRequest& request,
                                  QJsonArray& messages);
};

} // namespace NexusAI
