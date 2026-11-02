#include "SubAgentControlTool.h"

#include "SubAgentTool.h"
#include "core/agent/BackgroundTaskHub.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>

namespace NexusAI {

namespace {

std::shared_ptr<BackgroundTaskHub> currentHub() {
    return BackgroundTaskHubRouter::current();
}

QString taskIdParamDescription() {
    return QStringLiteral(
        "Background task id from the spawn_agent ack "
        "(shape: sa-<n>).");
}

QString unknownTask(const QString& id) {
    return QStringLiteral(
        "Unknown background sub-agent '%1'. Use the id from the spawn_agent "
        "ack; background tasks are session-scoped.").arg(id);
}

QJsonObject idParam() {
    QJsonObject param;
    param["type"] = QLatin1String("string");
    param["description"] = taskIdParamDescription();
    return param;
}

QJsonObject schemaWithId() {
    QJsonObject props;
    props.insert(QStringLiteral("id"), idParam());
    return QJsonObject{
        {"type", QLatin1String("object")},
        {"properties", props},
        {"required", QJsonArray{QStringLiteral("id")}}};
}

QString stateDescription(const QString& state) {
    if (state == QLatin1String("queued")) return QStringLiteral("queued");
    if (state == QLatin1String("running")) return QStringLiteral("running");
    if (state == QLatin1String("completed")) return QStringLiteral("completed");
    if (state == QLatin1String("failed")) return QStringLiteral("failed");
    if (state == QLatin1String("cancelled")) return QStringLiteral("cancelled");
    return state;
}

} // namespace

// ── subagent_wait ────────────────────────────────────────

ToolDefinition SubAgentWaitTool::definition() const {
    ToolDefinition def;
    def.name = QLatin1String(kName);
    def.description = QStringLiteral(
        "Wait for a background sub-agent (spawn_agent with background=true) "
        "to finish, then return its report. A timeout is not a failure: the "
        "result says the task is still running and can be waited on again. "
        "Use subagent_status for a non-blocking peek.");
    def.level = ToolLevel::Safe;
    def.readOnly = true;
    QJsonObject props;
    props.insert(QStringLiteral("id"), idParam());
    QJsonObject timeoutParam{
        {"type", QLatin1String("integer")},
        {"description", QStringLiteral(
            "Maximum time to block in milliseconds (default 60000, max "
            "600000). A shorter wait returns state=running while the task "
            "keeps going.")}};
    props.insert(QStringLiteral("timeout_ms"), timeoutParam);
    def.parameters = QJsonObject{
        {"type", QLatin1String("object")},
        {"properties", props},
        {"required", QJsonArray{QStringLiteral("id")}}};
    return def;
}

ToolResult SubAgentWaitTool::execute(const QJsonObject& args,
                                     const Cancellation& cancelled) {
    const std::shared_ptr<BackgroundTaskHub> hub = currentHub();
    if (!hub)
        return ToolResult::fail(QStringLiteral(
            "subagent_wait is only available inside an agent session."));
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "'id' is required (use the id from the spawn_agent ack)."));
    if (!hub->hasTask(id)) return ToolResult::fail(unknownTask(id));

    int timeoutMs = args.value(QStringLiteral("timeout_ms")).toInt(
        BackgroundTaskHub::kDefaultWaitMs);
    timeoutMs = qBound(0, timeoutMs, BackgroundTaskHub::kMaxWaitMs);
    if (timeoutMs == 0) timeoutMs = BackgroundTaskHub::kDefaultWaitMs;

    const QString state = hub->waitFor(id, timeoutMs, cancelled);
    if (state == QLatin1String("missing"))
        return ToolResult::fail(unknownTask(id));
    if (state == QLatin1String("queued") || state == QLatin1String("running")) {
        return ToolResult::ok(QStringLiteral(
            "Sub-agent %1 is still %2 after %3 ms. It keeps running in the "
            "background; call subagent_wait again when you need the report.")
            .arg(id, stateDescription(state)).arg(timeoutMs));
    }

    const auto report = hub->reportFor(id);
    if (!report)
        return ToolResult::fail(QStringLiteral(
            "Background sub-agent %1 finished without a stored report (internal "
            "error).").arg(id));
    ToolResult result = report->ok ? ToolResult::ok(SubAgentTool::reportOutput(*report))
                                   : ToolResult::fail(SubAgentTool::reportOutput(*report));
    if (!report->proposals.isEmpty()) {
        result.metadata.insert(QStringLiteral("subagentProposals"),
                               SubAgentTool::proposalIndex(*report));
        result.metadata.insert(QStringLiteral("subagentProposalData"),
                               SubAgentTool::proposalData(*report));
    }
    return result;
}

