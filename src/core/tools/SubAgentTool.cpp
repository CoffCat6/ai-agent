#include "SubAgentTool.h"

#include "core/agent/SubAgentRunner.h"
#include "core/tools/ToolRegistry.h"
#include "infrastructure/llm/IAIService.h"

#include <QJsonObject>
#include <QSet>
#include <QStringList>

namespace NexusAI {

namespace {
constexpr int kMaxParallelCeiling = 8;
constexpr int kSlotWaitSliceMs = 250;   // cancellation granularity while queued
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
    const QVector<SubAgentRole> presets = presetRoles();
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
    for (const auto& def : registry.listAll()) {
        // Read-only workspace exploration only: Safe level auto-executes in
        // the sub-agent (there is nobody to confirm a RequiresConfirm call)
        // and the readOnly flag keeps every mutating tool out.
        if (def.level != ToolLevel::Safe || !def.readOnly) continue;
        // Never hand the sub-agent the spawner itself (no recursive spawn).
        if (def.name == QLatin1String(SubAgentTool::kName)) continue;
        if (!registry.isToolUsable(def.name)) continue;
        // Mirror AIAgent::toolAllowedForModel's local-only gate for network
        // tools (MCP tools are never readOnly today; the check costs nothing).
        if (localOnly && (def.name == QLatin1String("web_search")
                          || def.name == QLatin1String("http_fetch")
                          || def.name.startsWith(QLatin1String("mcp__"))))
            continue;
        auto tool = registry.findShared(def.name);
        if (!tool) continue;
        snapshot.append(SubAgentToolEntry{def, std::move(tool)});
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
        "knowledge/web search); all write operations stay with you. The "
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
        "with todo_write.");
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

bool SubAgentTool::acquireSlot(const Cancellation& cancelled) {
    std::unique_lock<std::mutex> lock(m_gateMutex);
    while (true) {
        if (m_activeRuns < m_maxParallel) {
            ++m_activeRuns;
            return true;
        }
        // Cancel-aware queueing: bounded wait so a stop() during a long
        // queue does not strand the call.
        m_gateCv.wait_for(lock, std::chrono::milliseconds(kSlotWaitSliceMs),
                          [&]() { return m_activeRuns < m_maxParallel; });
        if (cancelled && cancelled->load()) return false;
    }
}

void SubAgentTool::releaseSlot() {
    {
        std::lock_guard<std::mutex> lock(m_gateMutex);
        --m_activeRuns;
    }
    m_gateCv.notify_one();
}

ToolResult SubAgentTool::execute(const QJsonObject& args, const Cancellation& cancelled) {
    const QString goal = args.value(QStringLiteral("goal")).toString().trimmed();
    if (goal.isEmpty())
        return ToolResult::fail(QStringLiteral("'goal' is required and must not be empty."));
    if (cancelled && cancelled->load())
        return ToolResult::fail(QStringLiteral("Cancelled before start."));

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
    // "hint" stays as the legacy fallback. An empty name = default (explore).
    const QString roleName = [&args]() {
        const QString role = args.value(QStringLiteral("role")).toString().trimmed();
        if (!role.isEmpty()) return role;
        return args.value(QStringLiteral("hint")).toString().trimmed();
    }();
    SubAgentRole resolved;
    QStringList roleNames;
    for (const SubAgentRole& r : rolesSnapshot()) roleNames.append(r.name);
    if (!roleName.isEmpty()) {
        if (!resolveRole(roleName, &resolved))
            return ToolResult::fail(QStringLiteral(
                "Unknown role '%1'. Available roles: %2")
                .arg(roleName, roleNames.join(QStringLiteral(", "))));
    }
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

    if (!acquireSlot(cancelled))
        return ToolResult::fail(QStringLiteral(
            "Cancelled while waiting for a free sub-agent slot."));

    SubAgentRequest request;
    request.goal = goal;
    request.context = args.value(QStringLiteral("context")).toString();
    request.hint = args.value(QStringLiteral("hint")).toString();
    request.rolePrompt = resolved.prompt;
    request.tools = catalog;
    request.maxSteps = m_maxSteps.load();
    request.timeoutMs = m_timeoutMs.load();
    request.cancelled = cancelled;
    // docs/32 P2: the dispatcher installs a thread-local progress hook around
    // execute(); empty (no-op) when spawned outside an agent dispatch (tests).
    request.progress = SubAgentProgressRouter::current();

    IAIService* service = m_factory();
    SubAgentReport report = SubAgentRunner::run(service, request);
    // The factory hands over ownership; the service was created and used on
    // this thread, so a plain delete here is safe.
    delete service;
    releaseSlot();

    QString stats = QStringLiteral("steps: %1, tokens: %2")
                        .arg(report.steps).arg(report.tokensUsed);
    // docs/33 E1: full transcript path for on-demand file_read; prefix keeps
    // the audit-side stats regex (steps/tokens) matching.
    if (!report.artifactPath.isEmpty())
        stats += QStringLiteral(", evidence: %1").arg(report.artifactPath);
    // docs/33 E4: 计划对齐回显——提醒模型按报告同步该 todo 项的状态
    // （追加在 tokens 之后，stats 前缀形态不变）。
    const QString todoId =
        args.value(QStringLiteral("todo_id")).toString().trimmed();
    if (!todoId.isEmpty())
        stats += QStringLiteral(", todo: %1").arg(todoId);
    if (report.ok)
        return ToolResult::ok(QStringLiteral(
            "[Sub-agent report] (%1)\n\n%2").arg(stats, report.text));
    return ToolResult::fail(QStringLiteral(
        "Sub-agent did not complete the goal (%1). Reason: %2")
        .arg(stats, report.text));
}

} // namespace NexusAI
