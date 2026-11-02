#include "SubAgentRunner.h"

#include "core/tools/Sandbox.h"
#include "infrastructure/llm/IAIService.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUuid>
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
// 过程可视化日志（assistant/tool 进度事件）的单段文本预算。
constexpr int kProgressTextMaxChars = 400;
constexpr int kProgressArgsMaxChars = 160;
constexpr int kProgressResultMaxChars = 200;

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
    // docs/33 E2: a resolved role profile owns the focus block when present;
    // the legacy hint enum remains the fallback for old callers/tests.
    if (!request.rolePrompt.trimmed().isEmpty()) {
        prompt += QStringLiteral("\nRole focus: %1\n")
                      .arg(request.rolePrompt.trimmed());
    } else if (request.hint == QLatin1String("research")) {
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

// ── docs/33 E1: evidence artifacts ─────────────────────────────────────
// The sub-agent's full transcript (incl. every tool result) written to the
// app-data artifacts root; the tool result carries the path so the primary
// agent can file_read the details on demand. AppData instead of the project
// directory: writing scratch into the user's workspace would pollute
// git status, while AppDataLocation is inside the sandbox read allowlist.

QString g_artifactsRootOverride;   // tests only; set before runs, no concurrent writes

QString artifactsRoot() {
    if (!g_artifactsRootOverride.isEmpty()) return g_artifactsRootOverride;
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/subagents");
}

void pruneEvidenceArtifacts() {
    const QDir dir(artifactsRoot());
    if (!dir.exists()) return;
    const QDateTime cutoff =
        QDateTime::currentDateTime().addDays(-SubAgentRunner::kArtifactRetentionDays);
    const auto files = dir.entryInfoList(QDir::Files, QDir::Time);
    for (const QFileInfo& info : files) {
        if (info.lastModified() < cutoff)
            QFile::remove(info.absoluteFilePath());   // best effort
    }
}

QString writeEvidenceArtifact(const SubAgentRequest& request, const QJsonArray& messages) {
    if (request.goal.trimmed().isEmpty()) return {};
    // Pure Q&A (no tool rounds) loses nothing in the report — skip the file.
    bool hasToolRound = false;
    for (const auto& entry : messages) {
        if (entry.toObject().value("role").toString() == QLatin1String("tool")) {
            hasToolRound = true;
            break;
        }
    }
    if (!hasToolRound) return {};
    const QString root = artifactsRoot();
    if (root.isEmpty()) return {};
    if (!QDir().mkpath(root)) return {};
    const QString path = root + QStringLiteral("/%1-%2.md")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
             QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};

    QString md;
    md += QStringLiteral("# Sub-agent evidence\n\n- Goal: %1\n")
              .arg(request.goal.trimmed());
    if (!request.context.trimmed().isEmpty())
        md += QStringLiteral("- Context: %1\n").arg(request.context.trimmed());
    md += QStringLiteral(
        "- The transcript is the sub-agent's own message history; tool "
        "results are truncated at %1 chars each (same as its live context).\n\n"
        "## Transcript\n\n").arg(SubAgentRunner::kSubToolResultMaxChars);
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();
        if (role == QLatin1String("assistant")) {
            md += QStringLiteral("### assistant\n");
            const QString content = msg.value("content").toString();
            if (!content.isEmpty()) md += content + QLatin1Char('\n');
            for (const auto& c : msg.value("tool_calls").toArray()) {
                const QJsonObject call = c.toObject();
                const QJsonObject fn = call.value("function").toObject();
                md += QStringLiteral("- tool_call %1: %2(%3)\n")
                          .arg(call.value("id").toString(), fn.value("name").toString(),
                               fn.value("arguments").toString());
            }
            md += QLatin1Char('\n');
        } else if (role == QLatin1String("tool")) {
            md += QStringLiteral("### tool result for %1\n%2\n\n")
                      .arg(msg.value("tool_call_id").toString(),
                           msg.value("content").toString());
        }
    }
    QByteArray bytes = md.toUtf8();
    if (bytes.size() > SubAgentRunner::kArtifactMaxBytes) {
        bytes = bytes.left(SubAgentRunner::kArtifactMaxBytes);
        bytes += "\n\n... [evidence truncated]\n";
    }
    file.write(bytes);
    return path;
}

