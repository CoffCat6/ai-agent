#include "SubAgentControlTool.h"

#include "SubAgentTool.h"
#include "core/agent/BackgroundTaskHub.h"


#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <memory>

namespace NexusAI {

namespace {

// docs/43 O3: one batch wait covers a realistic parallel fan-out (the gate
// tops out at 8 anyway); bigger batches would only hide which tasks are stuck.
constexpr int kMaxWaitIds = 8;

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

// docs/43 O3: stable "id=state, id=state" listing in the caller's id order.
QString stateLine(const QStringList& ids,
                  const QHash<QString, QString>& states) {
    QStringList parts;
    for (const QString& id : ids)
        parts << QStringLiteral("%1=%2")
                     .arg(id, stateDescription(states.value(id)));
    return parts.join(QStringLiteral(", "));
}

} // namespace

// ── subagent_wait ────────────────────────────────────────

ToolDefinition SubAgentWaitTool::definition() const {
    ToolDefinition def;
    def.name = QLatin1String(kName);
    def.description = QStringLiteral(
        "Wait for one or more background sub-agents (spawn_agent with "
        "background=true) and return their reports. Pass 'id' or 'ids'. "
        "mode=any (default) returns as soon as ONE task reaches a terminal "
        "state; mode=all waits until every id is terminal or the timeout "
        "fires. A timeout is not a failure: unfinished tasks keep running and "
        "can be waited on again. Batch waits return reports but not proposal "
        "ids - collect a coder run with a single-id wait when you need to "
        "apply its proposals. Use subagent_status for a non-blocking peek.");
    def.level = ToolLevel::Safe;
    def.readOnly = true;
    QJsonObject props;
    props.insert(QStringLiteral("id"), idParam());
    QJsonObject idsParam{
        {"type", QLatin1String("array")},
        {"items", QJsonObject{{"type", QLatin1String("string")}}},
        {"description", QStringLiteral(
            "Batch form: up to 8 background task ids to wait on together. "
            "Ignored when 'id' is also given (both are merged, duplicates "
            "removed).")}};
    props.insert(QStringLiteral("ids"), idsParam);
    QJsonObject modeParam{
        {"type", QLatin1String("string")},
        {"enum", QJsonArray{QStringLiteral("any"), QStringLiteral("all")}},
        {"description", QStringLiteral(
            "Batch semantics: any (default) returns when the first id "
            "finishes; all returns when every id finished or the timeout "
            "fires.")}};
    props.insert(QStringLiteral("mode"), modeParam);
    QJsonObject timeoutParam{
        {"type", QLatin1String("integer")},
        {"description", QStringLiteral(
            "Maximum time to block in milliseconds (default 60000, max "
            "600000). A shorter wait returns state=running while the task "
            "keeps going.")}};
    props.insert(QStringLiteral("timeout_ms"), timeoutParam);
    def.parameters = QJsonObject{
        {"type", QLatin1String("object")},
        {"properties", props}};
    return def;
}

ToolResult SubAgentWaitTool::execute(const QJsonObject& args,
                                     const Cancellation& cancelled) {
    const std::shared_ptr<BackgroundTaskHub> hub = currentHub();
    if (!hub)
        return ToolResult::fail(QStringLiteral(
            "subagent_wait is only available inside an agent session."));

    // docs/43 O3: single id, or a batch of up to 8 ids (merged, deduplicated).
    QStringList ids;
    const QString single = args.value(QStringLiteral("id")).toString().trimmed();
    if (!single.isEmpty()) ids.append(single);
    for (const QJsonValue& value : args.value(QStringLiteral("ids")).toArray()) {
        const QString id = value.toString().trimmed();
        if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
    }
    if (ids.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "'id' or a non-empty 'ids' array is required (use the ids from "
            "the spawn_agent ack)."));
    if (ids.size() > kMaxWaitIds)
        return ToolResult::fail(QStringLiteral(
            "Too many ids in one wait (%1); pass at most %2.")
            .arg(ids.size()).arg(kMaxWaitIds));
    for (const QString& id : ids) {
        if (!hub->hasTask(id)) return ToolResult::fail(unknownTask(id));
    }