ToolResult SubAgentWaitTool::execute(const QJsonObject& args) {
    return execute(args, {});
}

// ── subagent_status ──────────────────────────────────────

ToolDefinition SubAgentStatusTool::definition() const {
    ToolDefinition def;
    def.name = QLatin1String(kName);
    def.description = QStringLiteral(
        "Non-blocking status of a background sub-agent: state, role, elapsed "
        "time, steps, proposals and the latest progress lines. Use it to "
        "decide whether to wait now or later.");
    def.level = ToolLevel::Safe;
    def.readOnly = true;
    def.parameters = schemaWithId();
    return def;
}

ToolResult SubAgentStatusTool::execute(const QJsonObject& args) {
    const std::shared_ptr<BackgroundTaskHub> hub = currentHub();
    if (!hub)
        return ToolResult::fail(QStringLiteral(
            "subagent_status is only available inside an agent session."));
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "'id' is required (use the id from the spawn_agent ack)."));
    if (!hub->hasTask(id)) return ToolResult::fail(unknownTask(id));

    for (const QVariant& entry : hub->snapshots()) {
        const QVariantMap task = entry.toMap();
        if (task.value(QStringLiteral("id")).toString() != id) continue;
        QString text = QStringLiteral(
            "id: %1\nstate: %2\nrole: %3\nelapsed_ms: %4\nsteps: %5\n"
            "proposals: %6\ngoal: %7")
            .arg(id,
                 stateDescription(task.value(QStringLiteral("state")).toString()))
            .arg(task.value(QStringLiteral("role")).toString())
            .arg(task.value(QStringLiteral("elapsedMs")).toLongLong())
            .arg(task.value(QStringLiteral("steps")).toInt())
            .arg(task.value(QStringLiteral("proposalCount")).toInt())
            .arg(task.value(QStringLiteral("goal")).toString());
        const QString error = task.value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) text += QStringLiteral("\nerror: %1").arg(error);
        const QVariantList tail =
            task.value(QStringLiteral("progressTail")).toList();
        if (!tail.isEmpty()) {
            text += QStringLiteral("\nrecent:");
            for (const QVariant& line : tail) {
                const QVariantMap item = line.toMap();
                text += QStringLiteral("\n- %1: %2")
                            .arg(item.value(QStringLiteral("phase")).toString(),
                                 item.value(QStringLiteral("text")).toString());
            }
        }
        return ToolResult::ok(text);
    }
    return ToolResult::fail(unknownTask(id));
}

// ── subagent_cancel ──────────────────────────────────────

ToolDefinition SubAgentCancelTool::definition() const {
    ToolDefinition def;
    def.name = QLatin1String(kName);
    def.description = QStringLiteral(
        "Request cancellation of a queued or running background sub-agent. "
        "The call only flips its cancellation flag; use subagent_wait to "
        "collect the stopped report. Other sub-agents keep running.");
    def.level = ToolLevel::Safe;
    def.readOnly = true;
    def.parameters = schemaWithId();
    return def;
}

ToolResult SubAgentCancelTool::execute(const QJsonObject& args) {
    const std::shared_ptr<BackgroundTaskHub> hub = currentHub();
    if (!hub)
        return ToolResult::fail(QStringLiteral(
            "subagent_cancel is only available inside an agent session."));
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "'id' is required (use the id from the spawn_agent ack)."));
    if (!hub->hasTask(id)) return ToolResult::fail(unknownTask(id));
    if (!hub->cancel(id)) {
        return ToolResult::ok(QStringLiteral(
            "Sub-agent %1 is already finished (%2); nothing to cancel.")
            .arg(id, stateDescription(hub->stateLabel(id))));
    }
    return ToolResult::ok(QStringLiteral(
        "Cancellation requested for sub-agent %1. It stops at the next poll "
        "slice; collect the stopped report with subagent_wait(id).")
        .arg(id));
}

} // namespace NexusAI