QString calledToolNames(const QVector<ToolCallInfo>& calls) {
    QStringList names;
    for (const auto& call : calls) names.append(call.name);
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

// docs/34 P3.1: 把提案渲染成主代理可执行的报告段。diff 块只用于阅读——
// 应用时不依赖行号，主代理照抄 find 原文交给 edit_file。
// P3.3: 同一文件有多个提案时给出合并提示（按列出顺序逐条应用、每条之后
// 重新核对剩余锚点），文本上相互包含的锚点额外标冲突——先落盘的一条可能
// 让后一条的锚点移位或消失。
QString renderProposals(const QVector<SubAgentProposal>& proposals, bool partial) {
    if (proposals.isEmpty()) return {};

    // 项目相对展示路径（正文与合并提示共用）。
    const QString root = Sandbox::currentProjectDir();
    QVector<QString> displays;
    displays.reserve(proposals.size());
    for (const auto& proposal : proposals) {
        QString display = proposal.path;
        if (!root.isEmpty()) {
            const QString relative = QDir(root).relativeFilePath(proposal.path);
            if (!relative.startsWith(QLatin1String(".."))) display = relative;
        }
        displays.append(display);
    }

    QString out = QStringLiteral("\n\n## PROPOSALS (not applied)\n");
    if (partial) {
        out += QStringLiteral(
            "The run ended before a final report; the proposals below are "
            "the ones that were validated against the files at proposal "
            "time. Verify each against the current file before applying.\n");
    }
    out += QStringLiteral(
        "Apply each proposal with edit_file (use file_write only if a new "
        "file is needed) and pass the 'find' text verbatim as the old text; "
        "every write still requires user approval. Do not describe the change "
        "as done until the tool call succeeds.\n\n");

    // 合并提示：按首次出现顺序聚合同一文件的提案。
    QStringList fileOrder;
    QHash<QString, QVector<int>> byFile;
    for (int i = 0; i < displays.size(); ++i) {
        auto it = byFile.find(displays.at(i));
        if (it == byFile.end()) {
            fileOrder.append(displays.at(i));
            it = byFile.insert(displays.at(i), {});
        }
        it->append(i);
    }
    QStringList notes;
    for (const QString& file : fileOrder) {
        const QVector<int> indices = byFile.value(file);
        if (indices.size() < 2) continue;
        notes << QStringLiteral(
            "- %1 has %2 proposals. Apply them in the order listed; after "
            "each edit, re-check the remaining anchors against the current "
            "file.").arg(file).arg(indices.size());
        for (int a = 0; a < indices.size(); ++a) {
            for (int b = a + 1; b < indices.size(); ++b) {
                bool overlap = false;
                for (const SubAgentProposalEdit& first
                     : proposals.at(indices.at(a)).edits) {
                    for (const SubAgentProposalEdit& second
                         : proposals.at(indices.at(b)).edits) {
                        if (first.find == second.find
                                || first.find.contains(second.find)
                                || second.find.contains(first.find)) {
                            overlap = true;
                            break;
                        }
                    }
                    if (overlap) break;
                }
                if (overlap) {
                    notes << QStringLiteral(
                        "- Proposals %1 and %2 overlap on %3 (one anchor "
                        "contains the other). Applying one may invalidate the "
                        "other - re-read the file and re-propose if an anchor "
                        "stops matching.")
                        .arg(indices.at(a) + 1).arg(indices.at(b) + 1).arg(file);
                }
            }
        }
    }
    if (!notes.isEmpty()) {
        out += QStringLiteral("Merge notes:\n");
        out += notes.join(QLatin1Char('\n'));
        out += QStringLiteral("\n\n");
    }

    for (int i = 0; i < proposals.size(); ++i) {
        const SubAgentProposal& proposal = proposals.at(i);
        out += QStringLiteral("### %1. %2\n%3\n\n")
                   .arg(i + 1).arg(displays.at(i), proposal.rationale);
        for (const auto& edit : proposal.edits) {
            out += QStringLiteral("```diff\n");
            const QStringList before = edit.find.split(QLatin1Char('\n'));
            const QStringList after = edit.replace.split(QLatin1Char('\n'));
            for (const QString& line : before)
                out += QLatin1Char('-') + line + QLatin1Char('\n');
            for (const QString& line : after)
                out += QLatin1Char('+') + line + QLatin1Char('\n');
            out += QStringLiteral("```\n");
        }
        out += QLatin1Char('\n');
    }
    return out;
}

} // namespace

