#include "SubAgentRunner.h"

#include "infrastructure/llm/IAIService.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <functional>

namespace NexusAI {

namespace {

// Abort poll cadence inside the wait loop: the cancel flag and the deadline
// are only observed between events, so this bounds abort latency while
// keeping idle wakeups cheap.
constexpr int kAbortPollMs = 250;
// Floor for the wall-clock budget: below this the run would not even finish
// one LLM round-trip, so a mistyped 0/negative timeout cannot busy-loop.
constexpr qint64 kMinTimeoutMs = 200;

QString argsToWire(const QJsonObject& args) {
    return QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
}

QJsonArray toolsSchema(const QVector<SubAgentToolEntry>& tools) {
    QJsonArray schema;
    for (const auto& entry : tools) {
        QJsonObject toolObj;
        toolObj["type"] = "function";
        QJsonObject fn;
        fn["name"] = entry.def.name;
        fn["description"] = entry.def.description;
        fn["parameters"] = entry.def.parameters;
        toolObj["function"] = fn;
        schema.append(toolObj);
    }
    return schema;
}

QString systemPrompt(const SubAgentRequest& request) {
    QString prompt = QStringLiteral(
        "You are a read-only research sub-agent spawned by a primary agent. "
        "Complete the given goal using the read-only tools provided "
        "(file reads, directory listings, searches, git inspection) and "
        "report back. You cannot modify anything and you have no channel to "
        "the user: your final answer IS the deliverable the primary agent "
        "will consume.\n\n"
        "Rules:\n"
        "- Work autonomously; do not ask questions.\n"
        "- Every tool call costs a step from a tight budget. Avoid redundant "
        "calls; never repeat a call you already made.\n"
        "- Finish with a self-contained report: lead with the answer or "
        "conclusion, then the supporting evidence with concrete file paths "
        "(path:line where possible). If you cannot complete the goal, state "
        "exactly what you found and what blocked you.\n");
    if (request.hint == QLatin1String("research")) {
        prompt += QStringLiteral(
            "\nFocus: depth over breadth - chase the specific question and "
            "verify it against the sources before reporting.\n");
    } else {
        // "explore" is the default: the primary agent usually wants a survey.
        prompt += QStringLiteral(
            "\nFocus: breadth first - enumerate candidate locations before "
            "reading anything in full, then read selectively.\n");
    }
    if (!request.context.trimmed().isEmpty()) {
        prompt += QStringLiteral("\nContext from the primary agent:\n%1\n")
                      .arg(request.context.trimmed());
    }
    return prompt;
}

QString userPrompt(const SubAgentRequest& request) {
    return QStringLiteral("Goal: %1").arg(request.goal.trimmed());
}

QString truncate(const QString& text, int maxChars) {
    if (text.size() <= maxChars) return text;
    return text.left(maxChars)
        + QStringLiteral("\n... [truncated %1 characters]").arg(text.size() - maxChars);
}

QString toolResultText(const ToolResult& result) {
    return result.success ? result.output
                          : QStringLiteral("Error: %1").arg(result.error);
}

QString roundSignature(const QVector<ToolCallInfo>& calls) {
    QStringList parts;
    for (const auto& call : calls)
        parts.append(QStringLiteral("%1(%2)").arg(call.name, argsToWire(call.arguments)));
    return parts.join(QLatin1Char(';'));
}

QString toolNameList(const QVector<SubAgentToolEntry>& tools) {
    QStringList names;
    for (const auto& entry : tools) names.append(entry.def.name);
    return names.join(QStringLiteral(", "));
}

// Appends the assistant turn in OpenAI wire format (mirrors AIAgent's
// serialization: JSON-string arguments, null content when only tool calls).
void appendAssistantTurn(QJsonArray& messages, const LLMResponse& response,
                         const QVector<ToolCallInfo>& calls, quint64& callSeq) {
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    if (!response.content.isEmpty())
        assistantMsg["content"] = response.content;
    else
        assistantMsg["content"] = QJsonValue();   // null when only tool calls
    QJsonArray callsArr;
    for (const auto& call : calls) {
        QJsonObject c;
        c["id"] = call.id.isEmpty()
            ? QStringLiteral("call_%1").arg(++callSeq) : call.id;
        c["type"] = "function";
        QJsonObject fn;
        fn["name"] = call.name;
        fn["arguments"] = argsToWire(call.arguments);
        c["function"] = fn;
        callsArr.append(c);
    }
    assistantMsg["tool_calls"] = callsArr;
    messages.append(assistantMsg);
}

void appendToolResult(QJsonArray& messages, const ToolCallInfo& call,
                      const QString& content) {
    QJsonObject toolMsg;
    toolMsg["role"] = "tool";
    toolMsg["tool_call_id"] = call.id;
    toolMsg["content"] = content;
    messages.append(toolMsg);
}

} // namespace

SubAgentReport SubAgentRunner::run(IAIService* service, const SubAgentRequest& request) {
    SubAgentReport report;
    if (!service) {
        report.stop = SubAgentReport::Stop::Error;
        report.text = QStringLiteral("Sub-agent has no model service configured.");
        return report;
    }
    if (request.goal.trimmed().isEmpty()) {
        report.text = QStringLiteral("Sub-agent goal is empty.");
        return report;
    }

    const auto cancelledFlag = [&request]() {
        return request.cancelled && request.cancelled->load();
    };
    if (cancelledFlag()) {
        report.stop = SubAgentReport::Stop::Cancelled;
        report.text = QStringLiteral("Sub-agent cancelled before start.");
        return report;
    }

    const qint64 timeoutMs = request.timeoutMs > 0
        ? qMax(request.timeoutMs, kMinTimeoutMs)
        : 0;   // 0 = no wall-clock budget (caller opted out)
    QElapsedTimer deadline;
    deadline.start();

    QEventLoop loop;
    LLMResponse response;
    QString error;
    bool gotResponse{false};
    bool gotError{false};
    bool timedOut{false};
    QObject::connect(service, &IAIService::responseReady, &loop,
                     [&](const LLMResponse& r) {
                         response = r;
                         gotResponse = true;
                         loop.quit();
                     });
    QObject::connect(service, &IAIService::errorOccurred, &loop,
                     [&](const QString& e) {
                         error = e;
                         gotError = true;
                         loop.quit();
                     });
    // Wall-clock budget + cancellation poll. Runs only while the loop spins;
    // synchronous fakes that answer inside chatComplete() never reach it.
    QTimer abortTimer;
    abortTimer.setSingleShot(false);
    abortTimer.setInterval(kAbortPollMs);
    QObject::connect(&abortTimer, &QTimer::timeout, &loop, [&]() {
        if (cancelledFlag()) {
            error = QStringLiteral("cancelled by the primary agent");
            gotError = true;
            loop.quit();
            return;
        }
        if (timeoutMs > 0 && deadline.elapsed() >= timeoutMs) {
            timedOut = true;
            service->cancel();   // stop an in-flight network request
            error = QStringLiteral("sub-agent wall-clock budget exceeded");
            gotError = true;
            loop.quit();
        }
    });

    QJsonArray messages;
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = systemPrompt(request);
    messages.append(systemMsg);
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userPrompt(request);
    messages.append(userMsg);

    quint64 callSeq{0};
    QString lastSignature;

    for (int step = 1; step <= request.maxSteps; ++step) {
        report.steps = step;
        if (cancelledFlag()) {
            report.stop = SubAgentReport::Stop::Cancelled;
            report.text = QStringLiteral("Sub-agent cancelled after %1 step(s).").arg(step);
            return report;
        }

        // On the last allowed step withhold the tool schema: the model must
        // turn what it gathered into an answer (mirrors AIAgent's forced
        // final) instead of burning the budget on one more call.
        const bool forcedFinal = step == request.maxSteps;
        gotResponse = false;
        gotError = false;
        timedOut = false;
        error.clear();
        abortTimer.start();
        service->chatComplete(messages,
                              forcedFinal ? QJsonArray() : toolsSchema(request.tools));
        // Fakes may emit synchronously inside chatComplete (before exec);
        // quitting before exec() is a no-op, so only enter the loop when
        // nothing has arrived yet.
        if (!gotResponse && !gotError) loop.exec();
        abortTimer.stop();

        report.tokensUsed += response.tokensUsed;
        if (gotError) {
            report.stop = timedOut ? SubAgentReport::Stop::TimedOut
                                   : SubAgentReport::Stop::Error;
            report.text = QStringLiteral("Sub-agent failed on step %1: %2")
                              .arg(step).arg(error);
            return report;
        }
        if (!gotResponse) {
            report.stop = SubAgentReport::Stop::Error;
            report.text = QStringLiteral("Sub-agent service produced no response.");
            return report;
        }

        if (response.toolCalls.isEmpty()) {
            report.ok = true;
            report.stop = SubAgentReport::Stop::Completed;
            report.text = response.content;
            return report;
        }

        const QString signature = roundSignature(response.toolCalls);
        const bool repeated = signature == lastSignature;
        lastSignature = signature;

        appendAssistantTurn(messages, response, response.toolCalls, callSeq);

        for (const auto& call : response.toolCalls) {
            if (cancelledFlag()) {
                report.stop = SubAgentReport::Stop::Cancelled;
                report.text = QStringLiteral("Sub-agent cancelled during tool execution.");
                return report;
            }
            const SubAgentToolEntry* entry = nullptr;
            for (const auto& candidate : request.tools)
                if (candidate.def.name == call.name) { entry = &candidate; break; }
            if (!entry) {
                appendToolResult(messages, call,
                    QStringLiteral("Unknown tool: %1. Available tools: %2")
                        .arg(call.name, toolNameList(request.tools)));
                continue;
            }
            // Same thread as the dispatching tool: the sandbox ExecutionScope
            // pinned by the primary agent's dispatch still applies, and the
            // cancellation flag is shared, so a stop() also aborts nested
            // cooperative tools.
            const ToolResult result = entry->tool->execute(call.arguments, request.cancelled);
            appendToolResult(messages, call,
                             truncate(toolResultText(result), kSubToolResultMaxChars));
        }

        if (repeated) {
            // The model is stuck in a loop: converge now instead of letting
            // it run out of steps (mirrors AIAgent's round-level guard).
            report.stop = SubAgentReport::Stop::Budget;
            report.text = QStringLiteral(
                "Sub-agent stopped: it repeated the same tool-call round "
                "without converging. Partial information was gathered but no "
                "final report was produced.");
            return report;
        }
    }

    report.stop = SubAgentReport::Stop::Budget;
    report.text = QStringLiteral(
        "Sub-agent exhausted its step budget (%1) without a final report.")
        .arg(request.maxSteps);
    return report;
}

} // namespace NexusAI