    int timeoutMs = args.value(QStringLiteral("timeout_ms")).toInt(
        BackgroundTaskHub::kDefaultWaitMs);
    timeoutMs = qBound(0, timeoutMs, BackgroundTaskHub::kMaxWaitMs);
    if (timeoutMs == 0) timeoutMs = BackgroundTaskHub::kDefaultWaitMs;

    // Single-id path keeps the original contract (report + proposal ids).
    if (ids.size() == 1) {
        const QString id = ids.first();
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
                "Background sub-agent %1 finished without a stored report "
                "(internal error).").arg(id));
        ToolResult result = report->ok
            ? ToolResult::ok(SubAgentTool::reportOutput(*report))
            : ToolResult::fail(SubAgentTool::reportOutput(*report));
        // docs/43 O2: same structured report as the synchronous spawn path.
        result.metadata.insert(QStringLiteral("subagentReport"),
                               SubAgentTool::reportJson(*report));
        if (!report->proposals.isEmpty()) {
            result.metadata.insert(QStringLiteral("subagentProposals"),
                                   SubAgentTool::proposalIndex(*report));
            result.metadata.insert(QStringLiteral("subagentProposalData"),
                                   SubAgentTool::proposalData(*report));
        }
        return result;
    }

    // Batch path: mode=any returns at the first terminal task, mode=all waits
    // for every id (or the deadline).
    const QString mode = args.value(QStringLiteral("mode")).toString().trimmed();
    const bool waitAll = mode.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0;
    const QHash<QString, QString> states =
        hub->waitForMany(ids, waitAll, timeoutMs, cancelled);

    QStringList finished;
    QStringList pending;
    for (const QString& id : ids) {
        const QString state = states.value(id);
        if (state == QLatin1String("completed")
                || state == QLatin1String("failed")
                || state == QLatin1String("cancelled"))
            finished.append(id);
        else
            pending.append(id);
    }
    const bool timedOut = waitAll ? !pending.isEmpty() : finished.isEmpty();
    if (timedOut) {
        return ToolResult::ok(QStringLiteral(
            "No requested sub-agent is ready after %1 ms (%2). They keep "
            "running; wait again or peek with subagent_status(id).")
            .arg(timeoutMs).arg(stateLine(ids, states)));
    }

    QStringList blocks;
    QJsonArray batch;
    bool anyOk = false;
    for (const QString& id : finished) {
        const auto report = hub->reportFor(id);
        if (!report) continue;
        anyOk = anyOk || report->ok;
        blocks << QStringLiteral("[%1]\n%2").arg(
            id, SubAgentTool::reportOutput(*report));
        batch.append(SubAgentTool::reportJson(*report));
    }
    QString text = QStringLiteral("Sub-agent reports (%1 of %2 finished%3):")
        .arg(finished.size()).arg(ids.size())
        .arg(pending.isEmpty()
                 ? QString()
                 : QStringLiteral("; pending: ") + stateLine(pending, states));
    if (!blocks.isEmpty()) text += QStringLiteral("\n\n") + blocks.join(QStringLiteral("\n\n"));
    ToolResult result = anyOk ? ToolResult::ok(text) : ToolResult::fail(text);
    // docs/43 O3: batch metadata feeds audit aggregation; proposal ids stay on
    // the single-id path (a batch mixes several coder runs' ids). An empty
    // batch (nothing finished) carries no metadata so the audit row stays a
    // plain timeout notice.
    if (!batch.isEmpty())
        result.metadata.insert(QStringLiteral("subagentReports"), batch);
    return result;
}

ToolResult SubAgentWaitTool::execute(const QJsonObject& args) {
    return execute(args, {});
}

// ── subagent_status ──────────────────────────────────────

void SubAgentStatusTool::setSpawnTool(
    const std::shared_ptr<SubAgentTool>& tool) {
    m_spawnTool = tool;
}

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
        // docs/43 O3: slot pressure lets the model decide whether spawning
        // more work would only queue. The spawn tool reference is injected
        // on the GUI thread at creation time (P1-3: pool threads must never
        // query the lock-free ToolRegistry; gateSummary() itself is guarded
        // by the spawn tool's gate mutex).
        if (const auto spawn = m_spawnTool.lock())
            text += QStringLiteral("\n%1").arg(spawn->gateSummary());
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