SubAgentReport SubAgentRunner::runImpl(IAIService* service, const SubAgentRequest& request,
                                       QJsonArray& messages) {
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

    // maxSteps <= 0 = 无预算：只留内部安全上限防失控；进度事件照实上报
    // request.maxSteps（0），UI 标题显示 "step k" 而非误导性的 "k/1000"。
    const int stepBudget = request.maxSteps > 0
        ? request.maxSteps : kSubStepCap;

    QEventLoop loop;
    LLMResponse response;
    QString error;
    bool gotResponse{false};
    bool gotError{false};
    bool timedOut{false};
    // 取消旗标触发的中止走 gotError 通道（abort 以错误形式冒出），需要
    // 单独记账，报告才能归类为 Cancelled 而不是 Error。
    bool cancelledByFlag{false};
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
            // 与超时分支同理：请求还在飞时先 abort，别把在飞请求留给
            // 调用方的 delete service 在析构里收尾（见 LLMServiceBase
            // 析构注释——池线程上那是不干净的中止路径）。
            service->cancel();
            cancelledByFlag = true;
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
    // Per-round progress; the hook is optional, empty outside a dispatched run.
    const auto notify = [&](int stepNum, const QString& phase, const QString& detail) {
        if (request.progress)
            request.progress(SubAgentProgress{stepNum,
                                              request.maxSteps > 0 ? request.maxSteps : 0,
                                              phase, detail});
    };

    for (int step = 1; step <= stepBudget; ++step) {
        report.steps = step;
        if (cancelledFlag()) {
            report.stop = SubAgentReport::Stop::Cancelled;
            report.text = QStringLiteral("Sub-agent cancelled after %1 step(s).").arg(step);
            return report;
        }

        // On the last allowed step withhold the tool schema: the model must
        // turn what it gathered into an answer (mirrors AIAgent's forced
        // final) instead of burning the budget on one more call.
        const bool forcedFinal = step == stepBudget;
        notify(step, QStringLiteral("thinking"),
               forcedFinal ? QStringLiteral("final answer") : QString());
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
                : cancelledByFlag ? SubAgentReport::Stop::Cancelled
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

        // 过程可视化：本轮模型的可见文本（含最终答案）进日志，供 UI 展开。
        if (!response.content.trimmed().isEmpty())
            notify(step, QStringLiteral("assistant"),
                   truncate(response.content, kProgressTextMaxChars));

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
            // 过程可视化：每个工具调用的名字、参数与结果摘要进日志。
            notify(step, QStringLiteral("tool"),
                   QStringLiteral("%1(%2) ⇒ %3")
                       .arg(call.name,
                            truncate(QString::fromUtf8(
                                QJsonDocument(call.arguments).toJson(
                                    QJsonDocument::Compact)),
                                kProgressArgsMaxChars),
                            truncate(toolResultText(result), kProgressResultMaxChars)));
        }
        notify(step, QStringLiteral("tools"), calledToolNames(response.toolCalls));

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
        .arg(stepBudget);
    return report;
}

SubAgentReport SubAgentRunner::run(IAIService* service, const SubAgentRequest& request) {
    pruneEvidenceArtifacts();
    QJsonArray messages;
    // docs/34 P3.1: 提案收集槽只在本次运行的线程上生效；ProposeEditTool 从中
    // 取得 sink（不在运行内时拿不到，直接拒绝调用）。
    QVector<SubAgentProposal> proposals;
    SubAgentReport report;
    {
        SubAgentProposalSink sink(&proposals);
        report = runImpl(service, request, messages);
    }
    report.proposals = proposals;
    if (!proposals.isEmpty()) {
        report.text += renderProposals(proposals,
                                       report.stop != SubAgentReport::Stop::Completed);
    }
    // docs/33 E1: preserve the full transcript for on-demand file_read; a
    // pure Q&A run (no tool rounds) writes nothing.
    report.artifactPath = writeEvidenceArtifact(request, messages);
    return report;
}

void SubAgentRunner::setArtifactsRootOverride(const QString& dir) {
    g_artifactsRootOverride = dir;
}

bool SubAgentRunner::isEvidencePath(const QString& path) {
    if (path.isEmpty()) return false;
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) return false;   // missing file or symlink loop
    const QString root = artifactsRoot();
    if (root.isEmpty()) return false;
    // Root may not exist yet (no run ever wrote an artifact); the prefix
    // check is against its absolute (not canonical) form.
    const QString rootAbsolute = QFileInfo(root).absoluteFilePath();
    return canonical.startsWith(rootAbsolute + QLatin1Char('/'));
}

} // namespace NexusAI
