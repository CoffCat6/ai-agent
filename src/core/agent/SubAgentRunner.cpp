#include "SubAgentRunner.h"

#include "core/tools/ProposeEditTool.h"
#include "core/tools/Sandbox.h"
#include "core/utils/TokenEstimator.h"
#include "core/utils/ModelOutputSanitizer.h"
#include "infrastructure/llm/IAIService.h"

#include <QtConcurrent/QtConcurrentRun>

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
#include <QRegularExpression>

#include <QStandardPaths>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace NexusAI {

namespace {

// Abort poll cadence inside the wait loop: the cancel flag and the deadline
// are only observed between events, so this bounds abort latency while
// keeping idle wakeups cheap.
constexpr int kAbortPollMs = 250;
// docs/35 P4.6: 流式增量回传节流（与主界面的流式刷新同量级）。
constexpr int kStreamFlushMs = 200;
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
        "exactly what you found and what blocked you.\n"
        "- When you have findings (review/research/coder), end with a "
        "'## FINDINGS' section, one '- [severity] path:line - problem and "
        "fix' line each. Severity is critical/high/medium/low/info. Omit the "
        "section when there is nothing to report.\n");
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
    // docs/43 O1-T2: workspace rules travel with the run so a sub-agent
    // follows the same project conventions as the primary agent. Already
    // rendered (header + truncation marker) by AIAgent::projectRulesBlock().
    if (!request.projectRules.trimmed().isEmpty()) {
        prompt += request.projectRules;
        if (!prompt.endsWith(QLatin1Char('\n'))) prompt += QLatin1Char('\n');
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
        "Apply each proposal with apply_proposal(id), using the ids listed "
        "below (every call still requires user approval). If a proposal has "
        "no id listed, or its anchor needs adjusting, fall back to edit_file "
        "with the 'find' text verbatim (use file_write only for a new file). "
        "Do not describe the change as done until the tool call succeeds.\n\n");

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

// docs/43 O2: parse the trailing "## FINDINGS" section. Deliberately lenient:
// unrecognised lines are skipped and the report text is never rewritten.
QVector<SubAgentFinding> parseFindings(const QString& text) {
    constexpr int kMaxFindings = 50;
    QVector<SubAgentFinding> out;
    const int marker = text.lastIndexOf(QStringLiteral("## FINDINGS"), -1,
                                        Qt::CaseInsensitive);
    if (marker < 0) return out;
    static const QStringList kSeverities{
        QStringLiteral("critical"), QStringLiteral("high"),
        QStringLiteral("medium"), QStringLiteral("low"),
        QStringLiteral("info")};
    static const QRegularExpression lineRe(
        QStringLiteral("^\\s*[-*]\\s*\\[([^\\]]*)\\]\\s*(.+)$"));
    static const QRegularExpression locRe(
        QStringLiteral("^(.*?):(\\d+)\\b\\s*[-\u2013\u2014:]?\\s*(.*)$"));
    const QStringList lines =
        text.mid(marker).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QRegularExpressionMatch match = lineRe.match(line);
        if (!match.hasMatch()) continue;
        SubAgentFinding finding;
        finding.severity = match.captured(1).trimmed().toLower();
        if (!kSeverities.contains(finding.severity))
            finding.severity = QStringLiteral("info");
        const QString rest = match.captured(2).trimmed();
        const QRegularExpressionMatch loc = locRe.match(rest);
        if (loc.hasMatch()) {
            finding.path = loc.captured(1).trimmed();
            finding.line = loc.captured(2).toInt();
            finding.message = loc.captured(3).trimmed();
        } else {
            finding.message = rest;
        }
        if (finding.message.isEmpty()) continue;
        finding.path = finding.path.left(300);
        finding.message = finding.message.left(500);
        out.append(finding);
        if (out.size() >= kMaxFindings) break;
    }
    return out;
}

