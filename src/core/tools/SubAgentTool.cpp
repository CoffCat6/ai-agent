#include "SubAgentTool.h"

#include "ProposeEditTool.h"
#include "core/agent/BackgroundTaskHub.h"
#include "core/agent/SubAgentRunner.h"
#include "core/tools/Sandbox.h"
#include "core/tools/ToolRegistry.h"
#include "infrastructure/llm/IAIService.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace NexusAI {

namespace {
constexpr int kMaxParallelCeiling = 8;
constexpr int kSlotWaitSliceMs = 250;   // cancellation granularity while queued
// docs/43 O1-T3: transient failures get one automatic re-run (two attempts
// total). Each attempt accumulates steps/tokens into the final report.
constexpr int kSubAgentMaxAttempts = 2;
} // namespace

// docs/33 E2: the preset roster. Prompts stay English to match the base
// sub-agent system prompt; "explore" doubles as the default when the caller
// names no role.
QVector<SubAgentRole> SubAgentTool::presetRoles() {
    return {
        {QStringLiteral("explore"),
         QStringLiteral(
             "breadth first - enumerate candidate locations before reading "
             "anything in full, then read selectively. Report a structured "
             "map of what exists where."),
         {}},
        {QStringLiteral("research"),
         QStringLiteral(
             "depth over breadth - chase the specific question and verify it "
             "against the sources before reporting. Cite the exact evidence "
             "(path:line) for every claim."),
         {}},
        {QStringLiteral("review"),
         QStringLiteral(
             "code review - a single-pass critique of a changeset. First "
             "scope the changes yourself (git_status / git_diff, or the "
             "files named in the context), then inspect them for real "
             "defects only: correctness, boundary conditions, security, "
             "performance. Report a findings list ranked by severity, each "
             "with a concrete location (path:line) and what to change; no "
             "praise, no filler. If the changes are clean, say exactly "
             "that - do not invent issues. You critique once and report; "
             "fixing the code is the primary agent's job."),
         {}},
        // docs/34 P3.1: 写类子代理——只起草提案，不落盘（见 docs/34）。
        {QStringLiteral("coder"),
         QStringLiteral(
             "coding - you draft changes but never apply them. Work in this "
             "order: locate the relevant code, read it fully, then propose "
             "the smallest edit that achieves the goal. Every proposal goes "
             "through propose_edit with an anchor ('find') that occurs "
             "exactly once in the current file and a one-sentence rationale. "
             "Keep each proposal focused on one verifiable change; do not "
             "bundle unrelated refactors, formatting sweeps or speculative "
             "'improvements'. If an anchor does not match, re-read the file "
             "and retry - never guess. When the goal is covered, report a "
             "short summary of what you proposed and why; the primary agent "
             "applies the edits after user approval."),
         {QStringLiteral("read_file"), QStringLiteral("list_dir"),
          QStringLiteral("glob"), QStringLiteral("grep"),
          QStringLiteral("git_status"), QStringLiteral("git_diff"),
          QStringLiteral("git_show"), QStringLiteral("propose_edit")}},
    };
}

QVector<SubAgentRole> SubAgentTool::rolesFromVariant(const QVariantList& raw) {
    QVector<SubAgentRole> out;
    QSet<QString> seen;
    for (const QVariant& entry : raw) {
        const QVariantMap map = entry.toMap();
        const QString name = map.value(QStringLiteral("name")).toString().trimmed();
        const QString prompt = map.value(QStringLiteral("prompt")).toString().trimmed();
        if (name.isEmpty() || prompt.isEmpty()) continue;
        const QString nameKey = name.toLower();
        if (seen.contains(nameKey)) continue;   // first entry wins
        seen.insert(nameKey);
        SubAgentRole role;
        role.name = name;
        role.prompt = prompt;
        // docs/35 P4.6: 角色级模型配置（缺失 = 沿用活动 profile / 默认档）。
        role.profileId =
            map.value(QStringLiteral("profileId")).toString().trimmed();
        role.effort =
            map.value(QStringLiteral("effort")).toString().trimmed();
        // 角色级默认后台开关（缺失 / 非布尔 = false，旧档案自动兼容）。
        role.background = map.value(QStringLiteral("background")).toBool();
        const QVariantList tools = map.value(QStringLiteral("tools")).toList();
        for (const QVariant& t : tools) {
            const QString toolName = t.toString().trimmed();
            if (!toolName.isEmpty()) role.tools.append(toolName);
        }
        out.append(role);
    }
    return out;
}

void SubAgentTool::setUserRoles(QVector<SubAgentRole> roles) {
    QMutexLocker lock(&m_rolesMutex);
    m_userRoles = std::move(roles);
}

