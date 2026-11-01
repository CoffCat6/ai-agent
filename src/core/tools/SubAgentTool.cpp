#include "SubAgentTool.h"

#include "core/agent/SubAgentRunner.h"
#include "core/tools/ToolRegistry.h"
#include "infrastructure/llm/IAIService.h"

#include <QJsonObject>

namespace NexusAI {

namespace {
constexpr int kMaxParallelCeiling = 8;
constexpr int kSlotWaitSliceMs = 250;   // cancellation granularity while queued
} // namespace

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
        "that every sub-agent consumes tokens.");
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
    QJsonObject hintProp{
        {"type", QStringLiteral("string")},
        {"enum", QJsonArray{QStringLiteral("explore"), QStringLiteral("research")}},
        {"description", QStringLiteral(
            "explore (default): breadth-first survey. research: depth-first "
            "verification of a specific question.")}};
    QJsonObject properties;
    properties.insert(QStringLiteral("goal"), goalProp);
    properties.insert(QStringLiteral("context"), contextProp);
    properties.insert(QStringLiteral("hint"), hintProp);
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

    const QVector<SubAgentToolEntry> catalog = catalogSnapshot();
    if (catalog.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "No read-only tools are available for the sub-agent (all disabled "
            "or gated)."));

    if (!acquireSlot(cancelled))
        return ToolResult::fail(QStringLiteral(
            "Cancelled while waiting for a free sub-agent slot."));

    SubAgentRequest request;
    request.goal = goal;
    request.context = args.value(QStringLiteral("context")).toString();
    request.hint = args.value(QStringLiteral("hint")).toString();
    request.tools = catalog;
    request.maxSteps = m_maxSteps.load();
    request.timeoutMs = m_timeoutMs.load();
    request.cancelled = cancelled;

    IAIService* service = m_factory();
    SubAgentReport report = SubAgentRunner::run(service, request);
    // The factory hands over ownership; the service was created and used on
    // this thread, so a plain delete here is safe.
    delete service;
    releaseSlot();

    const QString stats = QStringLiteral("steps: %1, tokens: %2")
                              .arg(report.steps).arg(report.tokensUsed);
    if (report.ok)
        return ToolResult::ok(QStringLiteral(
            "[Sub-agent report] (%1)\n\n%2").arg(stats, report.text));
    return ToolResult::fail(QStringLiteral(
        "Sub-agent did not complete the goal (%1). Reason: %2")
        .arg(stats, report.text));
}

} // namespace NexusAI