// 子代理内的 web_search / http_fetch 是同步执行、只认自己 cancel() 原子旗标
// 的工具（ITool 默认的 execute(args, cancelled) 只在启动前检查令牌）。Runner
// 自己的取消检查只发生在轮次边界，于是「停止」在长时间网络调用里会一直停在
// 「正在停止…」直到工具超时。这个看门线程在 request.cancelled 翻转时对正在
// 执行的工具调用 cancel()；工具只翻转原子旗标/终止子句柄，可从任意线程安全
// 调用（子代理工具面限定为只读，不含 shell 等需要 GUI 线程收尾的实现）。
class ToolCancellationWatcher {
public:
    explicit ToolCancellationWatcher(ITool::Cancellation cancelled)
        : m_cancelled(std::move(cancelled)) {
        if (m_cancelled)
            m_thread = std::thread([this]() { watch(); });
    }
    ~ToolCancellationWatcher() { stop(); }

    void registerTool(ITool* tool) {
        if (!tool) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tools.append(tool);
    }
    void unregisterTool(ITool* tool) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tools.removeAll(tool);
    }

private:
    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) return;
            m_stopped = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }

    void watch() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (!m_stopped) {
            // 即使令牌在看门线程第一次醒来后才翻转、或翻转后才有工具
            // 登记，也继续周期性补发 cancel（工具实现幂等）。
            if (m_cancelled->load() && !m_tools.isEmpty()) {
                const QVector<ITool*> inFlight = m_tools;
                lock.unlock();
                for (ITool* tool : inFlight)
                    tool->cancel(m_cancelled);
                lock.lock();
            }
            m_cv.wait_for(lock, std::chrono::milliseconds(100));
        }
    }

    ITool::Cancellation m_cancelled;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    QVector<ITool*> m_tools;
    bool m_stopped{false};
    std::thread m_thread;
};

} // namespace