QVector<SubAgentRole> SubAgentTool::rolesSnapshot() const {
    QMutexLocker lock(&m_rolesMutex);
    // User entries first so a same-named user role shadows the preset.
    QVector<SubAgentRole> roles = m_userRoles;
    QVector<SubAgentRole> presets = presetRoles();
    // 设置页「内置子代理默认后台运行」：只改变预设的默认值；同名用户
    // 档案在上面且不会被预设覆盖，所以它们自己的 background 仍然优先。
    if (m_presetBackground.load()) {
        for (SubAgentRole& preset : presets) preset.background = true;
    }
    for (const SubAgentRole& preset : presets) {
        bool shadowed = false;
        for (const SubAgentRole& existing : roles) {
            if (existing.name == preset.name) { shadowed = true; break; }
        }
        if (!shadowed) roles.append(preset);
    }
    return roles;
}

bool SubAgentTool::resolveRole(const QString& roleName, SubAgentRole* out) const {
    const QVector<SubAgentRole> roles = rolesSnapshot();
    for (const SubAgentRole& role : roles) {
        if (role.name == roleName) {
            if (out) *out = role;
            return true;
        }
    }
    return false;
}

void SubAgentTool::refreshCatalog(bool localOnly) {
    QVector<SubAgentToolEntry> snapshot;
    auto& registry = ToolRegistry::instance();
    for (const auto& def : registry.listAll()) {        // Never hand the sub-agent the spawner itself (no recursive spawn).
        if (def.name == QLatin1String(SubAgentTool::kName)) continue;
        // docs/35 P4.3: 后台任务管理工具是主代理专用的会话入口，子代理
        // 不得看到（递归派生仍禁止）。
        if (def.name == QLatin1String("subagent_wait")
                || def.name == QLatin1String("subagent_status")
                || def.name == QLatin1String("subagent_cancel"))
            continue;
        // docs/32 P1 baseline: Safe + readOnly auto-executes in the
        // sub-agent (there is nobody to confirm a RequiresConfirm call)
        // and the readOnly flag keeps every mutating tool out.
        // Network retrievals: web_search/http_fetch are RequiresConfirm in
        // the primary loop (per-call privacy confirmation), but they are
        // GET-only readOnly tools and localOnlyMode=false is the user's
        // standing consent to network access — the Safe-only filter used to
        // silently drop them, leaving the sub-agent without web access.
        // localOnly mirroring follows AIAgent::toolAllowedForModel exactly:
        // web_search and mcp__* are gated, http_fetch stays available.
        const bool networkRead =
            def.name == QLatin1String("web_search")
            || def.name == QLatin1String("http_fetch");
        if (!(def.level == ToolLevel::Safe && def.readOnly) && !networkRead)
            continue;
        if (!registry.isToolUsable(def.name)) continue;
        if (localOnly && (def.name == QLatin1String("web_search")
                          || def.name.startsWith(QLatin1String("mcp__"))))
            continue;
        auto tool = registry.findShared(def.name);
        if (!tool) continue;
        snapshot.append(SubAgentToolEntry{def, std::move(tool)});
    }
    // docs/34 P3.1: propose_edit 是子代理专有能力，不进全局注册表（主代理
    // 的 schema 里不该出现它——主代理直接用 edit_file）。这里作为独立条目
    // 挂进只读目录；能否使用由角色白名单决定（见 execute 的能力门）。
    {
        auto propose = std::make_shared<ProposeEditTool>();
        snapshot.append(SubAgentToolEntry{propose->definition(), std::move(propose)});
    }
    QMutexLocker lock(&m_catalogMutex);
    m_catalog = std::move(snapshot);
}

void SubAgentTool::setMaxParallel(int n) {
    std::lock_guard<std::mutex> lock(m_gateMutex);
    m_maxParallel = qBound(1, n, kMaxParallelCeiling);
    m_gateCv.notify_all();
}

ToolDefinition SubAgentTool::definition() const {
    ToolDefinition def;
    def.name = QLatin1String(kName);
    def.description = QStringLiteral(
        "Spawn a read-only sub-agent to work on an independent sub-task and "
        "return its final report. The sub-agent can only use read-only tools "
        "(file reads, listings, glob/grep, git status/diff/log/show, "
        "knowledge search, web_search/http_fetch when local-only mode is "
        "off); all write operations stay with you. The "
        "sub-agent cannot ask you questions — the goal must be "
        "self-contained, and its report is the only thing that comes back. "
        "Issue several spawn_agent calls in one round to run sub-tasks in "
        "parallel; keep goals focused (one coherent question each) and mind "
        "that every sub-agent consumes tokens.\n"
        "Critic loop: after you complete a batch of edits, spawn a 'review' "
        "sub-agent over that changeset (state the touched files or diff "
        "scope in 'context'; it can also run git_diff itself) and fold its "
        "findings list back into your work before declaring the task done. "
        "Verify each finding and fix the valid ones yourself - do not "
        "debate with the sub-agent, and re-review only after a substantial "
        "new batch of edits.\n"
        "Plan alignment: write the plan with todo_write, then hand each "
        "step to a sub-agent - pass the step's todo id in 'todo_id' and "
        "restate the task itself in 'goal' (the sub-agent cannot see your "
        "todo list). When the report returns, update that todo's status "
        "with todo_write.\n"
        "Drafting changes: the 'coder' role can propose edits, but its "
        "proposals are NEVER applied - its report ends with a "
        "## PROPOSALS section listing anchored find/replace pairs. Apply them "
        "yourself with apply_proposal(id) using the ids listed under the "
        "section header (each call still needs user approval); fall back to "
        "edit_file when no id is shown. Verify, and only then report success.\n"
        "Background mode: set background=true for long surveys or drafting "
        "runs whose result is not needed for your next reasoning step. The "
        "call then returns immediately with a task id; collect the report "
        "later with subagent_wait(id) (or peek with subagent_status(id)). "
        "Keep short tasks that you need right away synchronous. Some roles "
        "default to background: omitting the argument uses the role's "
        "default, and background=false forces a synchronous run.");
    def.level = ToolLevel::Safe;
    def.autoEditSafe = true;
    def.readOnly = true;   // spawns read-only work only; usable in PlanOnly
    // Imperative construction: MinGW chokes on the triple-nested braced-init
    // form for this schema.
    QJsonObject goalProp{
        {"type", QStringLiteral("string")},
        {"description", QStringLiteral(
            "Self-contained task for the sub-agent: what to find out or "
            "analyze, and in which area (paths/directories if relevant).")}};
    QJsonObject contextProp{
        {"type", QStringLiteral("string")},
        {"description", QStringLiteral(
            "Optional background the sub-agent needs: constraints, starting "
            "points, what the primary agent already knows.")}};
    // docs/33 E2: the role enum is the live roster (presets + user profiles)
    // so the model can only pick names that actually resolve. Rebuilt each
    // time AIAgent re-serializes the tool schema.
    QJsonObject roleProp{
        {"type", QStringLiteral("string")},
        {"enum", QJsonArray()}};
    {
        QJsonArray enumValues;
        const QVector<SubAgentRole> roles = rolesSnapshot();
        for (const SubAgentRole& role : roles) enumValues.append(role.name);
        roleProp["enum"] = enumValues;
        QStringList roleNames;
        for (const SubAgentRole& role : roles) roleNames.append(role.name);
        roleProp["description"] = QStringLiteral(
            "Which sub-agent role profile to run (%1). Pick the one that "
            "matches the sub-task; omit for the default (explore).")
            .arg(roleNames.join(QStringLiteral(", ")));
    }
    QJsonObject properties;
    properties.insert(QStringLiteral("goal"), goalProp);
    properties.insert(QStringLiteral("context"), contextProp);
    properties.insert(QStringLiteral("role"), roleProp);
    // docs/33 E4: 计划对齐——todo 项（todo_write 有稳定 id）与子代理运行
    // 的显式关联，审计可归因、报告头回显提醒同步状态。
    QJsonObject todoIdProp{
        {"type", QStringLiteral("string")},
        {"description", QStringLiteral(
            "Optional id of the todo item (see todo_write) this sub-task "
            "implements. Echoed back in the report header and recorded in "
            "the audit trail; when the report returns, update that item's "
            "status via todo_write. The sub-agent cannot see your todo "
            "list - restate the task itself in 'goal'.")}};
    properties.insert(QStringLiteral("todo_id"), todoIdProp);
    // docs/35 P4.3: fork/join——默认 false 保持既有同步语义。
    QJsonObject backgroundProp{
        {"type", QStringLiteral("boolean")},
        {"description", QStringLiteral(
            "If true, start the sub-agent in the background and return a task "
            "id immediately; retrieve the report later with subagent_wait(id). "
            "Use it for long tasks that do not block your next step. When "
            "omitted, the selected role's default applies (synchronous unless "
            "the role opts into background); background=false forces a "
            "synchronous run.")}};
    properties.insert(QStringLiteral("background"), backgroundProp);
    QJsonArray required{QStringLiteral("goal")};
    def.parameters = QJsonObject{
        {"type", QStringLiteral("object")},
        {"properties", properties},
        {"required", required}};
    return def;
}

QVector<SubAgentToolEntry> SubAgentTool::catalogSnapshot() const {
    QMutexLocker lock(&m_catalogMutex);
    return m_catalog;
}

SubAgentTool::SlotResult SubAgentTool::acquireSlot(
        const Cancellation& cancelled,
        const std::function<void()>& onQueued) {
    std::unique_lock<std::mutex> lock(m_gateMutex);
    bool countedQueued = false;
    // docs/43 P3-18: 排队有界——同步路径的 join 与后台任务的调度都不该
    // 因为上游挂起而无限期滞留（旧行为用户只能 stop 打断）。上限对齐
    // subagent_wait 的 kMaxWaitMs；测试可注入调小。
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(m_maxSlotWaitMs.load());
    while (true) {
        if (m_activeRuns < m_maxParallel) {
            ++m_activeRuns;
            if (countedQueued) --m_queuedRuns;
            return SlotResult::Acquired;
        }
        // docs/43 O3: queued runs are visible to gateSummary() so the primary
        // agent can see that its next spawn will only wait.
        if (!countedQueued) {
            ++m_queuedRuns;
            countedQueued = true;
        }
        // docs/35 P4.5: report the wait at the same cadence as the cancel
        // poll, so the trace node shows "queued" instead of looking stuck.
        if (onQueued) onQueued();
        // Cancel-aware queueing: bounded wait so a stop() during a long
        // queue does not strand the call.
        m_gateCv.wait_for(lock, std::chrono::milliseconds(kSlotWaitSliceMs),
                          [&]() { return m_activeRuns < m_maxParallel; });
        if (cancelled && cancelled->load()) {
            if (countedQueued) --m_queuedRuns;
            return SlotResult::Cancelled;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            if (countedQueued) --m_queuedRuns;
            return SlotResult::TimedOut;
        }
    }
}