SubAgentReport SubAgentRunner::runImpl(IAIService* service, const SubAgentRequest& request,
                                       QJsonArray& messages) {
    SubAgentReport report;
    if (!service) {
        report.stop = SubAgentReport::Stop::Error;
        report.stopCode = QStringLiteral("no_service");
        report.text = QStringLiteral("Sub-agent has no model service configured.");
        return report;
    }
    report.usage.model = service->model();
    if (request.goal.trimmed().isEmpty()) {
        report.stopCode = QStringLiteral("invalid_goal");
        report.text = QStringLiteral("Sub-agent goal is empty.");
        return report;
    }

    const auto cancelledFlag = [&request]() {
        return request.cancelled && request.cancelled->load();
    };
    if (cancelledFlag()) {

        report.stop = SubAgentReport::Stop::Cancelled;
        report.stopCode = QStringLiteral("cancelled");
        report.text = QStringLiteral("Sub-agent cancelled before start.");
        return report;
    }

    // 轮次边界的 cancelledFlag() 检查之外的补充：工具在同步执行时也必须
    // 能被打断（见 ToolCancellationWatcher 注释）。
    ToolCancellationWatcher toolWatcher(request.cancelled);

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
                         // docs/43 O4 审计收口（P1-2 连带）: 错误轮没有响应
                         // payload（errorOccurred 只带字符串），清掉上一轮
                         // 残留的计量字段——否则主循环的累计段会把旧值
                         // 重复计入（错误轮重复计费，成本高估）。
                         response.promptTokens = 0;
                         response.completionTokens = 0;
                         response.cachedTokens = 0;
                         response.cacheReadTokens = 0;
                         response.tokensUsed = 0;
                         loop.quit();
                     });
    // docs/35 P4.6: streaming deltas fill the gap while a long thinking/model
    // response is in flight. They are throttled into progress events below.
    QString streamBuffer;
    QString reasoningBuffer;
    QObject::connect(service, &IAIService::streamingChunk, &loop,
                     [&](const QString& chunk) { streamBuffer += chunk; });
    QObject::connect(service, &IAIService::reasoningChunk, &loop,
                     [&](const QString& chunk) { reasoningBuffer += chunk; });
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

    // docs/35 P4.6: one throttled progress event per buffer; the GUI replaces
    // the trailing entry of the same phase instead of appending a new log
    // line, so a long stream cannot flush the history out of subagentLog.
    int streamStep{0};
    QTimer streamTimer;
    streamTimer.setInterval(kStreamFlushMs);
    QObject::connect(&streamTimer, &QTimer::timeout, &loop, [&]() {
        if (!streamBuffer.isEmpty()) {
            notify(streamStep, QStringLiteral("stream"),
                   truncate(streamBuffer, kProgressTextMaxChars));
            streamBuffer.clear();
        }
        if (!reasoningBuffer.isEmpty()) {
            notify(streamStep, QStringLiteral("reasoning"),
                   truncate(reasoningBuffer, kProgressTextMaxChars));
            reasoningBuffer.clear();
        }
    });

    // docs/35 P4.2: bounded internal parallelism for read-only tools. The
    // runner owns the pool (instead of using the global one) so nested tasks
    // cannot starve the primary agent's tool/embedding work. propose_edit is
    // excluded: it writes through a thread-local sink and its proposal order
    // must match the call order, so it stays on the run thread.
    QThreadPool toolPool;
    toolPool.setMaxThreadCount(kSubToolParallelCap);
    // Snapshot the project root on the run thread: Sandbox::ExecutionScope is
    // thread-local, so worker threads would otherwise lose the root the
    // primary agent pinned for this dispatch (relative paths then fail).
    const QString projectRoot = Sandbox::currentProjectDir();
    const auto findEntry = [&request](const QString& name)
            -> const SubAgentToolEntry* {
        for (const auto& candidate : request.tools)
            if (candidate.def.name == name) return &candidate;
        return nullptr;
    };
    const auto parallelEligible = [](const SubAgentToolEntry* entry) {
        return entry && entry->def.level == ToolLevel::Safe
               && entry->def.readOnly
               && entry->def.name != QLatin1String(ProposeEditTool::kName);
    };
    const auto logToolCall = [&notify, &messages]
            (int stepNum, const ToolCallInfo& call, const ToolResult& result) {
        appendToolResult(messages, call,
                         truncate(toolResultText(result),
                                  kSubToolResultMaxChars));
        // 过程可视化：每个工具调用的名字、参数与结果摘要进日志。
        notify(stepNum, QStringLiteral("tool"),
               QStringLiteral("%1(%2) ⇒ %3")
                   .arg(call.name,
                        truncate(QString::fromUtf8(
                            QJsonDocument(call.arguments).toJson(
                                QJsonDocument::Compact)),
                            kProgressArgsMaxChars),
                        truncate(toolResultText(result),
                                 kProgressResultMaxChars)));
    };

    for (int step = 1; step <= stepBudget; ++step) {
        report.steps = step;
        if (cancelledFlag()) {

            report.stop = SubAgentReport::Stop::Cancelled;
            report.stopCode = QStringLiteral("cancelled");
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
        streamStep = step;
        streamBuffer.clear();
        reasoningBuffer.clear();
        abortTimer.start();
        streamTimer.start();
        // docs/35 P4.1: keep the full transcript for the evidence artifact,
        // but send a budgeted copy of the request. Only the wire view is
        // compressed, so earlier evidence is still on disk and the next
        // round starts from the complete history again.
        const QJsonArray wireTools =
            forcedFinal ? QJsonArray() : toolsSchema(request.tools);
        QJsonArray wireMessages = messages;
        if (request.contextLimit > 0) {
            const int capacity = qMax(
                0, request.contextLimit
                       - TokenEstimator::estimateToolsSchema(wireTools));
            if (TokenEstimator::estimateTokens(wireMessages) > capacity) {
                int omittedRounds = 0;
                int truncatedResults = 0;
                wireMessages = TokenEstimator::compressToolRoundsToFit(
                    wireMessages, capacity, &omittedRounds, &truncatedResults);
                // Nothing to cut means the header alone exceeds the window;
                // no event, but the provider error stays self-explanatory.
                if (omittedRounds > 0 || truncatedResults > 0) {
                    notify(step, QStringLiteral("context"),
                           QStringLiteral(
                               "%1 earlier round(s) omitted, %2 tool result(s) "
                               "truncated (%3 → %4 messages)")
                               .arg(omittedRounds).arg(truncatedResults)
                               .arg(messages.size()).arg(wireMessages.size()));
                }
            }
        }
        service->chatComplete(wireMessages, wireTools);
        // Fakes may emit synchronously inside chatComplete (before exec);
        // quitting before exec() is a no-op, so only enter the loop when
        // nothing has arrived yet.
        if (!gotResponse && !gotError) loop.exec();
        abortTimer.stop();
        streamTimer.stop();

        report.tokensUsed += response.tokensUsed;
        // docs/43 O2: classified usage (input/output/cache) across steps and
        // retries. usageReported stays false when the provider gave only an
        // aggregate, so consumers never read zeros as "free".
        report.usage.inputTokens += response.promptTokens;
        report.usage.outputTokens += response.completionTokens;
        report.usage.cachedTokens += response.cachedTokens
                                   + response.cacheReadTokens;
        if (response.promptTokens > 0 || response.completionTokens > 0)
            report.usage.usageReported = true;
        if (gotError) {
            report.stop = timedOut ? SubAgentReport::Stop::TimedOut
                : cancelledByFlag ? SubAgentReport::Stop::Cancelled
                                  : SubAgentReport::Stop::Error;
            report.stopCode = timedOut ? QStringLiteral("timeout")
                : cancelledByFlag ? QStringLiteral("cancelled")
                                  : QStringLiteral("service_error");
            // docs/43 O1-T3: timeouts and service/network errors are worth
            // one automatic retry; a user-requested cancel is not.
            report.retryable = !cancelledByFlag;
            report.text = QStringLiteral("Sub-agent failed on step %1: %2")
                              .arg(step).arg(error);
            return report;
        }
        if (!gotResponse) {
            report.stop = SubAgentReport::Stop::Error;
            report.stopCode = QStringLiteral("empty_response");
            report.retryable = true;
            report.text = QStringLiteral("Sub-agent service produced no response.");
            return report;
        }

        // Same guard as the main loop: native template markup in `content`
        // is not a report (nor a progress line). Strip it before either is
        // surfaced; a markup-only final turn gets an explicit failure text
        // instead of an empty report.
        const int textualToolCalls =
            ModelOutput::stripTextualToolCalls(response.content);
        if (textualToolCalls > 0 && response.toolCalls.isEmpty()
                && response.content.isEmpty()) {
            response.content = QStringLiteral(
                "Sub-agent model returned a tool call as plain text instead of "
                "a report; it was discarded.");
        }

        // 过程可视化：本轮模型的可见文本（含最终答案）进日志，供 UI 展开。
        if (!response.content.trimmed().isEmpty())
            notify(step, QStringLiteral("assistant"),
                   truncate(response.content, kProgressTextMaxChars));

        if (response.toolCalls.isEmpty()) {

            report.ok = true;
            report.stop = SubAgentReport::Stop::Completed;
            report.stopCode = QStringLiteral("completed");
            report.text = response.content;
            return report;
        }

        const QString signature = roundSignature(response.toolCalls);
        const bool repeated = signature == lastSignature;
        lastSignature = signature;

        appendAssistantTurn(messages, response, response.toolCalls, callSeq);

        // docs/35 P4.2: run read-only calls in contiguous batches of at most
        // kSubToolParallelCap, then append their results in the ORIGINAL call
        // order. Serial calls (unknown tools, propose_edit, non-read-only)
        // stay on the run thread, so the wire history keeps call/result order
        // and the proposal sink only ever sees its owning thread.
        const auto& calls = response.toolCalls;
        int callIndex = 0;
        while (callIndex < calls.size()) {
            if (cancelledFlag()) {
                report.stop = SubAgentReport::Stop::Cancelled;
                report.stopCode = QStringLiteral("cancelled");
                report.text = QStringLiteral("Sub-agent cancelled during tool execution.");
                return report;
            }
            const ToolCallInfo call = calls.at(callIndex);
            const SubAgentToolEntry* entry = findEntry(call.name);
            if (!parallelEligible(entry)) {
                if (!entry) {
                    appendToolResult(messages, call,
                        QStringLiteral("Unknown tool: %1. Available tools: %2")
                            .arg(call.name, toolNameList(request.tools)));
                } else {
                    // Same thread as the dispatching tool: the sandbox
                    // ExecutionScope pinned by the primary agent's dispatch
                    // still applies, and the cancellation flag is shared, so
                    // a stop() also aborts nested cooperative tools.
                    toolWatcher.registerTool(entry->tool.get());
                    const ToolResult result =
                        entry->tool->execute(call.arguments, request.cancelled);
                    toolWatcher.unregisterTool(entry->tool.get());
                    logToolCall(step, call, result);
                }
                ++callIndex;
                continue;
            }

            int batchEnd = callIndex;
            while (batchEnd < calls.size()
                   && batchEnd - callIndex < kSubToolParallelCap
                   && parallelEligible(findEntry(calls.at(batchEnd).name)))
                ++batchEnd;

            QVector<QFuture<ToolResult>> futures;
            futures.reserve(batchEnd - callIndex);
            QVector<ITool*> batchTools;
            batchTools.reserve(batchEnd - callIndex);
            bool cancelledMidBatch = false;
            for (int k = callIndex; k < batchEnd; ++k) {
                if (cancelledFlag()) {
                    cancelledMidBatch = true;
                    break;
                }
                const ToolCallInfo batchCall = calls.at(k);
                const SubAgentToolEntry* batchEntry = findEntry(batchCall.name);
                batchTools.append(batchEntry->tool.get());
                toolWatcher.registerTool(batchEntry->tool.get());
                futures.append(QtConcurrent::run(
                    &toolPool,
                    [tool = batchEntry->tool,
                     args = batchCall.arguments,
                     cancelled = request.cancelled,
                     projectRoot]() {
                        const Sandbox::ExecutionScope scope(projectRoot);
                        return tool->execute(args, cancelled);
                    }));
            }
            // Join the whole batch before continuing: results must hit the
            // history (and the progress log) in call order.
            for (int k = 0; k < futures.size(); ++k)
                logToolCall(step, calls.at(callIndex + k), futures.at(k).result());
            for (ITool* tool : batchTools)
                toolWatcher.unregisterTool(tool);
            callIndex += futures.size();
            if (cancelledMidBatch) {
                report.stop = SubAgentReport::Stop::Cancelled;
                report.stopCode = QStringLiteral("cancelled");
                report.text = QStringLiteral("Sub-agent cancelled during tool execution.");
                return report;
            }
        }
        notify(step, QStringLiteral("tools"), calledToolNames(response.toolCalls));

        if (repeated) {
            // The model is stuck in a loop: converge now instead of letting
            // it run out of steps (mirrors AIAgent's round-level guard).
            report.stop = SubAgentReport::Stop::Budget;
            report.stopCode = QStringLiteral("repeated");
            report.text = QStringLiteral(
                "Sub-agent stopped: it repeated the same tool-call round "
                "without converging. Partial information was gathered but no "
                "final report was produced.");
            return report;
        }
    }

    report.stop = SubAgentReport::Stop::Budget;
    report.stopCode = QStringLiteral("budget");
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
    // docs/43 O2: last write wins — parse after the proposals section is
    // appended so a "## FINDINGS" block inside proposal text is not picked up
    // out of order (lastIndexOf finds the report's own trailing section).
    report.findings = parseFindings(report.text);
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