void SubAgentTool::releaseSlot() {
    {
        std::lock_guard<std::mutex> lock(m_gateMutex);
        --m_activeRuns;
    }
    m_gateCv.notify_one();
}

QString SubAgentTool::gateSummary() const {
    std::lock_guard<std::mutex> lock(m_gateMutex);
    return QStringLiteral("slots: %1/%2 running, %3 queued")
        .arg(m_activeRuns).arg(m_maxParallel).arg(m_queuedRuns);
}

ToolResult SubAgentTool::execute(const QJsonObject& args, const Cancellation& cancelled) {
    const QString goal = args.value(QStringLiteral("goal")).toString().trimmed();
    if (goal.isEmpty())
        return ToolResult::fail(QStringLiteral("'goal' is required and must not be empty."));
    if (cancelled && cancelled->load())
        return ToolResult::fail(QStringLiteral(
            "Sub-agent did not complete the goal. Reason: Sub-agent cancelled before start."));

    if (!m_factory)
        return ToolResult::fail(QStringLiteral(
            "Sub-agent is not wired to a model service (internal configuration "
            "error)."));

    QVector<SubAgentToolEntry> catalog = catalogSnapshot();
    if (catalog.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "No read-only tools are available for the sub-agent (all disabled "
            "or gated)."));

    // docs/33 E2: resolve the role profile. "role" is the current argument;
    // "hint" stays as the legacy fallback. An empty name = default (explore),
    // resolved like an explicit role so a user shadow of "explore" (tools,
    // model profile, default background) also applies when role is omitted.
    const QString requestedRole = [&args]() {
        const QString role = args.value(QStringLiteral("role")).toString().trimmed();
        if (!role.isEmpty()) return role;
        return args.value(QStringLiteral("hint")).toString().trimmed();
    }();
    const QString roleName = requestedRole.isEmpty()
        ? QStringLiteral("explore") : requestedRole;
    SubAgentRole resolved;
    QStringList roleNames;
    for (const SubAgentRole& r : rolesSnapshot()) roleNames.append(r.name);
    if (!resolveRole(roleName, &resolved))
        return ToolResult::fail(QStringLiteral(
            "Unknown role '%1'. Available roles: %2")
            .arg(roleName, roleNames.join(QStringLiteral(", "))));
    // Role tool whitelist: intersect with the read-only catalog so a stale
    // profile entry can never widen the sub-agent's tool set.
    if (!resolved.tools.isEmpty()) {
        const QSet<QString> whitelist(resolved.tools.cbegin(), resolved.tools.cend());
        QVector<SubAgentToolEntry> filtered;
        for (const SubAgentToolEntry& entry : catalog) {
            if (whitelist.contains(entry.def.name)) filtered.append(entry);
        }
        if (filtered.isEmpty())
            return ToolResult::fail(QStringLiteral(
                "Role '%1' whitelists none of the currently available "
                "read-only tools.").arg(resolved.name));
        catalog = std::move(filtered);
    }
    // docs/34 P3.1: propose_edit 是一项能力，不是普通读工具。白名单为空的
    // 角色（explore/research/review）语义是「整个只读目录」，不能让它们
    // 顺带获得提案权——只有显式点名 propose_edit 的角色才保留它。
    if (!resolved.tools.contains(QLatin1String(ProposeEditTool::kName))) {
        catalog.erase(
            std::remove_if(catalog.begin(), catalog.end(),
                           [](const SubAgentToolEntry& entry) {
                               return entry.def.name
                                   == QLatin1String(ProposeEditTool::kName);
                           }),
            catalog.end());
    }

    // docs/35 P4.6: 上下文窗口跟着角色选中的 profile 走；没有解析器时用
    // 固定值（测试/旧宿主）。解析器在主线程外也被调用，main.cpp 的实现只读
    // 不可变快照。
    const int contextLimit = m_contextLimitResolver
        ? m_contextLimitResolver(resolved.profileId)
        : m_contextLimit.load();

    // docs/35 P4.3: background fork/join. The ack is the valid tool result for
    // this call; the run is registered in the session hub and continues after
    // the primary loop moves on (and even after the task ends).
    // 角色默认后台：只在模型省略 background 时生效；显式 false 强制同步。
    const bool background = args.contains(QStringLiteral("background"))
        ? args.value(QStringLiteral("background")).toBool()
        : resolved.background;
    if (background) {
        const std::shared_ptr<BackgroundTaskHub> hub =
            BackgroundTaskHubRouter::current();
        if (!hub)
            return ToolResult::fail(QStringLiteral(
                "Background sub-agents are only available inside an agent "
                "session."));
        const auto bgCancelled = std::make_shared<std::atomic<bool>>(false);
        SubAgentRequest bgRequest;
        bgRequest.goal = goal;
        bgRequest.context = args.value(QStringLiteral("context")).toString();
        bgRequest.hint = args.value(QStringLiteral("hint")).toString();
        bgRequest.rolePrompt = resolved.prompt;
        bgRequest.tools = catalog;
        bgRequest.maxSteps = m_maxSteps.load();
        bgRequest.timeoutMs = m_timeoutMs.load();
        bgRequest.contextLimit = contextLimit;
        bgRequest.cancelled = bgCancelled;
        // docs/43 O1-T2: the hub work runs on ANOTHER pool thread where the
        // rules router is not installed, so snapshot it here (dispatch thread).
        bgRequest.projectRules = SubAgentRulesRouter::current();
        const ServiceFactory factory = m_factory;
        const QString roleProfileId = resolved.profileId;
        const QString roleEffort = resolved.effort;
        const QString roleLabel = resolved.name.isEmpty()
            ? QStringLiteral("explore") : resolved.name;
        // docs/43 O4 审计收口（P1-1）: 把解析后的 background 决策固化进
        // 重试快照——重试重放的是「当时」的调度决策，而非按当前设置/
        // 角色表重解析。否则设置变更（presetBackground 关闭、角色编辑）
        // 后一键重试会退化为同步执行：整个子代理在池线程同步跑完，结果
        // 被 retryBackgroundSubAgent 丢弃，不进 hub/面板/trace，不可观测
        // 也不可取消（显式 false 的调用不进此分支，固化 true 无歧义）。
        QJsonObject pinnedSpawnArgs = args;
        pinnedSpawnArgs.insert(QStringLiteral("background"), true);
        const QString taskId = hub->createTask(
            goal, roleLabel, bgCancelled,
            [this, bgRequest, factory, roleProfileId, roleEffort,
             bgCancelled, hub](const QString& id) mutable {
                SubAgentReport report;
                if (bgCancelled->load()) {
                    report.stop = SubAgentReport::Stop::Cancelled;
                    // B7: 任务在预启动阶段被取消，没有真正发起 attempt。
                    report.attempt = 0;
                    report.text = QStringLiteral(
                        "Sub-agent cancelled before start.");
                } else {
                    const SlotResult slot =
                        acquireSlot(bgCancelled, [hub, id]() {
                            // P4.5：后台任务的「等待并发槽位」也要上屏。
                            // 同步路径由 AIAgent 的线程局部进度钩子负责；
                            // 后台路径没有钩子，必须显式写 hub，否则面板
                            // 会把等待中的任务误报成运行中。
                            hub->updateProgress(
                                id, QStringLiteral("queued"),
                                QStringLiteral(
                                    "waiting for a free sub-agent slot"),
                                0, 0);
                        });
                    if (slot == SlotResult::Cancelled) {
                        report.stop = SubAgentReport::Stop::Cancelled;
                        // B7: 等槽位时用户取消，未开始 attempt。
                        report.attempt = 0;
                        report.text = QStringLiteral(
                            "Sub-agent cancelled while waiting for a free "
                            "sub-agent slot.");
                    } else if (slot == SlotResult::TimedOut) {
                        // docs/43 P3-18: 排队超时——不自动重试（槽位压力
                        // 是环境态，模型自己决定等/取消/稍后再试）。
                        report.stop = SubAgentReport::Stop::TimedOut;
                        report.stopCode =
                            QStringLiteral("slot_timeout");
                        // B7: 槽位超时同样没有运行 attempt。
                        report.attempt = 0;
                        report.text = QStringLiteral(
                            "Timed out after %1 ms waiting for a free "
                            "sub-agent slot. Wait for running sub-agents to "
                            "finish, cancel one, or retry later.")
                            .arg(m_maxSlotWaitMs.load());
                    } else {
                        bgRequest.progress =
                            [hub, id](const SubAgentProgress& p) {
                                hub->updateProgress(id, p.phase, p.detail,
                                                    p.step, p.maxSteps);
                            };
                        // docs/43 O1-T3: one automatic re-run on transient
                        // errors.
                        // B4: 跨 attempt 的 usage 汇总必须区分「部分上报」：
                        // reported 保持 OR（任一 attempt 上报即可展示分类），
                        // 同时记录 reportedCount/attemptCount 供 UI 标注。
                        int reportedAttempts = 0;
                        int startedAttempts = 0;
                        for (int attempt = 1;
                             attempt <= kSubAgentMaxAttempts; ++attempt) {
                            if (bgCancelled->load()) {
                                report.stop =
                                    SubAgentReport::Stop::Cancelled;
                                report.text = QStringLiteral(
                                    "Sub-agent cancelled before attempt %1.")
                                                      .arg(attempt);
                                break;
                            }
                            IAIService* service = factory
                                ? factory(roleProfileId, roleEffort)
                                : nullptr;
                            SubAgentReport attemptReport =
                                SubAgentRunner::run(service, bgRequest);
                            delete service;
                            // B7: attempt==0 表示 Runner 早退，没有真正发起
                            // 一次尝试，不计入分母。reported 只统计真正上报
                            // 分类用量的 attempt。
                            if (attemptReport.attempt != 0)
                                ++startedAttempts;
                            if (attemptReport.usage.usageReported)
                                ++reportedAttempts;
                            if (attempt > 1) {
                                attemptReport.steps += report.steps;
                                attemptReport.tokensUsed
                                    += report.tokensUsed;
                                // docs/43 O4 审计收口（P1-2）: 分类用量必须与
                                // steps/tokensUsed 同样跨 attempt 累计；否则被
                                // 末次 attempt 覆盖，input+output ≠ tokensUsed，
                                // 成本芯片与审计 subagent_usage 双通道失真。
                                attemptReport.usage.inputTokens
                                    += report.usage.inputTokens;
                                attemptReport.usage.outputTokens
                                    += report.usage.outputTokens;
                                attemptReport.usage.cachedTokens
                                    += report.usage.cachedTokens;
                                attemptReport.usage.cacheReadTokens
                                    += report.usage.cacheReadTokens;
                                attemptReport.usage.cacheCreationTokens
                                    += report.usage.cacheCreationTokens;
                                attemptReport.usage.usageReported
                                    = attemptReport.usage.usageReported
                                      || report.usage.usageReported;
                                // B9: 任一 attempt 使用估算回退时，聚合结果
                                // 仍标记 estimated（tokensUsed 里含估算部分）。
                                attemptReport.usage.estimated
                                    = attemptReport.usage.estimated
                                      || report.usage.estimated;
                            }
                            // B4/B7: 汇总计数写回本次结果，再整体替换 report。
                            // attempts 只数真正跑过的 attempt；Runner 早退
                            // （attempt=0）时保持 0（UI 显示「未开始」）。
                            attemptReport.attempt = startedAttempts;
                            attemptReport.usage.reportedCount =
                                reportedAttempts;
                            attemptReport.usage.attemptCount =
                                startedAttempts;
                            attemptReport.usage.partial =
                                reportedAttempts > 0
                                && reportedAttempts < startedAttempts;
                            report = attemptReport;
                            if (report.ok || !report.retryable
                                    || bgCancelled->load())
                                break;
                            if (attempt >= kSubAgentMaxAttempts) break;
                            bgRequest.context += QStringLiteral(
                                "\nPrevious attempt failed (%1); avoid the "
                                "same failure and try a different approach "
                                "if needed.")
                                .arg(report.text.left(300));
                            hub->updateProgress(
                                id, QStringLiteral("retry"),
                                QStringLiteral(
                                    "attempt %1/%2 after a transient failure")
                                    .arg(attempt + 1)
                                    .arg(kSubAgentMaxAttempts),
                                0, 0);
                        }
                        report.usage.profileId = roleProfileId;
                        releaseSlot();
                    }
                }
                hub->finishTask(id, report);
            },
            // docs/43 O4-2: keep the original arguments for one-click retry
            // (sessionId stays router-derived as before). The pinned copy
            // carries the resolved background decision (see P1-1 above).
            {}, pinnedSpawnArgs);
        if (taskId.isEmpty())
            return ToolResult::fail(QStringLiteral(
                "Too many background sub-agents (%1 max). Wait for one to "
                "finish or cancel it with subagent_cancel(id).")
                .arg(BackgroundTaskHub::kMaxTasks));
        return ToolResult::ok(QStringLiteral(
            "[Sub-agent started] id=%1. Collect with subagent_wait(id) when "
            "needed; subagent_status(id) reports progress.\n%2")
            .arg(taskId, gateSummary()));
    }

    // docs/32 P2: the dispatcher installs a thread-local progress hook around
    // execute(); empty (no-op) when spawned outside an agent dispatch (tests).
    // Fetched before queueing so P4.5 can report the wait.
    const SubAgentProgressHook progressHook = SubAgentProgressRouter::current();
    const auto onQueued = [&progressHook]() {
        if (progressHook)
            progressHook(SubAgentProgress{0, 0, QStringLiteral("queued"),
                                          QStringLiteral("waiting for a free slot")});
    };
    // docs/43 P3-18: TimedOut 与用户取消分开——排队本身放弃了，报可读
    // 错误让模型决定等运行中的子代理结束、取消一个、或稍后再试。
    const SlotResult slot = acquireSlot(cancelled, onQueued);
    if (slot == SlotResult::Cancelled) {
        return ToolResult::fail(QStringLiteral(
            "Sub-agent did not complete the goal. Reason: Sub-agent cancelled "
            "while waiting for a free sub-agent slot."));
    }
    if (slot == SlotResult::TimedOut) {
        return ToolResult::fail(QStringLiteral(
            "Sub-agent did not complete the goal. Reason: timed out after "
            "%1 ms waiting for a free sub-agent slot (all slots busy). Wait "
            "for running sub-agents to finish, cancel one, or retry later.")
            .arg(m_maxSlotWaitMs.load()));
    }

    SubAgentRequest request;
    request.goal = goal;
    request.context = args.value(QStringLiteral("context")).toString();
    request.hint = args.value(QStringLiteral("hint")).toString();
    request.rolePrompt = resolved.prompt;
    request.tools = catalog;
    request.maxSteps = m_maxSteps.load();
    request.timeoutMs = m_timeoutMs.load();
    request.contextLimit = contextLimit;
    request.cancelled = cancelled;
    request.progress = progressHook;
    // docs/43 O1-T2: workspace rules follow the same thread-local hand-off as
    // the progress hook (installed by AIAgent around this execute()).
    request.projectRules = SubAgentRulesRouter::current();

    // docs/43 O1-T3: transient failures (service/network error, timeout, empty
    // response) get one automatic re-run. A cancel never retries; Budget
    // failures do not either (the model itself failed to converge).
    SubAgentReport report;
    // B4: 与后台路径同口径——reported 取 OR，另记 reportedCount/attemptCount
    // 供 UI 标注「部分上报」。
    int reportedAttempts = 0;
    int startedAttempts = 0;
    for (int attempt = 1; attempt <= kSubAgentMaxAttempts; ++attempt) {
        IAIService* service = m_factory
            ? m_factory(resolved.profileId, resolved.effort) : nullptr;
        SubAgentReport attemptReport = SubAgentRunner::run(service, request);
        // The factory hands over ownership; the service was created and used
        // on this thread, so a plain delete here is safe.
        delete service;
        // B7: attempt==0 表示 Runner 早退，没有真正发起一次尝试。
        if (attemptReport.attempt != 0) ++startedAttempts;
        if (attemptReport.usage.usageReported) ++reportedAttempts;
        if (attempt > 1) {
            attemptReport.steps += report.steps;
            attemptReport.tokensUsed += report.tokensUsed;
            // docs/43 O4 审计收口（P1-2）: 与后台路径同口径——分类用量跨
            // attempt 累计，reported 取 OR（部分上报也是真实数据）。
            attemptReport.usage.inputTokens += report.usage.inputTokens;
            attemptReport.usage.outputTokens += report.usage.outputTokens;
            attemptReport.usage.cachedTokens += report.usage.cachedTokens;
            attemptReport.usage.cacheReadTokens += report.usage.cacheReadTokens;
            attemptReport.usage.cacheCreationTokens +=
                report.usage.cacheCreationTokens;
            attemptReport.usage.usageReported =
                attemptReport.usage.usageReported
                || report.usage.usageReported;
            // B9: 同后台路径——聚合里含估算回退时保留 estimated。
            attemptReport.usage.estimated =
                attemptReport.usage.estimated || report.usage.estimated;
        }
        // B4/B7: 汇总计数写回本次结果，attempts 只数真正跑过的尝试。
        attemptReport.attempt = startedAttempts;
        attemptReport.usage.reportedCount = reportedAttempts;
        attemptReport.usage.attemptCount = startedAttempts;
        attemptReport.usage.partial =
            reportedAttempts > 0 && reportedAttempts < startedAttempts;
        report = attemptReport;
        if (report.ok || !report.retryable) break;
        if (cancelled && cancelled->load()) break;
        if (attempt >= kSubAgentMaxAttempts) break;
        request.context += QStringLiteral(
            "\nPrevious attempt failed (%1); avoid the same failure and try a "
            "different approach if needed.").arg(report.text.left(300));
        if (progressHook) {
            progressHook(SubAgentProgress{0, 0, QStringLiteral("retry"),
                QStringLiteral("attempt %1/%2 after a transient failure")
                    .arg(attempt + 1).arg(kSubAgentMaxAttempts)});
        }
    }
    releaseSlot();

    report.usage.profileId = resolved.profileId;

    // docs/33 E4: 计划对齐回显——提醒模型按报告同步该 todo 项的状态
    // （追加在 tokens 之后，stats 前缀形态不变）。
    const QString todoId =
        args.value(QStringLiteral("todo_id")).toString().trimmed();
    ToolResult result = report.ok
        ? ToolResult::ok(reportOutput(report, todoId))
        : ToolResult::fail(reportOutput(report, todoId));
    // docs/43 O2: structured report rides alongside the human-readable text.
    result.metadata.insert(QStringLiteral("subagentReport"), reportJson(report));
    // docs/34 P3.2 / docs/35 P4.4: 提案随结果回传。trace 只持久化轻量索引；
    // 完整提案由 AIAgent 送进会话注册表（transient）。
    if (!report.proposals.isEmpty()) {
        result.metadata.insert(QStringLiteral("subagentProposals"),
                               proposalIndex(report));
        result.metadata.insert(QStringLiteral("subagentProposalData"),
                               proposalData(report));
    }
    return result;
}

QString SubAgentTool::reportOutput(const SubAgentReport& report,
                                   const QString& todoId) {
    QString stats = QStringLiteral("steps: %1, tokens: %2")
                        .arg(report.steps).arg(report.tokensUsed);
    // docs/43 O1-T3: after an automatic retry the steps/tokens above are the
    // accumulated cost; echo the attempt count so the primary agent knows.
    if (report.attempt > 1)
        stats += QStringLiteral(", attempts: %1").arg(report.attempt);
    // docs/33 E1: full transcript path for on-demand file_read; prefix keeps
    // the audit-side stats regex (steps/tokens) matching.
    if (!report.artifactPath.isEmpty())
        stats += QStringLiteral(", evidence: %1").arg(report.artifactPath);
    if (!todoId.isEmpty())
        stats += QStringLiteral(", todo: %1").arg(todoId);
    return report.ok
        ? QStringLiteral("[Sub-agent report] (%1)\n\n%2").arg(stats, report.text)
        : QStringLiteral("Sub-agent did not complete the goal (%1). Reason: %2")
              .arg(stats, report.text);
}

// docs/43 O2: machine-readable shape. Kept small (findings capped by the
// runner at 50) because it also rides in the trace/audit path.
QJsonObject SubAgentTool::reportJson(const SubAgentReport& report) {
    QJsonObject usage{
        {QStringLiteral("model"), report.usage.model},
        {QStringLiteral("profileId"), report.usage.profileId},
        {QStringLiteral("input"), report.usage.inputTokens},
        {QStringLiteral("output"), report.usage.outputTokens},
        {QStringLiteral("cached"), report.usage.cachedTokens},
        {QStringLiteral("cacheRead"), report.usage.cacheReadTokens},
        {QStringLiteral("cacheCreation"), report.usage.cacheCreationTokens},
        {QStringLiteral("reported"), report.usage.usageReported},
        // B9: 无 usage 时的估算回退标记，面板/过程卡片据此显示「≈」。
        {QStringLiteral("estimated"), report.usage.estimated},
        // B4: 部分上报标记——reported 仍为 OR，UI 据此追加「部分」提示。
        {QStringLiteral("reportedCount"), report.usage.reportedCount},
        {QStringLiteral("attemptCount"), report.usage.attemptCount},
        {QStringLiteral("partial"), report.usage.partial}};
    QJsonArray findings;

    for (const SubAgentFinding& finding : report.findings) {
        findings.append(QJsonObject{
            {QStringLiteral("severity"), finding.severity},
            {QStringLiteral("path"), finding.path},
            {QStringLiteral("line"), finding.line},
            {QStringLiteral("message"), finding.message}});
    }
    return QJsonObject{
        {QStringLiteral("ok"), report.ok},
        {QStringLiteral("stop"), static_cast<int>(report.stop)},
        {QStringLiteral("stopCode"), report.stopCode},
        {QStringLiteral("retryable"), report.retryable},
        {QStringLiteral("attempts"), report.attempt},
        {QStringLiteral("steps"), report.steps},
        {QStringLiteral("tokensUsed"), report.tokensUsed},
        {QStringLiteral("usage"), usage},
        {QStringLiteral("findings"), findings},
        {QStringLiteral("proposalCount"), report.proposals.size()},
        {QStringLiteral("artifactPath"), report.artifactPath}};
}

QJsonArray SubAgentTool::proposalIndex(const SubAgentReport& report) {
    const QString root = Sandbox::currentProjectDir();
    QJsonArray index;
    for (const SubAgentProposal& proposal : report.proposals) {
        QString display = proposal.path;
        if (!root.isEmpty()) {
            const QString relative = QDir(root).relativeFilePath(proposal.path);
            if (!relative.startsWith(QLatin1String(".."))) display = relative;
        }
        index.append(QJsonObject{
            {QStringLiteral("path"), display},
            {QStringLiteral("rationale"), proposal.rationale},
            {QStringLiteral("count"), proposal.edits.size()}});
    }
    return index;
}

QJsonArray SubAgentTool::proposalData(const SubAgentReport& report) {
    const QString root = Sandbox::currentProjectDir();
    QJsonArray full;
    for (const SubAgentProposal& proposal : report.proposals) {
        QString display = proposal.path;
        if (!root.isEmpty()) {
            const QString relative = QDir(root).relativeFilePath(proposal.path);
            if (!relative.startsWith(QLatin1String(".."))) display = relative;
        }
        QJsonArray edits;
        for (const SubAgentProposalEdit& edit : proposal.edits)
            edits.append(QJsonObject{{QStringLiteral("find"), edit.find},
                                     {QStringLiteral("replace"), edit.replace}});
        full.append(QJsonObject{
            {QStringLiteral("path"), proposal.path},
            {QStringLiteral("displayPath"), display},
            {QStringLiteral("rationale"), proposal.rationale},
            {QStringLiteral("edits"), edits}});
    }
    return full;
}
} // namespace NexusAI
