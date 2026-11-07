// AIAgent main-loop branch coverage, driven by a scripted fake LLM service.
// Exercises: direct completion, dangerous-tool exclusion, safe-tool feedback,
// confirmation approve/decline, unknown tools, max-iteration guard, stop and
// the context budget compression hook.
#include "core/agent/AIAgent.h"
#include "core/agent/AgentState.h"
#include "core/agent/EpisodicService.h"
#include "core/agent/SubAgentRunner.h"
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"
#include "core/tools/SubAgentTool.h"
#include "core/tools/ProposeEditTool.h"
#include "core/tools/ApplyProposalTool.h"
#include "core/tools/ProposalStore.h"
#include "core/tools/SubAgentControlTool.h"
#include "core/agent/BackgroundTaskHub.h"
#include "core/skills/Skill.h"
#include "core/tools/NetworkFetchTool.h"
#include "core/tools/WebSearchTool.h"
#include "core/memory/SqliteMemory.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "infrastructure/llm/IAIService.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"
#include "core/utils/TokenEstimator.h"
#include "core/utils/ModelOutputSanitizer.h"
#include "viewmodels/AgentViewModel.h"

#include <QSet>
#include <QSemaphore>
#include <QRegularExpression>
#include <QMutex>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSqlQuery>

#include <atomic>
#include <chrono>
#include <thread>
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using namespace NexusAI;

namespace {

// B1 test probe: tokenUsageRecords() omits source buckets, so read the row
// directly to prove subagent accounting landed in inputToolCall/outputToolCall.
QVariantList subagentTokenRows(SqliteMemory& memory, const QString& sessionId) {
    QVariantList rows;
    QSqlQuery q(memory.databaseForTest());
    q.prepare(QStringLiteral(
        "SELECT input_tokens, output_tokens, cache_hit_tokens, "
        "input_tool_call, output_tool_call, model "
        "FROM token_usage WHERE request_id = 0 AND session_id = ?"));
    q.addBindValue(sessionId);
    if (!q.exec()) return rows;
    while (q.next()) {
        rows.append(QVariantMap{
            {QStringLiteral("input"), q.value(0).toLongLong()},
            {QStringLiteral("output"), q.value(1).toLongLong()},
            {QStringLiteral("cacheHit"), q.value(2).toLongLong()},
            {QStringLiteral("inputToolCall"), q.value(3).toLongLong()},
            {QStringLiteral("outputToolCall"), q.value(4).toLongLong()},
            {QStringLiteral("model"), q.value(5).toString()},
        });
    }
    return rows;
}


class FakeAIService : public IAIService {
    Q_OBJECT
public:
    QVector<QJsonArray> seenMessages;   // history snapshot per chatComplete()
    QVector<QJsonArray> seenTools;
    QVector<LLMResponse> script;
    bool repeatLast{false};
    quint64 requestCounter{0};          // requestStarted()/requestId echo
    // A4 用例需要模拟“同模型重连 / 换模型”：默认值保持既有测试的 fake-model。
    QString modelName{QStringLiteral("fake-model")};

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);

        LLMResponse response;
        if (script.isEmpty()) {
            emit errorOccurred(QStringLiteral("FakeAIService: empty script"));
            return;
        }
        response = idx < script.size() ? script.at(idx) : script.last();
        response.requestId = requestId;
        ++idx;

        if (response.success) emit responseReady(response);
        else emit errorOccurred(response.error.isEmpty()
             ? QStringLiteral("FakeAIService: scripted error") : response.error);
    }
    void cancel() override {}
    void setApiKey(const QString&) override {}
    void setModel(const QString&) override {}
    void setBaseUrl(const QString&) override {}
    QString model() const override { return modelName; }

    void reset() {
        seenMessages.clear();
        seenTools.clear();
        script.clear();
        repeatLast = false;
        idx = 0;
        requestCounter = 0;
    }

private:
    int idx{0};
};

// A10: 首轮响应按“实际发出的 wire 请求”给出 promptTokens（消息 + tools
// schema 的 2 倍），用于验证 recordTokenUsage 的 EWMA 分母确实取了
// m_lastWireEstimate；后续轮不带 usage，避免再次更新系数。
class WireScalingFakeService : public FakeAIService {
public:
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);
        LLMResponse response = m_calls < script.size()
            ? script.at(m_calls) : script.last();
        if (m_calls == 0) {
            const int wire = TokenEstimator::estimateTokens(messages)
                           + TokenEstimator::estimateToolsSchema(tools);
            response.promptTokens = wire * 2;
        }
        ++m_calls;
        response.requestId = requestId;
        if (response.success) emit responseReady(response);
        else emit errorOccurred(response.error);
    }
private:
    int m_calls{0};
};

// A9: 在每次 chatComplete 时刻（响应回来之前）采集 contextComposition 的
// tools schema 预留，用来断言 forced-final 轮不再预留 schema。
class ForcedFinalSchemaService : public FakeAIService {
public:
    explicit ForcedFinalSchemaService(AIAgent* owner) : m_owner(owner) {}

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);
        if (m_owner) {
            schemas.append(static_cast<int>(
                m_owner->contextComposition().value(QStringLiteral("toolsSchema"))
                    .toLongLong()));
            toolsEmpty.append(tools.isEmpty());
        }
        LLMResponse response = m_calls < script.size()
            ? script.at(m_calls) : script.last();
        ++m_calls;
        response.requestId = requestId;
        if (response.success) emit responseReady(response);
        else emit errorOccurred(response.error);
    }

    QVector<int> schemas;
    QVector<bool> toolsEmpty;

private:
    AIAgent* m_owner;
    int m_calls{0};
};

LLMResponse textResponse(const QString& text) {
    LLMResponse r;
    r.success = true;
    r.content = text;
    return r;
}

LLMResponse toolResponse(const QString& name, const QJsonObject& args) {
    LLMResponse r;
    r.success = true;
    ToolCallInfo call;
    call.id = QStringLiteral("call_t0");
    call.name = name;
    call.arguments = args;
    r.toolCalls.append(call);
    return r;
}

// StepFun 风格的原生文本工具调用模板（真机原文，见 messages 表 id=98）。
QString textualToolCallMarkup() {
    return QStringLiteral(
        "<tool_call>\n"
        "<function=web_search>\n"
        "<parameter=max_results>\n8\n</parameter>\n"
        "<parameter=query>\n恒生指数 11月2日 收盘\n</parameter>\n"
        "</function>\n"
        "</tool_call>");
}

// docs/35 P4.4: the proposal id is only known after the sub-agent report
// lands in the main history, so the scripted response has to be built from
// the request it sees (a static FakeAIService script cannot know the UUID).
// docs/35 P4.6: a fake that streams asynchronously (inside the runner's event
// loop), so the 200ms progress throttle actually gets a chance to fire.
class StreamingFakeService : public FakeAIService {
public:
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);
        for (int i = 0; i < 5; ++i) {
            QTimer::singleShot(i * 10, this,
                               [this]() { emit streamingChunk(QStringLiteral("chunk ")); });
        }
        QTimer::singleShot(25, this, [this]() {
            emit reasoningChunk(QStringLiteral("pondering"));
        });
        QTimer::singleShot(350, this, [this, requestId]() {
            LLMResponse r;
            r.success = true;
            r.content = QStringLiteral("final answer");
            r.requestId = requestId;
            emit responseReady(r);
        });
    }
};

class ProposalFlowFakeService : public FakeAIService {
public:
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);

        LLMResponse response;
        if (requestCounter == 1) {
            response = toolResponse(QStringLiteral("spawn_agent"),
                QJsonObject{{QStringLiteral("goal"),
                             QStringLiteral("make main return zero")},
                            {QStringLiteral("role"), QStringLiteral("coder")}});
        } else if (requestCounter == 2) {
            QString proposalId;
            static const QRegularExpression idsRe(
                QStringLiteral("Proposal ids \\(pass one to apply_proposal\\): "
                               "([^\\s,]+)"));
            for (const auto& entry : messages) {
                const QRegularExpressionMatch match = idsRe.match(
                    entry.toObject().value("content").toString());
                if (match.hasMatch()) {
                    proposalId = match.captured(1);
                    break;
                }
            }
            response = toolResponse(QStringLiteral("apply_proposal"),
                QJsonObject{{QStringLiteral("id"), proposalId}});
        } else {
            response = textResponse(QStringLiteral("change applied"));
        }
        response.requestId = requestId;
        if (response.success) emit responseReady(response);
        else emit errorOccurred(response.error);
    }
};

// docs/35 P4.3: main-loop script for fork/join — the task id only exists after
// the spawn_agent ack, so round 2 parses it from the tool message.
class BackgroundFlowFakeService : public FakeAIService {
public:
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);

        LLMResponse response;
        if (requestCounter == 1) {
            response = toolResponse(QStringLiteral("spawn_agent"),
                QJsonObject{{QStringLiteral("goal"), QStringLiteral("survey the tree")},
                            {QStringLiteral("background"), true}});
        } else if (requestCounter == 2) {
            QString taskId;
            static const QRegularExpression idRe(
                QStringLiteral("\\[Sub-agent started\\] id=(sa-\\d+)"));
            for (const auto& entry : messages) {
                const QRegularExpressionMatch match = idRe.match(
                    entry.toObject().value("content").toString());
                if (match.hasMatch()) {
                    taskId = match.captured(1);
                    break;
                }
            }
            response = toolResponse(QStringLiteral("subagent_wait"),
                                    QJsonObject{{QStringLiteral("id"), taskId}});
        } else {
            response = textResponse(QStringLiteral("background work folded in"));
        }
        response.requestId = requestId;
        if (response.success) emit responseReady(response);
        else emit errorOccurred(response.error);
    }
};

class BatchWaitFakeService : public FakeAIService {
public:
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);

        LLMResponse response;
        if (requestCounter == 1) {
            response = toolResponse(QStringLiteral("spawn_agent"),
                QJsonObject{{QStringLiteral("goal"), QStringLiteral("one")},
                            {QStringLiteral("background"), true}});
        } else if (requestCounter == 2) {
            response = toolResponse(QStringLiteral("spawn_agent"),
                QJsonObject{{QStringLiteral("goal"), QStringLiteral("two")},
                            {QStringLiteral("background"), true}});
        } else if (requestCounter == 3) {
            // docs/43 O3: one batch wait for both acks.
            QJsonArray ids;
            static const QRegularExpression idRe(
                QStringLiteral("\\[Sub-agent started\\] id=(sa-\\d+)"));
            for (const auto& entry : messages) {
                const QRegularExpressionMatch match =
                    idRe.match(entry.toObject().value("content").toString());
                if (match.hasMatch() && !ids.contains(match.captured(1)))
                    ids.append(match.captured(1));
            }
            response = toolResponse(QStringLiteral("subagent_wait"),
                QJsonObject{{QStringLiteral("ids"), ids},
                            {QStringLiteral("mode"), QStringLiteral("all")},
                            {QStringLiteral("timeout_ms"), 5000}});
        } else {
            response = textResponse(QStringLiteral("batch folded"));
        }
        response.requestId = requestId;
        if (response.success) emit responseReady(response);
        else emit errorOccurred(response.error);
    }
};

QString toolMessageContent(const QJsonArray& messages, const QString& callId) {
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == "tool"
                && msg.value("tool_call_id").toString() == callId)
            return msg.value("content").toString();
    }
    return QString();
}

// The agent may regenerate duplicate provider call ids across turns, so the
// last tool message is the stable way to read the newest result.
QString lastToolMessageContent(const QJsonArray& messages) {
    QString content;
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QLatin1String("tool"))
            content = msg.value("content").toString();
    }
    return content;
}

// Batch 14: does the tools array passed to the LLM service expose the given
// tool? Matches both flat {"name":...} and OpenAI {"function":{"name":...}}.
bool toolsArrayContains(const QJsonArray& tools, const QString& name) {
    for (const auto& entry : tools) {
        const QJsonObject def = entry.toObject();
        if (def.value("name").toString() == name) return true;
        if (def.value("function").toObject().value("name").toString() == name)
            return true;
    }
    return false;
}

bool traceHas(const QJsonArray& trace, const QString& type,
              const QString& textContains = QString()) {
    for (const auto& entry : trace) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() != type) continue;
        if (textContains.isEmpty()
                || node.value("title").toString().contains(textContains)
                || node.value("result").toString().contains(textContains)
                || node.value("args").toString().contains(textContains))
            return true;
    }
    return false;
}

// 把下发的 tools 数组收敛为工具名集合，便于整集比较。
QSet<QString> toolsArrayNames(const QJsonArray& tools) {
    QSet<QString> names;
    for (const auto& entry : tools) {
        const QJsonObject def = entry.toObject();
        const QString name = def.value("function").toObject()
                                 .value("name").toString();
        if (!name.isEmpty()) names.insert(name);
    }
    return names;
}


// result messages covering all its ids - a dangling sequence is invalid for
// the OpenAI wire format and derails proxies on the next user turn.
bool noDanglingToolCalls(const QJsonArray& messages) {
    for (int i = 0; i < messages.size(); ++i) {
        const QJsonObject msg = messages.at(i).toObject();
        const QJsonArray calls = msg.value("tool_calls").toArray();
        if (calls.isEmpty()) continue;

        QSet<QString> unanswered;
        for (const auto& c : calls)
            unanswered.insert(c.toObject().value("id").toString());

        for (int j = i + 1; j < messages.size() && !unanswered.isEmpty(); ++j) {
            const QJsonObject later = messages.at(j).toObject();
            if (later.value("role").toString() != "tool") continue;
            unanswered.remove(later.value("tool_call_id").toString());
        }
        if (!unanswered.isEmpty()) return false;
    }
    return true;
}

// Every assistant-announced tool_call id must be unique across the whole
// history: Ollama-style "call_0" fallbacks and agent-generated fallbacks
// repeat across turns, and strict OpenAI-compatible proxies reject
// duplicated tool_call_id sequences.
bool noDuplicateToolCallIds(const QJsonArray& messages) {
    QSet<QString> seen;
    for (const auto& entry : messages) {
        const QJsonArray calls = entry.toObject().value("tool_calls").toArray();
        for (const auto& c : calls) {
            const QString id = c.toObject().value("id").toString();
            if (seen.contains(id)) return false;
            seen.insert(id);
        }
    }
    return true;
}

// Knowledge-base tool for the TEST-002 behavioral matrix (pseudo embedding,
// no network). Registered lazily on first use - the earlier
// dangerousToolsNotExposed test asserts the built-in-only tool count (4) and
// runs before any of the matrix tests.
void registerKnowledgeTool() {
    if (ToolRegistry::instance().isRegistered(QStringLiteral("search_knowledge_base")))
        return;
    static RagService rag; // outlives the registry's raw pointer
    rag.ingestDocument(QStringLiteral("团队手册"),
        QStringLiteral("孙曼是人文学院助理研究员，研究领域为明代文献学与道教史。"));
    ToolRegistry::instance().registerTool(
        std::make_unique<KnowledgeSearchTool>(&rag));
}

// A Safe tool that parks on a semaphore so a test can stop() the agent while
// the tool is genuinely executing on the QtConcurrent worker thread.
class BlockingTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_blocking");
        def.description = QStringLiteral("Test helper: blocks until released.");
        def.level = ToolLevel::Safe;
        def.parameters = QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}}, {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override {
        // Bounded park: a failing test must not wedge the worker pool forever.
        m_gate.tryAcquire(1, 15000);
        return ToolResult::ok(QStringLiteral("released"));
    }
    void release() { m_gate.release(); }
    // A queued invocation can now be cancelled before execute() enters.
    // Its test's release must not let the next invocation bypass its park.
    void resetGate() { m_gate.tryAcquire(m_gate.available()); }
private:
    QSemaphore m_gate;
};

BlockingTool* registerBlockingTool() {
    static BlockingTool* tool = nullptr;
    if (!ToolRegistry::instance().isRegistered(QStringLiteral("test_blocking"))) {
        auto owned = std::make_unique<BlockingTool>();
        tool = owned.get();
        ToolRegistry::instance().registerTool(std::move(owned));
    }
    tool->resetGate();
    return tool;
}

// Dangerous-level probe: proves the registry withholds Dangerous tools from
// the model's schema. No built-in tool carries this level anymore, so the
// dangerousToolsNotExposed test registers its own.
class DangerousProbeTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_dangerous");
        def.description = QStringLiteral("Test helper: must never be exposed.");
        def.level = ToolLevel::Dangerous;
        def.parameters = QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}}, {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override {
        return ToolResult::fail(QStringLiteral("must never execute"));
    }
};

// Removes the probe when the test scope ends - also on QCOMPARE failure,
// which returns early from the test function.
struct DangerousProbeGuard {
    ~DangerousProbeGuard() {
        ToolRegistry::instance().takeTool(QStringLiteral("test_dangerous"));
    }
};

// G1: confirm-level tool that explicitly declares read-only. Production
// web_search / http_fetch use the same flag; this probe exercises the
// PlanOnly exception without network access.
class ReadOnlyProbeTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_readonly");
        def.description = QStringLiteral("Test helper: read-only confirm-level tool.");
        def.level = ToolLevel::RequiresConfirm;
        def.readOnly = true;
        def.parameters = QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}}, {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override {
        return ToolResult::ok(QStringLiteral("probe-ok"));
    }
};

struct NamedToolGuard {
    explicit NamedToolGuard(QString name) : name(std::move(name)) {}
    ~NamedToolGuard() { ToolRegistry::instance().takeTool(name); }
    QString name;
};

// A Safe tool that can produce every anomaly the plan's Tool matrix lists,
// driven by a "mode" argument (deterministic anomalies for TEST-903).
class AnomalyTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_anomaly");
        def.description = QStringLiteral("Test helper: mode-driven anomalies.");
        def.level = ToolLevel::Safe;
        def.parameters = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{{"mode", QJsonObject{{"type", "string"}}}}},
            {"required", QJsonArray{QJsonValue(QStringLiteral("mode"))}}};
        return def;
    }
    ToolResult execute(const QJsonObject& args) override {
        const QString mode = args.value("mode").toString();
        if (mode == QStringLiteral("fail")) return ToolResult::fail(QStringLiteral("boom"));
        if (mode == QStringLiteral("empty")) return ToolResult::ok(QString());
        if (mode == QStringLiteral("huge")) return ToolResult::ok(QString(20000, u'x'));
        if (mode == QStringLiteral("sleep")) {
            // Batch 13: long-running execute, cancellable in slices. A hard
            // sleep_for(30s) would keep the trace "running" until the pool
            // thread returns (the cancelled-status assertion would be
            // unreachable) and park a worker for the full duration.
            // No entry reset of the flags: a cancel() that landed before
            // execute() entered (queued on the pool) must be honored - same
            // contract as ShellExecTool.
            for (int i = 0; i < 300; ++i) {   // up to 30 s
                if (m_cancelRequested.load())
                    return ToolResult::fail(QStringLiteral("cancelled"));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return ToolResult::ok(QStringLiteral("slept"));
        }
        return ToolResult::ok(QStringLiteral("fine"));
    }
    // Batch 13 (SEC-1301): GUI thread; atomics only.
    void cancel() override {
        m_cancelCalled.store(true);
        m_cancelRequested.store(true);
    }
    bool cancelCalled() const { return m_cancelCalled.load(); }
private:
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_cancelCalled{false};
};

// ── docs/32 P1: sub-agent fixtures ─────────────────────────────────────

// Safe+readOnly probe: a deterministic stand-in for the read tools so
// sub-agent tests never touch the filesystem.
class SubAgentProbeTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_sub_probe");
        def.description = QStringLiteral("Test helper: read-only probe for the sub-agent toolset.");
        def.level = ToolLevel::Safe;
        def.readOnly = true;
        def.parameters = QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}}, {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override {
        return ToolResult::ok(QStringLiteral("probe-evidence"));
    }
};

// docs/35 P4.1: deterministic padded output so a few rounds blow through a
// small context limit (the probe above returns a short string).
class LargeOutputProbeTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_large_output");
        def.description = QStringLiteral(
            "Test helper: returns a large padded read-only result.");
        def.level = ToolLevel::Safe;
        def.readOnly = true;
        def.parameters = QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}}, {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override {
        return ToolResult::ok(QString(2400, QChar('x')));
    }
};

// B8: a single sub-agent tool result is capped at 6000 chars, so the fallback
// context limit only becomes observable across many rounds. CJK content makes
// each round cost ~4000 tokens (1.5 chars/token), keeping the fixture small.
class HugeOutputProbeTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_huge_output");
        def.description = QStringLiteral(
            "Test helper: returns a large CJK-padded read-only result.");
        def.level = ToolLevel::Safe;
        def.readOnly = true;
        def.parameters = QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}}, {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override {
        return ToolResult::ok(QString(6000, QChar(0x4E2D)));
    }
};

// docs/35 P4.2: a sleeping read-only tool that records peak concurrency, so
// the test can prove the runner actually overlaps calls (and respects the cap).
class SleepProbeTool : public ITool {
public:
    SleepProbeTool(QString name, int sleepMs,
                   std::atomic<int>* active, std::atomic<int>* peak)
        : m_name(std::move(name)), m_sleepMs(sleepMs),
          m_active(active), m_peak(peak) {}

    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = m_name;
        def.description = QStringLiteral(
            "Test helper: sleeps and records concurrency.");
        def.level = ToolLevel::Safe;
        def.readOnly = true;
        def.parameters = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{{"n", QJsonObject{{"type", "integer"}}}}},
            {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject& args) override {
        const int now = m_active->fetch_add(1) + 1;
        int peak = m_peak->load();
        while (now > peak && !m_peak->compare_exchange_weak(peak, now)) {}
        QThread::msleep(static_cast<unsigned long>(m_sleepMs));
        m_active->fetch_sub(1);
        return ToolResult::ok(
            QStringLiteral("done %1").arg(args.value("n").toInt()));
    }

private:
    QString m_name;
    int m_sleepMs;
    std::atomic<int>* m_active;
    std::atomic<int>* m_peak;
};

// docs/35 P4.6：同步执行、只认 cancel() 的工具（web_search/http_fetch 的
// 实现方式）必须能被子代理的停止令牌打断，否则面板会卡在「正在停止…」
// 直到网络超时。
class CancelAwareBlockingProbeTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = QStringLiteral("test_blocking");
        def.description = QStringLiteral(
            "Test helper: blocks until cancel() is called.");
        def.level = ToolLevel::Safe;
        def.readOnly = true;
        def.parameters = QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}},
            {"required", QJsonArray{}}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override {
        QElapsedTimer timer;
        timer.start();
        while (!m_cancelRequested.load()) {
            if (timer.elapsed() > 5000) break;   // 防止看门线程失效时挂死
            QThread::msleep(10);
        }
        if (m_cancelRequested.load()) {
            cancelObserved = true;
            cancelLatencyMs = timer.elapsed();
        }
        return ToolResult::fail(QStringLiteral("blocking probe stopped"));
    }
    void cancel() override { m_cancelRequested.store(true); }

    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> cancelObserved{false};
    std::atomic<qint64> cancelLatencyMs{-1};
};

void ensureSubAgentProbe() {
    if (!ToolRegistry::instance().isRegistered(QStringLiteral("test_sub_probe")))
        ToolRegistry::instance().registerTool(std::make_unique<SubAgentProbeTool>());
}

// Removes spawn_agent when the test scope ends - also on QCOMPARE failure,
// which returns early from the test function.
struct SpawnToolGuard {
    ~SpawnToolGuard() {
        ToolRegistry::instance().takeTool(QStringLiteral("spawn_agent"));
    }
};

// Removes an auxiliary test tool from the registry when the test scope ends
// (also on QCOMPARE/QVERIFY early return).
struct ToolNameGuard {
    QString name;
    ~ToolNameGuard() { ToolRegistry::instance().takeTool(name); }
};

// FakeAIService variant for sub-agent runs: records the requests it sees into
// shared buffers (the runner deletes the service, so the copies must outlive
// it) and optionally parks the scripted reply behind a semaphore to emulate
// an in-flight network request inside the runner's event loop.
class RecordingFakeService : public FakeAIService {
public:
    RecordingFakeService(QMutex* mutex, QVector<QJsonArray>* tools,
                         QVector<QJsonArray>* messages)
        : m_mutex(mutex), m_tools(tools), m_messages(messages) {}
    void chatComplete(const QJsonArray& messages, const QJsonArray& toolsArr) override {
        {
            QMutexLocker lock(m_mutex);
            m_tools->append(toolsArr);
            m_messages->append(messages);
        }
        if (enteredFlag) enteredFlag->store(true);
        if (gate) gate->acquire();   // park before the scripted reply
        FakeAIService::chatComplete(messages, toolsArr);
    }
    QSemaphore* gate{nullptr};
    std::atomic<bool>* enteredFlag{nullptr};

private:
    QMutex* m_mutex;
    QVector<QJsonArray>* m_tools;
    QVector<QJsonArray>* m_messages;
};

// A service that accepts requests and never answers - only the runner's
// wall-clock abort can end such a run.
class SilentFakeService : public FakeAIService {
public:
    void chatComplete(const QJsonArray&, const QJsonArray&) override {}
};

// B2: 第一步正常回答（带 usage + tool_calls），之后永远挂起。取消/超时
// 路径收场时没有 response payload，Runner 绝不能复用上一步的计量。
class HangAfterFirstStepService : public FakeAIService {
public:
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        if (++m_calls >= 2) return;   // 后续步骤永不回包
        FakeAIService::chatComplete(messages, tools);
    }
private:
    int m_calls{0};
};

// B2: 第一步正常回答（带 usage），并在 60ms 后重复 emit 一个属于第一步
// requestId 的“迟到”responseReady；第二步只发请求（requestStarted）不回包，
// 靠取消旗标收场。旧实现整轮共用一条连接，迟到 payload 会落到第二步的
// loop 里被当成第二步响应（正文与 usage 串步）；每步连接隔离 + requestId
// 校验后必须丢弃它。
class LateDuplicateAfterFirstStepService : public FakeAIService {
public:
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);
        const quint64 requestId = ++requestCounter;
        emit requestStarted(requestId);
        if (requestId != 1) return;   // 第二步：请求已发出，响应永不到达
        LLMResponse first = toolResponse(QStringLiteral("test_sub_probe"), {});
        first.toolCalls[0].id = QStringLiteral("call_1");
        first.requestId = requestId;
        first.promptTokens = 100;
        first.completionTokens = 50;
        first.tokensUsed = 150;
        emit responseReady(first);
        QTimer::singleShot(60, this, [this, requestId]() {
            LLMResponse late = textResponse(QStringLiteral("late duplicate"));
            late.requestId = requestId;   // 上一步的请求
            late.promptTokens = 900;
            late.completionTokens = 100;
            late.tokensUsed = 1000;
            emit responseReady(late);
        });
    }
};

struct SubAgentFixture {
    QMutex mutex;
    QVector<QJsonArray> subSeenTools;
    QVector<QJsonArray> subSeenMessages;
    std::shared_ptr<SubAgentTool> tool;
};

// docs/32 修正：真实应用里 main.cpp 注册的 web 工具，测试内按需登记并在
// 守卫析构时摘除（只借 schema 走目录过滤，从不执行网络请求）。
struct WebToolsGuard {
    ~WebToolsGuard() {
        ToolRegistry::instance().takeTool(QStringLiteral("web_search"));
        ToolRegistry::instance().takeTool(QStringLiteral("http_fetch"));
    }
};

// docs/38 W6-2：spawn_agent 与后台控制工具在真实应用里由 main.cpp 注册，
// 单测按需登记并在守卫析构时只摘除自己新增的那些（不误删邻居夹具）。
class SubAgentToolsetGuard {
public:
    SubAgentToolsetGuard() {
        // P1-3: status 需要注入 spawn 引用才能输出 gateSummary（池线程
        // 禁止直查 ToolRegistry）。
        auto spawn = std::make_shared<SubAgentTool>();
        auto status = std::make_shared<SubAgentStatusTool>();
        status->setSpawnTool(spawn);
        add(QStringLiteral("spawn_agent"), spawn);
        add(QStringLiteral("subagent_wait"), std::make_shared<SubAgentWaitTool>());
        add(QStringLiteral("subagent_status"), status);
        add(QStringLiteral("subagent_cancel"), std::make_shared<SubAgentCancelTool>());
    }
    ~SubAgentToolsetGuard() {
        for (const QString& name : m_added)
            ToolRegistry::instance().takeTool(name);
    }

private:
    void add(const QString& name, std::shared_ptr<ITool> tool) {
        if (ToolRegistry::instance().isRegistered(name)) return;
        ToolRegistry::instance().registerTool(std::move(tool));
        m_added.append(name);
    }
    QStringList m_added;
};

void ensureNetworkTools() {
    if (!ToolRegistry::instance().isRegistered(QStringLiteral("web_search")))
        ToolRegistry::instance().registerTool(std::make_unique<WebSearchTool>());
    if (!ToolRegistry::instance().isRegistered(QStringLiteral("http_fetch")))
        ToolRegistry::instance().registerTool(std::make_unique<NetworkFetchTool>());
}

// Registers spawn_agent wired to a factory that dispenses one scripted
// RecordingFakeService per run. The guard must outlive the task.
std::shared_ptr<SubAgentTool> registerSpawnTool(
        SubAgentFixture& fixture,
        const std::function<void(RecordingFakeService&)>& script) {
    ensureSubAgentProbe();
    auto tool = std::make_shared<SubAgentTool>();
    tool->setServiceFactory([&fixture, script]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        script(*service);
        return service;
    });
    tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(tool);
    return tool;
}

AnomalyTool* registerAnomalyTool() {
    static AnomalyTool* tool = nullptr;
    if (!ToolRegistry::instance().isRegistered(QStringLiteral("test_anomaly"))) {
        auto owned = std::make_unique<AnomalyTool>();
        tool = owned.get();
        ToolRegistry::instance().registerTool(std::move(owned));
    }
    return tool;
}

// Batch 11: audit rows now include llm_request / task_complete observability
// entries alongside the tool events — matrix tests count by EVENT TYPE.
int auditCountByEvent(const QJsonArray& audit, const QString& event) {
    int n = 0;
    for (const auto& entry : audit) {
        if (entry.toObject().value("event").toString() == event) ++n;
    }
    return n;
}

bool traceHasRunningToolCall(const QJsonArray& trace, const QString& name) {
    for (const auto& entry : trace) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() == QStringLiteral("tool_call")
                && node.value("title").toString() == name
                && node.value("status").toString() == QStringLiteral("running"))
            return true;
    }
    return false;
}

// Batch 13: status of the newest tool_call node for the given tool name
// (empty when the tool was never dispatched).
QString lastToolCallStatus(const QJsonArray& trace, const QString& name) {
    QString status;
    for (const auto& entry : trace) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() == QStringLiteral("tool_call")
                && node.value("title").toString() == name)
            status = node.value("status").toString();
    }
    return status;
}

// B2-A: how many tool_call nodes for `name` are still running. A parallel
// burst must be observed fully in flight before stop() is called.
int runningToolCallCount(const QJsonArray& trace, const QString& name) {
    int count = 0;
    for (const auto& entry : trace) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() == QStringLiteral("tool_call")
                && node.value("title").toString() == name
                && node.value("status").toString() == QStringLiteral("running"))
            ++count;
    }
    return count;
}

// B2-A: number of tool-role messages carrying the given tool_call_id. The
// seal contract is exactly one per announced id - zero leaves a dangling
// sequence, two duplicates it.
int toolResponseCountForId(const QJsonArray& messages, const QString& id) {
    int count = 0;
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QStringLiteral("tool")
                && msg.value("tool_call_id").toString() == id)
            ++count;
    }
    return count;
}

// The default read sandbox only allows Documents/Downloads/Desktop/app data
// (audit P1-8), so test fixtures create their temp dirs there - a plain
// QTemporaryDir lands in TempLocation, which read_file must refuse.
QString testDirTemplate() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return base + QStringLiteral("/nexusai-tests-XXXXXX");
}

} // namespace

class TestAgent : public QObject {
    Q_OBJECT

    AIAgent agent;
    FakeAIService llm;
    FakeAIService otherLlm;   // fixture-owned: safe even when a test aborts early

private slots:
    // Batch 30 (F6): text attachments fold into the request copy of the user
    // message as "[Attached file: ...]" sections; images ride on the message.
    void textAttachmentsFoldIntoRequest() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        agent.setMemory(&memory);

        QJsonArray files;
        QJsonObject file;
        file["name"] = "notes.txt";
        file["content"] = "the launch code is 1234";
        files.append(file);
        agent.startTask(QStringLiteral("summarize the attachment"), {}, files);

        QTRY_VERIFY(!llm.seenMessages.isEmpty());
        const QJsonArray last = llm.seenMessages.last();
        bool found = false;
        for (const auto& entry : last) {
            const QJsonObject msg = entry.toObject();
            if (msg.value("role").toString() == "user"
                    && msg.value("content").toString()
                           .contains("the launch code is 1234")) {
                found = true;
                QVERIFY(msg.value("content").toString()
                            .contains("--- Attached file: notes.txt ---"));
                QVERIFY(!msg.contains("files"));   // stripped from the request
            }
        }
        QVERIFY(found);

        // The bubble renders modelData.files from the user_message trace node;
        // without this the attachment reaches the model but is invisible in
        // the transcript.
        bool traceHasFiles = false;
        for (const auto& entry : agent.traceModel()) {
            const QJsonObject node = entry.toObject();
            if (node.value("type").toString() != QLatin1String("user_message"))
                continue;
            const QJsonArray attached = node.value("files").toArray();
            QCOMPARE(attached.size(), 1);
            QCOMPARE(attached.first().toObject().value("name").toString(),
                     QStringLiteral("notes.txt"));
            traceHasFiles = true;
        }
        QVERIFY2(traceHasFiles, "user_message trace node must carry files");

        agent.setMemory(nullptr);
    }

    void initTestCase() {
        agent.setLLMService(&llm);
        agent.setToolRegistry(&ToolRegistry::instance());
        agent.setMemory(nullptr);
    }

    void init() {
        llm.reset();
        // A4 用例会把 agent 切到 otherLlm/third 等临时服务；QtTest 全程共用
        // 一个测试对象，init 统一恢复 fixture 默认服务，防止脚本错位串扰。
        otherLlm.modelName = QStringLiteral("fake-model");
        agent.setLLMService(&llm);
        agent.setContextLimit(8192);
        agent.setMemory(nullptr);
        // 工具执行预算生产默认关闭（0=无限）；预算用例显式设置后必须复位，
        // 否则会泄漏到后续用例（QtTest 全程共用一个测试对象）。
        agent.setMaxToolExecutions(0);
        // docs/25 B3: the episodic fixture is test-local — never leak it
        // into the neighbours (the async launch changes fresh-session flow).
        agent.setEpisodicService(nullptr);
        // docs/26 I4: 自动标题会追加一次 chatComplete，绝大多数用例的
        // seenMessages 计数断言不预期它——默认关，标题用例自行打开。
        agent.setAutoSessionTitleEnabled(false);
        // 权限模式用例会改动本实例（QtTest 全程共用一个测试对象），
        // 每个用例前回落默认，防止泄漏到后续用例。
        agent.setPermissionMode(PermissionMode::ConfirmChanges);
        // 同理：taskTimeoutSealsAndConverges 之类的用例会把墙钟预算改小，
        // 不复位会误杀随后任何慢于该阈值的用例（子代理墙钟用例即中招）。
        agent.setTaskTimeoutMs(AIAgent::kDefaultTaskTimeoutMs);
        agent.resetSession();
    }

    void cleanup() {
        // Batch 14: the registry is a process-wide singleton shared by the
        // whole suite - a local-only test that disabled the KB tool must not
        // leak that state into the knowledge-base matrix tests (idempotent
        // for tests that never disabled anything).
        ToolRegistry::instance().setToolEnabled(
            QStringLiteral("search_knowledge_base"), true);
    }

    // docs/17 P0-1: the active project enters the system prompt — the model's
    // only anchor for resolving user-mentioned relative paths.
    void projectContextBlockContent() {
        agent.setSystemPromptOverride(QString());
        agent.setActiveProject(QString(), QString(), QString());
        QVERIFY(!agent.effectiveSystemPrompt()
                     .contains(QStringLiteral("Active project")));

        agent.setActiveProject(QStringLiteral("p1"), QStringLiteral("Demo"),
                               QStringLiteral("D:/work/demo"));
        const QString prompt = agent.effectiveSystemPrompt();
        QVERIFY2(prompt.contains(QStringLiteral("Active project: \"Demo\"")),
                 qPrintable(prompt.right(400)));
        QVERIFY(prompt.contains(QStringLiteral("root: D:/work/demo")));

        // User override prompts get the block appended too, otherwise a
        // custom prompt would blind the model to the project again.
        agent.setSystemPromptOverride(QStringLiteral("You are a pirate."));
        QVERIFY(agent.effectiveSystemPrompt()
                    .startsWith(QStringLiteral("You are a pirate.")));
        QVERIFY(agent.effectiveSystemPrompt()
                    .contains(QStringLiteral("D:/work/demo")));

        agent.setSystemPromptOverride(QString());
        agent.setActiveProject(QString(), QString(), QString());
    }

    // startTask must carry the project root in the request's system message
    // (resumeSession shares effectiveSystemPrompt and needs no extra path).
    void projectContextEntersRequestSystemMessage() {
        agent.setActiveProject(QStringLiteral("p1"), QStringLiteral("Demo"),
                               QStringLiteral("D:/work/demo"));
        agent.startTask(QStringLiteral("hello"));
        QTRY_VERIFY(!llm.seenMessages.isEmpty());
        bool found = false;
        for (const auto& entry : llm.seenMessages.last()) {
            const QJsonObject msg = entry.toObject();
            if (msg.value("role").toString() == "system") {
                QVERIFY(msg.value("content").toString()
                            .contains(QStringLiteral("D:/work/demo")));
                found = true;
            }
        }
        QVERIFY(found);
        agent.setActiveProject(QString(), QString(), QString());
        agent.resetSession();
    }

    // docs/18 T5: 归属在**发出首条消息**时落定——空标签先选好目标，首条消息
    // 创建的会话就归属该项目。这正是徽标"空标签可点选目标"的落点。
    void firstMessageLandsInSelectedProject() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        const QString projectId = memory.createProject(QStringLiteral("demo"),
                                                       QStringLiteral("D:/work/demo"));
        QVERIFY(!projectId.isEmpty());
        agent.setMemory(&memory);

        // 按 id 精确取会话归属，不依赖 updated_at 排序（同毫秒可能并列）
        auto projectOf = [&memory](const QString& sessionId) {
            const QJsonArray all = memory.listSessions(50);
            for (const auto& entry : all) {
                const QJsonObject o = entry.toObject();
                if (o.value("id").toString() == sessionId)
                    return o.value("projectId").toString();
            }
            return QStringLiteral("<missing>");
        };

        // 目标 = 项目 demo → 首条消息的会话归属该项目
        agent.setActiveProject(projectId, QStringLiteral("demo"),
                               QStringLiteral("D:/work/demo"));
        llm.script.append(textResponse(QStringLiteral("ok")));
        agent.startTask(QStringLiteral("hello"));
        QTRY_VERIFY(!llm.seenMessages.isEmpty());
        const QString projectSession = agent.currentSessionId();
        QVERIFY(!projectSession.isEmpty());
        QCOMPARE(projectOf(projectSession), projectId);

        // 目标 = 本地 → 新会话不归属任何项目（projectId 空串 = NULL）
        agent.setActiveProject(QString(), QString(), QString());
        agent.resetSession();
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok2")));
        agent.startTask(QStringLiteral("hello2"));
        QTRY_VERIFY(llm.seenMessages.size() == 1);
        const QString localSession = agent.currentSessionId();
        QVERIFY(!localSession.isEmpty());
        QVERIFY(localSession != projectSession);
        QCOMPARE(projectOf(localSession), QString());

        agent.setActiveProject(QString(), QString(), QString());
        agent.setMemory(nullptr);
        agent.resetSession();
    }

    void directCompletion();
    void dangerousToolsNotExposed();
    void safeToolFeedbackLoop();
    void confirmApproveWritesFile();
    void fileCheckpointRecordedAndUndone();
    void fileCheckpointsUndoWholeFile();
    // UE-1: 审阅按钮的"未查看改动"高亮。语义是"有我没看过的改动"：
    // 新增改动点亮，确认后清零，之后的新改动再次点亮。
    void unreviewedFileCountTracksUnseenChanges();
    // 审阅的"确认"判定：接受改动后文件保留、行变已确认、不再可撤销；
    // 全部确认一次处理所有待决文件。
    void acceptFileCheckpointSettlesTheChange();
    // 全部撤销：待决文件被还原/删除，已确认与仅路径的记录被跳过，
    // 单个失败不影响其余，且结果如实上报（不把部分失败当成功）。
    void undoAllFileCheckpointsRevertsPendingOnly();
    void confirmDeclineFeedsRejection();
    // 会话权限模式：AutoEdit/FullAccess 审批旁路矩阵
    void autoEditBypassesFileTools();
    // 自动编辑下 git 只读工具免确认（不再被迫走 shell_exec）。
    void autoEditBypassesGitReadOnlyTools();
    void autoEditStillConfirmsShell();
    void fullAccessBypassesShellButNotDangerous();
    // G1: PlanOnly (read-only) matrix
    void planOnlyBlocksWorkspaceTools();
    void planOnlyAllowsSafeAndReadOnlyTools();
    void planOnlySwitchRestoresWrites();
    void planOnlyRefusesApprovalOfPendingWrite();
    void permissionModeStringRoundTrip();
    void unknownToolFeedsError();
    void maxIterationsStops();
    void repeatedCallWithAnswerFinishes();
    void oldToolRoundsCollapsed();
    // P0：单任务内的工具轮次不得被折叠（否则模型遗忘刚读到的东西，
    // 反复重读同一批路径，任务跑不完）。
    void currentTurnToolRoundsSurvive();
    void traceRestoredOnResume();
    // P1：长会话恢复只读最近一段轨迹，并给出可见提示（否则像历史丢了）。
    void resumeLoadsOnlyRecentTraceNodes();
    // F1: todo list lifetime is per session/tab, never carried across resets.
    void todoListClearedOnSessionResetAndResume();
    // F1: the todo list is persisted per session — reopening the SAME session
    // restores its list, and a different session yields its own (or empty).
    void todoListPersistsAcrossSessionResume();
    void todoIdsStableAcrossRewrites();
    void todoStateInjectedIntoSystemPromptAndKeptCurrent();
    void todoIdsNotReusedAfterListClear();
    void spawnAgentTodoIdEchoedInReportAndAudit();
    // docs/26 I4: 会话标题自动生成。
    void autoSessionTitleGeneratedAfterFirstExchange();
    void autoSessionTitleSkippedWhenUserRenamed();
    void autoSessionTitleFailureKeepsTruncatedTitle();
    // 思考强度：思考正文只落 thinking 节点，不混进答案、不回灌模型、可恢复
    void reasoningTextLandsOnThinkingNode();
    void stopDuringConfirmationIsNoOp();
    // 复现：同一轮里"自动放行的调用"排在"需确认的调用"之前时，
    // 确认期间不得推进到下一轮 LLM 请求。
    void confirmationSuspendsEvenWithParallelAutoApprovedCalls();
    void contextBudgetCompressesHistory();
    void auditTrailRecorded();
    // TEST-002: Knowledge/Normal behavioral matrix (agent level, scripted
    // provider). Locks the contract the provider-state fix must preserve:
    // once the provider stops leaking stale tool calls, a normal follow-up
    // question in the same session completes without any tool execution.
    void knowledgeThenNormalQuestion();
    void normalThenKnowledgeQuestion();
    void knowledgeNormalKnowledgeSequence();
    // Batch 2: agent tool-call lifecycle (FIX-201/202/203)
    void stopDuringConfirmationSealsToolCalls();
    void stopDuringExecutionSealsToolCalls();
    // B2-A: stop 必须封口同一轮所有在飞调用，而不只是最后一个派发的。
    void stopSealsEveryParallelToolCall();
    // 无参数工具在报文里的 arguments 必须是 "{}" 而不是 ""。
    void emptyArgumentsSerializeAsEmptyObject();
    // Batch 13: cooperative tool cancellation (SEC-1301..1304)
    void toolCancelledOnStop();
    void shellProcessKilledOnStop();
    // Batch 14: local-only mode knowledge-tool gating (SEC-1401..1404)
    void localOnlyModeHidesKnowledgeTool();
    void localOnlyModeRejectsResidualCall();
    // Batch 14 review fix: isToolEnabled predicate semantics
    void toolRegistryEnablementSemantics();
    void toolExecutionBudgetForcesFinal();
    void mainAgentToolExecutionsUnlimitedByDefault();
    void forcedFinalControlMessageNotPersisted();
    // A9: forced-final 请求不带 tools，窗口占用/来源统计也不得预留 schema。
    void forcedFinalDoesNotReserveToolsSchema();
    // docs/35: 模型原生文本工具调用模板不得当成回答（真机写进 messages 表）。
    void textualToolCallMarkupDiscardedFromAnswer();
    void forcedFinalTextualToolCallDoesNotBecomeTheAnswer();
    void textualToolCallSanitizerHandlesTruncatedAndMultipleBlocks();
    // Batch 4: request-context architecture (ARCH-402/403)
    void switchServiceMidTaskTerminatesCleanly();
    void staleResponseFromSupersededRequestIgnored();
    // Batch 7: resume semantics contract (MEM-701)
    void resumeKeepsCleanHistoryAcrossTurns();
    // Batch 9: test-matrix completion (TEST-902/903)
    void toolAnomalyPathsConverge();
    void multipleDistinctToolCallsInOneResponse();
    void concurrentAgentsStayIsolated();
    // Batch 10: stability (REL-1001/1003)
    void taskTimeoutSealsAndConverges();
    // Batch 11: observability (OBS-1101/1102/1103)
    void auditCarriesRequestContext();
    // Batch 22 (F8): user-editable system prompt
    void systemPromptOverrideReachesRequest();
    // docs/37 W3：$ 引用技能注入系统提示 + metadata/审计 + 8KB/32KB 截断。
    void skillInjectionAndBudget();
    // docs/41：allowed-tools 仅作为 legacy frontmatter 保留，运行期不限制工具。
    void allowedToolsFrontmatterDoesNotRestrictTools();

    // docs/15 §6.1.2: model-switch context-window adaptation
    void testSwitchToLargerWindowRestoresHistory();
    void testSwitchToSmallerWindowCompressesImmediately();
    void testResumeSessionAdaptsToContextLimit();
    void testNoAdaptationDuringActiveTask();
    // docs/20 §3.1（FORK-701）：重新生成
    void regenerateDropsLastAnswerAndResends();
    void regenerateWithoutHistoryOrBusyIsNoop();
    // docs/16 §13 AC-2/AC-4: token_usage 采集埋点（response 有分类 token 时正确采集）
    void testRecordTokenUsage();
    // docs/16 §5.1 统一口径：Anthropic cache_read 计入 input/total（cacheHit 是其子集）
    void testAnthropicCacheReadCountedInInput();
    // A5: 图片 token 只进 inputImage，不在 conversation 里重复计入
    void testImageTokensNotDoubleCounted();
    // B3: tokensUsed 含 thoughts（Gemini）时以总量为准补 output 差额
    void mainUsageTotalIncludesThoughts();
    // docs/16 §8.2: 会话累计 token（逐请求累加 / 清零 / 续会话恢复）
    void testSessionTokensUsed();
    // docs/16 §13 AC-7: 配额检查（超限 emit quotaWarning，strict 模式拒绝）
    void testCheckQuota();
    // docs/31 S4.1 N9：达到阈值（未超限）提醒一次，同一天不重复。
    void testQuotaAlertThreshold();
    // docs/16 §3.3: 估算器滚动校准（API 真实 promptTokens 反馈 → EWMA 系数）
    void testEstimateScaleCalibration();
    // A10: EWMA 分母取实际 wire 请求（files 折叠 / forced-final push）。
    void tokenEstimateScaleUsesWireRequest();
    // A2: 校准系数必须同样作用于 tools schema（与 EWMA 观测分母同口径）
    void testEstimateScaleAppliedToToolSchema();
    // A4: 切换模型时系数复位；同模型重连保留观测
    void testEstimateScaleResetOnModelSwitch();
    // A4: 同一 service 实例（configure 原地改 model）换模型清零；
    // setLLMService(nullptr) 跨空切换后同样比较观测基准。
    void estimateScaleResetOnSameServiceModelChange();
    // B1: runKey 带 sessionId，跨会话相同 call id 的同步 spawn 各自入账。
    void subagentRunKeySeparatesSessions();
    // docs/25 B3: fresh-session launch with an episodic service attached —
    // the retrieval resolves off-thread, the injection reaches the system
    // prompt, and the task completes normally.
    void taskWithEpisodicAsyncLaunch();
    // docs/25 B3: a slow embedder is bounded by the launch deadline — the
    // task starts anyway and the late retrieval result must not relaunch.
    void taskWithSlowEmbedderStillLaunches();
    // 审查 M2（任务 #12）：VM 层 QVariantMap 键名契约——QML delegate 消费的
    // modelData.* 字段名不可偏离（AgentViewModel.h 注释的机器验证）。
    void listUsageEventsExposesFieldContract();

    // docs/32 P1: 子代理（spawn_agent）
    void subagentDisabledNotExposed();
    // docs/43 O1: 编排准则、项目规则继承、瞬时失败自动重派。
    void orchestrationPolicyInSystemPrompt();
    void subagentInheritsProjectRules();
    void subagentTransientFailureRetriesOnce();
    void subagentRetryAccumulatesClassifiedUsage();
    // B2: 取消/超时步骤不得复用上一步的 response 计量（不翻倍）。
    void subagentCancelDoesNotDoubleCountUsage();
    // B2: 每步信号连接隔离——上一请求的迟到 responseReady 不得污染下一步。
    void subagentStepSignalsAreIsolated();
    // B3: cacheRead/cacheCreation 原值保留并进入 reportJson
    void subagentUsageKeepsCacheClassification();
    // B1: 同步 spawn 的用量入账到 token_usage（requestId=0 + tool 来源）
    void subagentUsageAccountedIntoSyncSession();
    // B1: 后台任务 taskFinished 入账一次，subagent_wait 收割不重复
    void subagentUsageAccountedOnceForBackgroundRun();
    void subagentRetrySnapshotPinsBackgroundDecision();
    // docs/43 O2: 结构化报告（findings/usage/stopCode）经 trace 与审计回传。
    void subagentReportCarriesFindingsAndUsage();
    void subagentWaitAuditCarriesStructuredUsage();
    // docs/43 O3: batch wait (any/all)、槽位可见、后台任务状态进系统提示。
    void subagentBatchWaitAnyAndAll();
    void subagentSlotPressureIsVisible();
    void subagentBatchWaitAuditAggregates();
    // B4: 批量 wait 部分任务未上报分类用量时保留 OR 语义并标记 partial。
    void subagentBatchWaitMarksPartialUsage();
    // docs/43 O4-2: 失败/取消的后台任务按原始参数一键重派。
    void subagentRetryFailedBackgroundTask();
    // docs/43 O4 审计收口第二批：终态审计/会话保留/排队上限/todo 回显/
    // findings 排序/未上报 usage 不入库。
    void subagentFinishAuditRowWritten();
    void subagentSurvivesSessionSwitch();
    void subagentSlotQueueTimesOut();
    void subagentWaitEchoesTodoId();
    void subagentTopFindingsSortedBySeverity();
    void subagentAuditOmitsUnreportedUsage();
    // docs/43 O4-3: findings 逐条结构化查询（面板详情区数据通道）。
    void subagentFindingsQueryReturnsStructuredList();
    void subagentReportFlowsBackToMainLoop();
    void subagentStepBudgetConverges();
    void subagentTimeoutAborts();
    void subagentStopCascades();
    // docs/32 §9: 会话窗口手动停止子代理（主任务继续）。
    void subagentManualCancelKeepsPrimaryRunning();
    // docs/34 P3.1: 写类子代理——提案工具、coder 角色、报告渲染。
    void proposeEditValidatesAnchorsAndNeverWrites();
    void coderRoleProposalsReachReportUnapplied();
    void exploreRoleCannotPropose();
    // docs/34 P3.3: 同文件多提案的合并提示与重叠标注。
    void coderSameFileProposalsGetMergeHints();
    // docs/35 P4.4: 提案 id 注入报告；apply_proposal 预览/应用/重复/锚点失效。
    void applyProposalToolValidatesAndApplies();
    void applyProposalPreviewAndApplyThroughAgent();
    // docs/35 P4.3: 后台子代理——ack/status/wait、超时非失败、按任务取消、
    // 主循环 fork/join 与 subagent_task trace 节点。
    void subagentBackgroundAckStatusWait();
    void subagentBackgroundCancelOneTask();
    void subagentBackgroundForkJoinThroughAgent();
    // docs/35 P4.3/P4.5: 面板数据与取消经 AgentVM 转发（TopBar/对话框的
    // 真实数据通道），而不是直接读 AIAgent。
    void subagentBackgroundPanelThroughViewModel();
    // 2026-11-03：面板实时统计——运行中步数/进度、槽位排队、取消中状态。
    void backgroundHubReportsLiveProgressAndCancelling();
    // B3-C: 后台任务绑定 sessionId——旧会话任务不得进入新会话的面板/历史。
    void backgroundTasksStayBoundToTheirSession();
    // B3-C: hub 析构在非池线程限时返回，不被不配合取消的任务挂住。
    void backgroundHubDestructorDoesNotHang();
    void subagentStopChipThroughViewModel();
    void subagentInflightAbortStressRealNetwork();
    // docs/35 P4.1: 子代理上下文预算——超限时只压缩线上请求，证据工件保留
    // 完整转录；压缩事件进过程日志。
    void subagentCompressesContextOnTheWire();
    // B8: contextLimit=0（profile 解析失败/未知）也必须用保守默认上限压缩。
    void subagentCompressesWithFallbackLimit();
    // B9: provider 不上报 usage 时用 TokenEstimator 回退并标记 estimated。
    void subagentWithoutUsageFallsBackToEstimatedTokens();
    // docs/35 P4.2: 同轮只读工具并发（有上限）但结果按调用顺序回填；
    // propose_edit 混排时仍在运行线程串行执行、提案顺序不变。
    void subagentReadOnlyToolsRunConcurrently();
    void subagentCancellationAbortsInflightTool();
    void subagentMixedRoundKeepsCallOrder();
    void subagentConcurrencyGateQueues();
    // docs/35 P4.5: 排队事件上标题；卡片级停止只影响目标子代理。
    void subagentQueuedProgressReported();
    void backgroundSlotQueueSurfacesThroughTool();
    void subagentCancelOnlyTargetNode();
    // docs/32 P2: 进度上屏与审计归因
    void subagentProgressUpdatesTraceNode();
    void subagentAuditCarriesSubagentStats();
    // docs/33 E1: 证据工件
    void subagentEvidenceArtifactWrittenAndPruned();
    // docs/33 E2: 角色档案。
    void subagentRolePresetShapesPromptAndSchema();
    void subagentUserRoleOverridesAndFiltersTools();
    void subagentUnknownRoleFailsReadable();
    void subagentRolesFromVariantParsesAndDedupes();
    // docs/35：角色级默认后台开关——省略 background 时按角色默认执行，
    // 显式 false 仍可强制同步。
    void subagentRoleDefaultBackgroundStartsAsync();
    // 2026-11-03：设置页「内置子代理默认后台运行」统一控制预设角色，
    // 用户同名档案仍然优先。
    void subagentPresetBackgroundSettingAppliesToBuiltins();
    // docs/35 P4.6: 角色级模型配置与流式进度回传。
    void subagentRoleModelProfileReachesFactory();
    void subagentStreamsProgressThrottled();
    void subagentStreamingLogCollapses();
    void reviewCriticLoopGuidanceInPresetAndSchema();
    void subagentCatalogCarriesNetworkTools();

    // G3: project rules file injection (docs/26 G3)
    void projectRulesInjectedIntoSystemPrompt() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        agent.setMemory(nullptr);
        agent.setActiveProject(QStringLiteral("p1"), QStringLiteral("Demo"),
                               dir.path());
        agent.resetSession();
        QString baseline;
        {
            llm.reset();
            llm.script.append(textResponse(QStringLiteral("ok")));
            QSignalSpy spy(&agent, &AIAgent::responseReady);
            agent.startTask(QStringLiteral("first"));
            QTRY_COMPARE(spy.count(), 1);
            baseline = llm.seenMessages.first().first().toObject()
                           .value("content").toString();
        }
        QVERIFY(baseline.contains("Active project"));
        QVERIFY(!baseline.contains("Project rules"));
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write("# Demo rules\n- use ninja\n");
        rules.close();
        // The system prompt is assembled only for a fresh session, so the
        // rules written mid-session need a new one to take effect.
        agent.resetSession();
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy2(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("second"));
        QTRY_COMPARE(spy2.count(), 1);
        const QString withRules = llm.seenMessages.first().first()
                                      .toObject().value("content").toString();
        QVERIFY(withRules.contains("use ninja"));
        QVERIFY(withRules.contains("Active project"));
        QVERIFY(withRules.indexOf("Project rules")
                    > withRules.indexOf("Active project"));
        agent.setActiveProject(QString(), QString(), QString());
    }

    // The system prompt is rebuilt at every task start (not once per
    // session): rules written MID-SESSION reach the next message without a
    // resetSession - the settings page's "next message" promise.
    void rulesEditMidSessionReachesNextMessage() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        agent.setMemory(nullptr);
        agent.setActiveProject(QStringLiteral("p8"), QStringLiteral("Live"),
                               dir.path());
        agent.resetSession();
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("first"));
        QTRY_COMPARE(spy.count(), 1);
        const QString baseline = llm.seenMessages.first().first().toObject()
                                     .value("content").toString();
        QVERIFY(!baseline.contains("Project rules"));

        // Mid-session edit - NO resetSession here.
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write("- live reload rule\n");
        rules.close();
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy2(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("second"));
        QTRY_COMPARE(spy2.count(), 1);
        const QString withRules = llm.seenMessages.first().first().toObject()
                                      .value("content").toString();
        QVERIFY(withRules.contains("live reload rule"));
        QVERIFY(withRules.contains("Active project"));
        agent.setActiveProject(QString(), QString(), QString());
    }

    void seedTemplateIsNotInjected() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write(AIAgent::rulesSeedTemplate());
        rules.close();
        agent.setMemory(nullptr);
        agent.setActiveProject(QStringLiteral("p2"), QStringLiteral("Seeded"),
                               dir.path());
        agent.resetSession();
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("hello"));
        QTRY_COMPARE(spy.count(), 1);
        const QString content = llm.seenMessages.first().first()
                                    .toObject().value("content").toString();
        QVERIFY(!content.contains("use ninja"));
        QVERIFY(!content.contains("Project rules"));
        agent.setActiveProject(QString(), QString(), QString());
    }

    void oversizedRulesTruncated() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write("- rule\n");
        rules.write(AIAgent::rulesSeedTemplate());
        rules.write(QByteArray(AIAgent::kProjectRulesMaxBytes + 512, 'z'));
        rules.close();
        agent.setMemory(nullptr);
        agent.setActiveProject(QStringLiteral("p3"), QStringLiteral("Big"),
                               dir.path());
        agent.resetSession();
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("hello"));
        QTRY_COMPARE(spy.count(), 1);
        const QString content = llm.seenMessages.first().first()
                                    .toObject().value("content").toString();
        QVERIFY(content.contains("[truncated]"));
        // The injected block carries at most the cap, not the whole file.
        QVERIFY(content.count(QLatin1Char('z')) <= AIAgent::kProjectRulesMaxBytes);
        QVERIFY(content.count(QLatin1Char('z')) > AIAgent::kProjectRulesMaxBytes / 2);
        agent.setActiveProject(QString(), QString(), QString());
    }

    // A file over the cap whose cut lands mid-character: the cut must back
    // off to a UTF-8 character boundary. Before that fix the split byte
    // decoded to U+FFFD, the whole file was re-read as local 8-bit, and a
    // valid Chinese rules file entered the prompt as mojibake on Windows.
    void oversizedUtf8RulesTruncateOnCharBoundary() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        QByteArray chinese;
        chinese.reserve(AIAgent::kProjectRulesMaxBytes + 1024);
        while (chinese.size() < AIAgent::kProjectRulesMaxBytes + 1024)
            chinese += "规则";
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write(chinese);
        rules.close();
        agent.setActiveProject(QStringLiteral("p6"), QStringLiteral("Cn"),
                               dir.path());
        const QString block = agent.projectRulesBlock();
        QVERIFY(block.contains("[truncated]"));
        QVERIFY(!block.contains(QChar::ReplacementCharacter));
        QVERIFY(block.contains(QStringLiteral("规则")));
        agent.setActiveProject(QString(), QString(), QString());
    }

    // The untouched template re-saved by a CRLF-rewriting editor is still
    // "no rules": line endings are not content.
    void crlfSeedTemplateIsNotInjected() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write(QByteArray(AIAgent::rulesSeedTemplate())
                        .replace('\n', "\r\n"));
        rules.close();
        agent.setActiveProject(QStringLiteral("p7"), QStringLiteral("Cr"),
                               dir.path());
        QVERIFY(agent.projectRulesBlock().isEmpty());
        agent.setActiveProject(QString(), QString(), QString());
    }

    // Writing rules never touches session titles (manual renames survive).
    void rulesDoNotTouchManualRenames() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        SqliteMemory memory(":memory:");
        agent.setMemory(&memory);
        agent.setActiveProject(QStringLiteral("p4"), QStringLiteral("R"),
                               dir.path());
        agent.resetSession();
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write("- rules\n");
        rules.close();
        agent.setActiveProject(QStringLiteral("p4"), QStringLiteral("R"),
                               dir.path());
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("hello"));
        QTRY_COMPARE(spy.count(), 1);
        const QString sessionId = agent.currentSessionId();
        memory.renameSession(sessionId, "my title");
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy2(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("again"));
        QTRY_COMPARE(spy2.count(), 1);
        QCOMPARE(memory.listSessions().first().toObject().value("title")
                     .toString(), QString("my title"));
        agent.setMemory(nullptr);
        agent.setActiveProject(QString(), QString(), QString());
    }

    // G3: rules stack on top of a user override (override is "replace the app
    // prompt", not "replace the project's own conventions"). The citation
    // instruction stays app-only, so it must NOT appear under an override.
    void rulesStackOnTopOfSystemPromptOverride() {
        QTemporaryDir dir(testDirTemplate());
        QVERIFY(dir.isValid());
        QFile rules(dir.path() + "/AGENTS.md");
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write("# Rules\n- always answer in French\n");
        rules.close();

        agent.setMemory(nullptr);
        agent.setActiveProject(QStringLiteral("p5"), QStringLiteral("Ov"),
                               dir.path());
        agent.setSystemPromptOverride(QStringLiteral("OVERRIDE PROMPT"));
        agent.resetSession();
        llm.reset();
        llm.script.append(textResponse(QStringLiteral("ok")));
        QSignalSpy spy(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("hello"));
        QTRY_COMPARE(spy.count(), 1);
        const QString content = llm.seenMessages.first().first()
                                    .toObject().value("content").toString();
        QVERIFY(content.startsWith("OVERRIDE PROMPT"));
        QVERIFY(content.contains("always answer in French"));
        QVERIFY(content.contains("Active project"));
        QVERIFY(!content.contains("You are NexusAI"));   // override replaced it
        QVERIFY(content.indexOf("always answer in French")
                    > content.indexOf("Active project"));   // lock the order

        agent.setSystemPromptOverride(QString());
        agent.setActiveProject(QString(), QString(), QString());
    }
};

void TestAgent::directCompletion() {
    llm.script.append(textResponse(QStringLiteral("Final answer")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("hello"));
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(agent.state(), AgentState::Completed);
    QCOMPARE(agent.lastResponse(), QStringLiteral("Final answer"));
    QCOMPARE(llm.seenMessages.size(), 1);
}

void TestAgent::dangerousToolsNotExposed() {
    // Built-ins carry no Dangerous level anymore (destructive work goes
    // through the confirm-gated shell_exec), so the filter is exercised
    // with a probe tool instead.
    DangerousProbeGuard guard;
    ToolRegistry::instance().registerTool(std::make_unique<DangerousProbeTool>());

    llm.script.append(textResponse("ok"));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask("hi");
    QTRY_COMPARE(spy.count(), 1);

    // The Safe/RequiresConfirm built-ins are exposed; the Dangerous probe
    // must be withheld from the schema entirely.
    const auto seen = llm.seenTools.first();
    // 内置工具总数：read_file / write_file / edit_file / list_dir /
    // shell_exec / glob / grep / git_status / git_diff / git_log / git_show /
    // git_commit / todo_write = 13（Dangerous 的探针工具不计入）。
    QCOMPARE(seen.size(), 13);
    for (const auto& entry : seen) {
        QVERIFY(entry.toObject().value("function").toObject().value("name").toString()
                    != QStringLiteral("test_dangerous"));
    }
}

void TestAgent::safeToolFeedbackLoop() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("hello.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("hello agent");
    }

    llm.script.append(toolResponse("read_file", QJsonObject{{"path", path}}));
    llm.script.append(textResponse("done"));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("read it"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    // Second LLM call must carry the tool result message
    QCOMPARE(llm.seenMessages.size(), 2);
    const QString toolContent = toolMessageContent(llm.seenMessages.last(), "call_t0");
    QVERIFY2(toolContent.contains("hello agent"), qPrintable(toolContent));
    QVERIFY(traceHas(agent.traceModel(), "tool_result", "hello agent"));
    QVERIFY(traceHas(agent.traceModel(), "tool_call", "read_file"));
}

void TestAgent::confirmApproveWritesFile() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("out.txt");

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", path}, {"content", "payload"}}));
    llm.script.append(textResponse("written"));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("write it"));
    QTRY_COMPARE(confirmSpy.count(), 1);
    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);

    // P3: verify the preview (diff) is generated and non-empty for write_file.
    const QList<QVariant> confirmArgs = confirmSpy.first();
    QCOMPARE(confirmArgs.size(), 4);
    const QString preview = confirmArgs.at(3).toString();
    QVERIFY2(!preview.isEmpty(), "write_file preview should be non-empty (new file diff)");
    QVERIFY2(preview.contains(QStringLiteral("+++")), qPrintable(preview));

    const QString id = confirmArgs.at(0).toString();
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.approveToolCall(id, true);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    QFile written(path);
    QVERIFY(written.exists());
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(written.readAll()), QStringLiteral("payload"));
    written.close();
    QVERIFY(toolMessageContent(llm.seenMessages.last(), "call_t0")
        .contains("File written successfully"));
}

void TestAgent::fileCheckpointRecordedAndUndone() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    // autoEditSafe: write_file runs without a confirmation round-trip.
    agent.setPermissionMode(PermissionMode::AutoEdit);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString existingPath = dir.filePath("existing.txt");
    const QString createdPath = dir.filePath("created.txt");
    {
        QFile file(existingPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("before"), qint64(6));
    }

    QSignalSpy checkpointSpy(&agent, &AIAgent::fileCheckpointsChanged);
    QSignalSpy ready(&agent, &AIAgent::responseReady);

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", existingPath}, {"content", "after"}}));
    llm.script.append(textResponse(QStringLiteral("edited")));
    agent.startTask(QStringLiteral("edit it"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", createdPath}, {"content", "created"}}));
    llm.script.append(textResponse(QStringLiteral("created")));
    agent.startTask(QStringLiteral("create it"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 15000);

    QVERIFY(checkpointSpy.count() >= 2);
    QJsonArray checkpoints = agent.fileCheckpoints();
    QCOMPARE(checkpoints.size(), 2);
    // Newest first: the created file, then the edited one.
    QJsonObject newest = checkpoints.at(0).toObject();
    QJsonObject older = checkpoints.at(1).toObject();
    QCOMPARE(QFileInfo(newest.value("path").toString()).canonicalFilePath(),
             QFileInfo(createdPath).canonicalFilePath());
    QVERIFY(!newest.value("existed").toBool());
    QVERIFY(!newest.value("undone").toBool());
    QCOMPARE(QFileInfo(older.value("path").toString()).canonicalFilePath(),
             QFileInfo(existingPath).canonicalFilePath());
    QVERIFY(older.value("existed").toBool());

    // Phase 1: the detail API returns the write-time diff and hash.
    const QJsonObject newerDetail =
        agent.fileCheckpointDetail(newest.value("id").toInteger());
    QCOMPARE(newerDetail.value("path").toString(),
             QFileInfo(createdPath).canonicalFilePath());
    QVERIFY2(newerDetail.value("diff").toString().contains(QStringLiteral("+created")),
             qPrintable(newerDetail.value("diff").toString()));
    QCOMPARE(newerDetail.value("addedLines").toInt(), 1);
    QCOMPARE(newerDetail.value("removedLines").toInt(), 0);
    QVERIFY(!newerDetail.value("afterHash").toString().isEmpty());
    const QJsonObject olderDetail =
        agent.fileCheckpointDetail(older.value("id").toInteger());
    QVERIFY2(olderDetail.value("diff").toString().contains(QStringLiteral("-before")),
             qPrintable(olderDetail.value("diff").toString()));
    QVERIFY2(olderDetail.value("diff").toString().contains(QStringLiteral("+after")),
             qPrintable(olderDetail.value("diff").toString()));
    QCOMPARE(olderDetail.value("addedLines").toInt(), 1);
    QCOMPARE(olderDetail.value("removedLines").toInt(), 1);
    QVERIFY(agent.fileCheckpointDetail(999999).isEmpty());

    // Undo the creation: the file disappears again.
    QVERIFY(agent.undoFileCheckpoint(newest.value("id").toInteger()));
    QVERIFY(!QFile::exists(createdPath));
    checkpoints = agent.fileCheckpoints();
    QCOMPARE(checkpoints.size(), 2);
    QVERIFY(checkpoints.at(0).toObject().value("undone").toBool());
    // Undo keeps the row (and its diff) as history.
    const QJsonObject undoneDetail =
        agent.fileCheckpointDetail(newest.value("id").toInteger());
    QVERIFY(undoneDetail.value("undone").toBool());
    QVERIFY(!undoneDetail.value("diff").toString().isEmpty());

    // Undo the edit: bytes restored exactly.
    QVERIFY(agent.undoFileCheckpoint(older.value("id").toInteger()));
    QFile restored(existingPath);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("before"));

    // A second undo is refused (already undone).
    QVERIFY(!agent.undoFileCheckpoint(older.value("id").toInteger()));

    // Legacy new-file rows (written before diffs were persisted) rebuild an
    // all-add comparison from the file while the hash still matches.
    const QString legacyPath = dir.filePath(QStringLiteral("legacy.txt"));
    {
        QFile file(legacyPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("legacy line"), qint64(11));
    }
    IMemory::FileCheckpointInput legacy;
    legacy.path = legacyPath;
    legacy.existed = false;
    const qint64 legacyId =
        memory.addFileCheckpoint(agent.currentSessionId(), legacy, 99);
    QVERIFY(legacyId > 0);
    const QJsonObject rebuilt = agent.fileCheckpointDetail(legacyId);
    QVERIFY(rebuilt.value("diffFallback").toBool());
    QVERIFY2(rebuilt.value("diff").toString().contains(
                 QStringLiteral("+legacy line")),
             qPrintable(rebuilt.value("diff").toString()));
    QCOMPARE(rebuilt.value("addedLines").toInt(), 1);

    // A hash mismatch means the file changed afterwards: do not present the
    // current content as if it were the write-time result.
    IMemory::FileCheckpointInput stale = legacy;
    stale.path = dir.filePath(QStringLiteral("stale.txt"));
    {
        QFile file(stale.path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("changed later"), qint64(13));
    }
    stale.afterHash = QString(64, QLatin1Char('0'));
    const qint64 staleId =
        memory.addFileCheckpoint(agent.currentSessionId(), stale, 100);
    QVERIFY(staleId > 0);
    const QJsonObject staleDetail = agent.fileCheckpointDetail(staleId);
    QVERIFY(!staleDetail.value("diffFallback").toBool());
    QVERIFY(staleDetail.value("diff").toString().isEmpty());

    agent.setMemory(nullptr);
}

// G4 acceptance: one undo reverts every change the session made to the
// file, restoring the bytes from before the session's first write.
void TestAgent::fileCheckpointsUndoWholeFile() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    agent.setPermissionMode(PermissionMode::AutoEdit);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("versioned.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("v0"), qint64(2));
    }

    QSignalSpy ready(&agent, &AIAgent::responseReady);
    llm.script.append(toolResponse("edit_file",
        QJsonObject{{"path", path}, {"old_string", "v0"}, {"new_string", "v1"}}));
    llm.script.append(textResponse(QStringLiteral("v1")));
    agent.startTask(QStringLiteral("first edit"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);

    llm.script.append(toolResponse("edit_file",
        QJsonObject{{"path", path}, {"old_string", "v1"}, {"new_string", "v2"}}));
    llm.script.append(textResponse(QStringLiteral("v2")));
    agent.startTask(QStringLiteral("second edit"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 15000);

    // Repeated edits collapse into one file row; one undo reverts them all.
    QJsonArray groups = agent.fileCheckpoints();
    QCOMPARE(groups.size(), 1);
    QJsonObject group = groups.at(0).toObject();
    QCOMPARE(group.value("changeCount").toInt(), 2);
    QCOMPARE(group.value("activeCount").toInt(), 2);
    QCOMPARE(group.value("path").toString(),
             QFileInfo(path).canonicalFilePath());

    auto fileContent = [&path]() {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };

    QCOMPARE(fileContent(), QByteArray("v2"));
    QVERIFY(agent.undoFileCheckpoint(group.value("id").toInteger()));
    // Session-level undo restores v0, not the intermediate v1.
    QCOMPARE(fileContent(), QByteArray("v0"));
    groups = agent.fileCheckpoints();
    QCOMPARE(groups.size(), 1);
    group = groups.at(0).toObject();
    QVERIFY(group.value("undone").toBool());
    QCOMPARE(group.value("activeCount").toInt(), 0);
    QCOMPARE(group.value("undoneCount").toInt(), 2);
    QVERIFY(!agent.undoFileCheckpoint(group.value("id").toInteger()));
    agent.setMemory(nullptr);
}

// UE-1: the review button's "unseen changes" counter, end to end through the
// real checkpoint chain (tool -> SQLite -> ChatSessionController -> AgentVM).
//
// The contract under test is deliberately about *unseen*, not *changed*: a
// counter that only ever went up would keep the toolbar lit forever.
void TestAgent::unreviewedFileCountTracksUnseenChanges() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    AgentViewModel vm(nullptr);
    vm.setMemory(&memory);
    vm.setAgent(&agent);
    // After setAgent: attaching a tab pushes the controller's permission mode
    // into the agent, so setting it before would be overwritten.
    agent.setPermissionMode(PermissionMode::AutoEdit);

    QCOMPARE(vm.unreviewedFileCount(), 0);
    QSignalSpy countSpy(&vm, &AgentViewModel::unreviewedFileCountChanged);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString first = dir.filePath("first.txt");
    const QString second = dir.filePath("second.txt");
    {
        QFile file(first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("before"), qint64(6));
    }

    QSignalSpy ready(&agent, &AIAgent::responseReady);

    // Write one file: the count follows the checkpoint list.
    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", first}, {"content", "after"}}));
    llm.script.append(textResponse(QStringLiteral("edited")));
    agent.startTask(QStringLiteral("edit it"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);

    QCOMPARE(vm.fileCheckpoints().size(), 1);
    QCOMPARE(vm.unreviewedFileCount(), 1);
    QVERIFY2(countSpy.count() >= 1, "计数变化必须发出信号（顶栏靠它刷新）");

    // A second file: two files to look at, one row each.
    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", second}, {"content", "created"}}));
    llm.script.append(textResponse(QStringLiteral("created")));
    agent.startTask(QStringLiteral("create it"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 15000);
    QCOMPARE(vm.unreviewedFileCount(), 2);

    // Editing an already-seen-file-path again must not double-count: the
    // counter is per file, which is what the review tree shows.
    llm.script.append(toolResponse("edit_file",
        QJsonObject{{"path", first}, {"old_string", "after"},
                   {"new_string", "after2"}}));
    llm.script.append(textResponse(QStringLiteral("edited again")));
    agent.startTask(QStringLiteral("edit again"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 15000);
    QCOMPARE(vm.unreviewedFileCount(), 2);

    // Acknowledge (the review dialog closed): zero, and the signal fired.
    countSpy.clear();
    vm.acknowledgeFileReview();
    QCOMPARE(vm.unreviewedFileCount(), 0);
    QCOMPARE(countSpy.count(), 1);

    // Acknowledging twice is a no-op - no signal storm, still zero.
    vm.acknowledgeFileReview();
    QCOMPARE(vm.unreviewedFileCount(), 0);
    QCOMPARE(countSpy.count(), 1);

    // A later write lifts it again on its own: the watermark is an id, so a
    // new (higher) checkpoint id is what re-lights the button.
    llm.script.append(toolResponse("edit_file",
        QJsonObject{{"path", second}, {"old_string", "created"},
                   {"new_string", "created2"}}));
    llm.script.append(textResponse(QStringLiteral("edited third")));
    agent.startTask(QStringLiteral("edit third"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 4, 15000);
    QVERIFY2(vm.unreviewedFileCount() >= 1,
             "确认后的新改动必须重新点亮审阅按钮");

    // Undoing the changes makes them "handled", so nothing stays unseen.
    vm.acknowledgeFileReview();
    QCOMPARE(vm.unreviewedFileCount(), 0);
    const QVariantList groups = vm.fileCheckpoints();
    QVERIFY(!groups.isEmpty());
    QVERIFY(vm.undoFileCheckpoint(groups.first().toMap().value("id").toLongLong()));
    QVERIFY2(vm.unreviewedFileCount() == 0,
             "已撤销的改动不应继续算作待查看");

    vm.setAgent(nullptr);
    agent.setMemory(nullptr);
}

// The review's other verdict. Undo is destructive (it rewrites the file), so
// its bookkeeping could ride on the file operation; accept is purely a
// decision about a change that already happened, which is why it must not
// touch the file and must be visible as its own state.
void TestAgent::acceptFileCheckpointSettlesTheChange() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    AgentViewModel vm(nullptr);
    vm.setMemory(&memory);
    vm.setAgent(&agent);
    agent.setPermissionMode(PermissionMode::AutoEdit);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString first = dir.filePath("first.txt");
    const QString second = dir.filePath("second.txt");
    {
        QFile file(first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("before");
    }

    QSignalSpy ready(&agent, &AIAgent::responseReady);
    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", first}, {"content", "after"}}));
    llm.script.append(textResponse(QStringLiteral("edited")));
    agent.startTask(QStringLiteral("edit it"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", second}, {"content", "created"}}));
    llm.script.append(textResponse(QStringLiteral("created")));
    agent.startTask(QStringLiteral("create it"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 15000);

    QCOMPARE(vm.fileCheckpoints().size(), 2);

    // ── Accepting one file ──
    const QVariantList before = vm.fileCheckpoints();
    QVariantMap target;
    for (const QVariant& entry : before) {
        if (entry.toMap().value("path").toString().endsWith(QStringLiteral("first.txt")))
            target = entry.toMap();
    }
    QVERIFY(!target.isEmpty());
    QVERIFY2(!target.value("accepted").toBool(), "前提：尚未确认");

    QCOMPARE(vm.acceptFileCheckpoint(target.value("id").toLongLong()), 1);

    // The file on disk is untouched: accepting keeps the change.
    {
        QFile file(first);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("after"));
    }

    QVariantList after = vm.fileCheckpoints();
    QVariantMap acceptedRow;
    QVariantMap pendingRow;
    for (const QVariant& entry : after) {
        const QVariantMap row = entry.toMap();
        if (row.value("path").toString().endsWith(QStringLiteral("first.txt")))
            acceptedRow = row;
        else
            pendingRow = row;
    }
    QCOMPARE(acceptedRow.value("accepted").toBool(), true);
    QVERIFY(acceptedRow.contains("acceptedAt"));
    QCOMPARE(pendingRow.value("accepted").toBool(), false);

    // Accepted changes are settled: undo must refuse them.
    QVERIFY2(!vm.undoFileCheckpoint(acceptedRow.value("id").toLongLong()),
             "已确认的改动不允许再撤销");
    {
        QFile file(first);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("after"));
    }

    // Accepting again is a no-op, not a second "1 file".
    QCOMPARE(vm.acceptFileCheckpoint(acceptedRow.value("id").toLongLong()), 0);

    // ── Accept all ──
    // Only the second file is still pending.
    QCOMPARE(vm.acceptAllFileCheckpoints(), 1);
    after = vm.fileCheckpoints();
    for (const QVariant& entry : after)
        QCOMPARE(entry.toMap().value("accepted").toBool(), true);
    QCOMPARE(vm.acceptAllFileCheckpoints(), 0);

    // Both files still hold the agent's content.
    {
        QFile file(second);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("created"));
    }

    // Accepting clears the "unseen changes" badge: a decision was made.
    QCOMPARE(vm.unreviewedFileCount(), 0);

    vm.setAgent(nullptr);
    agent.setMemory(nullptr);
}

// Bulk revert, the mirror of accept-all. The interesting part is not "it
// reverts things" but what it refuses to touch: an accepted file is a
// decision the user already made, and a path-only row has no stored content
// to restore. Both must be skipped and counted, never silently reverted.
void TestAgent::undoAllFileCheckpointsRevertsPendingOnly() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    AgentViewModel vm(nullptr);
    vm.setMemory(&memory);
    vm.setAgent(&agent);
    agent.setPermissionMode(PermissionMode::AutoEdit);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());

    // Three files with three different fates:
    //   modified.txt - existing file, edited   -> reverted to its original
    //   created.txt  - new file by the agent   -> deleted
    //   kept.txt     - edited, then accepted   -> left alone
    const QString modified = dir.filePath("modified.txt");
    const QString created = dir.filePath("created.txt");
    const QString kept = dir.filePath("kept.txt");
    for (const QString& path : {modified, kept}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original");
    }

    QSignalSpy ready(&agent, &AIAgent::responseReady);
    const auto runWrite = [&](const QString& path, const QString& content) {
        const int before = ready.count();
        llm.script.append(toolResponse("write_file",
            QJsonObject{{"path", path}, {"content", content}}));
        llm.script.append(textResponse(QStringLiteral("ok")));
        agent.startTask(QStringLiteral("write"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), before + 1, 15000);
    };

    runWrite(modified, "changed");
    runWrite(created, "brand new");
    runWrite(kept, "changed too");
    QCOMPARE(vm.fileCheckpoints().size(), 3);

    // Accept one of them: it must survive the bulk revert.
    QVariantMap keptRow;
    for (const QVariant& entry : vm.fileCheckpoints()) {
        if (entry.toMap().value("path").toString().endsWith(QStringLiteral("kept.txt")))
            keptRow = entry.toMap();
    }
    QVERIFY(!keptRow.isEmpty());
    QCOMPARE(vm.acceptFileCheckpoint(keptRow.value("id").toLongLong()), 1);

    const auto readAll = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
        return file.readAll();
    };

    // ── The bulk revert ──
    const QVariantMap result = vm.undoAllFileCheckpoints();
    QCOMPARE(result.value("reverted").toInt(), 2);
    // The accepted file is settled, so it is skipped rather than reverted.
    QCOMPARE(result.value("skipped").toInt(), 1);
    QCOMPARE(result.value("failed").toStringList().size(), 0);

    // Modified file: back to its pre-session content.
    QCOMPARE(readAll(modified), QByteArray("original"));
    // Created file: gone.
    QVERIFY(!QFile::exists(created));
    // Accepted file: untouched - the user said keep it.
    QCOMPARE(readAll(kept), QByteArray("changed too"));

    // Rows reflect what happened.
    int undoneRows = 0;
    int acceptedRows = 0;
    for (const QVariant& entry : vm.fileCheckpoints()) {
        const QVariantMap row = entry.toMap();
        if (row.value("accepted").toBool()) ++acceptedRows;
        if (row.value("undone").toBool()) ++undoneRows;
    }
    QCOMPARE(acceptedRows, 1);
    QCOMPARE(undoneRows, 2);

    // Running it again has nothing left to do.
    const QVariantMap again = vm.undoAllFileCheckpoints();
    QCOMPARE(again.value("reverted").toInt(), 0);

    // Accepted content is still intact after the second pass.
    QCOMPARE(readAll(kept), QByteArray("changed too"));

    vm.setAgent(nullptr);
    agent.setMemory(nullptr);
}

void TestAgent::confirmDeclineFeedsRejection() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("out.txt");

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", path}, {"content", "x"}}));
    llm.script.append(textResponse("ok, skipping"));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("write it"));
    QTRY_COMPARE(confirmSpy.count(), 1);
    const QString id = confirmSpy.first().first().toString();

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.approveToolCall(id, false);
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(!QFile::exists(path));
    QVERIFY(toolMessageContent(llm.seenMessages.last(), "call_t0")
        .contains("User declined"));
}

// 自动编辑模式：write_file 免确认直接执行；确认框不出现。
// 自动编辑模式：git 只读工具（git_log / git_show）必须免确认。
//
// 回归点：没有这两个工具时，模型要看提交历史只能用 shell_exec，而
// shell_exec 是 RequiresConfirm 且未声明 autoEditSafe——自动编辑下每次
// 都弹确认，用户体感就是"git 相关操作老是被拦"。
void TestAgent::autoEditBypassesGitReadOnlyTools() {
    agent.setPermissionMode(PermissionMode::AutoEdit);

    QTemporaryDir repo(testDirTemplate());
    QVERIFY(repo.isValid());
    const auto run = [&](const QStringList& args) {
        QProcess p;
        p.setWorkingDirectory(repo.path());
        p.start(QStringLiteral("git"), args);
        return p.waitForFinished(10000) && p.exitCode() == 0;
    };
    QVERIFY(run({QStringLiteral("init"), QStringLiteral("-q")}));
    QVERIFY(run({QStringLiteral("config"), QStringLiteral("user.email"),
                 QStringLiteral("t@e.c")}));
    QVERIFY(run({QStringLiteral("config"), QStringLiteral("user.name"),
                 QStringLiteral("T")}));

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("f.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("data");
    f.close();
    QVERIFY(run({QStringLiteral("add"), QStringLiteral("-A")}));
    QVERIFY(run({QStringLiteral("commit"), QStringLiteral("-qm"),
                 QStringLiteral("wired"), QStringLiteral("--allow-empty")}));

    // 让工具在仓库里工作：Sandbox 项目根指向临时仓库。
    Sandbox::setProjectDir(repo.path());

    llm.script.append(toolResponse("git_log", QJsonObject{}));
    llm.script.append(textResponse(QStringLiteral("listed")));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("show history"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    Sandbox::setProjectDir(QString());

    QCOMPARE(confirmSpy.count(), 0);
    QCOMPARE(agent.state(), AgentState::Completed);
    // 工具真的执行了（模型的上下文里能看到提交主题）。
    QVERIFY2(toolMessageContent(llm.seenMessages.last(), "call_t0")
                 .contains(QStringLiteral("wired")),
             "git_log 没有返回提交历史");
}

void TestAgent::autoEditBypassesFileTools() {
    agent.setPermissionMode(PermissionMode::AutoEdit);
    QCOMPARE(agent.permissionMode(), PermissionMode::AutoEdit);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("auto.txt");

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", path}, {"content", "auto"}}));
    llm.script.append(textResponse("done"));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("write it"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QCOMPARE(confirmSpy.count(), 0);

    QFile written(path);
    QVERIFY(written.exists());
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(written.readAll()), QStringLiteral("auto"));
    written.close();
}

// 自动编辑模式：shell_exec 未声明 autoEditSafe，仍走逐次确认。
void TestAgent::autoEditStillConfirmsShell() {
    agent.setPermissionMode(PermissionMode::AutoEdit);

    llm.script.append(toolResponse("shell_exec",
        QJsonObject{{"command", "echo hi"}}));
    llm.script.append(textResponse("fine"));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("run it"));
    QTRY_COMPARE(confirmSpy.count(), 1);
    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.approveToolCall(confirmSpy.first().first().toString(), false);
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(toolMessageContent(llm.seenMessages.last(), "call_t0")
        .contains("User declined"));
}

// 完全访问：shell_exec 免确认；Dangerous 工具任何模式下仍落入确认分支。
void TestAgent::fullAccessBypassesShellButNotDangerous() {
    agent.setPermissionMode(PermissionMode::FullAccess);

    llm.script.append(toolResponse("shell_exec",
        QJsonObject{{"command", "echo nexus_full_access"}}));
    llm.script.append(textResponse("done"));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("run it"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QCOMPARE(confirmSpy.count(), 0);
    QVERIFY(traceHas(agent.traceModel(), "tool_result", "shell_exec"));

    // Dangerous probe：不进 schema 的工具被幻觉调用时，完全访问模式下
    // 也必须等待人工确认（然后拒绝，不真正执行）。
    DangerousProbeGuard guard;
    ToolRegistry::instance().registerTool(std::make_unique<DangerousProbeTool>());
    llm.script.append(toolResponse("test_dangerous", QJsonObject{}));
    llm.script.append(textResponse("ok"));

    agent.startTask(QStringLiteral("try it"));
    QTRY_COMPARE(confirmSpy.count(), 1);
    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.approveToolCall(confirmSpy.first().first().toString(), false);
    QTRY_COMPARE(spy2.count(), 1);
    // 两个阶段共享 call_t0，取最后一条 tool 消息（本次拒绝的回执）。
    QString declined;
    for (const auto& entry : llm.seenMessages.last()) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QLatin1String("tool"))
            declined = msg.value("content").toString();
    }
    QVERIFY2(declined.contains("User declined"), qPrintable(declined));
}

// G1: PlanOnly rejects every workspace-mutating tool before execution and
// feeds the model a readable reason so it can continue with analysis.
void TestAgent::planOnlyBlocksWorkspaceTools() {
    agent.setPermissionMode(PermissionMode::PlanOnly);
    QCOMPARE(agent.permissionMode(), PermissionMode::PlanOnly);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString existing = dir.filePath("existing.txt");
    {
        QFile f(existing);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original");
        f.close();
    }
    const QString created = dir.filePath("created.txt");

    const QVector<QPair<QString, QJsonObject>> cases{
        {QStringLiteral("write_file"),
         QJsonObject{{"path", created}, {"content", "x"}}},
        {QStringLiteral("edit_file"),
         QJsonObject{{"path", existing}, {"old_string", "original"},
                     {"new_string", "changed"}}},
        {QStringLiteral("shell_exec"),
         QJsonObject{{"command", "echo should-not-run"}}},
        {QStringLiteral("git_commit"),
         QJsonObject{{"message", "blocked"}}}
    };

    for (const auto& c : cases) {
        const QString name = c.first;
        llm.script.append(toolResponse(name, c.second));
        llm.script.append(textResponse(QStringLiteral("blocked %1").arg(name)));

        QSignalSpy spy(&agent, &AIAgent::responseReady);
        agent.startTask(QStringLiteral("try %1").arg(name));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

        const QString result = lastToolMessageContent(llm.seenMessages.last());
        QVERIFY2(result.contains(QStringLiteral("read-only (plan) mode")),
                 qPrintable(result));
        QCOMPARE(agent.lastResponse(), QStringLiteral("blocked %1").arg(name));
        QVERIFY(traceHas(agent.traceModel(), "tool_call", name));
    }

    QVERIFY(!QFile::exists(created));
    QFile unchanged(existing);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(unchanged.readAll()), QStringLiteral("original"));
}

// G1: Safe tools and confirm-level tools that declare readOnly (web_search /
// http_fetch) stay available in PlanOnly.
void TestAgent::planOnlyAllowsSafeAndReadOnlyTools() {
    agent.setPermissionMode(PermissionMode::PlanOnly);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("read.txt");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello-plan");
        f.close();
    }

    QSignalSpy spy(&agent, &AIAgent::responseReady);

    llm.script.append(toolResponse("read_file", QJsonObject{{"path", path}}));
    llm.script.append(textResponse(QStringLiteral("read ok")));
    agent.startTask(QStringLiteral("read it"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QVERIFY(lastToolMessageContent(llm.seenMessages.last()).contains("hello-plan"));
    QCOMPARE(agent.lastResponse(), QStringLiteral("read ok"));

    ToolRegistry::instance().registerTool(std::make_unique<ReadOnlyProbeTool>());
    NamedToolGuard guard{QStringLiteral("test_readonly")};
    llm.script.append(toolResponse("test_readonly", QJsonObject{}));
    llm.script.append(textResponse(QStringLiteral("probe ok")));
    agent.startTask(QStringLiteral("probe it"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 15000);
    QCOMPARE(lastToolMessageContent(llm.seenMessages.last()), QStringLiteral("probe-ok"));
    QCOMPARE(agent.lastResponse(), QStringLiteral("probe ok"));
}

// G1: switching back to a writable mode takes effect on the next task.
void TestAgent::planOnlySwitchRestoresWrites() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString blockedPath = dir.filePath("blocked.txt");
    const QString allowedPath = dir.filePath("allowed.txt");

    QSignalSpy spy(&agent, &AIAgent::responseReady);

    agent.setPermissionMode(PermissionMode::PlanOnly);
    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", blockedPath}, {"content", "no"}}));
    llm.script.append(textResponse(QStringLiteral("blocked")));
    agent.startTask(QStringLiteral("blocked write"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QVERIFY(!QFile::exists(blockedPath));

    agent.setPermissionMode(PermissionMode::AutoEdit);
    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", allowedPath}, {"content", "yes"}}));
    llm.script.append(textResponse(QStringLiteral("written")));
    agent.startTask(QStringLiteral("allowed write"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 15000);

    QFile written(allowedPath);
    QVERIFY(written.exists());
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(written.readAll()), QStringLiteral("yes"));
}

// G1: if the user switches to PlanOnly while a write is waiting for
// approval, clicking "allow" must still be refused.
void TestAgent::planOnlyRefusesApprovalOfPendingWrite() {
    agent.setPermissionMode(PermissionMode::ConfirmChanges);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pending.txt");

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", path}, {"content", "x"}}));
    llm.script.append(textResponse(QStringLiteral("refused as expected")));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("write with approval"));
    QTRY_COMPARE(confirmSpy.count(), 1);
    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);

    agent.setPermissionMode(PermissionMode::PlanOnly);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.approveToolCall(confirmSpy.first().first().toString(), true);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    QVERIFY(!QFile::exists(path));
    const QString result = lastToolMessageContent(llm.seenMessages.last());
    QVERIFY2(result.contains(QStringLiteral("read-only (plan) mode")),
             qPrintable(result));
    QCOMPARE(agent.lastResponse(), QStringLiteral("refused as expected"));
}

void TestAgent::permissionModeStringRoundTrip() {
    QCOMPARE(permissionModeToString(PermissionMode::ConfirmChanges),
             QStringLiteral("confirm"));
    QCOMPARE(permissionModeToString(PermissionMode::PlanOnly),
             QStringLiteral("plan_only"));
    QCOMPARE(permissionModeFromString(QStringLiteral("plan_only")),
             PermissionMode::PlanOnly);
    QCOMPARE(permissionModeFromString(QStringLiteral("unknown")),
             PermissionMode::ConfirmChanges);
    QVERIFY(isValidPermissionMode(static_cast<int>(PermissionMode::PlanOnly)));
    QVERIFY(!isValidPermissionMode(-1));
    QVERIFY(!isValidPermissionMode(static_cast<int>(PermissionMode::PlanOnly) + 1));
}

void TestAgent::unknownToolFeedsError() {
    llm.script.append(toolResponse("no_such_tool", QJsonObject{}));
    llm.script.append(textResponse("ok"));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("go"));
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(toolMessageContent(llm.seenMessages.last(), "call_t0")
        .contains("unknown tool"));
    QVERIFY(traceHas(agent.traceModel(), "tool_call", "no_such_tool"));
}

void TestAgent::maxIterationsStops() {
    // 预算已不是用户设置（2026-11-01 起默认内部安全上限 1000），本用例断言
    // 的是"预算耗尽"这条路径本身，所以显式钉一个 10，与脚本长度对齐——否则
    // 脚本第 11 轮起会重复最后一条响应，被"同轮重复调用"守卫提前收尾，测不
    // 到预算耗尽。
    agent.setMaxIterations(10);

    // Each round probes a DIFFERENT directory: the identical-round loop
    // terminator (finishes early on repeated calls) must not kick in, so
    // the hard step budget is what actually stops the task.
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (int i = 0; i < 10; ++i)
        llm.script.append(toolResponse("list_dir",
            QJsonObject{{"path", home + "/nexusai_nonexistent_dir_" + QString::number(i)}}));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("loop"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    // Budget exhaustion no longer aborts with an error: one final no-tools
    // call turns the gathered results into a (fallback) answer.
    QVERIFY(!agent.lastResponse().isEmpty());
    QVERIFY(traceHas(agent.traceModel(), "context", "Step budget"));
    QCOMPARE(llm.seenMessages.size(), 11); // 10 iterations + 1 forced final

    // The forced call is primed with an explicit no-tools instruction and
    // carries no tool definitions.
    const QJsonArray lastCall = llm.seenMessages.last();
    QVERIFY(lastCall.last().toObject().value("content")
        .toString().contains("Step budget exhausted"));
    QVERIFY(llm.seenTools.last().isEmpty());

    agent.setMaxIterations(AIAgent::kDefaultMaxIterations);   // 还原共享 agent
}

// Switching conversations must restore the full visual timeline (thinking /
// tool calls / results), not just the user/assistant text.
// The model writes a full answer AND then re-issues the exact tool call it
// already ran (seen with OpenAI-compatible proxies). The second identical
// round must finish the task with the answer that is already on the table -
// no second execution, no step-budget burn.
void TestAgent::repeatedCallWithAnswerFinishes() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("doc.txt");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("hello agent");
    file.close();

    const QJsonObject args{{"path", path}};
    const QString fullAnswer = QStringLiteral(
        "白艳波 教师简介：人文学院助理研究员，研究领域为明代文献学、道教史，"
        "主持海南省哲社规划课题，参编两部著作，并在期刊发表多篇论文。");

    LLMResponse first = toolResponse("read_file", args);
    first.content = QStringLiteral("让我查一下。");
    LLMResponse second = toolResponse("read_file", args); // identical round
    second.content = fullAnswer;

    llm.script.append(first);
    llm.script.append(second);

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("白艳波是谁"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    QCOMPARE(agent.state(), AgentState::Completed);
    QCOMPARE(agent.lastResponse(), fullAnswer);

    // Exactly one real execution: the repeated round was cut off
    const QJsonArray audit = memory.auditLog(50);
    int executions = 0;
    for (const auto& entry : audit) {
        if (entry.toObject().value("event").toString() == QStringLiteral("tool_execute"))
            ++executions;
    }
    QCOMPARE(executions, 1);
    QVERIFY(traceHas(agent.traceModel(), "tool_result", "hello agent"));
}

// 跨用户轮次时，旧轮次的工具调用必须折叠成一条注记（只保留最近
// kKeepToolRounds 轮），否则一长串 tool_call/tool 消息会让模型把它读成
// "每次都要先调工具"的脚本（曾导致打招呼也去调 search_knowledge_base）。
// 注意作用域：折叠只对**之前用户轮次**生效，同一任务内的轮次永远保留
// （见 currentTurnToolRoundsSurvive）。
void TestAgent::oldToolRoundsCollapsed() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());

    for (int i = 0; i < 4; ++i) {
        const QString path = dir.filePath(QStringLiteral("f%1.txt").arg(i));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("payload");
        file.close();
        llm.script.append(toolResponse("read_file",
            QJsonObject{{"path", path}}));
    }
    llm.script.append(textResponse(QStringLiteral("all read")));

    // 第一轮用户任务：4 个工具轮次（同一任务内，全部应保留）。
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("read all four"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(agent.state(), AgentState::Completed);

    // 第二轮用户任务：这一次请求会把上一轮的工具轮次折叠掉。
    llm.script.append(textResponse(QStringLiteral("second turn done")));
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("now just answer"));
    QTRY_COMPARE_WITH_TIMEOUT(spy2.count(), 1, 20000);
    QCOMPARE(agent.state(), AgentState::Completed);

    // The final LLM call must carry at most kKeepToolRounds verbatim tool
    // rounds from the previous turn; the older ones are replaced by the
    // collapsed-rounds note. Pairing must stay valid after the replacement.
    const QJsonArray last = llm.seenMessages.last();
    int toolMsgs = 0;
    QString collapsedNote;
    for (const auto& entry : last) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QStringLiteral("tool")) ++toolMsgs;
        if (msg.value("content").toString().contains(QStringLiteral("Earlier tool calls")))
            collapsedNote = msg.value("content").toString();
    }
    // 上一轮的 4 个工具轮次里，只有最近 2 轮原样保留。
    QCOMPARE(toolMsgs, 2);
    QVERIFY2(!collapsedNote.isEmpty(),
             "跨用户轮次的旧工具轮次没有被折叠");
    // FIX-504: the note must not name any specific tool - a hardcoded
    // "e.g. search_knowledge_base" injects KB bias into every conversation
    // no matter which tools were actually used.
    QVERIFY2(!collapsedNote.contains(QStringLiteral("search_knowledge_base")),
             "collapse note hardcodes the knowledge tool");
    QVERIFY2(noDanglingToolCalls(last), "collapse broke tool_call/tool pairing");

    // All four reads really executed before collapsing kicked in
    QVERIFY(traceHas(agent.traceModel(), "tool_result", "payload"));
}

// P0: 单任务内的工具轮次必须原样留在上下文里。
//
// 回归点：collapseOldToolRounds 曾对**所有**工具轮次生效（只保留最近 2
// 轮），于是"浏览目录 → 读文件 → 分析"这类长任务在跑到第 3 轮时，模型
// 已经看不到前两轮的 list_dir 输出，只能重新浏览一遍；新的一轮又被下一
// 次折叠吃掉，形成「读 → 折叠 → 再读」的循环，直到步数预算耗尽——用户
// 看到的就是任务"没跑完"。
//
// 折叠的正确作用域是**跨用户轮次**：同一任务内的轮次一律保留。
void TestAgent::currentTurnToolRoundsSurvive() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());

    // 同一任务内连续 5 轮工具调用（超过 kKeepToolRounds 的 2 轮）。
    constexpr int kRounds = 5;
    for (int i = 0; i < kRounds; ++i) {
        const QString path = dir.filePath(QStringLiteral("turn%1.txt").arg(i));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("payload-marker");
        file.close();
        llm.script.append(toolResponse("read_file",
            QJsonObject{{"path", path}}));
    }
    llm.script.append(textResponse(QStringLiteral("all done")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("read them all in one go"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 30000);
    QCOMPARE(agent.state(), AgentState::Completed);

    // 最后一次请求：当前任务内的 5 轮结果必须全部还在，且没有任何折叠注记。
    const QJsonArray last = llm.seenMessages.last();
    int toolMsgs = 0;
    for (const auto& entry : last) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QLatin1String("tool")) ++toolMsgs;
        QVERIFY2(!msg.value("content").toString()
                      .contains(QStringLiteral("Earlier tool calls")),
                 "同一任务内的工具轮次被折叠了——模型会遗忘刚读到的内容");
    }
    QCOMPARE(toolMsgs, kRounds);
    QVERIFY2(noDanglingToolCalls(last), "保留全部工具轮次后配对仍须合法");
}

// P1：恢复会话读入的轨迹有上限（从最新往回取），超出时插一条说明节点。
//
// 回归点：此前 loadTraceNodes 无 LIMIT，长会话一次性把成千上万节点读进
// 内存，整条链路（SQL → QJsonArray → QVariantList → Repeater 全量实例化）
// 都在 GUI 线程同步完成，打开会话即出现内存峰值与卡顿。
void TestAgent::resumeLoadsOnlyRecentTraceNodes() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    const QString sessionId = QStringLiteral("long-session");
    memory.beginSession(sessionId, QStringLiteral("Long"));

    // 直接灌入超过上限的节点（模拟长期使用的会话）。
    constexpr int kNodes = 900;
    for (int i = 0; i < kNodes; ++i) {
        QJsonObject node;
        node["id"] = QStringLiteral("n%1").arg(i);
        node["type"] = QStringLiteral("tool_result");
        node["title"] = QStringLiteral("step-%1").arg(i);
        node["status"] = QStringLiteral("success");
        node["result"] = QStringLiteral("payload");
        node["timestamp"] = 1000.0 + i;
        memory.saveTraceNode(sessionId, node, i);
    }

    agent.resumeSession(sessionId);
    const QJsonArray restored = agent.traceModel();

    // 1) 不是全量：900 个原始节点里只取最近一段（+1 条截断说明）。
    QVERIFY2(restored.size() < kNodes,
             "恢复时仍然读入了全部轨迹节点");
    QVERIFY2(restored.size() <= 801,
             "恢复的节点数超出上限太多");

    // 2) 取的是**最近**的一段，最早的那个节点必须已经不在。
    QVERIFY(!traceHas(restored, "tool_result", "step-0"));

    // 3) 必须给用户可见说明，而不是让历史"悄悄消失"。
    QVERIFY2(traceHas(restored, "context", "most recent"),
             "轨迹被截断却没有向用户说明");

    agent.setMemory(nullptr);
}

void TestAgent::traceRestoredOnResume() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    llm.script.append(toolResponse("list_dir", QJsonObject{{"path", dir.path()}}));
    llm.script.append(textResponse("done"));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("scan"));
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(traceHas(agent.traceModel(), "tool_call", "list_dir"));
    QVERIFY(traceHas(agent.traceModel(), "tool_result"));

    const QString sessionId = agent.currentSessionId();
    QVERIFY(!sessionId.isEmpty());

    // Leave the session (clears the in-memory trace), then reopen it
    agent.resetSession();
    QCOMPARE(agent.traceModel().size(), 0);
    QVERIFY(traceHas(agent.traceModel(), "tool_call", "list_dir") == false);

    agent.resumeSession(sessionId);

    QVERIFY(agent.traceModel().size() >= 5); // user + thinking + call + result + summary
    QVERIFY(traceHas(agent.traceModel(), "user_message", "scan"));
    QVERIFY(traceHas(agent.traceModel(), "tool_call", "list_dir"));
    QVERIFY(traceHas(agent.traceModel(), "tool_result"));
    QVERIFY(traceHas(agent.traceModel(), "summary", "done"));
    QCOMPARE(agent.lastResponse(), QStringLiteral("done"));

    agent.setMemory(nullptr);
}

// F1: the todo list is in-memory and owned by one session. Leaving the
// session (reset) or opening another one (resume) must clear it; otherwise
// the new task list panel would show the previous session's tasks.
void TestAgent::todoListClearedOnSessionResetAndResume() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    const QJsonArray todos{
        QJsonObject{{"content", "Inspect code"}, {"status", "completed"}, {"priority", "high"}},
        QJsonObject{{"content", "Apply fix"}, {"status", "in_progress"}, {"priority", "medium"}}
    };
    llm.script.append(toolResponse("todo_write", QJsonObject{{"todos", todos}}));
    llm.script.append(textResponse(QStringLiteral("first done")));

    QSignalSpy todoSpy(&agent, &AIAgent::todoListChanged);
    QSignalSpy doneSpy(&agent, &AIAgent::responseReady);

    agent.startTask(QStringLiteral("plan the work"));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    QCOMPARE(agent.todoList().size(), 2);
    QVERIFY(todoSpy.count() >= 1);

    // 1) New chat in the same tab: resetSession drops the list.
    const int beforeReset = todoSpy.count();
    agent.resetSession();
    QCOMPARE(agent.todoList().size(), 0);
    QCOMPARE(todoSpy.count(), beforeReset + 1);

    // 2) Reopening a persisted session must not resurrect the list left by
    //    the previous session in this tab.
    llm.script.append(toolResponse("todo_write", QJsonObject{{"todos", todos}}));
    llm.script.append(textResponse(QStringLiteral("second done")));
    agent.startTask(QStringLiteral("plan again"));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 2, 15000);
    QCOMPARE(agent.todoList().size(), 2);

    memory.beginSession(QStringLiteral("other-session"), QStringLiteral("other"));
    const int beforeResume = todoSpy.count();
    agent.resumeSession(QStringLiteral("other-session"));
    QCOMPARE(agent.currentSessionId(), QStringLiteral("other-session"));
    QCOMPARE(agent.todoList().size(), 0);
    QCOMPARE(todoSpy.count(), beforeResume + 1);

    agent.setMemory(nullptr);
}

// F1: the todo list is persisted per session. Reopening the SAME session
// restores its stored list; a different session yields that session's own
// list (empty when it never used todo_write), never the previous one.
void TestAgent::todoListPersistsAcrossSessionResume() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    const QJsonArray todos{
        QJsonObject{{"content", "Inspect code"}, {"status", "completed"},
                    {"priority", "high"}},
        QJsonObject{{"content", "Apply fix"}, {"status", "in_progress"},
                    {"priority", "medium"}}
    };
    llm.script.append(toolResponse("todo_write", QJsonObject{{"todos", todos}}));
    llm.script.append(textResponse(QStringLiteral("first done")));

    QSignalSpy doneSpy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("plan the work"));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    QCOMPARE(agent.todoList().size(), 2);

    const QString sessionId = agent.currentSessionId();
    QVERIFY(!sessionId.isEmpty());

    // 1) Leaving the tab (new chat) drops the list; reopening the SAME
    //    session restores it from the database.
    agent.resetSession();
    QCOMPARE(agent.todoList().size(), 0);
    agent.resumeSession(sessionId);
    QCOMPARE(agent.currentSessionId(), sessionId);
    QCOMPARE(agent.todoList().size(), 2);
    QCOMPARE(agent.todoList().at(0).toObject().value("content").toString(),
             QStringLiteral("Inspect code"));

    // 2) A different session yields its own list (empty here), not the
    //    previous session's.
    memory.beginSession(QStringLiteral("other-session"), QStringLiteral("other"));
    agent.resumeSession(QStringLiteral("other-session"));
    QCOMPARE(agent.currentSessionId(), QStringLiteral("other-session"));
    QCOMPARE(agent.todoList().size(), 0);

    // 3) That session's own list survives its own resume, and switching back
    //    restores the first session's list again.
    const QJsonArray otherTodos{
        QJsonObject{{"content", "Other task"}, {"status", "pending"},
                    {"priority", "low"}}
    };
    QVERIFY(memory.saveSessionTodos(QStringLiteral("other-session"), otherTodos));
    agent.resumeSession(sessionId);
    QCOMPARE(agent.todoList().size(), 2);
    agent.resumeSession(QStringLiteral("other-session"));
    QCOMPARE(agent.todoList().size(), 1);
    QCOMPARE(agent.todoList().at(0).toObject().value("content").toString(),
             QStringLiteral("Other task"));

    agent.setMemory(nullptr);
}

// docs/33 E4: todo_write 是全量替换，但项身份需要跨轮稳定——模型显式回传
// 的已知 id 优先、content 匹配次之、新项分配 t<n>；伪造/陈旧 id 被替换；
// 带 id 的列表回写进历史供下一轮引用（spawn_agent 的 todo_id 由此可锚定）。
void TestAgent::todoIdsStableAcrossRewrites() {
    // 第一轮：模型伪造 id "t9" → 被替换为 t1。
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{QJsonObject{
            {"id", QStringLiteral("t9")},
            {"content", QStringLiteral("implement parser")},
            {"status", QStringLiteral("pending")}}}}}));
    llm.script.append(textResponse(QStringLiteral("planned")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("plan it"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    QJsonArray todos = agent.todoList();
    QCOMPARE(todos.size(), 1);
    QCOMPARE(todos.at(0).toObject().value("id").toString(),
             QStringLiteral("t1"));
    // 历史里的工具结果已带 id：模型下一轮引用得到。
    bool historyHasId = false;
    for (const auto& msg : agent.messages()) {
        if (msg.toObject().value("role").toString() == QLatin1String("tool")
                && msg.toObject().value("content").toString()
                       .contains(QStringLiteral("\"id\":\"t1\"")))
            historyHasId = true;
    }
    QVERIFY(historyHasId);

    // 第二轮：模型回传 t1（content 同 → 显式 id 生效）+ 新项 → t2。
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{
            QJsonObject{{"id", QStringLiteral("t1")},
                        {"content", QStringLiteral("implement parser")},
                        {"status", QStringLiteral("in_progress")}},
            QJsonObject{{"content", QStringLiteral("write tests")},
                        {"status", QStringLiteral("pending")}}}}}));
    llm.script.append(textResponse(QStringLiteral("updated")));
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("advance the plan"));
    QTRY_COMPARE_WITH_TIMEOUT(spy2.count(), 1, 15000);

    todos = agent.todoList();
    QCOMPARE(todos.size(), 2);
    QCOMPARE(todos.at(0).toObject().value("id").toString(),
             QStringLiteral("t1"));
    QCOMPARE(todos.at(1).toObject().value("id").toString(),
             QStringLiteral("t2"));
    QCOMPARE(todos.at(0).toObject().value("status").toString(),
             QStringLiteral("in_progress"));

    // 第三轮：模型重写列表时丢掉 id（只给 content）→ t1 仍按 content 保持。
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{
            QJsonObject{{"content", QStringLiteral("write tests")},
                        {"status", QStringLiteral("in_progress")}},
            QJsonObject{{"content", QStringLiteral("implement parser")},
                        {"status", QStringLiteral("pending")}}}}}));
    llm.script.append(textResponse(QStringLiteral("reordered")));
    QSignalSpy spy3(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("reorder"));
    QTRY_COMPARE_WITH_TIMEOUT(spy3.count(), 1, 15000);

    todos = agent.todoList();
    QCOMPARE(todos.size(), 2);
    QCOMPARE(todos.at(0).toObject().value("id").toString(),
             QStringLiteral("t2"));
    QCOMPARE(todos.at(1).toObject().value("id").toString(),
             QStringLiteral("t1"));
}

// 2026-11-03 真机缺陷：模型把 todo_write 当一次性计划，写完全部 in_progress
// 后再也不回写，最终答案说"全部完成"而面板仍停在未完成。修复是让系统提示
// 每轮携带当前清单 + 明确维护规则；这里锁定注入内容随状态变化而更新。
void TestAgent::todoStateInjectedIntoSystemPromptAndKeptCurrent() {
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{
            QJsonObject{{"content", QStringLiteral("Inspect code")},
                        {"status", QStringLiteral("in_progress")}},
            QJsonObject{{"content", QStringLiteral("Apply fix")},
                        {"status", QStringLiteral("pending")}}}}}));
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{
            QJsonObject{{"id", QStringLiteral("t1")},
                        {"content", QStringLiteral("Inspect code")},
                        {"status", QStringLiteral("completed")}},
            QJsonObject{{"id", QStringLiteral("t2")},
                        {"content", QStringLiteral("Apply fix")},
                        {"status", QStringLiteral("in_progress")}}}}}));
    llm.script.append(textResponse(QStringLiteral("all done")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("plan and work"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    // 每个请求都带系统提示：第 2 个请求（首次 todo_write 之后）应看到清单块。
    QCOMPARE(llm.seenMessages.size(), 3);
    const auto systemOf = [this](int index) {
        return llm.seenMessages.at(index).first().toObject()
            .value(QStringLiteral("content")).toString();
    };
    const QString afterPlan = systemOf(1);
    QVERIFY2(afterPlan.contains(QStringLiteral("Current task list")),
             qPrintable(afterPlan.left(200)));
    QVERIFY(afterPlan.contains(QStringLiteral("t1 [in_progress]")));
    QVERIFY(afterPlan.contains(QStringLiteral("t2 [pending]")));
    // 第二次 todo_write 之后的状态必须反映到下一轮系统提示。
    const QString afterAdvance = systemOf(2);
    QVERIFY(afterAdvance.contains(QStringLiteral("t1 [completed]")));
    QVERIFY(afterAdvance.contains(QStringLiteral("t2 [in_progress]")));

    // 工具结果里重复维护规则，避免"写完初版就再也不回写"。
    bool reminderInHistory = false;
    for (const auto& entry : agent.messages()) {
        const QJsonObject msg = entry.toObject();
        if (msg.value(QStringLiteral("role")).toString() == QLatin1String("tool")
                && msg.value(QStringLiteral("content")).toString()
                       .contains(QStringLiteral("Keep it current")))
            reminderInHistory = true;
    }
    QVERIFY2(reminderInHistory, "todo_write 结果缺少维护提醒");

    const QJsonArray todos = agent.todoList();
    QCOMPARE(todos.size(), 2);
    QCOMPARE(todos.at(0).toObject().value("status").toString(),
             QStringLiteral("completed"));
}

// 清单被清空（todo_write []）后，新项不得复用旧 id——历史/审计里的 t1
// 会因此指向另一个任务。计数器独立于当前列表保存在 AIAgent。
void TestAgent::todoIdsNotReusedAfterListClear() {
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{QJsonObject{
            {"content", QStringLiteral("First task")},
            {"status", QStringLiteral("pending")}}}}}));
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{}}}));
    llm.script.append(toolResponse(QStringLiteral("todo_write"),
        QJsonObject{{"todos", QJsonArray{QJsonObject{
            {"content", QStringLiteral("Second task")},
            {"status", QStringLiteral("pending")}}}}}));
    llm.script.append(textResponse(QStringLiteral("done")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("clear and rebuild"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QJsonArray todos = agent.todoList();
    QCOMPARE(todos.size(), 1);
    QCOMPARE(todos.at(0).toObject().value("content").toString(),
             QStringLiteral("Second task"));
    QCOMPARE(todos.at(0).toObject().value("id").toString(),
             QStringLiteral("t2"));
}

// docs/33 E4: spawn_agent 带 todo_id → 报告头回显（提醒模型同步状态），
// 审计行 detail 落 subagent_todo（清单项 ↔ 子代理运行可追溯）。
void TestAgent::spawnAgentTodoIdEchoedInReportAndAudit() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("review findings: none")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("review the diff")},
                    {"todo_id", QStringLiteral("t2")}}));
    llm.script.append(textResponse(QStringLiteral("integrated")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("run the planned review step"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY(report.contains(QStringLiteral("todo: t2")));

    bool found = false;
    const QJsonArray audit = memory.auditLog(50);
    for (const auto& entry : audit) {
        const QJsonObject row = entry.toObject();
        if (row.value("event").toString() != QLatin1String("tool_execute")
                || row.value("toolName").toString() != QLatin1String("spawn_agent"))
            continue;
        const QJsonObject detail = QJsonDocument::fromJson(
            row.value("detail").toString().toUtf8()).object();
        QCOMPARE(detail.value("subagent_todo").toString(),
                 QStringLiteral("t2"));
        found = true;
    }
    QVERIFY2(found, "spawn_agent audit row with subagent_todo missing");
    agent.setMemory(nullptr);
}

// ── docs/26 I4: 会话标题自动生成 ──────────────────────────────────────

void TestAgent::autoSessionTitleGeneratedAfterFirstExchange() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    agent.setAutoSessionTitleEnabled(true);
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("answer one")));
    llm.script.append(textResponse(QStringLiteral("Parser 机制分析")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("帮我分析 parser 的实现"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    // 标题请求在任务结束后发出（测试内无 episodic 服务，不经摘要链）。
    QTRY_COMPARE_WITH_TIMEOUT(llm.seenMessages.size(), 2, 15000);
    const QJsonArray titleMessages = llm.seenMessages.last();
    QCOMPARE(titleMessages.size(), 2);
    QCOMPARE(titleMessages.at(0).toObject().value("role").toString(),
             QStringLiteral("system"));
    QVERIFY(titleMessages.at(1).toObject().value("content").toString()
                .contains(QStringLiteral("parser")));
    // 标题写库；sessionsChanged 已通知侧栏重算。
    QTRY_VERIFY(!memory.listSessions(10).isEmpty());
    bool titled = false;
    for (const auto& s : memory.listSessions(10)) {
        const QJsonObject row = s.toObject();
        if (row.value("id").toString() == agent.currentSessionId()) {
            QCOMPARE(row.value("title").toString(),
                     QStringLiteral("Parser 机制分析"));
            titled = true;
        }
    }
    QVERIFY2(titled, "session row missing after auto title");
}

void TestAgent::autoSessionTitleSkippedWhenUserRenamed() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    agent.setAutoSessionTitleEnabled(true);
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("answer")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("plan the parser work"));
    QVERIFY(!agent.currentSessionId().isEmpty());
    // 任务进行中就手动命名：本会话的自动标题永久放弃。
    agent.renameSession(agent.currentSessionId(), QStringLiteral("手动命名"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    // 给自动标题一个机会窗口：不得发出第二次请求，标题保持手动命名。
    QTest::qWait(100);
    QCOMPARE(llm.seenMessages.size(), 1);
    for (const auto& s : memory.listSessions(10)) {
        const QJsonObject row = s.toObject();
        if (row.value("id").toString() == agent.currentSessionId())
            QCOMPARE(row.value("title").toString(),
                     QStringLiteral("手动命名"));
    }
}

void TestAgent::autoSessionTitleFailureKeepsTruncatedTitle() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    agent.setAutoSessionTitleEnabled(true);
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("answer")));
    LLMResponse boom;
    boom.success = false;
    boom.error = QStringLiteral("title request failed");
    llm.script.append(boom);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    const QString firstMessage = QStringLiteral("analyze the parser pipeline thoroughly");
    agent.startTask(firstMessage);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    // 标题请求发出且失败：静默保留 beginSession 的截断标题，且不重试。
    QTRY_COMPARE_WITH_TIMEOUT(llm.seenMessages.size(), 2, 15000);
    QTest::qWait(50);
    QCOMPARE(llm.seenMessages.size(), 2);
    bool titled = false;
    for (const auto& s : memory.listSessions(10)) {
        const QJsonObject row = s.toObject();
        if (row.value("id").toString() == agent.currentSessionId()) {
            QCOMPARE(row.value("title").toString(), firstMessage.left(60));
            titled = true;
        }
    }
    QVERIFY2(titled, "session row missing after failed auto title");
}

// 思考正文的三条硬要求：落进 thinking 节点供展示、不混进答案文本、不回灌给
// 模型（多数 API 会拒收陌生字段），并且重开会话后仍在。
void TestAgent::reasoningTextLandsOnThinkingNode() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    LLMResponse withThinking;
    withThinking.success = true;
    withThinking.content = QStringLiteral("the answer");
    withThinking.reasoning = QStringLiteral("weighed two options then chose");
    llm.script.append(withThinking);

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("why"));
    QTRY_COMPARE(spy.count(), 1);

    QString reasoning;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() == QLatin1String("thinking"))
            reasoning = node.value("reasoning").toString();
    }
    QCOMPARE(reasoning, QStringLiteral("weighed two options then chose"));
    QVERIFY(traceHas(agent.traceModel(), "summary", "the answer"));

    // 回灌的历史里一处思考正文都不许出现
    for (const auto& entry : llm.seenMessages.last()) {
        const QJsonValue content = entry.toObject().value("content");
        QVERIFY(!content.toString().contains(QStringLiteral("weighed two options")));
    }

    const QString sessionId = agent.currentSessionId();
    agent.resetSession();
    agent.resumeSession(sessionId);
    QString restored;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() == QLatin1String("thinking"))
            restored = node.value("reasoning").toString();
    }
    QCOMPARE(restored, QStringLiteral("weighed two options then chose"));

    agent.setMemory(nullptr);
}

// 复现并锁定：一轮回复里既有自动放行的工具、又有需确认的工具时，
// 确认请求必须真正"挂住"整个任务。
//
// 怀疑点：processNextToolCall 在遇到需确认的调用时，会先
// dispatchToolExecution 掉之前收集的 parallelCalls 再 return。那些并行
// 调用在 worker 线程完成后会回到 finishToolExecution → processNextToolCall，
// 而此时的 state 是 AwaitingToolConfirm，pending 队列里可能还有后续调用，
// 于是可能在用户点确认之前就把流程推进下去了。
void TestAgent::confirmationSuspendsEvenWithParallelAutoApprovedCalls() {
    agent.setPermissionMode(PermissionMode::ConfirmChanges);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("w.txt"));

    // 第 1 个：read_file（Safe，自动放行）；第 2 个：write_file（需确认）。
    LLMResponse mixed;
    mixed.success = true;
    ToolCallInfo readCall;
    readCall.id = QStringLiteral("call_read");
    readCall.name = QStringLiteral("read_file");
    readCall.arguments = QJsonObject{{"path", dir.filePath(QStringLiteral("r.txt"))}};
    mixed.toolCalls.append(readCall);
    ToolCallInfo writeCall;
    writeCall.id = QStringLiteral("call_write");
    writeCall.name = QStringLiteral("write_file");
    writeCall.arguments = QJsonObject{{"path", path}, {"content", "x"}};
    mixed.toolCalls.append(writeCall);
    llm.script.append(mixed);
    llm.script.append(textResponse(QStringLiteral("should wait for approval")));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("mixed"));
    QTRY_COMPARE_WITH_TIMEOUT(confirmSpy.count(), 1, 15000);

    // 确认请求已发出：此刻必须处于 AwaitingToolConfirm，且**没有**发起
    // 第二次 LLM 请求（那意味着流程绕过确认继续跑了）。
    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);
    QTest::qWait(600);          // 给并行调用充足时间完成
    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);
    QCOMPARE(responseSpy.count(), 0);
    QCOMPARE(llm.seenMessages.size(), 1);   // 只有第一次请求
    QVERIFY(!QFile::exists(path));          // 未确认前绝不写文件

    // 批准后才继续。
    agent.approveToolCall(confirmSpy.first().first().toString(), true);
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 15000);
    QCOMPARE(agent.state(), AgentState::Completed);
    QVERIFY(QFile::exists(path));
}

void TestAgent::stopDuringConfirmationIsNoOp() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("out.txt");

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", path}, {"content", "x"}}));
    llm.script.append(textResponse("should not reach"));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("write it"));
    QTRY_COMPARE(confirmSpy.count(), 1);
    const QString id = confirmSpy.first().first().toString();

    agent.stop();
    QCOMPARE(agent.state(), AgentState::Idle);

    agent.approveToolCall(id, true); // after stop this must be a no-op
    QCOMPARE(llm.seenMessages.size(), 1);
    QVERIFY(!QFile::exists(path));
}

void TestAgent::contextBudgetCompressesHistory() {
    // Turn 1 builds a history that overflows the (then-lowered) budget; the
    // second task must trigger compression: system + omission marker + the
    // newest user turn. MinContextTokens (2048) is the smallest budget the
    // agent accepts, so the reply must exceed it (~4 chars/token).
    llm.script.append(textResponse(QString(9600, 'a'))); // long assistant reply
    llm.script.append(textResponse("second answer"));

    QSignalSpy first(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("first"));
    QTRY_COMPARE(first.count(), 1);

    agent.setContextLimit(AIAgent::MinContextTokens);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("second"));
    QTRY_COMPARE(spy.count(), 1);

    const QJsonArray sent = llm.seenMessages.last();
    QCOMPARE(sent.size(), 3); // system + omission marker + newest user turn
    QCOMPARE(sent.at(1).toObject().value("role").toString(), QStringLiteral("system"));
    QVERIFY(sent.at(1).toObject().value("content").toString()
        .contains("[Context compressed"));
    QCOMPARE(sent.at(2).toObject().value("content").toString(), QStringLiteral("second"));
    QVERIFY(traceHas(agent.traceModel(), "context"));
}

void TestAgent::auditTrailRecorded() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Safe tool: auto-executed -> one tool_execute entry
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("a.txt");
    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", path}, {"content", "x"}}));
    llm.script.append(textResponse("done"));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("first"));
    QTRY_COMPARE(confirmSpy.count(), 1);
    const QString id = confirmSpy.first().first().toString();

    // Decline -> tool_declined; the loop then converges with script[1].
    // A declined tool never executes, so exactly one audit entry exists so far.
    agent.approveToolCall(id, false);
    QCOMPARE(agent.state(), AgentState::Completed); // converged after rejection

    const QJsonArray audit = memory.auditLog(20);
    // Batch 11: llm_request/task_complete rows join the tool events —
    // count by event type, not by total rows.
    QCOMPARE(auditCountByEvent(audit, QStringLiteral("tool_declined")), 1);
    // The declined row carries the tool identity...
    for (const auto& entry : audit) {
        const QJsonObject row = entry.toObject();
        if (row.value("event").toString() == QStringLiteral("tool_declined")) {
            QCOMPARE(row.value("toolName").toString(), QStringLiteral("write_file"));
            break;
        }
    }
    // ...and the newest row closes the chain with the outcome.
    // task_complete is a task-level event — toolName stays empty by design;
    // success=true because the task converged (decline is a valid outcome).
    QCOMPARE(audit.at(0).toObject().value("event").toString(),
             QStringLiteral("task_complete"));
    QVERIFY(audit.at(0).toObject().value("toolName").toString().isEmpty());
    QVERIFY(audit.at(0).toObject().value("success").toBool());

    // Safe tool execution produces tool_execute entries (declined entry + execute)
    agent.resetSession();
    llm.reset();
    { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); } // write was declined earlier
    llm.script.append(toolResponse("read_file", QJsonObject{{"path", path}}));
    llm.script.append(textResponse("done"));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("read it"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QJsonArray audit2 = memory.auditLog(20);
    QCOMPARE(auditCountByEvent(audit2, QStringLiteral("tool_execute")), 1);
    // The read task's tool_execute row exists; the newest row (task_complete)
    // closes the chain. Find tool_execute by event, not by position.
    for (const auto& entry : audit2) {
        const QJsonObject row = entry.toObject();
        if (row.value("event").toString() == QStringLiteral("tool_execute")) {
            QCOMPARE(row.value("toolName").toString(), QStringLiteral("read_file"));
            QVERIFY(row.value("success").toBool());
            break;
        }
    }
    QCOMPARE(audit2.at(0).toObject().value("event").toString(),
             QStringLiteral("task_complete"));

    agent.setMemory(nullptr);
}

// ── TEST-002: Knowledge/Normal behavioral matrix ──────────
// Agent-level contract with a scripted (honest) provider: the same session
// carries a knowledge round and normal rounds, and tool usage follows the
// CURRENT message - exactly the behavior the provider-state fix must let
// through. Pairs with test_provider_lifecycle, which proves the real
// providers stop leaking stale tool calls across these turns.

void TestAgent::knowledgeThenNormalQuestion() {
    registerKnowledgeTool();

    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Task 1: knowledge question -> KB tool -> answer from passages
    llm.script.append(toolResponse("search_knowledge_base",
                                   QJsonObject{{"query", QStringLiteral("孙曼")}}));
    llm.script.append(textResponse(QStringLiteral("孙曼是人文学院助理研究员。")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("查询知识库：孙曼是谁？"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QCOMPARE(agent.state(), AgentState::Completed);
    QCOMPARE(llm.seenMessages.size(), 2);    // tool round + final answer
    QCOMPARE(auditCountByEvent(memory.auditLog(50), QStringLiteral("tool_execute")), 1);

    // Task 2 (SAME session): ordinary question. A clean provider returns no
    // tool calls, so the agent must answer in a single LLM round and must
    // not execute any tool.
    const int callsBefore = llm.seenMessages.size();
    llm.script.append(textResponse(
        QStringLiteral("Python 装饰器是一种在运行时扩展函数行为的语法。")));
    agent.startTask(QStringLiteral("Python 装饰器是什么？"));
    QTRY_COMPARE(spy.count(), 2);
    QCOMPARE(llm.seenMessages.size(), callsBefore + 1);
    QCOMPARE(auditCountByEvent(memory.auditLog(50), QStringLiteral("tool_execute")), 1);
    QVERIFY2(noDanglingToolCalls(llm.seenMessages.last()),
             "Knowledge->Normal left an illegal history");

    agent.setMemory(nullptr);
}

void TestAgent::normalThenKnowledgeQuestion() {
    registerKnowledgeTool();

    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    llm.script.append(textResponse(
        QStringLiteral("Python 装饰器是一种在运行时扩展函数行为的语法。")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("Python 装饰器是什么？"));
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(llm.seenMessages.size(), 1);    // direct answer, no tool round

    llm.script.append(toolResponse("search_knowledge_base",
                                   QJsonObject{{"query", QStringLiteral("孙曼")}}));
    llm.script.append(textResponse(QStringLiteral("孙曼是人文学院助理研究员。")));
    agent.startTask(QStringLiteral("查询知识库：孙曼是谁？"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 15000);
    QCOMPARE(llm.seenMessages.size(), 3);    // tool round + final answer
    QCOMPARE(auditCountByEvent(memory.auditLog(50), QStringLiteral("tool_execute")), 1);
    QVERIFY(toolMessageContent(llm.seenMessages.last(), "call_t0").size() > 0);
    QVERIFY2(noDanglingToolCalls(llm.seenMessages.last()),
             "Normal->Knowledge left an illegal history");

    agent.setMemory(nullptr);
}

void TestAgent::knowledgeNormalKnowledgeSequence() {
    registerKnowledgeTool();

    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Knowledge -> Normal -> Knowledge: the middle turn must not suppress
    // the tool, and the last turn must execute again.
    llm.script.append(toolResponse("search_knowledge_base",
                                   QJsonObject{{"query", QStringLiteral("孙曼")}}));
    llm.script.append(textResponse(QStringLiteral("孙曼是人文学院助理研究员。")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("查询知识库：孙曼是谁？"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    llm.script.append(textResponse(
        QStringLiteral("Python 装饰器是一种在运行时扩展函数行为的语法。")));
    agent.startTask(QStringLiteral("Python 装饰器是什么？"));
    QTRY_COMPARE(spy.count(), 2);

    llm.script.append(toolResponse("search_knowledge_base",
                                   QJsonObject{{"query", QStringLiteral("孙曼著作")}}));
    llm.script.append(textResponse(QStringLiteral("孙曼参编两部著作。")));
    agent.startTask(QStringLiteral("再查知识库：孙曼的著作"));
    QTRY_COMPARE(spy.count(), 3);

    QCOMPARE(llm.seenMessages.size(), 2 + 1 + 2);
    QCOMPARE(auditCountByEvent(memory.auditLog(50), QStringLiteral("tool_execute")), 2);
    QVERIFY2(noDanglingToolCalls(llm.seenMessages.last()),
             "Knowledge->Normal->Knowledge left an illegal history");
    QVERIFY2(noDuplicateToolCallIds(llm.seenMessages.last()),
             "duplicate tool_call_id across turns (strict proxies reject these)");

    agent.setMemory(nullptr);
}

// ── Batch 2: agent tool-call lifecycle ────────────────────

// OpenAI 规范：assistant.tool_calls[].function.arguments 必须是 JSON 字符串。
// 无参数工具（git_status 的 required 为空）被调用时 arguments 是空对象，
// 序列化成 "" 会让严格校验的网关 400，报错文案即
// "tool_calls.function.arguments is required"（它把 "" 当作字段缺失）。
// 回归点：argsToString 曾对空对象返回空串。
void TestAgent::emptyArgumentsSerializeAsEmptyObject() {
    llm.reset();
    // git_status 的 required 是空数组：不传 path 即为"无参数调用"。
    llm.script.append(toolResponse(QStringLiteral("git_status"), QJsonObject{}));
    llm.script.append(textResponse(QStringLiteral("done")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("check status"));
    QTRY_COMPARE(spy.count(), 1);

    // 第二轮请求里带着上一轮的 assistant.tool_calls，它是真正上线的报文形态。
    QVERIFY(llm.seenMessages.size() >= 2);
    const QJsonArray secondTurn = llm.seenMessages.at(1);
    QJsonObject call;
    for (const auto& entry : secondTurn) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() != QLatin1String("assistant")) continue;
        const QJsonArray calls = msg.value("tool_calls").toArray();
        if (!calls.isEmpty()) { call = calls.first().toObject(); break; }
    }
    QVERIFY2(!call.isEmpty(), "第二轮请求里没有 assistant.tool_calls");

    const QJsonValue args = call.value("function").toObject().value("arguments");
    QVERIFY2(args.isString(), "arguments 必须是 JSON 字符串（OpenAI 报文要求）");
    QCOMPARE(args.toString(), QStringLiteral("{}"));
}
// Stopping while a RequiresConfirm call is pending must still seal the
// assistant's tool_calls - a dangling sequence is invalid for the wire
// format and derails proxies on the next user turn.
void TestAgent::stopDuringConfirmationSealsToolCalls() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("out.txt");

    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", path}, {"content", "x"}}));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("write it"));
    QTRY_COMPARE(confirmSpy.count(), 1);

    agent.stop();
    QCOMPARE(agent.state(), AgentState::Idle);
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "stop during confirmation left dangling tool_calls");
}

// Stopping while a Safe tool is genuinely executing on the worker thread
// must seal that call too - it has left the pending queue at that point.
void TestAgent::stopDuringExecutionSealsToolCalls() {
    BlockingTool* blocking = registerBlockingTool();
    llm.script.append(toolResponse("test_blocking", QJsonObject{}));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("block"));
    QTRY_VERIFY_WITH_TIMEOUT(traceHasRunningToolCall(agent.traceModel(),
                                                    QStringLiteral("test_blocking")),
                             5000);

    agent.stop();
    QCOMPARE(agent.state(), AgentState::Idle);
    blocking->release();            // let the parked worker finish
    QTest::qWait(300);              // late watcher callback settles (epoch drop)

    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "stop during execution left dangling tool_calls");
    QTest::qWait(200);
    QCOMPARE(spy.count(), 0);       // the stopped task must not resume
}

// B2-A: same-round parallel Safe tools all leave the pending queue at
// dispatch time. stop() only used to seal the LAST dispatched call
// (m_currentExecutingCall), leaving the other in-flight ids dangling. Assert
// exactly one tool response per announced id, and that the next request's
// history is wire-legal.
void TestAgent::stopSealsEveryParallelToolCall() {
    BlockingTool* blocking = registerBlockingTool();
    LLMResponse burst;
    burst.success = true;
    QStringList ids;
    for (int i = 0; i < 3; ++i) {
        ToolCallInfo call;
        call.id = QStringLiteral("call_par_%1").arg(i);
        call.name = QStringLiteral("test_blocking");
        burst.toolCalls.append(call);
        ids.append(call.id);
    }
    llm.script.append(burst);
    llm.script.append(textResponse(QStringLiteral("next turn ok")));

    QSignalSpy readySpy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("three parallel blockers"));
    QTRY_VERIFY_WITH_TIMEOUT(
        runningToolCallCount(agent.traceModel(), QStringLiteral("test_blocking")) == 3,
        5000);

    agent.stop();
    QCOMPARE(agent.state(), AgentState::Idle);
    // Let all three parked workers return; their late callbacks are
    // epoch-dropped and must not append a second tool response.
    blocking->release();
    blocking->release();
    blocking->release();

    const QJsonArray stopped = agent.messages();
    QVERIFY2(noDanglingToolCalls(stopped),
             "stop sealed only the last in-flight parallel call");
    for (const QString& id : ids) {
        QCOMPARE(toolResponseCountForId(stopped, id), 1);
        QVERIFY2(toolMessageContent(stopped, id)
                     .contains(QStringLiteral("Not executed")),
                 qPrintable(id));
    }

    // The next turn must see a valid history: each id answered exactly once.
    agent.startTask(QStringLiteral("next turn"));
    QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() == 1, 5000);
    QVERIFY(!llm.seenMessages.isEmpty());
    const QJsonArray nextHistory = llm.seenMessages.last();
    QVERIFY2(noDanglingToolCalls(nextHistory),
             "next request inherited a dangling tool_calls sequence");
    for (const QString& id : ids)
        QCOMPARE(toolResponseCountForId(nextHistory, id), 1);

    QTest::qWait(300);   // late watcher callbacks settle
    for (const QString& id : ids)
        QCOMPARE(toolResponseCountForId(agent.messages(), id), 1);
}

// Batch 13 (SEC-1304): stop() must reach the tool itself, not only the LLM
// stream - the sleeping execute observes the cancel flag, returns promptly,
// and the epoch-drop path flips the trace node to "cancelled".
void TestAgent::toolCancelledOnStop() {
    AnomalyTool* anomaly = registerAnomalyTool();
    llm.script.append(toolResponse("test_anomaly",
        QJsonObject{{"mode", "sleep"}}));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("sleep"));
    QTRY_VERIFY_WITH_TIMEOUT(traceHasRunningToolCall(agent.traceModel(),
                                                    QStringLiteral("test_anomaly")),
                             5000);

    QElapsedTimer clock;
    clock.start();
    agent.stop();
    QTRY_VERIFY_WITH_TIMEOUT(anomaly->cancelCalled(), 2000);
    // execute() returns within one 100 ms slice of the flag; the late
    // watcher callback (epoch drop) flips the trace node.
    QTRY_VERIFY_WITH_TIMEOUT(
        lastToolCallStatus(agent.traceModel(), QStringLiteral("test_anomaly"))
            == QStringLiteral("cancelled"), 2000);
    QVERIFY2(clock.elapsed() < 2500, "tool cancel took longer than 2 s");
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "stop during sleeping tool left dangling tool_calls");
    QVERIFY2(toolMessageContent(agent.messages(), QStringLiteral("call_t0"))
                 .contains(QStringLiteral("Not executed")),
             "cancelled call was not sealed with a tool result");
    QCOMPARE(spy.count(), 0);       // the stopped task must not resume
    QCOMPARE(agent.state(), AgentState::Idle);
}

// Batch 13 (SEC-1303/1304): the real child process must die when the task is
// stopped. execute() tears down on the pool thread within one 200 ms slice
// (Windows: tree-kill so the cmd.exe /c grandchild does not survive) and only
// returns after the reaping, so the trace flip bounds the side effect.
void TestAgent::shellProcessKilledOnStop() {
    QTemporaryDir directory(testDirTemplate());
    QVERIFY(directory.isValid());
    const QString marker = directory.filePath("shell-started.txt");
    llm.script.append(toolResponse("shell_exec",
        QJsonObject{{"command",
#ifdef Q_OS_WIN
            QStringLiteral("echo started > \"%1\" & ping -n 31 127.0.0.1 > nul").arg(marker)
#else
            QStringLiteral("echo started > \"%1\"; sleep 30").arg(marker)
#endif
        }}));

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("run something long"));
    QTRY_COMPARE_WITH_TIMEOUT(confirmSpy.count(), 1, 5000);
    const QString id = confirmSpy.first().first().toString();
    agent.approveToolCall(id, true);
    QTRY_VERIFY_WITH_TIMEOUT(traceHasRunningToolCall(agent.traceModel(),
                                                    QStringLiteral("shell_exec")),
                             5000);
    // Separately from queued cancellation, this case must stop a command
    // that really launched, not merely a displayed "running" trace node.
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 5000);

    QElapsedTimer clock;
    clock.start();
    agent.stop();
    // 上限放宽到 15 s：全量 ctest 并发负载下，QtConcurrent 池调度 +
    // 事件循环会让"已 kill"的 trace 更新迟到（实测需要过 8 s）；严格
    // 的及时性由下一条 clock 断言把守（放宽到 5 s，仍远小于 sleep 30）。
    QTRY_VERIFY_WITH_TIMEOUT(
        lastToolCallStatus(agent.traceModel(), QStringLiteral("shell_exec"))
            == QStringLiteral("cancelled"), 15000);
    QVERIFY2(clock.elapsed() < 5000, "shell cancel took longer than 5 s");
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "stopped shell left dangling tool_calls");
    QVERIFY2(toolMessageContent(agent.messages(), QStringLiteral("call_t0"))
                 .contains(QStringLiteral("Not executed")),
             "cancelled shell call was not sealed with a tool result");
    QCOMPARE(agent.state(), AgentState::Idle);
}

// Batch 14 (SEC-1404): with the KB tool gated off, its schema must not reach
// the wire at all - asserted at request level via FakeAIService.seenTools.
void TestAgent::localOnlyModeHidesKnowledgeTool() {
    registerKnowledgeTool();
    auto* registry = &ToolRegistry::instance();
    QVERIFY(registry->isRegistered(QStringLiteral("search_knowledge_base")));

    // Baseline: enabled → schema present on the wire.
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    llm.script.append(textResponse(QStringLiteral("kb available")));
    agent.startTask(QStringLiteral("warmup"));
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(llm.seenTools.size(), 1);
    QVERIFY2(toolsArrayContains(llm.seenTools.first(),
                                QStringLiteral("search_knowledge_base")),
             "baseline: KB schema missing while the tool is enabled");

    // Gated: schema gone from the request body.
    registry->setToolEnabled(QStringLiteral("search_knowledge_base"), false);
    QVERIFY(!registry->toolNamesForModel()
                 .contains(QStringLiteral("search_knowledge_base")));
    llm.script.append(textResponse(QStringLiteral("gated")));
    agent.startTask(QStringLiteral("second"));
    QTRY_COMPARE(spy.count(), 2);
    QCOMPARE(llm.seenTools.size(), 2);
    QVERIFY2(!toolsArrayContains(llm.seenTools.last(),
                                 QStringLiteral("search_knowledge_base")),
             "KB schema still exposed while the tool is disabled");
    // cleanup() restores the singleton for the matrix tests.
}

// Batch 14 (SEC-1404): a stale/hallucinated call to the gated tool gets a
// structured error fed back - never executed, pairing intact.
// docs/17 B1: the gate now goes through the POLICY layer; the user's
// tools/disabled persistence must stay untouched.
void TestAgent::localOnlyModeRejectsResidualCall() {
    registerKnowledgeTool();
    auto* registry = &ToolRegistry::instance();
    registry->setPolicyBlocked(QStringList{QStringLiteral("search_knowledge_base")}, true);

    llm.script.append(toolResponse("search_knowledge_base",
        QJsonObject{{"query", QStringLiteral("anything")}}));
    llm.script.append(textResponse(QStringLiteral("final answer")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("residual kb call"));
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("final answer"));

    QVERIFY2(toolMessageContent(agent.messages(), QStringLiteral("call_t0"))
                 .contains(QStringLiteral("local-only")),
             "policy-blocked tool call was not rejected with a local-only error");
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "blocked-tool rejection left dangling tool_calls");
    // Never executed: the call node failed, and no tool_result node exists.
    QCOMPARE(lastToolCallStatus(agent.traceModel(),
                                QStringLiteral("search_knowledge_base")),
             QStringLiteral("failed"));
    QVERIFY2(!traceHas(agent.traceModel(), QStringLiteral("tool_result"),
                       QStringLiteral("search_knowledge_base")),
             "gated KB tool was executed despite being blocked");
    // P0-1 回归：策略拦截只写内存层，用户使能状态不被触碰；
    // 解除拦截后工具立即恢复可用（等价于 localOnly 关闭）。
    QVERIFY(registry->isToolEnabled(QStringLiteral("search_knowledge_base")));
    registry->setPolicyBlocked(QStringList{QStringLiteral("search_knowledge_base")}, false);
    QVERIFY(registry->isToolUsable(QStringLiteral("search_knowledge_base")));
    // cleanup() restores the singleton for the matrix tests.
}

// Batch 14 review fix: isToolEnabled must be false for unregistered names -
// "enabled" only means something for a registered tool.
void TestAgent::toolRegistryEnablementSemantics() {
    auto* registry = &ToolRegistry::instance();
    QVERIFY2(!registry->isToolEnabled(QStringLiteral("no_such_tool")),
             "unregistered tool must not report enabled");

    registerKnowledgeTool();
    QVERIFY(registry->isToolEnabled(QStringLiteral("search_knowledge_base")));

    registry->setToolEnabled(QStringLiteral("search_knowledge_base"), false);
    QVERIFY(!registry->isToolEnabled(QStringLiteral("search_knowledge_base")));
    QVERIFY(!registry->toolNamesForModel()
                 .contains(QStringLiteral("search_knowledge_base")));
    QVERIFY(registry->isRegistered(QStringLiteral("search_knowledge_base")));
    bool inListAll = false;
    for (const auto& def : registry->listAll())
        if (def.name == QStringLiteral("search_knowledge_base")) inListAll = true;
    QVERIFY2(inListAll, "disabled tool must stay in listAll() (UI visibility)");

    registry->setToolEnabled(QStringLiteral("search_knowledge_base"), true);
    QVERIFY(registry->isToolEnabled(QStringLiteral("search_knowledge_base")));

    // docs/17 B1: 双层组合矩阵。策略层拦截不改用户层；两任一为不可用即不可用。
    QSignalSpy changedSpy(registry, &ToolRegistry::toolsChanged);
    registry->setPolicyBlocked(QStringList{QStringLiteral("search_knowledge_base")}, true);
    QCOMPARE(changedSpy.count(), 1);            // UI（徽标）依赖 toolsChanged
    QVERIFY(registry->isPolicyBlocked(QStringLiteral("search_knowledge_base")));
    QVERIFY(registry->isToolEnabled(QStringLiteral("search_knowledge_base")));
    QVERIFY(!registry->isToolUsable(QStringLiteral("search_knowledge_base")));
    QVERIFY(!registry->toolNamesForModel()
                 .contains(QStringLiteral("search_knowledge_base")));

    // 策略解除后恢复可用（用户层从未被动过）。
    registry->setPolicyBlocked(QStringList{QStringLiteral("search_knowledge_base")}, false);
    QVERIFY(registry->isToolUsable(QStringLiteral("search_knowledge_base")));

    // 用户禁用 + 策略解除：仍不可用（P0-1 语义——策略绝不重开用户禁用的工具）。
    registry->setToolEnabled(QStringLiteral("search_knowledge_base"), false);
    registry->setPolicyBlocked(QStringList{QStringLiteral("search_knowledge_base")}, true);
    registry->setPolicyBlocked(QStringList{QStringLiteral("search_knowledge_base")}, false);
    QVERIFY(!registry->isToolUsable(QStringLiteral("search_knowledge_base")));
    registry->setToolEnabled(QStringLiteral("search_knowledge_base"), true);

    // cleanup() also restores the singleton for later tests.
}

// A single response announcing more tool calls than an EXPLICITLY set
// execution cap allows must execute the cap, seal the rest, and converge
// into one final no-tools answer instead of running unbounded work.
// Production defaults to no cap (see mainAgentToolExecutionsUnlimitedByDefault).
void TestAgent::toolExecutionBudgetForcesFinal() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // 显式钉一个小预算，让 16 次上限低于本轮声明的 20 个调用，才测得到
    // "执行预算耗尽"这条路径。生产默认 0（不限制），不会走到这里。
    agent.setMaxToolExecutions(16);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    LLMResponse burst;
    burst.success = true;
    for (int i = 0; i < 20; ++i) {
        ToolCallInfo call;
        call.id = QStringLiteral("call_b%1").arg(i);
        call.name = QStringLiteral("read_file");
        call.arguments = QJsonObject{
            {"path", dir.filePath(QStringLiteral("b%1.txt").arg(i))}};
        burst.toolCalls.append(call);
    }
    llm.script.append(burst);
    llm.script.append(textResponse(QStringLiteral("summarized"))); // forced final

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("read twenty files"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(agent.state(), AgentState::Completed);

    int executions = 0;
    const QJsonArray audit = memory.auditLog(100);
    for (const auto& entry : audit) {
        if (entry.toObject().value("event").toString() == QStringLiteral("tool_execute"))
            ++executions;
    }
    // 断言实际生效的上限（而不是某个常量），上限与步数预算的换算关系变了
    // 这条用例仍然成立。
    QCOMPARE(executions, agent.maxToolExecutions());
    QVERIFY(traceHas(agent.traceModel(), "context", "budget"));
    QVERIFY2(noDanglingToolCalls(llm.seenMessages.last()),
             "budget path left dangling tool_calls");

    agent.setMaxToolExecutions(0);   // 还原生产默认（无限）
    agent.setMemory(nullptr);
}

// 2026-11-02 用户决策：主代理不再有 16 次工具执行预算，与子代理的
// maxSteps=0 语义对齐。默认 0=无限；一轮 20 个调用必须全部执行，且不得
// 出现 budget 收尾节点（否则又会触发强制收尾/伪工具调用泄漏）。
void TestAgent::mainAgentToolExecutionsUnlimitedByDefault() {
    QCOMPARE(agent.maxToolExecutions(), 0);

    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    LLMResponse burst;
    burst.success = true;
    for (int i = 0; i < 20; ++i) {
        ToolCallInfo call;
        call.id = QStringLiteral("call_u%1").arg(i);
        call.name = QStringLiteral("read_file");
        call.arguments = QJsonObject{
            {"path", dir.filePath(QStringLiteral("u%1.txt").arg(i))}};
        burst.toolCalls.append(call);
    }
    llm.script.append(burst);
    llm.script.append(textResponse(QStringLiteral("all read")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("read twenty files without a budget"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(agent.state(), AgentState::Completed);

    int executions = 0;
    for (const auto& entry : memory.auditLog(100)) {
        if (entry.toObject().value("event").toString() == QStringLiteral("tool_execute"))
            ++executions;
    }
    QCOMPARE(executions, 20);
    QVERIFY2(!traceHas(agent.traceModel(), "context", "budget"),
             "explicit budget path fired without a configured cap");

    agent.setMemory(nullptr);
}

// The "[System] Step budget exhausted..." control message drives the forced
// final answer but belongs to the REQUEST, not the conversation: it must be
// injected into the forced call and must never leak into later turns.
void TestAgent::forcedFinalControlMessageNotPersisted() {
    // 脚本按 10 轮预算写；钉住预算，否则第 11 轮起会重复最后一条响应，被
    // "同轮重复调用"守卫提前收尾，测不到"预算耗尽后的强制收尾"这条路径。
    agent.setMaxIterations(10);
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (int i = 0; i < 10; ++i)
        llm.script.append(toolResponse("list_dir",
            QJsonObject{{"path", home + "/nexusai_nonexistent_dir_" + QString::number(i)}}));
    llm.script.append(textResponse(QStringLiteral("forced summary")));  // forced final
    llm.script.append(textResponse(QStringLiteral("next answer")));     // next task

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("loop"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    agent.startTask(QStringLiteral("next question"));
    QTRY_COMPARE(spy.count(), 2);

    const QJsonArray sent = llm.seenMessages.last();
    for (const auto& entry : sent) {
        QVERIFY2(!entry.toObject().value("content").toString()
                      .contains(QStringLiteral("Step budget exhausted")),
                 "forced-final control message leaked into the next request");
    }
    QVERIFY2(noDanglingToolCalls(sent),
             "forced final + next turn left dangling tool_calls");

    agent.setMaxIterations(AIAgent::kDefaultMaxIterations);   // 还原共享 agent
}

// A9: forced-final 轮的请求确实不带 tools（processIteration 构建空数组），
// 因此窗口占用/来源拆分也必须不再预留完整 schema；任务结束后 m_forcedFinal
// 复位，下一次请求/新任务恢复 schema 预留。
void TestAgent::forcedFinalDoesNotReserveToolsSchema() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    struct RestoreMemory {
        AIAgent& agent;
        ~RestoreMemory() { agent.setMemory(nullptr); }
    } restoreMemory{agent};

    ForcedFinalSchemaService service(&agent);
    struct RestoreService {
        AIAgent& agent;
        FakeAIService* original;
        ~RestoreService() { agent.setLLMService(original); }
    } restoreService{agent, &llm};
    agent.setLLMService(&service);

    agent.setMaxIterations(3);
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (int i = 0; i < 3; ++i) {
        service.script.append(toolResponse(QStringLiteral("list_dir"),
            QJsonObject{{QStringLiteral("path"),
                         home + QStringLiteral("/nexusai_final_schema_%1").arg(i)}}));
    }
    service.script.append(textResponse(QStringLiteral("forced summary")));
    service.script.append(textResponse(QStringLiteral("next answer")));

    QSignalSpy tokensSpy(&agent, &AIAgent::tokensChanged);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("loop"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    // 3 轮正常请求 + 1 轮 forced-final：前 3 轮带 schema，最后一轮为空。
    QCOMPARE(service.schemas.size(), 4);
    QVERIFY2(service.schemas.at(0) > 0,
             "normal round must reserve the tools schema");
    QCOMPARE(service.schemas.at(3), 0);
    QVERIFY2(service.toolsEmpty.at(3), "forced-final request must carry no tools");

    // 任务结束后 m_forcedFinal 必须复位：composition 与 tokensChanged 恢复
    // schema 预留，而不是长期显示“无工具”的偏小占用。
    const QVariantMap afterTask = agent.contextComposition();
    QVERIFY2(afterTask.value(QStringLiteral("toolsSchema")).toLongLong() > 0,
             "forcedFinal leaked into the idle composition");
    QVERIFY(tokensSpy.count() >= 1);
    QCOMPARE(tokensSpy.last().at(0).toLongLong(),
             afterTask.value(QStringLiteral("totalTokens")).toLongLong());

    // 新任务的第一个请求恢复非 0 schema 预留。
    agent.startTask(QStringLiteral("next question"));
    QTRY_COMPARE(spy.count(), 2);
    QVERIFY2(service.schemas.size() >= 5, "next task did not issue a request");
    QVERIFY2(service.schemas.at(4) > 0,
             "new task must reserve the tools schema again");

    agent.setMaxIterations(AIAgent::kDefaultMaxIterations);   // 还原共享 agent
}
// docs/35: 部分模型的 chat template 把工具调用写成 XML 文本。端点只有在
// 模板完整时才会翻译成结构化 tool_calls；服务端解析失败时模板会作为普通
// content 回来。它不得被当成回答展示/落库（真机曾把整条伪调用写进
// messages 表并在聊天区显示成最终答复）。
void TestAgent::textualToolCallMarkupDiscardedFromAnswer() {
    llm.script.append(textResponse(QStringLiteral("Here is the summary.\n")
                                   + textualToolCallMarkup()));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("news roundup"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    const QString answer = spy.at(0).at(0).toString();
    QCOMPARE(answer, QStringLiteral("Here is the summary."));

    // 摘要节点就是用户看到的正文，同样不能带模板。
    bool sawSummary = false;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString()
                != QLatin1String("summary"))
            continue;
        sawSummary = true;
        QVERIFY2(!node.value(QStringLiteral("title")).toString()
                      .contains(QStringLiteral("<tool_call")),
                 "textual tool-call markup leaked into the summary node");
    }
    QVERIFY(sawSummary);
}

// 真机根因路径：工具预算耗尽 → 撤掉 tools 的强制收尾调用 → 模型仍想搜索，
// 把原生模板当文本输出。此时必须回落到明确说明，而不是把伪调用当答案。
void TestAgent::forcedFinalTextualToolCallDoesNotBecomeTheAnswer() {
    agent.setMaxIterations(3);   // 下限就是 3：三轮工具调用后进入强制收尾
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (int i = 0; i < 3; ++i) {
        llm.script.append(toolResponse(QStringLiteral("list_dir"),
            QJsonObject{{QStringLiteral("path"),
                         home + QStringLiteral("/nexusai_missing_markup_dir_%1")
                                    .arg(i)}}));
    }
    llm.script.append(textResponse(textualToolCallMarkup()));   // 强制收尾的回复

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("what is the market doing"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    const QString answer = spy.at(0).at(0).toString();
    QVERIFY2(!answer.contains(QStringLiteral("<tool_call")), qPrintable(answer));
    QVERIFY2(answer.startsWith(QStringLiteral("Step budget exhausted")),
             qPrintable(answer));

    agent.setMaxIterations(AIAgent::kDefaultMaxIterations);   // 还原共享 agent
}

// 未闭合的模板（流被截断 / 用户看到的就是截断版）要把尾部一起吃掉；
// 多个块逐个移除，块外的正文保留。
void TestAgent::textualToolCallSanitizerHandlesTruncatedAndMultipleBlocks() {
    QString text = QStringLiteral("a\n<tool_call><function=x>...</tool_call>\nb\n"
                                  "<tool_call><function=y><parameter=q>news");
    QCOMPARE(ModelOutput::stripTextualToolCalls(text), 2);
    QCOMPARE(text, QStringLiteral("a\n\nb"));

    QString plain = QStringLiteral("no markup here");
    QCOMPARE(ModelOutput::stripTextualToolCalls(plain), 0);
    QCOMPARE(plain, QStringLiteral("no markup here"));
}

// ── Batch 4: request-context architecture ─────────────────

// Switching the LLM service while a task is mid-flight (provider profile
// change) must terminate the active task cleanly - the previous behavior
// disconnected the in-flight response's receiver and left the agent stuck
// in ExecutingTool forever, unable to accept new tasks.
void TestAgent::switchServiceMidTaskTerminatesCleanly() {
    BlockingTool* blocking = registerBlockingTool();
    llm.script.append(toolResponse("test_blocking", QJsonObject{}));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("block"));
    QTRY_VERIFY_WITH_TIMEOUT(traceHasRunningToolCall(agent.traceModel(),
                                                    QStringLiteral("test_blocking")),
                             5000);

    agent.setLLMService(&otherLlm);   // must stop the active task first

    QCOMPARE(agent.state(), AgentState::Idle);
    blocking->release();              // let the parked worker finish
    QTest::qWait(300);
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "mid-task service switch left dangling tool_calls");

    // The agent is immediately usable on the new service.
    otherLlm.script.append(textResponse(QStringLiteral("on new service")));
    agent.startTask(QStringLiteral("again"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QCOMPARE(agent.lastResponse(), QStringLiteral("on new service"));
    QCOMPARE(agent.state(), AgentState::Completed);

    agent.setLLMService(&llm);        // restore the fixture default
}

// A response echoing a superseded request's id must be dropped instead of
// being acted on. Here the agent sits in AwaitingToolConfirm for request 2
// while a late reply from request 1 (with its own tool call) arrives.
void TestAgent::staleResponseFromSupersededRequestIgnored() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Request 1 completes normally
    llm.script.append(textResponse(QStringLiteral("first")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("hello"));
    QTRY_COMPARE(spy.count(), 1);

    // Request 2 dispatches and suspends on a confirmation
    llm.script.append(toolResponse("write_file",
        QJsonObject{{"path", QDir::temp().filePath("nx_stale.txt")}, {"content", "y"}}));
    agent.startTask(QStringLiteral("second"));
    QCOMPARE(llm.requestCounter, 2u);
    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);

    // A late response from request 1 arrives: must be dropped wholesale.
    LLMResponse stale;
    stale.success = true;
    stale.requestId = 1;
    stale.toolCalls.append(ToolCallInfo{
        QStringLiteral("call_stale"), QStringLiteral("read_file"),
        QJsonObject{{"path", QDir::temp().filePath("nx_stale.txt")}}});
    emit llm.responseReady(stale);

    QCOMPARE(agent.state(), AgentState::AwaitingToolConfirm);   // unchanged
    QCOMPARE(llm.seenMessages.size(), 2);                       // no new LLM call

    agent.stop();   // finish the fixture state cleanly
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "stale-response handling left dangling tool_calls");
    agent.setMemory(nullptr);
}


// ── Batch 7: resume semantics contract (MEM-701) ──────────
// Persisted history is clean user/assistant pairs; tool evidence lives in
// trace_nodes for the UI and is NEVER re-injected into the LLM context.
// resume → continue → re-resume must behave like an uninterrupted
// conversation with the same visible text.
void TestAgent::resumeKeepsCleanHistoryAcrossTurns() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("memo.txt");
    { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("payload"); f.close(); }

    // Turn 1 with a tool round, then a final answer
    llm.script.append(toolResponse("read_file", QJsonObject{{"path", path}}));
    llm.script.append(textResponse(QStringLiteral("first done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("first task"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QString sessionId = agent.currentSessionId();
    QVERIFY(!sessionId.isEmpty());
    agent.resetSession();
    agent.resumeSession(sessionId);
    // resumed in-memory history: system + user1 + assistant1 (no tool round)
    QCOMPARE(agent.messages().size(), 3);

    // Turn 2 continues on the same session
    llm.script.append(textResponse(QStringLiteral("second done")));
    agent.startTask(QStringLiteral("second task"));
    QTRY_COMPARE(spy.count(), 2);

    const QJsonArray sent2 = llm.seenMessages.last();
    for (const auto& entry : sent2) {
        const QJsonObject msg = entry.toObject();
        QVERIFY2(!msg.contains("tool_calls"),
                 "tool_calls resurrected after resume");
        QVERIFY2(msg.value("role").toString() != QStringLiteral("tool"),
                 "tool result re-injected after resume");
    }
    QVERIFY2(noDanglingToolCalls(sent2), "resumed history illegal");

    // Leave and re-resume, then continue again: sequence must stay equivalent
    agent.resetSession();
    agent.resumeSession(sessionId);

    llm.script.append(textResponse(QStringLiteral("third done")));
    agent.startTask(QStringLiteral("third task"));
    QTRY_COMPARE(spy.count(), 3);

    // system + user1 + assistant1 + user2 + assistant2 + user3
    QCOMPARE(llm.seenMessages.last().size(), 6);
    QCOMPARE(llm.seenMessages.last().at(1).toObject().value("content").toString(),
             QStringLiteral("first task"));
    QVERIFY2(noDanglingToolCalls(llm.seenMessages.last()),
             "re-resumed history illegal");

    agent.setMemory(nullptr);
}


// ── Batch 9: TEST-903 Tool anomaly matrix ─────────────────
// Every anomaly a tool can produce must feed back into the conversation and
// converge — no path may wedge the loop or leave dangling tool_calls.
void TestAgent::toolAnomalyPathsConverge() {
    registerAnomalyTool();
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    LLMResponse burst;
    burst.success = true;
    const QVector<QPair<QString, QString>> modes = {
        {"fail", "a"}, {"empty", "b"}, {"huge", "c"}};
    int i = 0;
    for (const auto& mode : modes) {
        ToolCallInfo call;
        call.id = QStringLiteral("call_anom%1").arg(i);
        call.name = QStringLiteral("test_anomaly");
        call.arguments = QJsonObject{{"mode", mode.first}};
        burst.toolCalls.append(call);
        ++i;
    }
    llm.script.append(burst);
    llm.script.append(textResponse(QStringLiteral("anomalies handled")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("exercise anomalies"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(agent.state(), AgentState::Completed);

    int executions = 0;
    for (const auto& entry : memory.auditLog(50)) {
        if (entry.toObject().value("event").toString() == QStringLiteral("tool_execute"))
            ++executions;
    }
    QCOMPARE(executions, 3);

    const QJsonArray sent = llm.seenMessages.last();
    QString failMsg, hugeMsg;
    for (const auto& entry : sent) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() != QStringLiteral("tool")) continue;
        if (msg.value("content").toString().contains(QStringLiteral("boom")))
            failMsg = msg.value("content").toString();
        if (msg.value("content").toString().contains(QStringLiteral("[truncated")))
            hugeMsg = msg.value("content").toString();
    }
    QVERIFY2(failMsg.startsWith(QStringLiteral("Error: ")),
             "failed tool result not fed back as an error");
    QVERIFY2(!hugeMsg.isEmpty(), "huge tool output not truncated for the LLM");
    QVERIFY2(noDanglingToolCalls(sent), "anomaly burst left dangling tool_calls");
    agent.setMemory(nullptr);
}


// One response carrying several DIFFERENT tool calls dispatches all of them
// in order and converges (complements the same-tool burst in
// toolExecutionBudgetForcesFinal).
void TestAgent::multipleDistinctToolCallsInOneResponse() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    registerAnomalyTool();

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath("a.txt");
    { QFile f(filePath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("payload"); f.close(); }

    LLMResponse mixed;
    mixed.success = true;
    ToolCallInfo readCall;
    readCall.id = QStringLiteral("call_m0");
    readCall.name = QStringLiteral("read_file");
    readCall.arguments = QJsonObject{{"path", filePath}};
    mixed.toolCalls.append(readCall);
    ToolCallInfo anomalyCall;
    anomalyCall.id = QStringLiteral("call_m1");
    anomalyCall.name = QStringLiteral("test_anomaly");
    anomalyCall.arguments = QJsonObject{{"mode", QStringLiteral("ok")}};
    mixed.toolCalls.append(anomalyCall);
    llm.script.append(mixed);
    llm.script.append(textResponse(QStringLiteral("mixed handled")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("mixed tools"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(agent.state(), AgentState::Completed);
    QCOMPARE(llm.seenMessages.size(), 2);

    const QJsonArray sent = llm.seenMessages.last();
    int readPos = -1, anomalyPos = -1, idx = 0;
    for (const auto& entry : sent) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() != QStringLiteral("tool")) { ++idx; continue; }
        const QString content = msg.value("content").toString();
        if (content.contains(QStringLiteral("payload"))) readPos = idx;
        if (content.contains(QStringLiteral("fine"))) anomalyPos = idx;
        ++idx;
    }
    // P2: parallel execution — results may arrive in any order; verify both
    // are present rather than asserting a specific ordering.
    QVERIFY(readPos >= 0 && anomalyPos >= 0);
    QVERIFY2(noDanglingToolCalls(sent), "mixed burst left dangling tool_calls");
    agent.setMemory(nullptr);
}


// ── Batch 9: TEST-902 concurrent-agent isolation ──────────
// Three agents (users A/B/C) with independent memories and services share
// only the stateless tool registry. Interleaved tasks must never leak
// content or state across agents.
void TestAgent::concurrentAgentsStayIsolated() {
    BlockingTool* blocking = registerBlockingTool();

    AIAgent agentA, agentB, agentC;
    FakeAIService llmA, llmB, llmC;
    SqliteMemory memA(QStringLiteral(":memory:")), memB(QStringLiteral(":memory:")),
        memC(QStringLiteral(":memory:"));
    agentA.setLLMService(&llmA);  agentA.setToolRegistry(&ToolRegistry::instance());  agentA.setMemory(&memA);
    agentB.setLLMService(&llmB);  agentB.setToolRegistry(&ToolRegistry::instance());  agentB.setMemory(&memB);
    agentC.setLLMService(&llmC);  agentC.setToolRegistry(&ToolRegistry::instance());  agentC.setMemory(&memC);

    llmA.script.append(toolResponse("test_blocking", QJsonObject{}));
    llmA.script.append(textResponse(QStringLiteral("A done")));
    llmB.script.append(textResponse(QStringLiteral("B done")));
    llmC.script.append(textResponse(QStringLiteral("C done")));

    QSignalSpy spyA(&agentA, &AIAgent::responseReady);
    QSignalSpy spyB(&agentB, &AIAgent::responseReady);
    QSignalSpy spyC(&agentC, &AIAgent::responseReady);

    agentA.startTask(QStringLiteral("A task"));
    QTRY_VERIFY_WITH_TIMEOUT(traceHasRunningToolCall(agentA.traceModel(),
                                                    QStringLiteral("test_blocking")),
                             5000);

    agentB.startTask(QStringLiteral("B task"));
    QTRY_COMPARE_WITH_TIMEOUT(spyB.count(), 1, 5000);
    agentC.startTask(QStringLiteral("C task"));
    QTRY_COMPARE_WITH_TIMEOUT(spyC.count(), 1, 5000);

    QVERIFY2(!agentB.lastResponse().contains(QStringLiteral("A done")),
             "B received A's response");
    QVERIFY2(!agentC.lastResponse().contains(QStringLiteral("A task")),
             "C saw A's input");

    blocking->release();
    QTRY_COMPARE_WITH_TIMEOUT(spyA.count(), 1, 10000);
    QCOMPARE(agentA.lastResponse(), QStringLiteral("A done"));
    QCOMPARE(agentB.lastResponse(), QStringLiteral("B done"));
    QCOMPARE(agentC.lastResponse(), QStringLiteral("C done"));

    for (const auto& entry : agentB.messages()) {
        QVERIFY2(!entry.toObject().value("content").toString()
                        .contains(QStringLiteral("A task")),
                 "A's input leaked into B's history");
    }
    QVERIFY2(noDanglingToolCalls(agentA.messages()),
             "A's interleave left dangling tool_calls");
}


// ── Batch 10: REL-1001 task timeout ───────────────────────
// A task parked beyond the configured timeout must terminate gracefully:
// sealed tool calls, a final response, and no further processing when the
// parked worker eventually returns.
void TestAgent::taskTimeoutSealsAndConverges() {
    BlockingTool* blocking = registerBlockingTool();
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    agent.setTaskTimeoutMs(300);   // fires while the tool is parked

    llm.script.append(toolResponse("test_blocking", QJsonObject{}));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("slow task"));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
    QCOMPARE(agent.state(), AgentState::Completed);
    QVERIFY2(agent.lastResponse().contains(QStringLiteral("timed out")),
             qPrintable(QStringLiteral("unexpected final response: ")
                            + agent.lastResponse()));
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "timeout left dangling tool_calls");

    blocking->release();   // parked worker returns later: must be a no-op
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1);
    agent.setMemory(nullptr);
}


// ── Batch 11: observability chain (OBS-1101/1102/1103) ────
// One tool-using task must produce audit rows linkable by request_id:
// llm_request (per LLM call) → tool_execute → task_complete, each carrying
// model/latency/tokens, plus a metrics trace node.
void TestAgent::auditCarriesRequestContext() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("a.txt");
    { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("payload"); f.close(); }

    llm.script.append(toolResponse("read_file", QJsonObject{{"path", path}}));
    llm.script.append(textResponse(QStringLiteral("done answer")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("audit me"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QJsonArray audit = memory.auditLog(50);
    int llmRequests = 0, toolExecutes = 0, taskComplete = 0;
    for (const auto& entry : audit) {
        const QJsonObject row = entry.toObject();
        QVERIFY2(row.value("requestId").toDouble() > 0,
                 qPrintable(QStringLiteral("row without requestId: %1")
                                .arg(row.value("event").toString())));
        QVERIFY2(!row.value("model").toString().isEmpty(),
                 "row without model identity");
        const QString event = row.value("event").toString();
        if (event == QStringLiteral("llm_request")) ++llmRequests;
        if (event == QStringLiteral("tool_execute")) ++toolExecutes;
        if (event == QStringLiteral("task_complete")) {
            ++taskComplete;
            QVERIFY(row.value("latencyMs").toDouble() >= 0);
            QVERIFY(row.value("detail").toString()
                        .contains(QStringLiteral("done answer")));
        }
    }
    QCOMPARE(llmRequests, 2);        // one per LLM call
    QCOMPARE(toolExecutes, 1);
    if (taskComplete != 1) {
        for (const auto& entry : audit)
            qWarning() << "audit row:" << entry.toObject().value("event").toString()
                       << entry.toObject().value("requestId").toDouble();
    }
    QCOMPARE(taskComplete, 1);

    // OBS-1103: metrics trace node
    QVERIFY(traceHas(agent.traceModel(), "metrics"));
    agent.setMemory(nullptr);
}


// Batch 22 (F8): a non-empty override replaces the built-in system prompt on
// the wire; clearing it (trim-to-empty) restores the built-in.
void TestAgent::systemPromptOverrideReachesRequest() {
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("ok")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);

    agent.setSystemPromptOverride(QStringLiteral("  CUSTOM SYSTEM PROMPT  "));
    agent.startTask(QStringLiteral("hello"));
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(llm.seenMessages.size(), 1);
    const QJsonObject sysMsg = llm.seenMessages.first().first().toObject();
    QCOMPARE(sysMsg.value("role").toString(), QStringLiteral("system"));
    QCOMPARE(sysMsg.value("content").toString(), QStringLiteral("CUSTOM SYSTEM PROMPT"));  // trimmed

    // Clearing restores the built-in prompt for the next session
    agent.setSystemPromptOverride(QStringLiteral("   "));
    agent.resetSession();
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("ok")));
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("hello again"));
    QTRY_COMPARE(spy2.count(), 1);
    const QString content = llm.seenMessages.first().first().toObject()
                                .value("content").toString();
    QVERIFY(content.contains(QStringLiteral("You are NexusAI")));
    agent.setSystemPromptOverride(QString());   // leave the fixture clean
}

// docs/37 W3: `$` 引用的技能正文按轮注入系统提示；user 消息 metadata 与
// 审计行携带 skill_refs；任务结束后清空，绝不泄漏到下一任务。
// 单技能 8 KiB / 总量 32 KiB 的上限必须截断并留下可见标记。
void TestAgent::skillInjectionAndBudget() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    Skill review;
    review.id = QStringLiteral("review");
    review.name = QStringLiteral("review");
    review.description = QStringLiteral("Review a diff");
    review.body = QStringLiteral("Always report the highest severity first.");
    review.source = SkillSource::Project;
    review.sourcePath = QStringLiteral("D:/work/demo/.nexusai/skills/review/SKILL.md");
    review.bytes = review.body.toUtf8().size();

    agent.setActiveSkills({review});
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("ok")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("please $review this diff"));
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(!llm.seenMessages.isEmpty());

    QString systemPrompt;
    for (const auto& entry : llm.seenMessages.first()) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QLatin1String("system"))
            systemPrompt = msg.value("content").toString();
    }
    QVERIFY2(systemPrompt.contains(QStringLiteral("## 已引用技能")),
             qPrintable(systemPrompt.right(500)));
    QVERIFY(systemPrompt.contains(
        QStringLiteral("<skill name=\"review\" source=\"project\">")));
    QVERIFY(systemPrompt.contains(
        QStringLiteral("Always report the highest severity first.")));
    QVERIFY(systemPrompt.contains(QStringLiteral("</skill>")));
    QVERIFY(systemPrompt.contains(QStringLiteral("</skills>")));

    // 用户消息 metadata：UI/审计可读的 skill 引用列表。
    bool metadataFound = false;
    for (const auto& entry : llm.seenMessages.first()) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() != QLatin1String("user")) continue;
        const QJsonArray refs =
            msg.value("metadata").toObject().value("skills").toArray();
        if (refs.size() != 1) continue;
        QCOMPARE(refs.first().toObject().value("name").toString(),
                 QStringLiteral("review"));
        QCOMPARE(refs.first().toObject().value("source").toString(),
                 QStringLiteral("project"));
        metadataFound = true;
    }
    QVERIFY2(metadataFound, "user message metadata must carry skills");

    // 任务开始审计：当前 audit 表无专用列，event=skill_refs + arguments=JSON。
    bool auditFound = false;
    for (const auto& entry : memory.auditLog(50)) {
        const QJsonObject row = entry.toObject();
        if (row.value("event").toString() != QLatin1String("skill_refs"))
            continue;
        QVERIFY(row.value("arguments").toString()
                    .contains(QStringLiteral("\"review\"")));
        auditFound = true;
    }
    QVERIFY2(auditFound, "task start must audit skill_refs");

    // 任务结束即清空：同一会话的下一次请求不得再带技能块。
    QVERIFY(agent.activeSkills().isEmpty());
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("ok")));
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("plain follow-up"));
    QTRY_COMPARE(spy2.count(), 1);
    const QString nextSystem =
        llm.seenMessages.first().first().toObject()
            .value("content").toString();
    QVERIFY(!nextSystem.contains(QStringLiteral("<skill name=")));

    // 显式清除同样生效（防止未被任务收尾覆盖的路径泄漏）。
    agent.setActiveSkills({review});
    QCOMPARE(agent.activeSkills().size(), 1);
    agent.clearActiveSkills();
    QVERIFY(agent.activeSkills().isEmpty());

    // 单技能上限 8 KiB：截断并追加标记。
    Skill big;
    big.id = QStringLiteral("big");
    big.name = QStringLiteral("big");
    big.source = SkillSource::User;
    big.body = QString(9000, QLatin1Char('x'));
    agent.setActiveSkills({big});
    const QString bigPrompt = agent.effectiveSystemPrompt();
    QVERIFY(bigPrompt.contains(QStringLiteral("[skill truncated]")));
    QVERIFY(bigPrompt.contains(QString(8192, QLatin1Char('x'))));
    QVERIFY(!bigPrompt.contains(QString(8193, QLatin1Char('x'))));

    // 总上限 32 KiB：5 × 8 KiB 只能注入前 4 条。
    QVector<Skill> many;
    for (int i = 0; i < 5; ++i) {
        Skill s;
        s.id = QStringLiteral("s%1").arg(i);
        s.name = s.id;
        s.source = SkillSource::User;
        s.body = QString(8192, QLatin1Char(static_cast<char>('a' + i)));
        many.append(s);
    }
    agent.setActiveSkills(many);
    const QString totalPrompt = agent.effectiveSystemPrompt();
    int blocks = 0;
    for (int pos = totalPrompt.indexOf(QStringLiteral("<skill name="));
         pos >= 0;
         pos = totalPrompt.indexOf(QStringLiteral("<skill name="), pos + 1)) {
        ++blocks;
    }
    QCOMPARE(blocks, 4);

    // empty body 跳过：不产生空的 skill 块。
    Skill empty;
    empty.id = QStringLiteral("empty");
    empty.name = empty.id;
    empty.body = QStringLiteral("   ");
    agent.setActiveSkills({empty});
    QVERIFY(!agent.effectiveSystemPrompt()
                 .contains(QStringLiteral("## 已引用技能")));

    agent.clearActiveSkills();
    agent.setMemory(nullptr);
    agent.resetSession();
}

// docs/41：Skill 只注入正文，allowed-tools 仅是 legacy 元数据——
// schema 仍下发全部可用工具，派发不被技能拒绝，系统提示不含白名单段。
void TestAgent::allowedToolsFrontmatterDoesNotRestrictTools() {
    agent.setLocalOnlyMode(false);
    Skill skill;
    skill.id = QStringLiteral("review");
    skill.name = QStringLiteral("review");
    skill.source = SkillSource::Project;
    skill.body = QStringLiteral("Only report the highest severity first.");
    skill.allowedTools = {QStringLiteral("read_file")};
    skill.allowedToolsSpecified = true;
    agent.setActiveSkills({skill});

    const QString prompt = agent.effectiveSystemPrompt();
    QVERIFY2(prompt.contains(QStringLiteral("<skill")), qPrintable(prompt.right(500)));
    QVERIFY(prompt.contains(QStringLiteral("Only report the highest severity first.")));
    QVERIFY(!prompt.contains(QStringLiteral("allowed-tools")));
    QVERIFY(!prompt.contains(QStringLiteral("本轮允许工具")));

    llm.reset();
    llm.script.append(textResponse(QStringLiteral("ok")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("go"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    // allowed-tools 写了 read_file，但写/执行等其它可用工具仍在 schema 中。
    const QSet<QString> names = toolsArrayNames(llm.seenTools.first());
    QVERIFY(names.contains(QStringLiteral("read_file")));
    QVERIFY(names.contains(QStringLiteral("write_file")));
    QVERIFY(names.contains(QStringLiteral("shell_exec")));
    QVERIFY(names.contains(QStringLiteral("todo_write")));
    QVERIFY(names.size() > 3);

    // 任务结束清理行为不变：技能不跨任务保留。
    QVERIFY(agent.activeSkills().isEmpty());
    QVERIFY(!agent.effectiveSystemPrompt().contains(QStringLiteral("<skill")));
}

// (4) Switching to a larger window restores previously compressed history
// from SQLite (scenario B in docs/15 §4.1).
void TestAgent::testSwitchToLargerWindowRestoresHistory() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Start with a small window so the second turn's history overflows and
    // gets compressed (system + omission marker + newest user turn). Each
    // reply is ~3000 tokens; two turns exceed the 4096 budget even after
    // subtracting the reserved tools-schema tokens, and the compressed
    // suffix (user2 + assistant2) stays under 4096.
    agent.setContextLimit(4096);
    llm.script.append(textResponse(QString(12000, 'a')));
    llm.script.append(textResponse(QString(12000, 'b')));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("first question"));
    QTRY_COMPARE(spy.count(), 1);
    agent.startTask(QStringLiteral("second question"));
    QTRY_COMPARE(spy.count(), 2);

    const int compressedSize = agent.messages().size();

    // Switch to a larger window: onContextLimitChanged restores the full
    // history from SQLite.
    agent.setContextLimit(128000);
    const int restoredSize = agent.messages().size();

    QVERIFY2(restoredSize > compressedSize,
             "History should be restored after switching to larger window");
    agent.setMemory(nullptr);
}

// (5) Switching to a smaller window compresses the history immediately
// (scenario A in docs/15 §4.1).
void TestAgent::testSwitchToSmallerWindowCompressesImmediately() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Start with a large window and build a history that fits comfortably.
    // Each reply is ~3000 tokens; two turns give ~6000 tokens, well above
    // the 4096 small-window budget.
    agent.setContextLimit(128000);
    llm.script.append(textResponse(QString(12000, 'a')));
    llm.script.append(textResponse(QString(12000, 'b')));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("first question"));
    QTRY_COMPARE(spy.count(), 1);
    agent.startTask(QStringLiteral("second question"));
    QTRY_COMPARE(spy.count(), 2);

    const int beforeSwitch = agent.messages().size();

    // Switch to a small window: onContextLimitChanged compresses immediately.
    agent.setContextLimit(4096);
    const int afterSwitch = agent.messages().size();

    QVERIFY2(afterSwitch < beforeSwitch,
             "History should be compressed immediately after switching to smaller window");
    agent.setMemory(nullptr);
}

// (6) resumeSession loads persisted history and adapts it to the current
// (small) context window (docs/15 §4.2.4).
void TestAgent::testResumeSessionAdaptsToContextLimit() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Build a large history with a big window and persist it. Each reply is
    // ~3000 tokens; two turns give ~6000 tokens, well above the 4096 budget.
    agent.setContextLimit(128000);
    llm.script.append(textResponse(QString(12000, 'a')));
    llm.script.append(textResponse(QString(12000, 'b')));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("first question"));
    QTRY_COMPARE(spy.count(), 1);
    agent.startTask(QStringLiteral("second question"));
    QTRY_COMPARE(spy.count(), 2);

    const QString sessionId = agent.currentSessionId();
    QVERIFY(!sessionId.isEmpty());

    // Switch to a small window, then resume the session. resumeSession loads
    // the full history from SQLite and compresses it to fit.
    agent.resetSession();
    agent.setContextLimit(4096);
    agent.resumeSession(sessionId);

    // The resumed history must have been compressed to fit the small window.
    QVERIFY2(TokenEstimator::estimateTokens(agent.messages()) <= 4096,
             "Resumed history should be compressed to fit the small context window");
    agent.setMemory(nullptr);
}

// (7) While a task is active (m_active == true), switching the context window
// must not trigger immediate adaptation (risk #6 in docs/15 §8).
void TestAgent::testNoAdaptationDuringActiveTask() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // Build a history with a large window.
    agent.setContextLimit(128000);
    llm.script.append(textResponse(QString(9600, 'a')));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("first question"));
    QTRY_COMPARE(spy.count(), 1);

    // Start a task that blocks so m_active stays true during the switch.
    BlockingTool* blocking = registerBlockingTool();
    llm.script.append(toolResponse("test_blocking", QJsonObject{}));
    agent.startTask(QStringLiteral("block"));
    QTRY_VERIFY_WITH_TIMEOUT(traceHasRunningToolCall(agent.traceModel(),
                                                    QStringLiteral("test_blocking")),
                             5000);
    QVERIFY(agent.isActive());

    const int beforeSwitch = agent.messages().size();

    // Switch to a small window while the task is active: onContextLimitChanged
    // must not compress.
    agent.setContextLimit(4096);
    QCOMPARE(agent.messages().size(), beforeSwitch);

    agent.stop();
    blocking->release();
    QTest::qWait(300);
    agent.setMemory(nullptr);
}

// docs/16 §13 AC-2/AC-4: AIAgent 采集埋点 —— response 携带分类 token 时
// recordTokenUsage 正确采集并持久化到 token_usage 表，来源拆分总和 = 输入合计。
// recordTokenUsage/checkQuota 是 private 方法，通过 startTask 驱动 FakeAIService
// 发射 responseReady 间接触发 onLLMResponse → recordTokenUsage。
void TestAgent::testRecordTokenUsage() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    LLMResponse resp = textResponse(QStringLiteral("final answer"));
    resp.promptTokens = 100;
    resp.completionTokens = 50;
    resp.cachedTokens = 10;
    llm.script.append(resp);

    QSignalSpy costSpy(&agent, &AIAgent::costChanged);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("hello"));
    QTRY_COMPARE(responseSpy.count(), 1);

    // AC-2: API 返回分类 token 时正确采集
    const QVariantMap stats = memory.tokenStatsDetailed(30);
    QCOMPARE(stats.value("totalCalls").toInt(), 1);
    QCOMPARE(stats.value("totalInputTokens").toLongLong(), qint64(100));
    QCOMPARE(stats.value("totalOutputTokens").toLongLong(), qint64(50));
    QCOMPARE(stats.value("totalCacheHitTokens").toLongLong(), qint64(10));

    // AC-4: 来源拆分总和 = 输入合计
    const qint64 sourceSum =
          stats.value("totalInputSystemPrompt").toLongLong()
        + stats.value("totalInputConversation").toLongLong()
        + stats.value("totalInputToolCall").toLongLong()
        + stats.value("totalInputMcp").toLongLong()
        + stats.value("totalInputSkill").toLongLong()
        + stats.value("totalInputImage").toLongLong()
        + stats.value("totalInputOther").toLongLong();
    QCOMPARE(sourceSum, stats.value("totalInputTokens").toLongLong());

    // AC-5: costChanged 触发（fake-model 未知定价 → 0 分，docs/16 §6.2）
    QCOMPARE(costSpy.count(), 1);
    QCOMPARE(costSpy.at(0).at(0).toLongLong(), qint64(0));

    agent.setMemory(nullptr);
}

// docs/16 §5.1 统一口径：cacheHitTokens 是 inputTokens 的子集。Anthropic 的
// input_tokens 不含缓存类 token，cache_creation/cache_read 必须显式补入
// input（进而进 total/会话累计/计费基数），否则总量偏小、命中率可能 >100%。
void TestAgent::testAnthropicCacheReadCountedInInput() {
    agent.resetSession();
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    LLMResponse resp = textResponse(QStringLiteral("anthropic answer"));
    resp.promptTokens = 200;          // Anthropic input_tokens（不含缓存类）
    resp.completionTokens = 20;
    resp.cacheReadTokens = 80;        // cache_read_input_tokens
    resp.cacheCreationTokens = 30;    // cache_creation_input_tokens
    llm.reset();
    llm.script.append(resp);

    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    QSignalSpy sessionSpy(&agent, &AIAgent::sessionTokensChanged);
    QVERIFY(agent.startTask(QStringLiteral("cache accounting")));
    QTRY_COMPARE(responseSpy.count(), 1);

    const QVariantMap stats = memory.tokenStatsDetailed(30);
    // input = 200(prompt) + 30(creation) + 80(read) = 310
    QCOMPARE(stats.value("totalInputTokens").toLongLong(), qint64(310));
    // total = input + output = 310 + 20
    QCOMPARE(stats.value("totalTokens").toLongLong(), qint64(330));
    // cacheHit = cache_read（Anthropic 无 cached_tokens）；是 input 的子集
    QCOMPARE(stats.value("totalCacheHitTokens").toLongLong(), qint64(80));
    const double hitRate = stats.value("cacheHitRate").toDouble();
    QVERIFY(hitRate > 0.0);
    QVERIFY(hitRate <= 1.0);

    // 会话累计与逐行 input+output 同口径
    QCOMPARE(agent.sessionTokensUsed(), qint64(330));
    QCOMPARE(sessionSpy.last().at(0).toLongLong(), qint64(330));

    // 来源拆分总和 == 输入合计（cacheRead 落在 inputOther 残差，不被重复计）
    const qint64 sourceSum =
          stats.value("totalInputSystemPrompt").toLongLong()
        + stats.value("totalInputConversation").toLongLong()
        + stats.value("totalInputToolCall").toLongLong()
        + stats.value("totalInputMcp").toLongLong()
        + stats.value("totalInputSkill").toLongLong()
        + stats.value("totalInputImage").toLongLong()
        + stats.value("totalInputOther").toLongLong();
    QCOMPARE(sourceSum, qint64(310));

    agent.setMemory(nullptr);
}

// A5：TokenEstimator::estimateTokens(msg) 已含 images.size()*1000，来源拆分
// 必须把这部分从 conversation 扣除、只归 inputImage；否则无 usage 的回退
// 路径上来源之和会比 inputTokens 多出一整份图片 token（UI 占比 >100%）。
void TestAgent::testImageTokensNotDoubleCounted() {
    agent.resetSession();
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    llm.reset();
    // 无 usage → recordTokenUsage 走 estimate(messages)+schema 的回退口径，
    // 来源之和必须与 inputTokens 严格相等。
    llm.script.append(textResponse(QStringLiteral("image answer")));

    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    QVERIFY(agent.startTask(QStringLiteral("describe the image"),
                            QStringList{QStringLiteral(
                                "data:image/png;base64,iVBORw0KGgo=")}));
    QTRY_COMPARE(responseSpy.count(), 1);

    const QVariantMap stats = memory.tokenStatsDetailed(30);
    const qint64 input = stats.value("totalInputTokens").toLongLong();
    const qint64 image = stats.value("totalInputImage").toLongLong();
    QVERIFY(image > 0);   // 图片 token 归入 image 桶

    const qint64 sourceSum =
          stats.value("totalInputSystemPrompt").toLongLong()
        + stats.value("totalInputConversation").toLongLong()
        + stats.value("totalInputToolCall").toLongLong()
        + stats.value("totalInputMcp").toLongLong()
        + stats.value("totalInputSkill").toLongLong()
        + image
        + stats.value("totalInputOther").toLongLong();
    // 旧口径会把图片同时算进 conversation 与 inputImage，这里会多出 image。
    QCOMPARE(sourceSum, input);
    QVERIFY(stats.value("totalInputConversation").toLongLong() < input);

    // 会话累计仍等于逐行 input+output
    QCOMPARE(agent.sessionTokensUsed(),
             input + stats.value("totalOutputTokens").toLongLong());

    agent.setMemory(nullptr);
}

// B3：Gemini 把 thoughts 计入 tokensUsed 但不进 completionTokens。总量以
// tokensUsed 为准，把差额补进 output，保证 input+output == total。
void TestAgent::mainUsageTotalIncludesThoughts() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    llm.reset();
    LLMResponse resp = textResponse(QStringLiteral("thoughtful answer"));
    resp.promptTokens = 100;
    resp.completionTokens = 20;
    resp.tokensUsed = 150;   // 30 个 thoughts token
    llm.script.append(resp);

    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    QVERIFY(agent.startTask(QStringLiteral("think harder")));
    QTRY_COMPARE(responseSpy.count(), 1);

    const QVariantMap stats = memory.tokenStatsDetailed(30);
    QCOMPARE(stats.value("totalInputTokens").toLongLong(), qint64(100));
    QCOMPARE(stats.value("totalOutputTokens").toLongLong(), qint64(50));
    QCOMPARE(stats.value("totalTokens").toLongLong(), qint64(150));
    QCOMPARE(agent.sessionTokensUsed(), qint64(150));

    agent.setMemory(nullptr);
}

// docs/16 §8.2: 会话累计 token —— 每次请求累加 input+output，多轮任务
// 持续增长；resetSession 清零；resumeSession 从 token_usage 恢复基数。
void TestAgent::testSessionTokensUsed() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QSignalSpy sessionTokensSpy(&agent, &AIAgent::sessionTokensChanged);
    QSignalSpy tokensSpy(&agent, &AIAgent::tokensChanged);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);

    // 第一轮：promptTokens=100 / completion=50 → 累计 150
    LLMResponse first = textResponse(QStringLiteral("answer one"));
    first.promptTokens = 100;
    first.completionTokens = 50;
    llm.script.append(first);
    agent.startTask(QStringLiteral("hello"));
    QTRY_COMPARE(responseSpy.count(), 1);
    QCOMPARE(agent.sessionTokensUsed(), qint64(150));
    QVERIFY(sessionTokensSpy.count() >= 1);
    QCOMPARE(sessionTokensSpy.last().at(0).toLongLong(), qint64(150));

    // 第二轮（同一会话）：累加 100+100+25 = 累计 375
    LLMResponse second = textResponse(QStringLiteral("answer two"));
    second.promptTokens = 200;
    second.completionTokens = 25;
    llm.script.append(second);
    agent.startTask(QStringLiteral("hello again"));
    QTRY_COMPARE(responseSpy.count(), 2);
    QCOMPARE(agent.sessionTokensUsed(), qint64(375));

    const QString sessionId = agent.currentSessionId();
    QVERIFY(!sessionId.isEmpty());

    // resetSession → 清零
    agent.resetSession();
    QCOMPARE(agent.sessionTokensUsed(), qint64(0));
    QCOMPARE(sessionTokensSpy.last().at(0).toLongLong(), qint64(0));
    // A3: 清空后窗口占用必须重算并广播（空历史 + tools schema），
    // TopBar 不会残留旧会话的占用。
    QVERIFY(tokensSpy.count() >= 1);
    QCOMPARE(tokensSpy.last().at(0).toInt(),
             static_cast<int>(agent.contextComposition()
                                  .value("totalTokens").toLongLong()));

    // resumeSession → 从 DB 恢复累计基数（375 = 两轮 input+output 之和）
    agent.resumeSession(sessionId);
    QCOMPARE(agent.sessionTokensUsed(), qint64(375));
    QCOMPARE(sessionTokensSpy.last().at(0).toLongLong(), qint64(375));

    agent.setMemory(nullptr);
}

// docs/16 §3.3: 估算器滚动校准 —— 响应携带 API 真实 promptTokens 时，
// recordTokenUsage 以 promptTokens / 原始估算（消息 + tools schema）观测
// 校准系数（EWMA α=0.3，钳制 [0.5, 3.0]）；notifyTokens / contextComposition
// 的窗口占用乘该系数。无 usage 的响应不更新系数（保持 1.0 基线）。
// recordTokenUsage 为 private，经 startTask → onLLMResponse 间接触发。
void TestAgent::testEstimateScaleCalibration() {
    QSignalSpy tokensSpy(&agent, &AIAgent::tokensChanged);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);

    // ── 基线：无 usage 的响应，估算保持原始公式（scale = 1.0）──
    llm.script.append(textResponse(QStringLiteral("no usage here")));
    agent.startTask(QStringLiteral("你好，请回答"));
    QTRY_COMPARE(responseSpy.count(), 1);
    const int baseline = tokensSpy.last().at(0).toInt();
    QVERIFY(baseline > 0);
    // 无观测时 notifyTokens 与 contextComposition().totalTokens 同为原始口径。
    const QVariantMap baseComp = agent.contextComposition();
    QCOMPARE(baseComp.value("totalTokens").toLongLong(),
             static_cast<qint64>(baseline));
// 消息段 = 总占用 - tools schema 预留（scale=1 时即原始预留）。
    const int rawBefore = baseline
        - static_cast<int>(baseComp.value("toolsSchema").toLongLong());
    QVERIFY(rawBefore > 0);

    // ── 校准生效：API 报告的 promptTokens = 2× 原始消息估算 → 系数 2.0
    // （在 [0.5, 3.0] 钳制内）。下一次 tokensChanged 的消息与 schema 都按
    // 同一系数放大，但 scale<2 且新增用户消息使总占用仍小于 2×基线。
    LLMResponse cal = textResponse(QStringLiteral("calibrated"));
    cal.promptTokens = rawBefore * 2;
    cal.completionTokens = 10;
    llm.script.append(cal);
    agent.startTask(QStringLiteral("第二轮"));
    QTRY_COMPARE(responseSpy.count(), 2);
    const int scaled = tokensSpy.last().at(0).toInt();
    QVERIFY(scaled > baseline);         // 系数生效，总占用变大
    QVERIFY(scaled < baseline * 2 + 1); // 系数 < 2×，整体占用仍小于 2×基线

    // contextComposition 与 notifyTokens 同口径（各桶独立取整，允许 ≤4 的
    // 截断差）；校准只重分配桶，不破坏“桶之和 == usedTokens”的合同。
    const QVariantMap comp = agent.contextComposition();
    QVERIFY(qAbs(comp.value("totalTokens").toLongLong()
                 - static_cast<qint64>(scaled)) <= 4);
    QCOMPARE(comp.value("systemPrompt").toLongLong()
           + comp.value("conversation").toLongLong()
           + comp.value("toolResults").toLongLong()
           + comp.value("images").toLongLong(),
              comp.value("usedTokens").toLongLong());
    QCOMPARE(comp.value("usedTokens").toLongLong()
             + comp.value("toolsSchema").toLongLong(),
              comp.value("totalTokens").toLongLong());
    // resetSession 清零系数，新会话回到基线口径（重新取一份 composition 的
    // toolsSchema 才是未校准值）。
    agent.resetSession();
    const QVariantMap resetComp = agent.contextComposition();
    QCOMPARE(static_cast<int>(resetComp.value("totalTokens").toLongLong()),
             static_cast<int>(TokenEstimator::estimateTokens(agent.messages())
                              + resetComp.value("toolsSchema").toLongLong()));
}

// A10: EWMA 分母必须取实际发出的 wire 请求（files 折叠成文本、控制消息
// 追加等），而不是持久化消息数组的估算。首轮按 wire 估算的 2 倍上报
// promptTokens，第二轮的显示值必须精确按 2.0 放大；旧口径（消息数组 +
// schema，不含折叠包装）会得到 >2 的系数。
void TestAgent::tokenEstimateScaleUsesWireRequest() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    struct RestoreMemory {
        AIAgent& agent;
        ~RestoreMemory() { agent.setMemory(nullptr); }
    } restoreMemory{agent};

    WireScalingFakeService service;
    struct RestoreService {
        AIAgent& agent;
        FakeAIService* original;
        ~RestoreService() { agent.setLLMService(original); }
    } restoreService{agent, &llm};
    agent.setLLMService(&service);
    service.script.append(textResponse(QStringLiteral("first")));
    service.script.append(textResponse(QStringLiteral("second")));

    QSignalSpy tokensSpy(&agent, &AIAgent::tokensChanged);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);

    QJsonArray files;
    files.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("big.txt")},
                             {QStringLiteral("content"), QString(4000, QChar('f'))}});
    QVERIFY(agent.startTask(QStringLiteral("with attachment"), {}, files));
    QTRY_COMPARE(responseSpy.count(), 1);

    QVERIFY(agent.startTask(QStringLiteral("second")));
    QTRY_COMPARE(responseSpy.count(), 2);

    QCOMPARE(service.seenMessages.size(), 2);
    const int rawSchema =
        TokenEstimator::estimateToolsSchema(service.seenTools.last());
    const int rawMessages = TokenEstimator::estimateTokens(agent.messages());
    const int expected = (rawMessages + rawSchema) * 2;
    QCOMPARE(tokensSpy.last().at(0).toInt(), expected);

    agent.setMemory(nullptr);
}

// A2：校准系数只作用于消息、schema 保持原始，会让 EWMA 观测（分母含
// schema）与显示/预算（不含 schema 缩放）产生 rawSchema×(scale-1) 的偏差。
// 本用例注册一个带 schema 的 tool，用超大 promptTokens 把系数顶到 3.0，
// 验证 toolsSchema / totalTokens 与 notifyTokens 一起放大且口径一致。
void TestAgent::testEstimateScaleAppliedToToolSchema() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    if (!ToolRegistry::instance().isRegistered(QStringLiteral("test_readonly")))
        ToolRegistry::instance().registerTool(std::make_unique<ReadOnlyProbeTool>());
    NamedToolGuard guard{QStringLiteral("test_readonly")};

    // scale==1 基线：空历史的 toolsSchema 即原始预留。
    const int rawSchema = agent.contextComposition()
                              .value("toolsSchema").toInt();
    QVERIFY(rawSchema > 0);

    QSignalSpy tokensSpy(&agent, &AIAgent::tokensChanged);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);

    LLMResponse cal = textResponse(QStringLiteral("huge usage"));
    cal.promptTokens = 100000;   // 首次观测即触顶 3.0
    cal.completionTokens = 10;
    llm.script.append(cal);
    QVERIFY(agent.startTask(QStringLiteral("校准 schema")));
    QTRY_COMPARE(responseSpy.count(), 1);

    const QVariantMap comp = agent.contextComposition();
    const qint64 scaledSchema = comp.value("toolsSchema").toLongLong();
    // 首次 EWMA 直接取钳制上限 3.0，schema 也必须同步放大。
    QVERIFY2(scaledSchema >= static_cast<qint64>(rawSchema) * 2,
             "tools schema must scale with the EWMA coefficient");
    QCOMPARE(comp.value("usedTokens").toLongLong() + scaledSchema,
             comp.value("totalTokens").toLongLong());
    // 各桶独立取整，notifyTokens 整体取整，二者最大差 4。
    QVERIFY2(qAbs(tokensSpy.last().at(0).toLongLong()
                  - comp.value("totalTokens").toLongLong()) <= 4,
             "notifyTokens and contextComposition must share the scaled口径");

    agent.setMemory(nullptr);
}

// A4：不同模型 tokenizer/分词口径不同，切换模型必须让旧系数失效；
// configure 每次新建 service 但模型不变的“同模型重连”则必须保留观测。
void TestAgent::testEstimateScaleResetOnModelSwitch() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // 空历史 toolsSchema 作为未校准预留基线（无注册工具时为 0，不影响断言）。
    const int rawSchema = agent.contextComposition()
                              .value("toolsSchema").toInt();

    QSignalSpy tokensSpy(&agent, &AIAgent::tokensChanged);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);

    // ── 先构造 scale > 1（首次观测顶到 3.0）──
    LLMResponse first = textResponse(QStringLiteral("first"));
    first.promptTokens = 100000;
    first.completionTokens = 10;
    llm.script.append(first);
    QVERIFY(agent.startTask(QStringLiteral("校准")));
    QTRY_COMPARE(responseSpy.count(), 1);
    const int calibratedRaw = TokenEstimator::estimateTokens(agent.messages())
                            + rawSchema;
    QVERIFY(tokensSpy.last().at(0).toInt() >= calibratedRaw * 2);

    // ── 同模型重连：model 名不变 → 系数保留，下一轮仍按 3× 放大 ──
    otherLlm.modelName = QStringLiteral("fake-model");
    otherLlm.reset();
    otherLlm.script.append(textResponse(QStringLiteral("same model")));
    agent.setLLMService(&otherLlm);
    QVERIFY(agent.startTask(QStringLiteral("同模型重连")));
    QTRY_COMPARE(responseSpy.count(), 2);
    const int sameRaw = TokenEstimator::estimateTokens(agent.messages())
                      + rawSchema;
    QCOMPARE(tokensSpy.last().at(0).toInt(),
             static_cast<int>(sameRaw * 3.0));

    // ── 换模型：生效模型从 fake-model 换成 other-model → 系数复位 ──
    // 注意模型名必须在 startTask 的请求真正生效（生效模型才是观测基准），
    // 仅改一个从未使用的实例是旧用例的场景错误。
    otherLlm.modelName = QStringLiteral("other-model");
    agent.setLLMService(&otherLlm);
    otherLlm.reset();
    otherLlm.script.append(textResponse(QStringLiteral("different model")));
    QVERIFY(agent.startTask(QStringLiteral("换模型")));
    QTRY_COMPARE(responseSpy.count(), 3);
    const int resetRaw = TokenEstimator::estimateTokens(agent.messages())
                       + rawSchema;
    // 下一轮 tokensChanged 回落到未校准量级（scale 已复位 1.0）。
    QCOMPARE(tokensSpy.last().at(0).toInt(), resetRaw);

    otherLlm.modelName = QStringLiteral("fake-model");
    agent.setMemory(nullptr);
}

// A4（Major）：main.cpp 按 apiProtocol 复用同一 service 实例，configure()
// 原地改写 model() 后 setLLMService(同一指针) 走早退分支——旧实现直接
// return，旧模型观测出的系数跨模型残留。另覆盖跨 setLLMService(nullptr)
// 换到新 service 时观测基准不丢失。
void TestAgent::estimateScaleResetOnSameServiceModelChange() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    const int rawSchema = agent.contextComposition()
                              .value("toolsSchema").toInt();
    QSignalSpy tokensSpy(&agent, &AIAgent::tokensChanged);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);

    llm.reset();
    llm.modelName = QStringLiteral("fake-model");
    // ── 第一轮：真实 promptTokens 远大于估算 → 首次观测即顶到 3.0 ──
    LLMResponse first = textResponse(QStringLiteral("first"));
    first.promptTokens = 100000;
    first.completionTokens = 10;
    llm.script.append(first);
    QVERIFY(agent.startTask(QStringLiteral("校准")));
    QTRY_COMPARE(responseSpy.count(), 1);
    const int calibratedRaw = TokenEstimator::estimateTokens(agent.messages())
                            + rawSchema;
    QVERIFY(tokensSpy.last().at(0).toInt() >= calibratedRaw * 2);

    // ── 同一指针只改 modelName（等效 configure 原地换模型）──
    // setLLMService 命中指针相等的早退分支，必须比较 m_scaleModelName。
    llm.modelName = QStringLiteral("other-model");
    agent.setLLMService(&llm);
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("same instance")));
    QVERIFY(agent.startTask(QStringLiteral("同实例换模型")));
    QTRY_COMPARE(responseSpy.count(), 2);
    const int resetRaw = TokenEstimator::estimateTokens(agent.messages())
                       + rawSchema;
    // 下一轮无 usage，tokensChanged 必须回落到未校准量级（系数已清零）。
    QCOMPARE(tokensSpy.last().at(0).toInt(), resetRaw);

    // ── 再次构造高系数，然后跨 nullptr 换到第三个模型 ──
    llm.reset();
    LLMResponse again = textResponse(QStringLiteral("recalibrate"));
    again.promptTokens = 100000;
    again.completionTokens = 10;
    llm.script.append(again);
    QVERIFY(agent.startTask(QStringLiteral("重新校准")));
    QTRY_COMPARE(responseSpy.count(), 3);
    QVERIFY(tokensSpy.last().at(0).toInt() >= resetRaw * 2);

    FakeAIService third;
    third.modelName = QStringLiteral("third-model");
    agent.setLLMService(nullptr);   // 基准不清空
    agent.setLLMService(&third);    // 与基准不同 → 清零
    third.script.append(textResponse(QStringLiteral("third")));
    QVERIFY(agent.startTask(QStringLiteral("跨空换模型")));
    QTRY_COMPARE(responseSpy.count(), 4);
    const int thirdRaw = TokenEstimator::estimateTokens(agent.messages())
                       + rawSchema;
    QCOMPARE(tokensSpy.last().at(0).toInt(), thirdRaw);

    agent.setLLMService(&llm);   // 恢复 fixture 默认服务
    llm.modelName = QStringLiteral("fake-model");
    agent.setMemory(nullptr);
}

// B1（Major）：provider 每会话可能复用同一 call id。runKey 必须带
// sessionId，resetSession 也必须清空去重集合；否则第二个会话的同 id
// 同步子代理会被当成“已入账”漏记。
void TestAgent::subagentRunKeySeparatesSessions() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse r = textResponse(QStringLiteral("dup report"));
        r.promptTokens = 20;
        r.completionTokens = 5;
        r.tokensUsed = 25;
        sub.script.append(r);
    });

    const auto runOnce = [&](const QString& prompt) -> QString {
        llm.reset();
        LLMResponse spawn = toolResponse(QStringLiteral("spawn_agent"),
            QJsonObject{{QStringLiteral("goal"), QStringLiteral("dup goal")}});
        spawn.toolCalls[0].id = QStringLiteral("call_dup");
        spawn.promptTokens = 10;
        spawn.completionTokens = 1;
        spawn.tokensUsed = 11;
        llm.script.append(spawn);
        llm.script.append(textResponse(QStringLiteral("done")));
        QSignalSpy spy(&agent, &AIAgent::responseReady);
        if (!agent.startTask(prompt)) return QString();
        QElapsedTimer timer;
        timer.start();
        while (spy.count() < 1 && timer.elapsed() < 20000)
            QTest::qWait(50);
        return spy.count() >= 1 ? agent.currentSessionId() : QString();
    };

    const QString firstSession = runOnce(QStringLiteral("first session"));
    QVERIFY(!firstSession.isEmpty());
    QCOMPARE(subagentTokenRows(memory, firstSession).size(), 1);
    QVERIFY(agent.sessionTokensUsed() > 0);

    agent.resetSession();
    QCOMPARE(agent.sessionTokensUsed(), qint64(0));

    const QString secondSession = runOnce(QStringLiteral("second session"));
    QVERIFY(!secondSession.isEmpty());
    QVERIFY(secondSession != firstSession);
    QCOMPARE(subagentTokenRows(memory, secondSession).size(), 1);
    QVERIFY(agent.sessionTokensUsed() > 0);

    // 跨会话总计 2 条子代理行（旧实现 runKey="sync:call_dup" + 集合不清，
    // 第二会话会被去重漏记，只剩 1 条）。
    QSqlQuery q(memory.databaseForTest());
    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM token_usage WHERE request_id = 0")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);

    agent.setMemory(nullptr);
}

// docs/16 §13 AC-7: 配额检查 —— 超限时 emit quotaWarning，strict 模式拒绝请求，
// 非 strict 模式仅警告后任务继续。checkQuota 在 startTask 开头同步调用。
void TestAgent::testCheckQuota() {
    // checkQuota 用 QSettings() 默认构造读取配额上限，需对齐组织/应用名。
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    QSettings settings;
    // 清理可能残留的配额键，确保起点干净
    settings.remove("dailyTokenQuota");
    settings.remove("dailyCostQuotaCents");
    settings.remove("monthlyTokenQuota");
    settings.remove("monthlyCostQuotaCents");
    settings.remove("strictQuotaMode");

    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // 预置当日已消耗 1000 token（超额）
    QVariantMap rec;
    rec["requestId"] = quint64(1);
    rec["sessionId"] = QStringLiteral("s1");
    rec["model"] = QStringLiteral("gpt-4o");
    rec["inputTokens"] = 600;
    rec["outputTokens"] = 400;
    memory.logTokenUsage(rec);

    // strict 模式 + 日 token 配额 10（已消耗 1000 超额）
    settings.setValue("dailyTokenQuota", 10);
    settings.setValue("strictQuotaMode", true);

    llm.script.append(textResponse(QStringLiteral("ok")));
    QSignalSpy quotaSpy(&agent, &AIAgent::quotaWarning);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("hi"));
    QTest::qWait(100);

    // AC-7: 超限触发警告
    QVERIFY(quotaSpy.count() >= 1);
    QCOMPARE(quotaSpy.at(0).at(0).toString(), QStringLiteral("dailyToken"));
    QVERIFY(quotaSpy.at(0).at(1).toLongLong() >= quotaSpy.at(0).at(2).toLongLong());
    // strict 模式拒绝请求：LLM 未被调用，无 responseReady
    QCOMPARE(responseSpy.count(), 0);

    // 切换非 strict 模式：仅警告，任务继续完成
    settings.setValue("strictQuotaMode", false);
    quotaSpy.clear();
    responseSpy.clear();
    agent.resetSession();
    agent.startTask(QStringLiteral("hi again"));
    QTRY_COMPARE(responseSpy.count(), 1);
    QVERIFY(quotaSpy.count() >= 1);

    // 清理配额设置避免泄漏
    settings.remove("dailyTokenQuota");
    settings.remove("dailyCostQuotaCents");
    settings.remove("monthlyTokenQuota");
    settings.remove("monthlyCostQuotaCents");
    settings.remove("strictQuotaMode");
    agent.setMemory(nullptr);
}

// docs/31 S4.1 N9：用量达到 alertPercent（默认 80）但**未超限**时提醒一次；
// 同一天同一类型不重复；0 = 关闭；超限仍由 quotaWarning 负责，不得混报。
void TestAgent::testQuotaAlertThreshold() {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    QSettings settings;
    settings.remove("dailyTokenQuota");
    settings.remove("dailyCostQuotaCents");
    settings.remove("monthlyTokenQuota");
    settings.remove("monthlyCostQuotaCents");
    settings.remove("strictQuotaMode");
    settings.setValue("usage/alertPercent", 80);

    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // 已用 80000 / 上限 100000：正好 80%，整数边界必须命中。
    QVariantMap rec;
    rec["requestId"] = quint64(1);
    rec["sessionId"] = QStringLiteral("s1");
    rec["model"] = QStringLiteral("gpt-4o");
    rec["inputTokens"] = 50000;
    rec["outputTokens"] = 30000;
    memory.logTokenUsage(rec);

    settings.setValue("dailyTokenQuota", 100000);
    settings.setValue("strictQuotaMode", false);

    QSignalSpy alertSpy(&agent, &AIAgent::quotaAlert);
    QSignalSpy quotaSpy(&agent, &AIAgent::quotaWarning);
    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);

    llm.reset();
    llm.script.append(textResponse(QStringLiteral("ok")));
    agent.startTask(QStringLiteral("hi"));
    QTRY_COMPARE(responseSpy.count(), 1);

    QCOMPARE(alertSpy.count(), 1);
    QCOMPARE(alertSpy.at(0).at(0).toString(), QStringLiteral("dailyToken"));
    QCOMPARE(alertSpy.at(0).at(1).toLongLong(), qint64(80000));
    QCOMPARE(alertSpy.at(0).at(2).toLongLong(), qint64(100000));
    // 未超限：不能同时报"超限"。
    QCOMPARE(quotaSpy.count(), 0);

    // 同一天第二次请求：用量继续增长但仍未超限 → 去重后不再提醒。
    llm.script.append(textResponse(QStringLiteral("ok again")));
    agent.startTask(QStringLiteral("again"));
    QTRY_COMPARE(responseSpy.count(), 2);
    QCOMPARE(alertSpy.count(), 1);
    QCOMPARE(quotaSpy.count(), 0);

    // alertPercent = 0 关闭提醒：换一个没有去重状态的新 agent 验证。
    settings.setValue("usage/alertPercent", 0);
    AIAgent fresh;
    fresh.setLLMService(&llm);
    fresh.setToolRegistry(&ToolRegistry::instance());
    fresh.setContextLimit(8192);
    fresh.setMemory(&memory);
    QSignalSpy freshAlert(&fresh, &AIAgent::quotaAlert);
    QSignalSpy freshResponse(&fresh, &AIAgent::responseReady);
    llm.script.append(textResponse(QStringLiteral("ok")));
    fresh.startTask(QStringLiteral("hi"));
    QTRY_COMPARE(freshResponse.count(), 1);
    QCOMPARE(freshAlert.count(), 0);

    settings.remove("dailyTokenQuota");
    settings.remove("dailyCostQuotaCents");
    settings.remove("monthlyTokenQuota");
    settings.remove("monthlyCostQuotaCents");
    settings.remove("strictQuotaMode");
    settings.remove("usage/alertPercent");
    fresh.setMemory(nullptr);
    agent.setMemory(nullptr);
}

// docs/20 §3.1（FORK-701）：重新生成。丢弃最后一条 assistant 回答（含末轮
// 工具消息），从最后一条 user 消息重新发起请求；持久层同步删除回答，
// audit_log 不动。
void TestAgent::regenerateDropsLastAnswerAndResends() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // 第一轮：带工具调用的完整回合（tool → final）。
    llm.reset();
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("memo.txt");
    { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("payload"); f.close(); }
    llm.script.append(toolResponse("read_file", QJsonObject{{"path", path}}));
    llm.script.append(textResponse(QStringLiteral("first answer")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("first question"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QString sessionId = agent.currentSessionId();
    QVERIFY(!sessionId.isEmpty());
    const int requestsBefore = llm.seenMessages.size();

    // 第二轮：普通一问一答。
    llm.script.append(textResponse(QStringLiteral("second answer")));
    agent.startTask(QStringLiteral("second question"));
    QTRY_COMPARE(spy.count(), 2);

    // 重新生成：丢弃第二条回答，从第二条 user 消息重新发起。
    llm.script.append(textResponse(QStringLiteral("regenerated answer")));
    agent.regenerateLast();
    QTRY_COMPARE(spy.count(), 3);

    // 新请求的 history：末条回答被丢弃 —— 最后一轮请求应是
    // system + user1 + assistant(tool_calls) + tool + assistant1 + user2
    //（第一轮带工具回合；不含 old assistant2）。
    const QJsonArray sent = llm.seenMessages.last();
    QCOMPARE(sent.size(), 6);
    QCOMPARE(sent.at(1).toObject().value("content").toString(),
             QStringLiteral("first question"));
    QCOMPARE(sent.at(5).toObject().value("content").toString(),
             QStringLiteral("second question"));
    // 末条请求不含旧回答。
    for (const auto& entry : sent) {
        const QJsonObject msg = entry.toObject();
        QVERIFY2(msg.value("role").toString() != QStringLiteral("assistant")
                 || msg.value("content").toString()
                    != QStringLiteral("second answer"),
                 "old answer survived regeneration");
    }
    QVERIFY2(noDanglingToolCalls(sent), "regenerated history illegal");

    // 持久层：重新生成完成后重新收口 —— user1, assistant1, user2, 新回答。
    //（旧 assistant2 被删，新回答由本轮 finishTask 保存。）
    const QJsonArray persisted = memory.loadMessages(sessionId, 200);
    QCOMPARE(persisted.size(), 4);
    QCOMPARE(persisted.at(2).toObject().value("content").toString(),
             QStringLiteral("second question"));
    QCOMPARE(persisted.at(3).toObject().value("content").toString(),
             QStringLiteral("regenerated answer"));

    // trace 展示层：最后一个 user_message 节点保留，回答节点被本轮重新写入。
    const QJsonArray trace = agent.traceModel();
    int lastUserIdx = -1;
    for (int i = trace.size() - 1; i >= 0; --i) {
        if (trace.at(i).toObject().value("type").toString() == "user_message") {
            lastUserIdx = i;
            break;
        }
    }
    QVERIFY(lastUserIdx >= 0);
    QCOMPARE(trace.at(lastUserIdx).toObject().value("title").toString(),
             QStringLiteral("second question"));

    Q_UNUSED(requestsBefore);
    agent.setMemory(nullptr);
}

// docs/20 §3.1：重新生成的守卫 —— 无历史 / busy / 无管线都是 no-op。
void TestAgent::regenerateWithoutHistoryOrBusyIsNoop() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    llm.reset();

    // 无历史（fresh）：不发起请求。
    agent.regenerateLast();
    QTest::qWait(50);
    QCOMPARE(llm.seenMessages.size(), 0);

    // busy 中重生成：拒绝。test_blocking 工具在 worker 线程真实执行
    //（停在信号量上），任务保持活跃，regenerateLast 被拒后任务照常完成。
    BlockingTool* blocking = registerBlockingTool();
    llm.script.append(toolResponse("test_blocking", QJsonObject{}));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("busy q"));
    QTRY_VERIFY_WITH_TIMEOUT(traceHasRunningToolCall(agent.traceModel(),
                                                    QStringLiteral("test_blocking")),
                             5000);
    const int requests = llm.seenMessages.size();
    agent.regenerateLast();   // busy：应被拒绝
    QTest::qWait(100);
    QCOMPARE(llm.seenMessages.size(), requests);
    QCOMPARE(agent.state(), AgentState::ExecutingTool);   // 任务未被中断

    // 放行工具，原任务照常完成。
    blocking->release();
    llm.script.append(textResponse(QStringLiteral("done")));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

    // 完成后重新生成：合法路径 —— 丢弃回答并重发。
    llm.script.append(textResponse(QStringLiteral("regenerated")));
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.regenerateLast();
    QTRY_COMPARE(spy2.count(), 1);
    // 新请求不含旧回答与 stop 残留（干净对白，末条是 user 消息）。
    const QJsonArray sent = llm.seenMessages.last();
    QCOMPARE(sent.at(sent.size() - 1).toObject().value("role").toString(),
             QStringLiteral("user"));
    QCOMPARE(sent.at(sent.size() - 1).toObject().value("content").toString(),
             QStringLiteral("busy q"));
    QVERIFY2(noDanglingToolCalls(sent), "regenerated history illegal");

    agent.setMemory(nullptr);
}

// docs/25 B3: with an episodic service attached, a fresh-session startTask
// resolves the retrieval off the GUI thread (embedder on a QtConcurrent
// worker) and continues the launch via retrievalReady. The user message is
// appended/persisted/traced BEFORE the async launch (visible immediately);
// the system message — carrying the injected summary — is prepended by the
// continuation.
void TestAgent::taskWithEpisodicAsyncLaunch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteMemory memory(dir.filePath(QStringLiteral("episodic_launch.db")));

    // Seed one retrievable episode. searchEpisodic matches the query as an
    // FTS5 phrase (contiguous substring), so the user message must BE a
    // substring of the summary for the FTS channel to hit it.
    QJsonObject episode;
    episode["summary"] = QStringLiteral("episodic anchor: remembered zeta fact");
    episode["tags"] = QStringLiteral("t");
    memory.saveEpisodic(QStringLiteral("old-session"), episode);

    EpisodicService episodic;
    episodic.setMemory(&memory);
    episodic.setEnabled(true);
    episodic.setEmbedder([](const QString&) { return QVector<float>{1.0f, 0.0f}; });
    agent.setEpisodicService(&episodic);
    agent.setMemory(&memory);

    llm.script.append(textResponse(QStringLiteral("done with memory")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    QVERIFY(agent.startTask(QStringLiteral("episodic anchor")));

    // The user message is visible while the async launch is still pending.
    QVERIFY(traceHas(agent.traceModel(), "user_message", "episodic anchor"));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QCOMPARE(agent.state(), AgentState::Completed);
    QCOMPARE(llm.seenMessages.size(), 1);
    // System message prepended at launch time (docs/25 B3), user second.
    const QJsonArray first = llm.seenMessages.at(0);
    QCOMPARE(first.at(0).toObject().value("role").toString(), QStringLiteral("system"));
    QCOMPARE(first.at(1).toObject().value("role").toString(), QStringLiteral("user"));
    // The episodic summary was injected into the system prompt.
    const QString sys = first.at(0).toObject().value("content").toString();
    QVERIFY2(sys.contains(QStringLiteral("remembered zeta fact")), qPrintable(sys));

    agent.setEpisodicService(nullptr);
    agent.setMemory(nullptr);
}

// docs/25 B3: a slow embedder is bounded by kEpisodicLaunchTimeoutMs — the
// task launches anyway once the deadline fires (without injection), and the
// late retrievalReady must NOT trigger a second launch: exactly one LLM
// request, one completion.
void TestAgent::taskWithSlowEmbedderStillLaunches() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteMemory memory(dir.filePath(QStringLiteral("episodic_slow.db")));

    EpisodicService episodic;
    episodic.setMemory(&memory);
    episodic.setEnabled(true);
    episodic.setEmbedder([](const QString&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));  // > 200ms deadline
        return QVector<float>{1.0f, 0.0f};
    });
    agent.setEpisodicService(&episodic);
    agent.setMemory(&memory);

    llm.script.append(textResponse(QStringLiteral("deadline answer")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    QVERIFY(agent.startTask(QStringLiteral("hello slow embedder")));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QCOMPARE(agent.state(), AgentState::Completed);
    QCOMPARE(llm.seenMessages.size(), 1);   // launched via the deadline path
    QVERIFY(!llm.seenMessages.at(0).at(0).toObject()
                 .value("content").toString().contains(QStringLiteral("历史会话摘要")));

    // The embedder lands AFTER the deadline: its late retrievalReady is
    // ignored (pending id cleared) — no second request, no state change.
    QTest::qWait(600);
    QCOMPARE(llm.seenMessages.size(), 1);
    QCOMPARE(agent.state(), AgentState::Completed);

    agent.setEpisodicService(nullptr);
    agent.setMemory(nullptr);
}

// 审查 M2（任务 #12）：VM 层 QVariantMap 键名契约。QML delegate 直接消费
// modelData.timestamp/category/action/target/requestId/detail；若 VM 转换层
// usageEventsToVariantList 的键名被误改（如套用同文件 episodesToVariantList
// 的 snake_case → camelCase 映射习惯），QML 会静默显示空白字段，而既有
// test_memory 用例只断言到 QJsonObject 层、不会红。本用例经 AgentViewModel
// 全链路（setMemory → listUsageEvents）对键集合、类型、排序与 limit 语义
// 做机器验证。
void TestAgent::listUsageEventsExposesFieldContract() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    memory.addUsageEvent(QStringLiteral("nexus.user"),
                         QStringLiteral("session.create"),
                         QStringLiteral("s1"),
                         quint64{7},
                         QStringLiteral("count=1"));
    memory.addUsageEvent(QStringLiteral("nexus.network"),
                         QStringLiteral("llm_result"),
                         QStringLiteral("https://api.example.com/v1"),
                         quint64{8},
                         QString());

    AgentViewModel vm(nullptr);
    vm.setMemory(&memory);

    const QVariantList events = vm.listUsageEvents(500);
    QCOMPARE(events.size(), 2);

    // 键集合精确断言：恰好六个键——多键、漏键、改名都红。
    const QSet<QString> expectedKeys{
        QStringLiteral("timestamp"), QStringLiteral("category"),
        QStringLiteral("action"),    QStringLiteral("target"),
        QStringLiteral("requestId"), QStringLiteral("detail"),
    };
    for (const QVariant& v : events) {
        const QVariantMap e = v.toMap();
        QCOMPARE(QSet<QString>(e.keyBegin(), e.keyEnd()), expectedKeys);
    }

    // 类型契约 + 最新在前（id DESC）。数值字段：timestamp 为 epoch ms、
    // requestId 为数值；字符串字段：category/action/target/detail。
    const QVariantMap newest = events.at(0).toMap();
    QCOMPARE(newest.value(QStringLiteral("category")).toString(),
             QStringLiteral("nexus.network"));
    QCOMPARE(newest.value(QStringLiteral("action")).toString(),
             QStringLiteral("llm_result"));
    QCOMPARE(newest.value(QStringLiteral("target")).toString(),
             QStringLiteral("https://api.example.com/v1"));
    QVERIFY(newest.value(QStringLiteral("detail")).toString().isEmpty());
    QVERIFY(newest.value(QStringLiteral("timestamp")).canConvert<qint64>());
    QVERIFY(newest.value(QStringLiteral("timestamp")).toLongLong() > 0);
    QCOMPARE(newest.value(QStringLiteral("requestId")).toDouble(), 8.0);

    const QVariantMap oldest = events.at(1).toMap();
    QCOMPARE(oldest.value(QStringLiteral("action")).toString(),
             QStringLiteral("session.create"));
    QCOMPARE(oldest.value(QStringLiteral("requestId")).toDouble(), 7.0);
    QCOMPARE(oldest.value(QStringLiteral("detail")).toString(),
             QStringLiteral("count=1"));

    // 排序：id DESC 下 timestamp 非升（同一毫秒连写允许相等）。
    QVERIFY(newest.value(QStringLiteral("timestamp")).toLongLong()
            >= oldest.value(QStringLiteral("timestamp")).toLongLong());

    // limit 语义：截断后保留最新的那一条。
    const QVariantList capped = vm.listUsageEvents(1);
    QCOMPARE(capped.size(), 1);
    QCOMPARE(capped.at(0).toMap().value(QStringLiteral("action")).toString(),
             QStringLiteral("llm_result"));
}

// ── docs/32 P1: 子代理（spawn_agent）───────────────────────────────────

// docs/43 O1-T4 + O4-4: the prompt carries the delegation policy only when
// spawn_agent is actually in the dispatched tool set (the same gate the tool
// schema serialization applies); a user override takes over the whole prompt.
// O4-4 之前该段无条件内联：禁用子代理的会话也在教模型委托（并引导它调用
// schema 里不存在的工具）。
void TestAgent::orchestrationPolicyInSystemPrompt() {
    agent.setSystemPromptOverride(QString());
    QVERIFY(!agent.effectiveSystemPrompt()
                 .contains(QStringLiteral("Delegation policy")));
    {
        SubAgentToolsetGuard guard;
        const QString prompt = agent.effectiveSystemPrompt();
        QVERIFY(prompt.contains(QStringLiteral("Delegation policy")));
        QVERIFY(prompt.contains(QStringLiteral("explore = survey")));
        // O4-4 四段式编排循环（Plan/Spawn/Collect/Synthesize）。
        QVERIFY(prompt.contains(QStringLiteral("- Plan:")));
        QVERIFY(prompt.contains(QStringLiteral("- Spawn:")));
        QVERIFY(prompt.contains(QStringLiteral("- Collect:")));
        QVERIFY(prompt.contains(QStringLiteral("- Synthesize:")));
        // docs/43 O3: 批量收割指引（wait-any/all）必须进决策层提示。
        QVERIFY(prompt.contains(QStringLiteral("mode=any|all")));

        agent.setSystemPromptOverride(QStringLiteral("You are a pirate."));
        QVERIFY(!agent.effectiveSystemPrompt()
                     .contains(QStringLiteral("Delegation policy")));
        agent.setSystemPromptOverride(QString());
    }
}

// docs/43 O1-T2: AGENTS.md content reaches the sub-agent's system message,
// not only the primary agent's prompt.
void TestAgent::subagentInheritsProjectRules() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    QFile rules(dir.path() + "/AGENTS.md");
    QVERIFY(rules.open(QIODevice::WriteOnly));
    rules.write("# Demo rules\n- use ninja\n");
    rules.close();
    agent.setActiveProject(QStringLiteral("p9"), QStringLiteral("Rules"),
                           dir.path());

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("rules followed")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("apply the rules")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn with rules"));
    QTRY_COMPARE(spy.count(), 1);

    QMutexLocker lock(&fixture.mutex);
    QVERIFY(!fixture.subSeenMessages.isEmpty());
    const QString system = fixture.subSeenMessages.first().first().toObject()
                               .value("content").toString();
    QVERIFY2(system.contains(QStringLiteral("Project rules")), qPrintable(system));
    QVERIFY(system.contains(QStringLiteral("use ninja")));
    lock.unlock();
    agent.setActiveProject(QString(), QString(), QString());
}

// docs/43 O1-T3: a transient first-attempt failure is retried once; the report
// echoes the attempt count and accumulates the cost of both runs.
void TestAgent::subagentTransientFailureRetriesOnce() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    int attempts = 0;
    fixture.tool = registerSpawnTool(fixture,
        [&attempts](RecordingFakeService& sub) {
            // The factory runs once per attempt: first fails, second answers.
            if (++attempts == 1) {
                LLMResponse failure;
                failure.success = false;
                failure.error = QStringLiteral("transient network error");
                sub.script.append(failure);
            } else {
                sub.script.append(textResponse(
                    QStringLiteral("recovered after retry")));
            }
        });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("survive a blip")}}));
    llm.script.append(textResponse(QStringLiteral("handled")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("retry once"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(attempts, 2);
    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY2(report.contains(QStringLiteral("[Sub-agent report]")),
             qPrintable(report));
    QVERIFY(report.contains(QStringLiteral("recovered after retry")));
    QVERIFY(report.contains(QStringLiteral("attempts: 2")));
}

// docs/43 O4 审计收口（P1-2）：自动重试的分类 usage 必须跨 attempt 累计，
// 与 steps/tokensUsed 同口径。旧行为只累计 tokensUsed，input/output 被
// 末次 attempt 覆盖 → input+output ≠ tokensUsed（tokensUsed 是两 attempt
// 总量），成本芯片与审计 subagent_usage 双通道失真。失败 attempt 内已
// 成功的步携带 usage（100/50），末次成功步 1200/300 → 累计 1300/350。
void TestAgent::subagentRetryAccumulatesClassifiedUsage() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    int attempts = 0;
    fixture.tool = registerSpawnTool(fixture,
        [&attempts](RecordingFakeService& sub) {
            if (++attempts == 1) {
                // attempt 1: step 1 成功（带分类 usage）→ step 2 瞬时失败。
                LLMResponse step =
                    toolResponse(QStringLiteral("test_sub_probe"), {});
                step.promptTokens = 100;
                step.completionTokens = 50;
                sub.script.append(step);
                LLMResponse failure;
                failure.success = false;
                failure.error = QStringLiteral("transient network error");
                sub.script.append(failure);
            } else {
                LLMResponse ok =
                    textResponse(QStringLiteral("usage recovered"));
                ok.promptTokens = 1200;
                ok.completionTokens = 300;
                sub.script.append(ok);
            }
        });

    const ToolResult result = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("usage across attempts")}});
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(attempts, 2);
    QVERIFY(result.output.contains(QStringLiteral("attempts: 2")));
    const QJsonObject report = result.metadata
        .value(QStringLiteral("subagentReport")).toObject();
    QVERIFY2(!report.isEmpty(), "structured report missing from metadata");
    const QJsonObject usage = report.value(QStringLiteral("usage")).toObject();
    QCOMPARE(usage.value(QStringLiteral("input")).toInt(), 1300);
    QCOMPARE(usage.value(QStringLiteral("output")).toInt(), 350);
    QCOMPARE(usage.value(QStringLiteral("reported")).toBool(), true);
}

// B2：取消/超时的步骤没有 response payload，Runner 若把跨 step 复用的
// `response` 无条件累加，会把上一步 usage 再计一遍（成本/统计翻倍）。
// 本用例直接驱动 Runner：第 1 步返回 tool_calls + usage(100/50/150)，
// 第 2 步挂起后分别由墙钟超时与取消旗标收场，断言计量只等于第 1 步。
void TestAgent::subagentCancelDoesNotDoubleCountUsage() {
    auto probe = std::make_shared<SubAgentProbeTool>();
    const auto scriptFirstStep = [](HangAfterFirstStepService& service) {
        LLMResponse step = toolResponse(QStringLiteral("test_sub_probe"), {});
        step.toolCalls[0].id = QStringLiteral("call_1");
        step.promptTokens = 100;
        step.completionTokens = 50;
        step.tokensUsed = 150;
        service.script.append(step);
    };

    // ── 超时收场 ──
    {
        HangAfterFirstStepService service;
        scriptFirstStep(service);
        SubAgentRequest request;
        request.goal = QStringLiteral("hang on the second step");
        request.maxSteps = 3;
        request.timeoutMs = 300;
        request.tools.append(SubAgentToolEntry{probe->definition(), probe});

        const SubAgentReport report = SubAgentRunner::run(&service, request);
        QCOMPARE(int(report.stop), int(SubAgentReport::Stop::TimedOut));
        QCOMPARE(report.stopCode, QStringLiteral("timeout"));
        // 旧实现：第 2 步没有 payload，仍复用第 1 步 response → 300/200/100。
        QCOMPARE(report.tokensUsed, 150);
        QCOMPARE(report.usage.inputTokens, 100);
        QCOMPARE(report.usage.outputTokens, 50);
        QCOMPARE(report.usage.cachedTokens, 0);
        QVERIFY(report.usage.usageReported);
    }

    // ── 取消旗标收场 ──
    {
        HangAfterFirstStepService service;
        scriptFirstStep(service);
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        QTimer::singleShot(150, nullptr,
                           [flag = cancelled.get()]() { flag->store(true); });
        SubAgentRequest request;
        request.goal = QStringLiteral("cancel while the second step hangs");
        request.maxSteps = 3;
        request.timeoutMs = 0;   // 只靠取消旗标收场
        request.cancelled = cancelled;
        request.tools.append(SubAgentToolEntry{probe->definition(), probe});

        const SubAgentReport report = SubAgentRunner::run(&service, request);
        QCOMPARE(int(report.stop), int(SubAgentReport::Stop::Cancelled));
        QCOMPARE(report.stopCode, QStringLiteral("cancelled"));
        QCOMPARE(report.tokensUsed, 150);
        QCOMPARE(report.usage.inputTokens, 100);
        QCOMPARE(report.usage.outputTokens, 50);
    }
}

// B2（Major，推测竞态）：第一步的迟到/重复 responseReady 不得被第二步
// 的等待循环捕获。修复前整轮共用一条信号连接，迟到 payload 会改写第二
// 步的 response 并让 loop.quit()——报告把迟到正文和 usage 当成本步结果。
void TestAgent::subagentStepSignalsAreIsolated() {
    auto probe = std::make_shared<SubAgentProbeTool>();
    LateDuplicateAfterFirstStepService service;

    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    // abortTimer 每 250ms 轮询：300ms 置位 → 500ms 检测到并收场。
    QTimer::singleShot(300, nullptr,
                       [flag = cancelled.get()]() { flag->store(true); });

    SubAgentRequest request;
    request.goal = QStringLiteral("late payload must not leak into step two");
    request.maxSteps = 3;
    request.timeoutMs = 0;   // 只靠取消旗标收场
    request.cancelled = cancelled;
    request.tools.append(SubAgentToolEntry{probe->definition(), probe});

    const SubAgentReport report = SubAgentRunner::run(&service, request);

    // 第二步没有被迟到 payload 提前“完成”，仍按取消收场。
    QCOMPARE(int(report.stop), int(SubAgentReport::Stop::Cancelled));
    QCOMPARE(report.stopCode, QStringLiteral("cancelled"));
    // 只有第一步的真实 usage；迟到 payload（1000）绝不能计入。
    QCOMPARE(report.tokensUsed, 150);
    QCOMPARE(report.usage.inputTokens, 100);
    QCOMPARE(report.usage.outputTokens, 50);
    QVERIFY(report.usage.usageReported);
    QVERIFY2(!report.text.contains(QStringLiteral("late duplicate")),
             "report text was replaced by the late payload");
}

// B3：Anthropic 缓存分类必须保留原值——cacheRead 并入展示用 cached，
// cacheCreation 单独保留；reportJson 同时暴露两者供入账归一化。
void TestAgent::subagentUsageKeepsCacheClassification() {
    FakeAIService fake;
    LLMResponse resp = textResponse(QStringLiteral("classified"));
    resp.promptTokens = 100;
    resp.completionTokens = 20;
    resp.cachedTokens = 7;          // OpenAI cached
    resp.cacheReadTokens = 30;      // Anthropic cache_read
    resp.cacheCreationTokens = 10;  // Anthropic cache_creation
    resp.tokensUsed = 160;
    fake.script.append(resp);

    SubAgentRequest request;
    request.goal = QStringLiteral("classify usage");
    const SubAgentReport report = SubAgentRunner::run(&fake, request);
    QVERIFY2(report.ok, qPrintable(report.text));
    QCOMPARE(report.usage.inputTokens, 100);
    QCOMPARE(report.usage.outputTokens, 20);
    QCOMPARE(report.usage.cachedTokens, 37);      // 7 + 30 展示合并值
    QCOMPARE(report.usage.cacheReadTokens, 30);
    QCOMPARE(report.usage.cacheCreationTokens, 10);
    QVERIFY(report.usage.usageReported);

    const QJsonObject usage =
        SubAgentTool::reportJson(report).value(QStringLiteral("usage")).toObject();
    QCOMPARE(usage.value(QStringLiteral("cached")).toInt(), 37);
    QCOMPARE(usage.value(QStringLiteral("cacheRead")).toInt(), 30);
    QCOMPARE(usage.value(QStringLiteral("cacheCreation")).toInt(), 10);
}

// B1 同步：spawn_agent 的同步报告在 finishToolExecution 入账。子代理行
// requestId=0（父请求不可靠），来源统一 inputToolCall/outputToolCall，
// 会话累计包含主 + 子。
void TestAgent::subagentUsageAccountedIntoSyncSession() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse r = textResponse(QStringLiteral("sync report"));
        r.promptTokens = 40;
        r.completionTokens = 10;
        r.cachedTokens = 2;
        r.cacheReadTokens = 5;
        r.cacheCreationTokens = 3;
        r.tokensUsed = 50;
        sub.script.append(r);
    });

    llm.reset();
    LLMResponse spawn = toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("sync accounting")}});
    spawn.toolCalls[0].id = QStringLiteral("call_spawn_sync");
    spawn.promptTokens = 100;
    spawn.completionTokens = 5;
    spawn.tokensUsed = 105;
    llm.script.append(spawn);
    LLMResponse finalResp = textResponse(QStringLiteral("done"));
    finalResp.promptTokens = 120;
    finalResp.completionTokens = 10;
    finalResp.tokensUsed = 130;
    llm.script.append(finalResp);

    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    QVERIFY(agent.startTask(QStringLiteral("spawn sync")));
    QTRY_COMPARE(responseSpy.count(), 1);

    const QString sessionId = agent.currentSessionId();
    QVERIFY(!sessionId.isEmpty());
    const QVariantList subRows = subagentTokenRows(memory, sessionId);
    QCOMPARE(subRows.size(), 1);
    const QVariantMap row = subRows.first().toMap();
    // input = 40 + cacheRead 5 + cacheCreation 3 = 48；tokensUsed(50) 不足以
    // 覆盖分类合计 → output 保持 10。
    QCOMPARE(row.value("input").toLongLong(), qint64(48));
    QCOMPARE(row.value("output").toLongLong(), qint64(10));
    QCOMPARE(row.value("cacheHit").toLongLong(), qint64(7));   // 2 + 5
    QCOMPARE(row.value("inputToolCall").toLongLong(), qint64(48));
    QCOMPARE(row.value("outputToolCall").toLongLong(), qint64(10));
    QCOMPARE(row.value("model").toString(), QStringLiteral("fake-model"));

    // 会话累计 == token_usage 所有行（主 + 子）之和。
    const QVariantMap records = memory.tokenUsageRecords(0, 0, 500, 0);
    qint64 expected = 0;
    for (const QVariant& value : records.value("rows").toList()) {
        const QVariantMap rec = value.toMap();
        expected += rec.value("inputTokens").toLongLong()
                  + rec.value("outputTokens").toLongLong();
    }
    QCOMPARE(agent.sessionTokensUsed(), expected);
    QCOMPARE(records.value("total").toInt(), 3);   // 2 主行 + 1 子行

    agent.setMemory(nullptr);
}

// B1 后台：taskFinished 入账一次；随后 subagent_wait 只收割报告，不得
// 再写 token_usage 或增加会话累计。
void TestAgent::subagentUsageAccountedOnceForBackgroundRun() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse r = textResponse(QStringLiteral("bg report"));
        r.promptTokens = 60;
        r.completionTokens = 20;
        r.cacheReadTokens = 10;
        r.cacheCreationTokens = 5;
        r.tokensUsed = 80;
        sub.script.append(r);
    });
    ToolNameGuard waitGuard{QString::fromLatin1(SubAgentWaitTool::kName)};
    if (!ToolRegistry::instance().isRegistered(
            QString::fromLatin1(SubAgentWaitTool::kName))) {
        ToolRegistry::instance().registerTool(std::make_shared<SubAgentWaitTool>());
    }

    llm.reset();
    LLMResponse spawn = toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("bg accounting")},
                    {QStringLiteral("background"), true}});
    spawn.toolCalls[0].id = QStringLiteral("call_spawn_bg");
    spawn.promptTokens = 100;
    spawn.completionTokens = 5;
    spawn.tokensUsed = 105;
    llm.script.append(spawn);
    llm.script.append(textResponse(QStringLiteral("started")));

    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    QVERIFY(agent.startTask(QStringLiteral("spawn background")));
    QTRY_COMPARE(responseSpy.count(), 1);

    QString taskId;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const QVariant& value : agent.backgroundSubAgents()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("state")).toString()
                    != QLatin1String("completed"))
                continue;
            taskId = row.value(QStringLiteral("id")).toString();
            return true;
        }
        return false;
    }(), 10000);
    QVERIFY(!taskId.isEmpty());

    const QString sessionId = agent.currentSessionId();
    // taskFinished 是 Queued 连接，等 GUI 线程把入账行写完。
    QTRY_COMPARE_WITH_TIMEOUT(subagentTokenRows(memory, sessionId).size(), 1,
                              5000);
    const QVariantMap subRow = subagentTokenRows(memory, sessionId)
                                   .first().toMap();
    // input = 60 + 10 + 5 = 75；output 20。
    QCOMPARE(subRow.value("input").toLongLong(), qint64(75));
    QCOMPARE(subRow.value("output").toLongLong(), qint64(20));
    QCOMPARE(subRow.value("inputToolCall").toLongLong(), qint64(75));
    QCOMPARE(subRow.value("outputToolCall").toLongLong(), qint64(20));

    const int rowsAfterBg =
        memory.tokenUsageRecords(0, 0, 500, 0).value("total").toInt();
    const qint64 sessionAfterBg = agent.sessionTokensUsed();
    QVERIFY(sessionAfterBg > 0);

    // 收割同一后台任务：只回报告，不得重复入账。
    LLMResponse waitCall = toolResponse(QStringLiteral("subagent_wait"),
        QJsonObject{{QStringLiteral("id"), taskId},
                    {QStringLiteral("timeout_ms"), 1000}});
    waitCall.toolCalls[0].id = QStringLiteral("call_wait_bg");
    llm.script.append(waitCall);
    llm.script.append(textResponse(QStringLiteral("harvested")));
    QVERIFY(agent.startTask(QStringLiteral("harvest the background run")));
    QTRY_COMPARE(responseSpy.count(), 2);

    QCOMPARE(subagentTokenRows(memory, sessionId).size(), 1);
    const int rowsAfterWait =
        memory.tokenUsageRecords(0, 0, 500, 0).value("total").toInt();
    // 两轮主任务各 2 条主记录；等待轮多出 2 条主记录，但没有新增子代理行。
    QCOMPARE(rowsAfterWait, rowsAfterBg + 2);
    QVERIFY(agent.sessionTokensUsed() > sessionAfterBg);

    // 总量仍与逐行 input+output 一致。
    const QVariantMap records = memory.tokenUsageRecords(0, 0, 500, 0);
    qint64 expected = 0;
    for (const QVariant& value : records.value("rows").toList()) {
        const QVariantMap rec = value.toMap();
        expected += rec.value("inputTokens").toLongLong()
                  + rec.value("outputTokens").toLongLong();
    }
    QCOMPARE(agent.sessionTokensUsed(), expected);

    agent.setMemory(nullptr);
}

// docs/43 O4 审计收口（P1-1）：重试快照必须固化 spawn 当时的调度决策。
// 角色默认后台的任务在角色表变更后重试，仍按当时决策走后台——旧行为
// 快照只存原始 args（无 background 键），重试按当前角色重解析：设置
// 变更后退化为同步执行，结果被 retryBackgroundSubAgent 丢弃（不可观测、
// 不可取消、烧 token）。
void TestAgent::subagentRetrySnapshotPinsBackgroundDecision() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("pinned bg report")));
    });
    fixture.tool->setUserRoles(SubAgentTool::rolesFromVariant({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("explore")},
                    {QStringLiteral("prompt"),
                     QStringLiteral("BG-DEFAULT")},
                    {QStringLiteral("background"), true}}}));

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);

    // 省略 background：按角色默认进后台；原始 args 里没有 background 键。
    const ToolResult ack = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("pin the decision")}});
    QVERIFY2(ack.success, qPrintable(ack.error));
    QCOMPARE(hub->snapshots().size(), 1);
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const QString id = idRe.match(ack.output).captured(1);
    QVERIFY(!id.isEmpty());

    SubAgentWaitTool waitTool;
    const ToolResult report = waitTool.execute(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("timeout_ms"), 10000}});
    QVERIFY2(report.success, qPrintable(report.error));
    QVERIFY(report.output.contains(QStringLiteral("pinned bg report")));

    // 快照固化了解析后的 background 决策（旧行为：无该键 → toBool()==false）。
    const QJsonObject spawnArgs = hub->spawnArgsForSession(QString(), id);
    QCOMPARE(spawnArgs.value(QStringLiteral("background")).toBool(), true);

    // 角色表随后变更（不再默认后台）。按快照重放：仍进 hub 而非同步执行
    // 后被丢弃（旧行为：hub 仍 1 条、结果静默丢弃）。
    fixture.tool->setUserRoles(SubAgentTool::rolesFromVariant({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("explore")},
                    {QStringLiteral("prompt"),
                     QStringLiteral("NO-BG")}}}));
    const ToolResult replay = fixture.tool->execute(spawnArgs);
    QVERIFY2(replay.success, qPrintable(replay.error));
    QVERIFY(replay.output.contains(QStringLiteral("[Sub-agent started]")));
    QCOMPARE(hub->snapshots().size(), 2);

    // 收尾第二条任务，保持 guard 析构前的干净状态。
    const QString id2 = idRe.match(replay.output).captured(1);
    QVERIFY(!id2.isEmpty());
    QVERIFY(waitTool.execute(QJsonObject{
        {QStringLiteral("id"), id2},
        {QStringLiteral("timeout_ms"), 10000}}).success);
}

void TestAgent::subagentDisabledNotExposed() {
    // 未注册 spawn_agent：模型 schema 里不得出现该工具。
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("no sub needed")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("hi"));
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(!llm.seenTools.isEmpty());
    QVERIFY(!toolsArrayContains(llm.seenTools.first(),
                                QStringLiteral("spawn_agent")));
}

void TestAgent::subagentReportFlowsBackToMainLoop() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
        sub.script.append(textResponse(
            QStringLiteral("Sub findings: probe-evidence confirmed.")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("inspect the probe")}}));
    llm.script.append(textResponse(QStringLiteral("integrated")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("use a sub-agent"));
    QTRY_COMPARE(spy.count(), 1);

    // 主循环以工具结果形态拿到子代理报告，报告含子代理的证据。
    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY(report.contains(QStringLiteral("[Sub-agent report]")));
    QVERIFY(report.contains(QStringLiteral("probe-evidence")));
    QVERIFY(report.contains(QStringLiteral("Sub findings")));

    // 子代理工具面：probe 在列；派生器与写类工具排除。子剧本两条 = 两次
    // chatComplete（工具轮 + 最终报告），只断言第一轮的 schema。
    QMutexLocker lock(&fixture.mutex);
    QCOMPARE(fixture.subSeenTools.size(), 2);
    const QJsonArray subTools = fixture.subSeenTools.first();
    QVERIFY(toolsArrayContains(subTools, QStringLiteral("test_sub_probe")));
    QVERIFY(!toolsArrayContains(subTools, QStringLiteral("spawn_agent")));
    QVERIFY(!toolsArrayContains(subTools, QStringLiteral("write_file")));
    QVERIFY(!toolsArrayContains(subTools, QStringLiteral("edit_file")));
    QVERIFY(!toolsArrayContains(subTools, QStringLiteral("shell_exec")));
    // 子代理系统提示声明只读角色，goal 进入用户消息。
    const QJsonArray subMessages = fixture.subSeenMessages.first();
    QVERIFY(subMessages.size() >= 2);
    QCOMPARE(subMessages.first().toObject().value("role").toString(),
             QStringLiteral("system"));
    QVERIFY(subMessages.first().toObject().value("content").toString()
                .contains(QStringLiteral("read-only research sub-agent")));
    QVERIFY(subMessages.at(1).toObject().value("content").toString()
                .contains(QStringLiteral("inspect the probe")));
}

void TestAgent::subagentStepBudgetConverges() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        // 三步互不相同的 tool_call 剧本：绕开重复轮守卫，真正撞到步数上限
        // （剧本耗尽后 FakeAIService 重复最后一条，但参数已不同于第二步，
        // 强制收尾轮带不回最终报告）。
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"),
                                       QJsonObject{{"round", 2}}));
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"),
                                       QJsonObject{{"round", 3}}));
    });
    fixture.tool->setMaxSteps(3);
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("loop forever")}}));
    llm.script.append(textResponse(QStringLiteral("converged anyway")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn a stubborn sub-agent"));
    QTRY_COMPARE(spy.count(), 1);

    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY(report.contains(QStringLiteral("step budget")));
    // 主循环从子代理失败中恢复并给出自己的回答。
    QCOMPARE(agent.lastResponse(), QStringLiteral("converged anyway"));
}

void TestAgent::subagentTimeoutAborts() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    // 永不响应的服务：只有 Runner 的墙钟中止能收场。
    fixture.tool->setServiceFactory([]() -> IAIService* {
        return new SilentFakeService();
    });
    fixture.tool->refreshCatalog(false);
    fixture.tool->setTimeoutMs(300);
    ToolRegistry::instance().registerTool(fixture.tool);

    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("stuck")}}));
    llm.script.append(textResponse(QStringLiteral("gave up cleanly")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn a frozen sub-agent"));
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(lastToolMessageContent(agent.messages())
                .contains(QStringLiteral("wall-clock budget")));
    QCOMPARE(agent.lastResponse(), QStringLiteral("gave up cleanly"));
}

void TestAgent::subagentStopCascades() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    auto enteredFlag = std::make_shared<std::atomic<bool>>(false);
    auto releaseGate = std::make_shared<QSemaphore>();
    fixture.tool = registerSpawnTool(fixture, [&](RecordingFakeService& sub) {
        sub.enteredFlag = enteredFlag.get();
        sub.gate = releaseGate.get();
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("slow probe")}}));
    // 无后续剧本：任务只能经由 stop() 收场。
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("stop a running sub-agent"));
    QTRY_VERIFY(enteredFlag->load());   // 子代理请求已挂起
    agent.stop();                       // 级联取消旗标
    releaseGate->release();             // 放行挂起的回复，Runner 随后观察到旗标
    QTRY_VERIFY(!agent.isActive());
    // 派生调用在历史里被密封，无悬挂 tool_calls。
    QVERIFY(lastToolMessageContent(agent.messages())
                .contains(QStringLiteral("[Not executed]")));
    QVERIFY(noDanglingToolCalls(agent.messages()));
}

// docs/32 §9: 用户点「停止子代理」只中止在飞的 spawn_agent——主任务拿到
// Cancelled 报告后继续跑，由模型自己收尾。与 stop() 的级联封口相对。
void TestAgent::subagentManualCancelKeepsPrimaryRunning() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    auto enteredFlag = std::make_shared<std::atomic<bool>>(false);
    auto releaseGate = std::make_shared<QSemaphore>();
    fixture.tool = registerSpawnTool(fixture, [&](RecordingFakeService& sub) {
        sub.enteredFlag = enteredFlag.get();
        sub.gate = releaseGate.get();
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
    });
    // 不注入预算：默认即无步数/超时限制（只能这样收场或模型自收敛）。
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("slow probe")}}));
    llm.script.append(textResponse(QStringLiteral("wrapped up")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("cancel a running sub-agent"));
    QTRY_VERIFY(enteredFlag->load());   // 子代理请求已挂起
    // 无预算时的进度标题只显示步号（不出现误导性的 /0 或 /1000）。
    QTRY_VERIFY(traceHas(agent.traceModel(), QStringLiteral("tool_call"),
                         QStringLiteral("spawn_agent · step 1")));
    QVERIFY(agent.cancelSubAgents());   // 只翻旗标，主任务不受影响
    releaseGate->release();             // 放行挂起的回复，Runner 观察旗标收场
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(agent.lastResponse(), QStringLiteral("wrapped up"));
    QVERIFY(lastToolMessageContent(agent.messages())
                .contains(QStringLiteral("Sub-agent cancelled")));
    QVERIFY(noDanglingToolCalls(agent.messages()));
}

// docs/34 P3.1：propose_edit 是子代理的「写意图」出口——只校验锚点并登记
// 提案，绝不落盘。失败路径（无汇聚槽/越界/锚点缺失/锚点不唯一/配额）都不
// 得产生提案；成功路径下文件字节必须原样。
void TestAgent::proposeEditValidatesAnchorsAndNeverWrites() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("sample.txt"));
    const QByteArray original("alpha\nbeta\ngamma\n");
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(original), qint64(original.size()));
    }
    // 直接调用（非 agent 派发）：全局项目目录就是执行域。
    Sandbox::setProjectDir(dir.path());
    ProposeEditTool tool;

    const auto edit = [](const QString& find, const QString& replace) {
        return QJsonObject{{QStringLiteral("find"), find},
                           {QStringLiteral("replace"), replace}};
    };
    const auto call = [&tool, &edit](const QString& path,
                                     const QList<QJsonObject>& edits,
                                     const QString& rationale = QStringLiteral("why")) {
        QJsonArray arr;
        for (const auto& e : edits) arr.append(e);
        return tool.execute(QJsonObject{{QStringLiteral("path"), path},
                                        {QStringLiteral("rationale"), rationale},
                                        {QStringLiteral("edits"), arr}});
    };

    // 不在子代理运行内（无线程局部汇聚槽）→ 明确拒绝并指引主代理用写工具。
    ToolResult result = call(QStringLiteral("sample.txt"), {edit("beta", "BETA")});
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("only available inside a sub-agent run")));

    QVector<SubAgentProposal> collected;
    SubAgentProposalSink sink(&collected);

    // 锚点缺失 / 锚点不唯一 / 编辑列表为空 / 越出项目根：四种失败都不入槽。
    result = call(QStringLiteral("sample.txt"), {edit("delta", "DELTA")});
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("does not occur")));
    result = call(QStringLiteral("sample.txt"), {edit("a", "A")});
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("ambiguous anchor")));
    QVERIFY(!call(QStringLiteral("sample.txt"), {}).success);
    result = call(dir.filePath(QStringLiteral("../escape.txt")),
                  {edit("alpha", "A")});
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("escapes the project root")));
    result = call(QStringLiteral("sample.txt"), {edit("beta", "BETA")},
                  QString());   // rationale 缺失
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("'rationale' is required")));
    QVERIFY(collected.isEmpty());

    // 同一调用的多条编辑按顺序在模拟内容上校验：第二条锚点引用第一条的
    // 替换结果才合法（主代理依次应用时不会半路失败）。
    result = call(QStringLiteral("sample.txt"),
                  {edit("beta", "BETA"), edit("BETA\ngamma", "BETA\nGAMMA")});
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(collected.size(), 1);
    QCOMPARE(collected.first().rationale, QStringLiteral("why"));
    QCOMPARE(collected.first().edits.size(), 2);
    QCOMPARE(QFileInfo(collected.first().path).absoluteFilePath(),
             QFileInfo(filePath).absoluteFilePath());
    // 提案只读：文件字节不变。
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), original);
    }
    // 顺序校验真生效：第二条锚点若只匹配「原始内容」而非替换后内容则失败。
    result = call(QStringLiteral("sample.txt"),
                  {edit("beta", "BETA"), edit("beta", "BETA2")});
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("does not occur")));
    QCOMPARE(collected.size(), 1);

    // 配额：每轮运行最多 kMaxProposalsPerRun 条，超出给出可读原因。
    while (collected.size() < ProposeEditTool::kMaxProposalsPerRun) {
        const ToolResult ok = call(QStringLiteral("sample.txt"),
                                   {edit("alpha", "alpha")});
        QVERIFY2(ok.success, qPrintable(ok.error));
    }
    result = call(QStringLiteral("sample.txt"), {edit("alpha", "alpha")});
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("Proposal limit reached")));
    QCOMPARE(collected.size(), ProposeEditTool::kMaxProposalsPerRun);

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), original);
    f.close();
    Sandbox::setProjectDir(QString());
}

// docs/34 P3.1 集成：coder 角色经 propose_edit 起草 → 报告带 `## PROPOSALS`
// 段 → 文件保持原样（应用由主代理的 edit_file 完成，走既有确认流）。
void TestAgent::coderRoleProposalsReachReportUnapplied() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("app.cpp"));
    const QByteArray original("int main() { return 1; }\n");
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(original), qint64(original.size()));
    }
    agent.setActiveProject(QStringLiteral("p34"), QStringLiteral("Demo"), dir.path());

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("propose_edit"),
            QJsonObject{
                {QStringLiteral("path"), QStringLiteral("app.cpp")},
                {QStringLiteral("rationale"),
                 QStringLiteral("main should exit successfully")},
                {QStringLiteral("edits"), QJsonArray{QJsonObject{
                    {QStringLiteral("find"), QStringLiteral("return 1;")},
                    {QStringLiteral("replace"), QStringLiteral("return 0;")}}}}}));
        sub.script.append(textResponse(QStringLiteral("proposed one edit")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("make main return 0")},
                    {QStringLiteral("role"), QStringLiteral("coder")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("draft the change"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    // 提案由 Runner 汇聚进报告：段头 + 理由 + find/replace 的 diff。
    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY2(report.contains(QStringLiteral("## PROPOSALS")), qPrintable(report));
    QVERIFY(report.contains(QStringLiteral("not applied")));
    QVERIFY(report.contains(QStringLiteral("main should exit successfully")));
    QVERIFY(report.contains(QStringLiteral("-return 1;")));
    QVERIFY(report.contains(QStringLiteral("+return 0;")));
    // 报告里给出应用指引（主代理用 edit_file 逐条应用）。
    QVERIFY(report.contains(QStringLiteral("edit_file")));

    // 子代理全程只读：磁盘文件不变。
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), original);
    }

    // docs/34 P3.2：提案索引挂到 spawn_agent 的 tool_result 节点 metadata
    // （卡片徽章/列表的数据源），路径已转成项目相对形式。
    bool sawProposalIndex = false;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("tool_result")
                || node.value(QStringLiteral("title")).toString()
                       != QLatin1String("spawn_agent"))
            continue;
        const QJsonArray index =
            node.value(QStringLiteral("subagentProposals")).toArray();
        if (index.size() != 1) continue;
        const QJsonObject item = index.first().toObject();
        if (item.value(QStringLiteral("path")).toString() == QLatin1String("app.cpp")
                && item.value(QStringLiteral("count")).toInt() == 1
                && item.value(QStringLiteral("rationale")).toString()
                       == QLatin1String("main should exit successfully"))
            sawProposalIndex = true;
    }
    QVERIFY(sawProposalIndex);

    // coder 的工具面：授予 propose_edit，其余仍是只读工具。
    QMutexLocker lock(&fixture.mutex);
    QVERIFY(!fixture.subSeenTools.isEmpty());
    const QJsonArray subTools = fixture.subSeenTools.first();
    QVERIFY(toolsArrayContains(subTools, QStringLiteral("propose_edit")));
    QVERIFY(toolsArrayContains(subTools, QStringLiteral("read_file")));
    QVERIFY(!toolsArrayContains(subTools, QStringLiteral("edit_file")));
    QVERIFY(!toolsArrayContains(subTools, QStringLiteral("write_file")));
    lock.unlock();

    agent.setActiveProject(QString(), QString(), QString());
}

// docs/34 P3.1：propose_edit 是能力而非普通读工具——角色白名单为空
// （explore/research/review = 整个只读目录）不意味着顺带获得提案权。
void TestAgent::exploreRoleCannotPropose() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("app.cpp"));
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("int main() { return 1; }\n");
    }
    agent.setActiveProject(QStringLiteral("p34"), QStringLiteral("Demo"), dir.path());

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        // 幻觉调用：explore 的 schema 里没有 propose_edit，Runner 应回
        // 「Unknown tool」并继续，报告里不出现 PROPOSALS 段。
        sub.script.append(toolResponse(QStringLiteral("propose_edit"),
            QJsonObject{
                {QStringLiteral("path"), QStringLiteral("app.cpp")},
                {QStringLiteral("rationale"), QStringLiteral("x")},
                {QStringLiteral("edits"), QJsonArray{QJsonObject{
                    {QStringLiteral("find"), QStringLiteral("return 1;")},
                    {QStringLiteral("replace"), QStringLiteral("return 0;")}}}}}));
        sub.script.append(textResponse(QStringLiteral("cannot draft here")));
    });
    llm.reset();
    // 不点名 role → 默认 explore（白名单为空）。
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("look around")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn explorer"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY(report.contains(QStringLiteral("cannot draft here")));
    QVERIFY(!report.contains(QStringLiteral("## PROPOSALS")));

    QMutexLocker lock(&fixture.mutex);
    QVERIFY(fixture.subSeenTools.size() >= 2);
    QVERIFY(!toolsArrayContains(fixture.subSeenTools.first(),
                                QStringLiteral("propose_edit")));
    QVERIFY(toolsArrayContains(fixture.subSeenTools.first(),
                               QStringLiteral("read_file")));
    // 子代理第二轮看到的工具结果里是 Unknown tool 提示。
    bool sawUnknown = false;
    const QJsonArray second = fixture.subSeenMessages.at(1);
    for (const auto& entry : second) {
        const QJsonObject msg = entry.toObject();
        if (msg.value(QStringLiteral("role")).toString() == QLatin1String("tool")
                && msg.value(QStringLiteral("content")).toString()
                       .contains(QStringLiteral("Unknown tool")))
            sawUnknown = true;
    }
    QVERIFY(sawUnknown);
    lock.unlock();

    // 文件同样未变。
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("int main() { return 1; }\n"));
    f.close();
    agent.setActiveProject(QString(), QString(), QString());
}

// docs/34 P3.3：同一文件的多个提案在报告里带合并提示；文本上相互包含的
// 锚点额外标注冲突（先应用的一条可能让后一条的锚点失效）。
void TestAgent::coderSameFileProposalsGetMergeHints() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("app.cpp"));
    const QByteArray original("// header\nint main() { return 1; }\n// footer\n");
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(original), qint64(original.size()));
    }
    agent.setActiveProject(QStringLiteral("p34"), QStringLiteral("Demo"), dir.path());

    const auto propose = [](const QString& find, const QString& replace) {
        return toolResponse(QStringLiteral("propose_edit"),
            QJsonObject{
                {QStringLiteral("path"), QStringLiteral("app.cpp")},
                {QStringLiteral("rationale"), QStringLiteral("cleanup")},
                {QStringLiteral("edits"), QJsonArray{QJsonObject{
                    {QStringLiteral("find"), find},
                    {QStringLiteral("replace"), replace}}}}});
    };
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [propose](RecordingFakeService& sub) {
        // 1、2 互不重叠；3、4 的锚点互相包含（4 的 find 在 3 的 find 里）。
        sub.script.append(propose(QStringLiteral("// header"), QStringLiteral("// HEADER")));
        sub.script.append(propose(QStringLiteral("// footer"), QStringLiteral("// FOOTER")));
        sub.script.append(propose(QStringLiteral("int main() { return 1; }"),
                                  QStringLiteral("int main() { return 0; }")));
        sub.script.append(propose(QStringLiteral("return 1;"),
                                  QStringLiteral("return 42;")));
        sub.script.append(textResponse(QStringLiteral("proposed four edits")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("clean up main")},
                    {QStringLiteral("role"), QStringLiteral("coder")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("draft same-file changes"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY2(report.contains(QStringLiteral("Merge notes:")), qPrintable(report));
    QVERIFY(report.contains(QStringLiteral("app.cpp has 4 proposals")));
    QVERIFY(report.contains(QStringLiteral("Apply them in the order listed")));
    QVERIFY(report.contains(QStringLiteral("Proposals 3 and 4 overlap on app.cpp")));
    QVERIFY(!report.contains(QStringLiteral("Proposals 1 and 2 overlap")));
    // docs/35 P4.4: 报告引导改用 apply_proposal，并注入可复制的 id。
    QVERIFY(report.contains(QStringLiteral("apply_proposal(id)")));
    QVERIFY(report.contains(
        QStringLiteral("Proposal ids (pass one to apply_proposal)")));
    QVERIFY(report.contains(QStringLiteral("#1 → app.cpp")));
    QVERIFY(report.contains(QStringLiteral("#4 → app.cpp")));

    // 提案索引仍是 4 条（合并提示不改变数据结构），且 trace 只存轻量索引
    // ——完整 find/replace 只在会话注册表/报告里，节点体积不回退。
    bool sawIndex = false;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("title")).toString() != QLatin1String("spawn_agent")
                || node.value(QStringLiteral("type")).toString() != QLatin1String("tool_result"))
            continue;
        const QJsonArray index =
            node.value(QStringLiteral("subagentProposals")).toArray();
        if (index.size() != 4) continue;
        sawIndex = true;
        for (const auto& item : index) {
            const QJsonObject proposal = item.toObject();
            QVERIFY(!proposal.contains(QStringLiteral("edits")));
            QVERIFY(!proposal.contains(QStringLiteral("find")));
        }
    }
    QVERIFY(sawIndex);

    // 子代理全程只读：四次提案都不落盘。
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), original);
    f.close();
    agent.setActiveProject(QString(), QString(), QString());
}

// docs/35 P4.4: 结构化应用工具的纯工具层行为——预览取注册表、执行重新校验、
// 成功后标记 applied、重复应用/未知 id/锚点失效/歧义锚点都可读失败。
void TestAgent::applyProposalToolValidatesAndApplies() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.cpp"));
    const QByteArray original("int main() { return 1; }\n");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(original), qint64(original.size()));
    }
    Sandbox::setProjectDir(dir.path());
    Sandbox::setProjectWriteEnabled(true);
    // 早退（QVERIFY 失败）也要恢复全局沙箱策略，避免影响后续用例。
    struct RestoreSandbox {
        ~RestoreSandbox() {
            Sandbox::setProjectWriteEnabled(false);
            Sandbox::setProjectDir(QString());
        }
    } restoreSandbox;

    ApplyProposalTool tool;
    auto store = std::make_shared<ProposalStore>();
    ProposalStoreRouter router(store);

    // 没有路由（例如子代理内部 / 测试直接调用）时拒绝。
    QVERIFY(!tool.execute(QJsonObject{{"id", QStringLiteral("x#1")}}).success);
    // 参数与 id 校验。
    QVERIFY(!tool.execute(QJsonObject{}).success);
    const ToolResult unknown =
        tool.execute(QJsonObject{{"id", QStringLiteral("nope#9")}});
    QVERIFY(!unknown.success);
    QVERIFY(unknown.error.contains(QStringLiteral("Unknown or expired")));

    StoredProposal proposal;
    proposal.id = QStringLiteral("node#1");
    proposal.path = path;
    proposal.rationale = QStringLiteral("return success");
    proposal.edits.append(SubAgentProposalEdit{
        QStringLiteral("int main() { return 1; }"),
        QStringLiteral("int main() { return 0; }")});
    store->put(proposal);

    // 预览与 edit_file 同形状：未确认不落盘。
    const QString preview =
        tool.generatePreview(QJsonObject{{"id", QStringLiteral("node#1")}});
    QVERIFY2(preview.contains(QStringLiteral("-int main() { return 1; }")),
             qPrintable(preview));
    QVERIFY2(preview.contains(QStringLiteral("+int main() { return 0; }")),
             qPrintable(preview));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("int main() { return 1; }\n"));
    }

    const ToolResult applied =
        tool.execute(QJsonObject{{"id", QStringLiteral("node#1")}});
    QVERIFY2(applied.success, qPrintable(applied.error));
    QCOMPARE(applied.checkpoint.path, path);
    QVERIFY(!applied.checkpoint.diff.isEmpty());
    QVERIFY(applied.checkpoint.addedLines >= 1);
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("int main() { return 0; }\n"));
    }

    // 重复应用：可读失败，文件不再变化。
    const ToolResult again =
        tool.execute(QJsonObject{{"id", QStringLiteral("node#1")}});
    QVERIFY(!again.success);
    QVERIFY(again.error.contains(QStringLiteral("already applied")));

    // 锚点失效：带确定性重定位建议。
    StoredProposal stale;
    stale.id = QStringLiteral("node#2");
    stale.path = path;
    stale.edits.append(SubAgentProposalEdit{
        QStringLiteral("int main() { return 7; }"),
        QStringLiteral("int main() { return 9; }")});
    store->put(stale);
    const ToolResult staleResult =
        tool.execute(QJsonObject{{"id", QStringLiteral("node#2")}});
    QVERIFY(!staleResult.success);
    QVERIFY(staleResult.error.contains(QStringLiteral("no longer occurs")));
    QVERIFY(staleResult.error.contains(QStringLiteral("Re-read the file")));

    // 歧义锚点：当前内容里出现两次，拒绝而不是猜一个。
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(f.write("same text\nsame text\n"), qint64(20));
    }
    StoredProposal ambiguous;
    ambiguous.id = QStringLiteral("node#3");
    ambiguous.path = path;
    ambiguous.edits.append(SubAgentProposalEdit{
        QStringLiteral("same text"), QStringLiteral("changed")});
    store->put(ambiguous);
    const ToolResult ambiguousResult =
        tool.execute(QJsonObject{{"id", QStringLiteral("node#3")}});
    QVERIFY(!ambiguousResult.success);
    QVERIFY(ambiguousResult.error.contains(QStringLiteral("occurs 2 times")));
}

// docs/35 P4.4: 全链路——coder 产出提案 → 报告注入 id → 主代理调
// apply_proposal → 确认弹窗预览 → 批准后落盘；trace 仍只有索引。
void TestAgent::applyProposalPreviewAndApplyThroughAgent() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.cpp"));
    const QByteArray original("int main() { return 1; }\n");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(original), qint64(original.size()));
    }
    agent.setActiveProject(QStringLiteral("p44"), QStringLiteral("Demo"),
                           dir.path());

    SubAgentFixture fixture;
    SpawnToolGuard spawnGuard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("propose_edit"),
            QJsonObject{
                {QStringLiteral("path"), QStringLiteral("app.cpp")},
                {QStringLiteral("rationale"), QStringLiteral("return success")},
                {QStringLiteral("edits"), QJsonArray{QJsonObject{
                    {QStringLiteral("find"),
                     QStringLiteral("int main() { return 1; }")},
                    {QStringLiteral("replace"),
                     QStringLiteral("int main() { return 0; }")}}}}}));
        sub.script.append(textResponse(QStringLiteral("proposed one edit")));
    });

    auto applyTool = std::make_shared<ApplyProposalTool>();
    ToolNameGuard applyGuard{QString::fromLatin1(ApplyProposalTool::kName)};
    ToolRegistry::instance().registerTool(applyTool);

    // 动态脚本服务：第二轮从 spawn 报告里提取 Proposal id 再返回 apply 调用。
    ProposalFlowFakeService dynamicLlm;
    struct RestoreLlm {
        AIAgent& agent;
        FakeAIService* original;
        ~RestoreLlm() { agent.setLLMService(original); }
    } restoreLlm{agent, &llm};
    agent.setLLMService(&dynamicLlm);
    Sandbox::setProjectWriteEnabled(true);
    struct RestoreSandbox {
        ~RestoreSandbox() {
            Sandbox::setProjectWriteEnabled(false);
            Sandbox::setProjectDir(QString());
        }
    } restoreSandbox;

    QSignalSpy confirmSpy(&agent, &AIAgent::toolConfirmRequired);
    agent.startTask(QStringLiteral("apply the drafted change"));
    QTRY_COMPARE_WITH_TIMEOUT(confirmSpy.count(), 1, 15000);

    // 确认弹窗识别的是结构化工具，预览就是 diff。
    const QList<QVariant> confirm = confirmSpy.first();
    QCOMPARE(confirm.at(1).toString(), QStringLiteral("apply_proposal"));
    QVERIFY2(confirm.at(3).toString().contains(
                 QStringLiteral("-int main() { return 1; }")),
             qPrintable(confirm.at(3).toString()));
    QVERIFY(confirm.at(3).toString().contains(
        QStringLiteral("+int main() { return 0; }")));

    // 未批准前不落盘。
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), original);
    }

    QSignalSpy responseSpy(&agent, &AIAgent::responseReady);
    agent.approveToolCall(confirm.at(0).toString(), true);
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 15000);

    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("int main() { return 0; }\n"));
    }
    const QString lastTool = lastToolMessageContent(agent.messages());
    QVERIFY2(lastTool.contains(QStringLiteral("Applied proposal")),
             qPrintable(lastTool));
    QVERIFY(lastTool.contains(QStringLiteral("#1")));

    agent.setActiveProject(QString(), QString(), QString());
}

// docs/35 P4.3: background spawn returns an ack immediately; status/wait read
// the same session hub; a wait timeout is success with state=running.
void TestAgent::subagentBackgroundAckStatusWait() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();

    QMutex gatesMutex;
    QVector<std::shared_ptr<QSemaphore>> gates;
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        auto gate = std::make_shared<QSemaphore>();
        {
            QMutexLocker lock(&gatesMutex);
            gates.append(gate);
        }
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("background report")));
        return service;
    });
    auto gateCount = [&]() -> int {
        QMutexLocker lock(&gatesMutex);
        return gates.size();
    };
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    auto hub = std::make_shared<BackgroundTaskHub>();
    SubAgentWaitTool waitTool;
    SubAgentStatusTool statusTool;
    // P1-3: status 在池线程经注入引用读 gateSummary（不再查 registry）。
    statusTool.setSpawnTool(fixture.tool);

    // 没有 route 的路径（子代理内部/未挂载）拒绝。注意 QThreadStorage
    // 不是栈：必须在安装正式 router 之前测，析构只会清空当前线程。
    QVERIFY(!waitTool.execute(QJsonObject{{"id", "sa-1"}}).success);
    QVERIFY(!statusTool.execute(QJsonObject{{"id", "sa-1"}}).success);
    BackgroundTaskHubRouter router(hub);

    const ToolResult ack = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("background survey")},
        {QStringLiteral("background"), true}});
    QVERIFY2(ack.success, qPrintable(ack.error));
    QVERIFY(ack.output.contains(QStringLiteral("[Sub-agent started]")));
    QVERIFY(!ack.output.contains(QStringLiteral("[Sub-agent report]")));
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const QRegularExpressionMatch idMatch = idRe.match(ack.output);
    QVERIFY(idMatch.hasMatch());
    const QString id = idMatch.captured(1);

    // The worker is parked in chatComplete; the ack already returned.
    QTRY_COMPARE(gateCount(), 1);
    const ToolResult status = statusTool.execute(QJsonObject{{"id", id}});
    QVERIFY2(status.success, qPrintable(status.error));
    QVERIFY(status.output.contains(QStringLiteral("state: running")));

    QElapsedTimer waitTimer;
    waitTimer.start();
    const ToolResult timedOut = waitTool.execute(QJsonObject{
        {QStringLiteral("id"), id}, {QStringLiteral("timeout_ms"), 300}});
    QVERIFY2(timedOut.success, qPrintable(timedOut.error));
    QVERIFY(timedOut.output.contains(QStringLiteral("still running")));
    QVERIFY(waitTimer.elapsed() >= 250);

    {
        QMutexLocker lock(&gatesMutex);
        gates.first()->release();
    }
    const ToolResult report = waitTool.execute(QJsonObject{{"id", id}});
    QVERIFY2(report.success, qPrintable(report.error));
    QVERIFY(report.output.contains(QStringLiteral("[Sub-agent report]")));
    QVERIFY(report.output.contains(QStringLiteral("background report")));

    const QVariantList snapshots = hub->snapshots();
    QCOMPARE(snapshots.size(), 1);
    const QVariantMap row = snapshots.first().toMap();
    QCOMPARE(row.value(QStringLiteral("state")).toString(),
             QStringLiteral("completed"));
    QCOMPARE(row.value(QStringLiteral("role")).toString(),
             QStringLiteral("explore"));
    QCOMPARE(hub->activeCount(), 0);

    // Unknown ids fail readably on all three tools.
    SubAgentCancelTool cancelTool;
    QVERIFY(!waitTool.execute(QJsonObject{{"id", "sa-999"}}).success);
    QVERIFY(!statusTool.execute(QJsonObject{{"id", "sa-999"}}).success);
    QVERIFY(!cancelTool.execute(QJsonObject{{"id", "sa-999"}}).success);
}

// docs/35 P4.3: cancelling one background task flips only its token; the
// other keeps running to completion.
void TestAgent::subagentBackgroundCancelOneTask() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();

    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    auto slowTool = std::make_shared<SleepProbeTool>(
        QStringLiteral("test_slow_bg"), 120, &active, &peak);
    ToolNameGuard slowGuard{slowTool->definition().name};
    ToolRegistry::instance().registerTool(slowTool);

    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        for (int i = 0; i < 6; ++i)
            service->script.append(toolResponse(
                QStringLiteral("test_slow_bg"), QJsonObject{{"n", i}}));
        service->script.append(textResponse(QStringLiteral("report ok")));
        return service;
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);
    SubAgentCancelTool cancelTool;

    const auto start = [&fixture](const QString& goal) {
        return fixture.tool->execute(QJsonObject{
            {QStringLiteral("goal"), goal},
            {QStringLiteral("background"), true}});
    };
    const ToolResult ackA = start(QStringLiteral("alpha"));
    const ToolResult ackB = start(QStringLiteral("beta"));
    QVERIFY(ackA.success);
    QVERIFY(ackB.success);
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const QString idA = idRe.match(ackA.output).captured(1);
    const QString idB = idRe.match(ackB.output).captured(1);
    QVERIFY(!idA.isEmpty());
    QVERIFY(!idB.isEmpty());
    QVERIFY(idA != idB);

    QTRY_VERIFY(hub->stateLabel(idA) == QLatin1String("running")
                && hub->stateLabel(idB) == QLatin1String("running"));
    const ToolResult cancel = cancelTool.execute(QJsonObject{{"id", idA}});
    QVERIFY2(cancel.success, qPrintable(cancel.error));
    QVERIFY(cancel.output.contains(QStringLiteral("Cancellation requested")));

    QCOMPARE(hub->waitFor(idA, 15000, {}), QStringLiteral("cancelled"));
    QCOMPARE(hub->waitFor(idB, 15000, {}), QStringLiteral("completed"));
    const auto reportA = hub->reportFor(idA);
    const auto reportB = hub->reportFor(idB);
    QVERIFY(reportA.has_value());
    QVERIFY(reportB.has_value());
    QVERIFY(!reportA->ok);
    QVERIFY(reportA->stop == SubAgentReport::Stop::Cancelled);
    QVERIFY(reportB->ok);
}

// docs/35 P4.3: full main-loop flow — spawn(background) ack, wait for the id
// in round 2, then the report flows back like a synchronous spawn; the hub
// feeds the panel API and an independent subagent_task trace node.
void TestAgent::subagentBackgroundForkJoinThroughAgent() {
    SubAgentFixture fixture;
    SpawnToolGuard spawnGuard;
    ToolNameGuard waitGuard{QString::fromLatin1(SubAgentWaitTool::kName)};
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
        sub.script.append(textResponse(QStringLiteral("background findings")));
    });
    ToolRegistry::instance().registerTool(std::make_shared<SubAgentWaitTool>());

    BackgroundFlowFakeService dynamicLlm;
    struct RestoreLlm {
        AIAgent& agent;
        FakeAIService* original;
        ~RestoreLlm() { agent.setLLMService(original); }
    } restoreLlm{agent, &llm};
    agent.setLLMService(&dynamicLlm);

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("fork and join"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    QString ackText;
    for (const auto& entry : agent.messages()) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() != QLatin1String("tool")) continue;
        const QString content = msg.value("content").toString();
        if (content.contains(QStringLiteral("[Sub-agent started]")))
            ackText = content;
    }
    QVERIFY(ackText.contains(QStringLiteral("[Sub-agent started]")));
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const QString taskId = idRe.match(ackText).captured(1);
    QVERIFY(!taskId.isEmpty());

    // The wait round folded the report into the main history.
    const QString lastTool = lastToolMessageContent(agent.messages());
    QVERIFY2(lastTool.contains(QStringLiteral("background findings")),
             qPrintable(lastTool));

    // Panel API reads the same registry.
    const QVariantList rows = agent.backgroundSubAgents();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("id")).toString(),
             taskId);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("state")).toString(),
             QStringLiteral("completed"));
    QCOMPARE(agent.backgroundSubAgentActiveCount(), 0);
    QVERIFY(agent.backgroundSubAgentReport(taskId)
                .contains(QStringLiteral("background findings")));

    // Independent trace node (type subagent_task) with attribution metadata.
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        for (const auto& entry : agent.traceModel()) {
            const QJsonObject node = entry.toObject();
            if (node.value("type").toString() != QLatin1String("subagent_task"))
                continue;
            if (node.value("taskId").toString() != taskId) continue;
            if (node.value("state").toString() != QLatin1String("completed"))
                continue;
            if (node.value("status").toString() != QLatin1String("success"))
                continue;
            return node.value("goal").toString()
                       == QLatin1String("survey the tree")
                && node.value("role").toString() == QLatin1String("explore");
        }
        return false;
    })(), 5000);

    agent.dismissFinishedBackgroundSubAgents();
    QVERIFY(agent.backgroundSubAgents().isEmpty());
}

// docs/35 P4.3: 面板的数据通道是 AgentVM（TopBar/对话框绑定它），
// ChatSessionController 转发活动标签的 agent 信号。这里覆盖整条链：
// 后台任务运行中 count/列表可见，结束后仍可取报告，清除后消失。
void TestAgent::subagentBackgroundPanelThroughViewModel() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    QMutex gatesMutex;
    QVector<std::shared_ptr<QSemaphore>> gates;
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        auto gate = std::make_shared<QSemaphore>();
        {
            QMutexLocker lock(&gatesMutex);
            gates.append(gate);
        }
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("bg report")));
        return service;
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    AgentViewModel vm(nullptr);
    vm.setAgent(&agent);

    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("bg probe")},
                    {QStringLiteral("background"), true}}));
    llm.script.append(textResponse(QStringLiteral("ack done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("start a background task"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    // 主任务已结束，后台任务仍在跑：VM 必须看得到（TopBar 角标的数据源）。
    QTRY_COMPARE_WITH_TIMEOUT(vm.backgroundSubAgentActiveCount(), 1, 3000);
    const QVariantList rows = vm.backgroundSubAgents();
    QCOMPARE(rows.size(), 1);
    const QString taskId =
        rows.first().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!taskId.isEmpty());
    QVERIFY(vm.cancelBackgroundSubAgent(taskId));

    {
        QMutexLocker lock(&gatesMutex);
        gates.first()->release();
    }
    QTRY_COMPARE_WITH_TIMEOUT(vm.backgroundSubAgentActiveCount(), 0, 5000);
    // 终态任务仍留在面板里（入口不能因为 activeCount=0 就丢失报告）。
    QCOMPARE(vm.backgroundSubAgents().size(), 1);
    const QString report = vm.backgroundSubAgentReport(taskId);
    QVERIFY(!report.isEmpty());
    vm.dismissFinishedBackgroundSubAgents();
    QCOMPARE(vm.backgroundSubAgents().size(), 0);
}

// 2026-11-03：面板不只显示终态。hub 要在运行中暴露步数/最近进度，把
// 「占用线程但还在等槽位」如实标成排队，并在 cancel() 后立刻给出
// cancelling（Runner 收尾前状态仍是 running，界面上先显示「正在停止…」）。
void TestAgent::backgroundHubReportsLiveProgressAndCancelling() {
    auto hub = std::make_shared<BackgroundTaskHub>();
    QSemaphore gate;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    const QString id = hub->createTask(
        QStringLiteral("live stats"), QStringLiteral("research"), cancelled,
        [hub, &gate](const QString& taskId) {
            gate.acquire();
            SubAgentReport report;
            report.stop = SubAgentReport::Stop::Cancelled;
            report.text = QStringLiteral("stopped");
            hub->finishTask(taskId, report);
        });
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE(hub->stateLabel(id), QStringLiteral("running"));

    hub->updateProgress(id, QStringLiteral("thinking"), QStringLiteral("step 1"),
                        1, 0);
    QVariantMap row = hub->snapshots().first().toMap();
    QCOMPARE(row.value(QStringLiteral("steps")).toInt(), 1);
    QCOMPARE(row.value(QStringLiteral("progressTail")).toList().size(), 1);
    QVERIFY(!row.value(QStringLiteral("slotQueued")).toBool());
    QVERIFY(!row.value(QStringLiteral("cancelling")).toBool());

    // 线程已启动、Runner 却在 acquireSlot 排队：状态仍 running，但面板
    // 必须按排队统计（displayState 会看 slotQueued）。
    hub->updateProgress(id, QStringLiteral("queued"),
                        QStringLiteral("waiting for a free slot"), 0, 0);
    row = hub->snapshots().first().toMap();
    QVERIFY(row.value(QStringLiteral("slotQueued")).toBool());
    hub->updateProgress(id, QStringLiteral("thinking"), QStringLiteral("step 2"),
                        2, 0);
    row = hub->snapshots().first().toMap();
    QVERIFY(!row.value(QStringLiteral("slotQueued")).toBool());
    QCOMPARE(row.value(QStringLiteral("steps")).toInt(), 2);

    // 进度尾巴加深到 24 条，面板可不滚动看到最近一段。
    for (int i = 0; i < 30; ++i) {
        hub->updateProgress(id, QStringLiteral("thinking"),
                            QStringLiteral("step %1").arg(i + 3), i + 3, 0);
    }
    row = hub->snapshots().first().toMap();
    QCOMPARE(row.value(QStringLiteral("progressTail")).toList().size(), 24);
    QCOMPARE(row.value(QStringLiteral("steps")).toInt(), 32);

    QVERIFY(hub->cancel(id));
    row = hub->snapshots().first().toMap();
    QVERIFY(row.value(QStringLiteral("cancelling")).toBool());
    QCOMPARE(row.value(QStringLiteral("state")).toString(),
             QStringLiteral("running"));

    gate.release();
    QTRY_COMPARE(hub->stateLabel(id), QStringLiteral("cancelled"));
    row = hub->snapshots().first().toMap();
    QVERIFY(!row.value(QStringLiteral("cancelling")).toBool());
    QVERIFY(!row.value(QStringLiteral("slotQueued")).toBool());
}

// B3-C: a background run belongs to the session that spawned it. After
// resetSession() the old run must disappear from the panel/report APIs and,
// critically, must not be re-injected as a subagent_task trace node into the
// fresh session's history by the 250ms coalescing tick.
void TestAgent::backgroundTasksStayBoundToTheirSession() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();

    QMutex gatesMutex;
    QVector<std::shared_ptr<QSemaphore>> gates;
    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        auto gate = std::make_shared<QSemaphore>();
        {
            QMutexLocker lock(&gatesMutex);
            gates.append(gate);
        }
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("old report")));
        return service;
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("old session work")},
                    {QStringLiteral("background"), true}}));
    llm.script.append(textResponse(QStringLiteral("old ack")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn in old session"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    const QString oldSession = agent.currentSessionId();
    QVERIFY(!oldSession.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(agent.backgroundSubAgentActiveCount(), 1, 5000);
    const QVariantList oldRows = agent.backgroundSubAgents();
    QCOMPARE(oldRows.size(), 1);
    const QString oldTaskId =
        oldRows.first().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!oldTaskId.isEmpty());

    // Leave the session. The hub still holds the (now cancelling) entry, but
    // the new session must not see it.
    agent.resetSession();
    QVERIFY(agent.currentSessionId().isEmpty());
    QVERIFY2(agent.backgroundSubAgents().isEmpty(),
             "old-session background task leaked into the fresh session panel");
    QVERIFY2(agent.backgroundSubAgentActiveCount() == 0,
             "old-session task counted as active in the fresh session");
    QVERIFY2(agent.backgroundSubAgentReport(oldTaskId).isEmpty(),
             "old-session report must not be readable from the fresh session");
    QVERIFY(!agent.cancelBackgroundSubAgent(oldTaskId));

    // Start a genuinely new session and let the coalescing tick run.
    llm.reset();
    llm.script.append(textResponse(QStringLiteral("fresh ok")));
    QSignalSpy freshSpy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("fresh session"));
    QTRY_COMPARE_WITH_TIMEOUT(freshSpy.count(), 1, 15000);
    QVERIFY(!agent.currentSessionId().isEmpty());
    QVERIFY(agent.currentSessionId() != oldSession);

    QTest::qWait(700);   // > one 250ms background notification tick
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        QVERIFY2(node.value(QStringLiteral("type")).toString()
                     != QLatin1String("subagent_task"),
                 "old-session background task injected a trace node into the "
                 "new session");
    }
    QVERIFY(agent.backgroundSubAgents().isEmpty());

    // Unblock and drain the parked old worker for a clean teardown.
    {
        QMutexLocker lock(&gatesMutex);
        for (const auto& gate : gates) gate->release();
    }
}

// B3-C: the hub owns a private pool. Destroying it from a non-pool thread
// must cancel and return within a bounded window even when a task ignores
// the cancellation token; the old implementation called waitForDone() with
// no timeout and hung forever.
void TestAgent::backgroundHubDestructorDoesNotHang() {
    QSemaphore gate;                 // deliberately never signalled until the end
    std::atomic<bool> started{false};
    std::atomic<bool> destroyed{false};
    std::thread terminator([&]() {
        auto hub = std::make_shared<BackgroundTaskHub>();
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        hub->createTask(QStringLiteral("uncooperative"), QStringLiteral("explore"),
                        cancelled, [&gate, &started](const QString&) {
            started.store(true);
            gate.acquire();   // ignores cancellation on purpose
        });
        QElapsedTimer spin;
        spin.start();
        while (!started.load() && spin.elapsed() < 3000) QThread::msleep(5);
        hub.reset();          // dtor runs on this (non-pool) thread
        destroyed.store(true);
    });

    const bool finished = QTest::qWaitFor([&]() { return destroyed.load(); }, 4000);
    gate.release();           // let the parked worker exit before joining
    terminator.join();

    QVERIFY2(finished,
             "BackgroundTaskHub dtor blocked waiting for an uncooperative task");
}

// docs/35 P4.5: ProcessCard 的停止芯片 → AgentVM.cancelSubAgent → AIAgent
// 单节点令牌。QML 点击本身由 test_process_card_qml 覆盖，这里锁 VM 转发。
void TestAgent::subagentStopChipThroughViewModel() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    auto enteredFlag = std::make_shared<std::atomic<bool>>(false);
    auto releaseGate = std::make_shared<QSemaphore>();
    fixture.tool = registerSpawnTool(fixture, [&](RecordingFakeService& sub) {
        sub.enteredFlag = enteredFlag.get();
        sub.gate = releaseGate.get();
        sub.script.append(textResponse(QStringLiteral("slow report")));
    });
    AgentViewModel vm(nullptr);
    vm.setAgent(&agent);

    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("slow probe")}}));
    llm.script.append(textResponse(QStringLiteral("wrapped up")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("cancel via card"));
    QTRY_VERIFY(enteredFlag->load());

    QString nodeId;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() == QLatin1String("tool_call")
                && node.value("title").toString().startsWith(
                    QLatin1String("spawn_agent"))
                && node.value("status").toString() == QLatin1String("running")) {
            nodeId = node.value("id").toString();
            break;
        }
    }
    QVERIFY2(!nodeId.isEmpty(), "没有找到运行中的 spawn_agent 节点");
    QVERIFY(vm.cancelSubAgent(nodeId));
    // 同一个节点重复点按仍返回 true（令牌存在），未知节点返回 false。
    QVERIFY(!vm.cancelSubAgent(QStringLiteral("no-such-node")));

    releaseGate->release();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
}

// 真实网络栈下的两条中止路径压力回归：中断（取消旗标在请求在飞时翻转，
// Runner 循环退出但不调 cancel，析构函数里才 abort 在飞请求）与墙钟超时
// （cancel 后立刻 delete 服务）。服务指向一个永不响应的本地 TCP 端口，
// 把请求钉在在飞状态——同步假桩覆盖不到这条时序。
void TestAgent::subagentInflightAbortStressRealNetwork() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTcpSocket* pending = nullptr;
    connect(&server, &QTcpServer::newConnection, &server, [&]() {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            if (pending) pending->deleteLater();
            pending = socket;   // 请求永远得不到响应，保持 in-flight
        }
    });

    LlmProfileConfig config;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    config.apiKey = QStringLiteral("test-key");
    config.model = QStringLiteral("test-model");

    for (int i = 0; i < 30; ++i) {
        const bool interruptCase = (i % 2) == 0;
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        if (interruptCase) {
            // Runner 的 QEventLoop 在本线程自旋，单发定时器在其中触发。
            QTimer::singleShot(150, nullptr, [flag = cancelled.get()]() {
                flag->store(true);
            });
        }
        SubAgentRequest request;
        request.goal = QStringLiteral("never completes");
        request.maxSteps = 2;
        // timeoutMs == 0 = 无墙钟预算（中断用例只靠取消旗标收场）。
        request.timeoutMs = interruptCase ? 0 : 300;
        request.cancelled = cancelled;

        IAIService* service = LlmProtocolRegistry::createService(
            QStringLiteral("openai-chat"), nullptr);
        QVERIFY(service != nullptr);
        service->configure(config);
        const SubAgentReport report = SubAgentRunner::run(service, request);
        // 与 SubAgentTool::execute 的所有权语义一致：创建与销毁都在本线程。
        delete service;

        if (interruptCase)
            QVERIFY2(report.stop == SubAgentReport::Stop::Cancelled,
                     qPrintable(QStringLiteral("iter %1: stop=%2 text=%3")
                                    .arg(i).arg(int(report.stop)).arg(report.text)));
        else
            QVERIFY2(report.stop == SubAgentReport::Stop::TimedOut,
                     qPrintable(QStringLiteral("iter %1: stop=%2 text=%3")
                                    .arg(i).arg(int(report.stop)).arg(report.text)));
    }
    if (pending) pending->deleteLater();
}

// docs/35 P4.1: the sub-agent had no context budget at all — a long run
// eventually exceeded the model window and the provider rejected the request.
// The wire copy is now compressed per round (oldest assistant→tool rounds
// dropped), while the full transcript still goes to the evidence artifact.
void TestAgent::subagentCompressesContextOnTheWire() {
    QTemporaryDir artifacts;
    QVERIFY(artifacts.isValid());
    SubAgentRunner::setArtifactsRootOverride(artifacts.path());

    FakeAIService fake;
    for (int i = 0; i < 6; ++i) {
        LLMResponse r = toolResponse(QStringLiteral("test_large_output"),
                                     QJsonObject{{"round", i}});
        r.toolCalls[0].id = QStringLiteral("call_%1").arg(i);
        fake.script.append(r);
    }
    fake.script.append(textResponse(QStringLiteral("done")));

    auto probe = std::make_shared<LargeOutputProbeTool>();
    SubAgentRequest request;
    request.goal = QStringLiteral("gather evidence");
    request.tools.append(SubAgentToolEntry{probe->definition(), probe});
    request.contextLimit = 1200;
    QStringList contextEvents;
    request.progress = [&contextEvents](const SubAgentProgress& p) {
        if (p.phase == QLatin1String("context"))
            contextEvents.append(p.detail);
    };

    const SubAgentReport report = SubAgentRunner::run(&fake, request);
    SubAgentRunner::setArtifactsRootOverride(QString());

    QVERIFY2(report.ok, qPrintable(report.text));
    QCOMPARE(report.steps, 7);
    QVERIFY2(!contextEvents.isEmpty(),
             "compression happened without a progress event");
    QVERIFY(contextEvents.last().contains(
        QStringLiteral("earlier round(s) omitted")));

    // Every request the model saw is a valid wire history: header intact, no
    // orphan tool message, and never over the configured window.
    QCOMPARE(fake.seenMessages.size(), 7);
    bool sawCompressed = false;
    for (const QJsonArray& wire : fake.seenMessages) {
        QVERIFY2(noDanglingToolCalls(wire),
                 "compression broke tool_call/tool pairing");
        QCOMPARE(wire.first().toObject().value("role").toString(),
                 QStringLiteral("system"));
        QCOMPARE(wire.at(1).toObject().value("role").toString(),
                 QStringLiteral("user"));
        QVERIFY2(TokenEstimator::estimateTokens(wire) <= 1200,
                 qPrintable(QStringLiteral("wire uses %1 tokens")
                                .arg(TokenEstimator::estimateTokens(wire))));
        for (const auto& entry : wire) {
            if (entry.toObject().value("content").toString()
                    .contains(QStringLiteral("earlier tool round(s)")))
                sawCompressed = true;
        }
    }
    QVERIFY2(sawCompressed, "history was never compressed on the wire");

    // The newest request no longer carries the oldest round...
    bool oldestOnWire = false;
    for (const auto& entry : fake.seenMessages.last()) {
        for (const auto& call : entry.toObject()
                                    .value("tool_calls").toArray()) {
            if (call.toObject().value("id").toString()
                    == QLatin1String("call_0"))
                oldestOnWire = true;
        }
    }
    QVERIFY2(!oldestOnWire, "oldest round should have been dropped");

    // ...but the evidence artifact keeps the complete transcript.
    QVERIFY(!report.artifactPath.isEmpty());
    QFile artifact(report.artifactPath);
    QVERIFY(artifact.open(QIODevice::ReadOnly));
    const QByteArray evidence = artifact.readAll();
    QVERIFY(evidence.contains("call_0"));
    QVERIFY(evidence.contains("test_large_output"));
}

// B8: profile 解析失败时 contextLimit 为 0（未知），Runner 不能把超窗请求
// 原样发给 provider。用保守默认上限压缩 wire 副本：大量 CJK tool 结果把
// 历史推过 128k tokens，断言每一次请求的 messages 估算都不超上限，且有
// 压缩进度事件（旧实现 contextLimit=0 完全跳过压缩）。
void TestAgent::subagentCompressesWithFallbackLimit() {
    QTemporaryDir artifacts;
    QVERIFY(artifacts.isValid());
    SubAgentRunner::setArtifactsRootOverride(artifacts.path());

    constexpr int kRounds = 36;
    FakeAIService fake;
    for (int i = 0; i < kRounds; ++i) {
        LLMResponse r = toolResponse(QStringLiteral("test_huge_output"),
                                     QJsonObject{{QStringLiteral("round"), i}});
        r.toolCalls[0].id = QStringLiteral("call_%1").arg(i);
        fake.script.append(r);
    }
    fake.script.append(textResponse(QStringLiteral("done")));

    auto probe = std::make_shared<HugeOutputProbeTool>();
    SubAgentRequest request;
    request.goal = QStringLiteral("gather a very large evidence set");
    request.tools.append(SubAgentToolEntry{probe->definition(), probe});
    request.maxSteps = kRounds + 1;
    request.contextLimit = 0;   // unknown profile window
    QStringList contextEvents;
    request.progress = [&contextEvents](const SubAgentProgress& p) {
        if (p.phase == QLatin1String("context"))
            contextEvents.append(p.detail);
    };

    const SubAgentReport report = SubAgentRunner::run(&fake, request);
    SubAgentRunner::setArtifactsRootOverride(QString());

    QVERIFY2(report.ok, qPrintable(report.text));
    QVERIFY2(!contextEvents.isEmpty(),
             "contextLimit=0 skipped compression instead of using the fallback");
    QCOMPARE(fake.seenMessages.size(), kRounds + 1);

    bool sawMarker = false;
    for (const QJsonArray& wire : fake.seenMessages) {
        const int tokens = TokenEstimator::estimateTokens(wire);
        QVERIFY2(tokens <= 128000,
                 qPrintable(QStringLiteral("fallback wire uses %1 tokens")
                                .arg(tokens)));
        for (const auto& entry : wire) {
            if (entry.toObject().value(QStringLiteral("content")).toString()
                    .contains(QStringLiteral("earlier tool round(s)")))
                sawMarker = true;
        }
    }
    QVERIFY2(sawMarker, "fallback compression never dropped an old round");
}

// B9: provider 未上报 usage（prompt/completion/tokensUsed 全 0）时，Runner
// 必须用 TokenEstimator 回退估算（实际 wire 消息 + schema + 响应正文），
// 并标记 estimated；不能把子代理成本显示成 0。
void TestAgent::subagentWithoutUsageFallsBackToEstimatedTokens() {
    FakeAIService fake;
    fake.script.append(textResponse(QStringLiteral("no usage payload")));

    auto probe = std::make_shared<SubAgentProbeTool>();
    SubAgentRequest request;
    request.goal = QStringLiteral("estimate my cost");
    request.tools.append(SubAgentToolEntry{probe->definition(), probe});

    const SubAgentReport report = SubAgentRunner::run(&fake, request);
    QVERIFY2(report.ok, qPrintable(report.text));
    QVERIFY2(report.tokensUsed > 0,
             "missing usage left the sub-agent cost at zero");
    QVERIFY2(!report.usage.usageReported, "fallback must not fake reported usage");
    QCOMPARE(report.usage.inputTokens, 0);
    QCOMPARE(report.usage.outputTokens, 0);

    QCOMPARE(fake.seenMessages.size(), 1);
    const int expected = TokenEstimator::estimateTokens(fake.seenMessages.last())
                       + TokenEstimator::estimateToolsSchema(fake.seenTools.last())
                       + TokenEstimator::estimateTokens(
                             QStringLiteral("no usage payload"));
    QCOMPARE(report.tokensUsed, expected);

    // 结构化报告同步暴露 estimated，供面板/过程卡片加「≈」。
    const QJsonObject usage =
        SubAgentTool::reportJson(report).value(QStringLiteral("usage")).toObject();
    QVERIFY2(usage.value(QStringLiteral("estimated")).toBool(),
             "reportJson lost the estimated marker");
    QCOMPARE(usage.value(QStringLiteral("reported")).toBool(), false);
}

// docs/35 P4.2: five read-only calls in one round used to execute strictly
// one after another. They now overlap (up to kSubToolParallelCap), while the
// tool messages still land in call order.
void TestAgent::subagentReadOnlyToolsRunConcurrently() {
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    const auto probe = std::make_shared<SleepProbeTool>(
        QStringLiteral("test_sleep"), 120, &active, &peak);

    LLMResponse round;
    round.success = true;
    for (int i = 0; i < 5; ++i) {
        ToolCallInfo call;
        call.id = QStringLiteral("call_%1").arg(i);
        call.name = QStringLiteral("test_sleep");
        call.arguments = QJsonObject{{"n", i}};
        round.toolCalls.append(call);
    }
    FakeAIService fake;
    fake.script.append(round);
    fake.script.append(textResponse(QStringLiteral("report")));

    SubAgentRequest request;
    request.goal = QStringLiteral("run probes");
    request.maxSteps = 2;   // one tool round + forced final
    request.tools.append(SubAgentToolEntry{probe->definition(), probe});

    const SubAgentReport report = SubAgentRunner::run(&fake, request);
    QVERIFY2(report.ok, qPrintable(report.text));
    QCOMPARE(report.steps, 2);

    // More than one in flight (the old loop was fully serial), never above
    // the cap. 5 calls therefore run as 3 + 2.
    const int observed = peak.load();
    QVERIFY2(observed >= 2,
             qPrintable(QStringLiteral("peak concurrency %1").arg(observed)));
    QVERIFY(observed <= SubAgentRunner::kSubToolParallelCap);

    // Results in original call order, each carrying its own argument.
    const QJsonArray second = fake.seenMessages.at(1);
    int expected = 0;
    for (const auto& entry : second) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() != QLatin1String("tool")) continue;
        QCOMPARE(msg.value("tool_call_id").toString(),
                 QStringLiteral("call_%1").arg(expected));
        QCOMPARE(msg.value("content").toString(),
                 QStringLiteral("done %1").arg(expected));
        ++expected;
    }
    QCOMPARE(expected, 5);
}

// docs/35 P4.6: 停止必须打断正在同步执行的工具。ITool 默认的
// execute(args, cancelled) 只在进入前检查令牌，web_search/http_fetch 这类
// 长调用自己轮询 cancel() 旗标；Runner 以前从不调用 cancel()，于是后台
// 面板永远停在「正在停止…」。看门线程修好后，这里锁住端到端时延。
void TestAgent::subagentCancellationAbortsInflightTool() {
    auto probe = std::make_shared<CancelAwareBlockingProbeTool>();

    LLMResponse round;
    round.success = true;
    ToolCallInfo call;
    call.id = QStringLiteral("call_block");
    call.name = QStringLiteral("test_blocking");
    round.toolCalls.append(call);
    FakeAIService fake;
    fake.script.append(round);
    fake.script.append(textResponse(QStringLiteral("unreachable")));

    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    // 工具执行期间 runImpl 阻塞等待 future，没有事件循环，定时器不可用。
    std::thread canceller([flag = cancelled.get()]() {
        QThread::msleep(300);
        flag->store(true);
    });

    SubAgentRequest request;
    request.goal = QStringLiteral("block until stopped");
    request.maxSteps = 2;
    request.tools.append(SubAgentToolEntry{probe->definition(), probe});
    request.cancelled = cancelled;

    QElapsedTimer elapsed;
    elapsed.start();
    const SubAgentReport report = SubAgentRunner::run(&fake, request);
    const qint64 totalMs = elapsed.elapsed();
    canceller.join();

    QVERIFY2(report.stop == SubAgentReport::Stop::Cancelled,
             qPrintable(QStringLiteral("stop=%1 text=%2")
                            .arg(int(report.stop)).arg(report.text)));
    QVERIFY2(probe->cancelObserved.load(),
             "看门线程没有把取消传到正在执行的工具");
    // 300ms 时翻转令牌；看门线程 100ms 一醒，工具 10ms 一片，2s 足够宽松。
    QVERIFY2(totalMs < 2000,
             qPrintable(QStringLiteral("cancellation took %1ms").arg(totalMs)));
    QVERIFY2(probe->cancelLatencyMs.load() >= 0
                 && probe->cancelLatencyMs.load() < 1500,
             qPrintable(QStringLiteral("tool saw cancel after %1ms")
                            .arg(probe->cancelLatencyMs.load())));
}

// docs/35 P4.2: a mixed round [read_file, propose_edit, read_file] must keep
// the wire order; propose_edit stays on the run thread (thread-local proposal
// sink) while the reads may execute concurrently.
void TestAgent::subagentMixedRoundKeepsCallOrder() {
    QTemporaryDir dir(testDirTemplate());
    QVERIFY(dir.isValid());
    const QByteArray original("// header\nint main() { return 1; }\n// footer\n");
    {
        QFile f(dir.filePath(QStringLiteral("app.cpp")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(original), qint64(original.size()));
    }
    agent.setActiveProject(QStringLiteral("p42"), QStringLiteral("Demo"),
                           dir.path());

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse round;
        round.success = true;
        const auto add = [&round](const QString& id, const QString& name,
                                  const QJsonObject& args) {
            ToolCallInfo call;
            call.id = id;
            call.name = name;
            call.arguments = args;
            round.toolCalls.append(call);
        };
        add(QStringLiteral("call_0"), QStringLiteral("read_file"),
            QJsonObject{{"path", "app.cpp"}});
        add(QStringLiteral("call_1"), QStringLiteral("propose_edit"),
            QJsonObject{
                {"path", "app.cpp"},
                {"rationale", "make main return success"},
                {"edits", QJsonArray{QJsonObject{
                    {"find", "int main() { return 1; }"},
                    {"replace", "int main() { return 0; }"}}}}});
        add(QStringLiteral("call_2"), QStringLiteral("read_file"),
            QJsonObject{{"path", "app.cpp"}});
        sub.script.append(round);
        sub.script.append(textResponse(QStringLiteral("mixed done")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("inspect and propose")},
                    {"role", QStringLiteral("coder")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("mixed round"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    {
        QMutexLocker lock(&fixture.mutex);
        QVERIFY2(fixture.subSeenMessages.size() >= 2, "no follow-up round");
        QStringList ids;
        QStringList contents;
        for (const auto& entry : fixture.subSeenMessages.at(1)) {
            const QJsonObject msg = entry.toObject();
            if (msg.value("role").toString() != QLatin1String("tool")) continue;
            ids.append(msg.value("tool_call_id").toString());
            contents.append(msg.value("content").toString());
        }
        const QStringList expectedIds{
            QStringLiteral("call_0"), QStringLiteral("call_1"),
            QStringLiteral("call_2")};
        QCOMPARE(ids, expectedIds);
        QCOMPARE(contents.size(), 3);
        QVERIFY(contents.at(0).contains(QStringLiteral("int main")));
        QVERIFY(contents.at(1).contains(QStringLiteral("Proposed 1 edit(s)")));
        QVERIFY(contents.at(2).contains(QStringLiteral("int main")));
    }

    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY2(report.contains(QStringLiteral("## PROPOSALS")), qPrintable(report));
    QVERIFY(report.contains(QStringLiteral("make main return success")));

    // 提案从未落盘。
    QFile f(dir.filePath(QStringLiteral("app.cpp")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), original);
    f.close();
    agent.setActiveProject(QString(), QString(), QString());
}

void TestAgent::subagentConcurrencyGateQueues() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setMaxParallel(1);

    QMutex gatesMutex;
    QVector<std::shared_ptr<QSemaphore>> gates;
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        auto gate = std::make_shared<QSemaphore>();
        {
            QMutexLocker lock(&gatesMutex);
            gates.append(gate);
        }
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("report ok")));
        return service;
    });
    // 线程安全的 gates.size() 读取（工厂在池线程写入）。
    auto gateCount = [&]() -> int {
        QMutexLocker lock(&gatesMutex);
        return gates.size();
    };
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    QVector<QString> results(2);
    auto runOne = [&fixture, &results](int index, const QString& goal) {
        const ToolResult result = fixture.tool->execute(
            QJsonObject{{"goal", goal}});
        results[index] = result.success ? result.output : result.error;
    };
    auto future1 = QtConcurrent::run(runOne, 0, QStringLiteral("first"));
    // 第一个进入并挂起（占住唯一槽位）。
    QTRY_COMPARE(gateCount(), 1);
    auto future2 = QtConcurrent::run(runOne, 1, QStringLiteral("second"));
    // 排队窗口内不允许第二个并发进入。
    QTest::qWait(400);
    QCOMPARE(gateCount(), 1);
    // 放行第一个 → 槽位释放 → 第二个才能进入。
    QMutexLocker lock(&gatesMutex);
    gates.first()->release();
    lock.unlock();
    QTRY_COMPARE(gateCount(), 2);
    {
        QMutexLocker lock2(&gatesMutex);
        gates.last()->release();
    }
    future1.waitForFinished();
    future2.waitForFinished();
    QVERIFY(results.at(0).contains(QStringLiteral("[Sub-agent report]")));
    QVERIFY(results.at(1).contains(QStringLiteral("[Sub-agent report]")));
}

// docs/35 P4.5：并发上限占满时，排队的 spawn_agent 要在标题上显示
// "queued"，而不是一直停在初始的 "spawn_agent" 或伪造步号。
void TestAgent::subagentQueuedProgressReported() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setMaxParallel(1);

    QMutex gatesMutex;
    QVector<std::shared_ptr<QSemaphore>> gates;
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        auto gate = std::make_shared<QSemaphore>();
        {
            QMutexLocker lock(&gatesMutex);
            gates.append(gate);
        }
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("report ok")));
        return service;
    });
    auto gateCount = [&]() -> int {
        QMutexLocker lock(&gatesMutex);
        return gates.size();
    };
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    LLMResponse round;
    round.success = true;
    const auto addSpawn = [&round](const QString& id, const QString& goal) {
        ToolCallInfo call;
        call.id = id;
        call.name = QStringLiteral("spawn_agent");
        call.arguments = QJsonObject{{"goal", goal}};
        round.toolCalls.append(call);
    };
    addSpawn(QStringLiteral("call_qa"), QStringLiteral("first goal"));
    addSpawn(QStringLiteral("call_qb"), QStringLiteral("second goal"));
    llm.reset();
    llm.script.append(round);
    llm.script.append(textResponse(QStringLiteral("done")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("queue a sub-agent"));
    // 第一个占住唯一槽位，第二个在 acquireSlot 里排队。
    QTRY_COMPARE(gateCount(), 1);
    QTRY_VERIFY(traceHas(agent.traceModel(), QStringLiteral("tool_call"),
                         QStringLiteral("spawn_agent · queued")));

    {
        QMutexLocker lock(&gatesMutex);
        gates.first()->release();
    }
    QTRY_COMPARE(gateCount(), 2);
    {
        QMutexLocker lock(&gatesMutex);
        gates.last()->release();
    }
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
}

// 2026-11-03 回归：后台 spawn_agent 的池线程一启动 hub 就把它标成 running，
// 但 Runner 可能还卡在 acquireSlot 等槽位。若不在 onQueued 里写 hub，面板
// 会把「占线程但没在跑」的任务全算作进行中（4 个任务显示 4 进行中、0 排队）。
void TestAgent::backgroundSlotQueueSurfacesThroughTool() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    auto tool = std::make_shared<SubAgentTool>();
    tool->setMaxParallel(1);

    QMutex gatesMutex;
    QVector<std::shared_ptr<QSemaphore>> gates;
    tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        auto gate = std::make_shared<QSemaphore>();
        {
            QMutexLocker lock(&gatesMutex);
            gates.append(gate);
        }
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("bg done")));
        return service;
    });
    auto gateCount = [&]() -> int {
        QMutexLocker lock(&gatesMutex);
        return gates.size();
    };
    tool->refreshCatalog(false);

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);

    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const auto spawn = [&tool](const QString& goal) -> QString {
        const ToolResult ack = tool->execute(
            QJsonObject{{QStringLiteral("goal"), goal},
                        {QStringLiteral("background"), true}});
        if (!ack.success) return {};
        const QRegularExpressionMatch match = idRe.match(ack.output);
        return match.hasMatch() ? match.captured(1) : QString();
    };
    const auto rowFor = [&hub](const QString& id) -> QVariantMap {
        for (const QVariant& row : hub->snapshots()) {
            if (row.toMap().value(QStringLiteral("id")).toString() == id)
                return row.toMap();
        }
        return {};
    };

    const QString first = spawn(QStringLiteral("first goal"));
    QVERIFY(!first.isEmpty());
    // 第一个真正进了服务（占住唯一槽位）。
    QTRY_COMPARE(gateCount(), 1);

    const QString second = spawn(QStringLiteral("second goal"));
    QVERIFY(!second.isEmpty());
    // hub 状态已是 running（池线程启动了），但 runner 在等槽位：行上必须
    // 暴露 slotQueued，否则面板会误报进行中。
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        return rowFor(second).value(QStringLiteral("slotQueued")).toBool();
    }(), 3000);
    // acquireSlot 每 250ms 回调一次，hub 去重后只保留一条 queued 事件。
    int queuedEvents = 0;
    for (const QVariant& item :
         rowFor(second).value(QStringLiteral("progressTail")).toList()) {
        if (item.toMap().value(QStringLiteral("phase")).toString()
            == QLatin1String("queued"))
            ++queuedEvents;
    }
    QCOMPARE(queuedEvents, 1);
    QCOMPARE(hub->stateLabel(first), QStringLiteral("running"));

    // 停在排队里的任务也能立刻取消，不必等槽位。
    QVERIFY(hub->cancel(second));
    QTRY_COMPARE(hub->stateLabel(second), QStringLiteral("cancelled"));
    QCOMPARE(hub->stateLabel(first), QStringLiteral("running"));

    {
        QMutexLocker lock(&gatesMutex);
        gates.first()->release();
    }
    QTRY_COMPARE_WITH_TIMEOUT(hub->stateLabel(first), QStringLiteral("completed"),
                              5000);
    QCOMPARE(gateCount(), 1);   // 第二个从未创建服务
}

// docs/35 P4.5：卡片上的「停止」只翻转目标节点的旗标，另一个子代理继续
// 跑完并把报告回灌；主任务照常收尾。
void TestAgent::subagentCancelOnlyTargetNode() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();

    // 慢速只读工具进目录快照，让两个子代理都保持运行足够久，等我们按下
    // 「停止」。120ms × 6 轮 ≈ 720ms。
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    auto sleepTool = std::make_shared<SleepProbeTool>(
        QStringLiteral("test_slow_remove"), 120, &active, &peak);
    ToolNameGuard slowGuard{sleepTool->definition().name};
    ToolRegistry::instance().registerTool(sleepTool);

    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(&fixture.mutex,
                                                 &fixture.subSeenTools,
                                                 &fixture.subSeenMessages);
        for (int i = 0; i < 6; ++i)
            service->script.append(toolResponse(
                QStringLiteral("test_slow_remove"), QJsonObject{{"n", i}}));
        service->script.append(textResponse(QStringLiteral("report ok")));
        return service;
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    LLMResponse round;
    round.success = true;
    const auto addSpawn = [&round](const QString& id, const QString& goal) {
        ToolCallInfo call;
        call.id = id;
        call.name = QStringLiteral("spawn_agent");
        call.arguments = QJsonObject{{"goal", goal}};
        round.toolCalls.append(call);
    };
    addSpawn(QStringLiteral("call_ca"), QStringLiteral("alpha goal"));
    addSpawn(QStringLiteral("call_cb"), QStringLiteral("beta goal"));
    llm.reset();
    llm.script.append(round);
    llm.script.append(textResponse(QStringLiteral("wrapped up")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("cancel one sub-agent"));

    // 两个子代理都在运行（各自节点 status=running），取一个节点取消。
    QString targetNode;
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        int running = 0;
        for (const auto& entry : agent.traceModel()) {
            const QJsonObject node = entry.toObject();
            if (node.value("type").toString() != QLatin1String("tool_call"))
                continue;
            if (!node.value("title").toString().startsWith(
                    QLatin1String("spawn_agent")))
                continue;
            if (node.value("status").toString() != QLatin1String("running"))
                continue;
            if (targetNode.isEmpty())
                targetNode = node.value("id").toString();
            ++running;
        }
        return running >= 2;
    })(), 10000);
    QVERIFY(!targetNode.isEmpty());
    QVERIFY(agent.cancelSubAgent(targetNode));
    // 未在飞/已完成/非 spawn 节点：静默 false，不误伤别的子代理。
    QVERIFY(!agent.cancelSubAgent(QStringLiteral("no-such-node")));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QTRY_VERIFY(!agent.isActive());

    // 一个以 Cancelled 收场，另一个拿到完整报告。

    int cancelled = 0;
    int reports = 0;
    for (const auto& entry : agent.messages()) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() != QLatin1String("tool")) continue;
        const QString content = msg.value("content").toString();
        if (content.contains(QStringLiteral("Reason: Sub-agent cancelled")))
            ++cancelled;
        if (content.contains(QStringLiteral("[Sub-agent report]"))) ++reports;
    }
    QCOMPARE(cancelled, 1);
    QCOMPARE(reports, 1);
    // 任务结束后节点映射已清理。
    QVERIFY(!agent.cancelSubAgent(targetNode));
}

// docs/32 P2: 子代理每轮进度以事件回传，落到派生调用的 tool_call 节点标题上。
void TestAgent::subagentProgressUpdatesTraceNode() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        // 三轮工具调用（参数不同以绕开重复轮守卫）+ 预算 4 让第 4 步成为
        // 强制收尾轮：验证摘要跨轮累积 ×3（×2→×3 的计数合并路径）与
        // "final answer" 的追加语义。
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"),
                                       QJsonObject{{"round", 2}}));
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"),
                                       QJsonObject{{"round", 3}}));
        sub.script.append(textResponse(QStringLiteral("final report text")));
    });
    fixture.tool->setMaxSteps(4);
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("probe with progress")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("watch a sub-agent"));
    QTRY_COMPARE(spy.count(), 1);

    // 摘要跨轮累积：probe 跑了三次 → 标题聚合成 ×3（覆盖 ×2→×3 的计数
    // 增量合并），强制收尾轮的 "final answer" 追加在摘要之后而非覆盖它。
    // 终态标题即为最后一次事件。
    QTRY_VERIFY(traceHas(agent.traceModel(), QStringLiteral("tool_call"),
                         QStringLiteral("step 4/4 · test_sub_probe×3 · final answer")));
    QVERIFY(traceHas(agent.traceModel(), QStringLiteral("tool_call"),
                     QStringLiteral("spawn_agent")));

    // 过程可视化：assistant/tool 进度事件累积在节点元数据 subagentLog——
    // 三轮工具调用各一条（名(参数) ⇒ 结果），最终答案一条。
    const auto fetchLog = [](const QJsonArray& trace) -> QJsonArray {
        for (const auto& entry : trace) {
            const QJsonObject node = entry.toObject();
            if (node.value("type").toString() == QLatin1String("tool_call")
                    && node.value("title").toString().startsWith(QLatin1String("spawn_agent")))
                return node.value("subagentLog").toArray();
        }
        return {};
    };
    QTRY_COMPARE(fetchLog(agent.traceModel()).size(), 4);
    const QJsonArray log = fetchLog(agent.traceModel());
    QCOMPARE(log.at(0).toObject().value("phase").toString(), QStringLiteral("tool"));
    QVERIFY(log.at(0).toObject().value("text").toString()
                .startsWith(QStringLiteral("test_sub_probe(")));
    QVERIFY(log.at(0).toObject().value("text").toString()
                .contains(QStringLiteral("probe-evidence")));
    QCOMPARE(log.at(1).toObject().value("phase").toString(), QStringLiteral("tool"));
    QVERIFY(log.at(1).toObject().value("text").toString()
                .contains(QStringLiteral("\"round\":2")));
    QCOMPARE(log.at(2).toObject().value("phase").toString(), QStringLiteral("tool"));
    QVERIFY(log.at(2).toObject().value("text").toString()
                .contains(QStringLiteral("\"round\":3")));
    QCOMPARE(log.at(3).toObject().value("phase").toString(), QStringLiteral("assistant"));
    QCOMPARE(log.at(3).toObject().value("text").toString(),
             QStringLiteral("final report text"));
}

// docs/32 P2: 审计行的 request_id 即主请求（parent 归因），detail 以 JSON
// 携带子代理自身的 steps/tokens。
void TestAgent::subagentAuditCarriesSubagentStats() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("quick report")));
    });
    llm.reset();
    LLMResponse scripted = textResponse(QStringLiteral("done"));
    scripted.tokensUsed = 42;
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("audit me")}}));
    llm.script.append(scripted);
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn for audit"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    bool found = false;
    const QJsonArray audit = memory.auditLog(50);
    for (const auto& entry : audit) {
        const QJsonObject row = entry.toObject();
        if (row.value("event").toString() != QLatin1String("tool_execute")
                || row.value("toolName").toString() != QLatin1String("spawn_agent"))
            continue;
        const QJsonObject detail = QJsonDocument::fromJson(
            row.value("detail").toString().toUtf8()).object();
        QVERIFY(detail.contains(QStringLiteral("subagent_steps")));
        QVERIFY(detail.contains(QStringLiteral("subagent_tokens")));
        QCOMPARE(detail.value("subagent_steps").toInt(), 1);
        // 主请求归因：审计行 request_id 是派生调用的主请求，恒 > 0。
        QVERIFY(row.value("requestId").toDouble() > 0);
        found = true;
    }
    QVERIFY2(found, "spawn_agent tool_execute audit row missing");
    agent.setMemory(nullptr);
}

// docs/43 O2: findings + classified usage reach the trace node (light
// projection) and the audit detail, while the report text stays readable.
void TestAgent::subagentReportCarriesFindingsAndUsage() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"),
                                       QJsonObject{{"q", QStringLiteral("x")}}));
        LLMResponse finalReport = textResponse(QStringLiteral(
            "Two issues were found.\n\n"
            "## FINDINGS\n"
            "- [high] src/a.cpp:12 - null deref after the check\n"
            "- [low] src/b.cpp:40 - stale comment\n"));
        finalReport.promptTokens = 100;
        finalReport.completionTokens = 20;
        finalReport.cachedTokens = 30;
        sub.script.append(finalReport);
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("review the patch")}}));
    llm.script.append(textResponse(QStringLiteral("synthesized")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("review through a sub-agent"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    // Human-readable report keeps the FINDINGS block verbatim.
    const QString reportText = lastToolMessageContent(agent.messages());
    QVERIFY(reportText.contains(QStringLiteral("## FINDINGS")));

    // Trace node carries only the light projection.
    QJsonObject light;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject candidate =
            entry.toObject().value("subagentReport").toObject();
        if (candidate.contains(QStringLiteral("stopCode")))
            light = candidate;
    }
    QVERIFY2(!light.isEmpty(), "trace node missing subagentReport");
    QCOMPARE(light.value("stopCode").toString(), QStringLiteral("completed"));
    QCOMPARE(light.value("findingCount").toInt(), 2);
    const QJsonArray top = light.value("topFindings").toArray();
    QCOMPARE(top.size(), 2);
    QCOMPARE(top.at(0).toObject().value("severity").toString(),
             QStringLiteral("high"));
    QCOMPARE(top.at(0).toObject().value("path").toString(),
             QStringLiteral("src/a.cpp"));
    QCOMPARE(top.at(0).toObject().value("line").toInt(), 12);
    const QJsonObject usage = light.value("usage").toObject();
    QVERIFY(usage.value("reported").toBool());
    QCOMPARE(usage.value("input").toInt(), 100);
    QCOMPARE(usage.value("output").toInt(), 20);
    QCOMPARE(usage.value("cached").toInt(), 30);
    QCOMPARE(usage.value("model").toString(), QStringLiteral("fake-model"));

    // Audit detail prefers metadata and records the structured usage.
    bool found = false;
    for (const auto& entry : memory.auditLog(50)) {
        const QJsonObject row = entry.toObject();
        if (row.value("toolName").toString() != QLatin1String("spawn_agent"))
            continue;
        const QJsonObject detail = QJsonDocument::fromJson(
            row.value("detail").toString().toUtf8()).object();
        if (!detail.contains(QStringLiteral("subagent_usage"))) continue;
        QCOMPARE(detail.value("subagent_stop_code").toString(),
                 QStringLiteral("completed"));
        QCOMPARE(detail.value("subagent_source").toString(),
                 QStringLiteral("spawn"));
        QVERIFY(detail.value("subagent_usage").toObject()
                    .value("reported").toBool());
        found = true;
    }
    QVERIFY2(found, "audit row missing subagent_usage");
    agent.setMemory(nullptr);
}

// docs/43 O2: background reports retrieved via subagent_wait carry the same
// structured data into the audit trail (source=wait).
void TestAgent::subagentWaitAuditCarriesStructuredUsage() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard spawnGuard;
    ToolNameGuard waitGuard{QString::fromLatin1(SubAgentWaitTool::kName)};
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse finalReport = textResponse(QStringLiteral(
            "background findings\n\n"
            "## FINDINGS\n- [medium] src/c.cpp:7 - missing guard\n"));
        finalReport.promptTokens = 55;
        finalReport.completionTokens = 11;
        sub.script.append(finalReport);
    });
    ToolRegistry::instance().registerTool(std::make_shared<SubAgentWaitTool>());

    BackgroundFlowFakeService dynamicLlm;
    struct RestoreLlm {
        AIAgent& agent;
        FakeAIService* original;
        ~RestoreLlm() { agent.setLLMService(original); }
    } restoreLlm{agent, &llm};
    agent.setLLMService(&dynamicLlm);

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("background then wait"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    bool found = false;
    for (const auto& entry : memory.auditLog(50)) {
        const QJsonObject row = entry.toObject();
        if (row.value("toolName").toString() != QLatin1String("subagent_wait"))
            continue;
        const QJsonObject detail = QJsonDocument::fromJson(
            row.value("detail").toString().toUtf8()).object();
        if (!detail.contains(QStringLiteral("subagent_usage"))) continue;
        QCOMPARE(detail.value("subagent_source").toString(),
                 QStringLiteral("wait"));
        QCOMPARE(detail.value("subagent_stop_code").toString(),
                 QStringLiteral("completed"));
        QCOMPARE(detail.value("subagent_usage").toObject()
                     .value("input").toInt(), 55);
        found = true;
    }
    QVERIFY2(found, "subagent_wait audit row missing subagent_usage");
    agent.setMemory(nullptr);
}

// docs/43 O3: batch wait — mode=any returns at the first terminal task,
// mode=all collects every report.
void TestAgent::subagentBatchWaitAnyAndAll() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();

    QMutex gatesMutex;
    QVector<std::shared_ptr<QSemaphore>> gates;
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(
            &fixture.mutex, &fixture.subSeenTools, &fixture.subSeenMessages);
        auto gate = std::make_shared<QSemaphore>();
        {
            QMutexLocker lock(&gatesMutex);
            gates.append(gate);
        }
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("batch report")));
        return service;
    });
    auto gateAt = [&](int index) -> std::shared_ptr<QSemaphore> {
        QMutexLocker lock(&gatesMutex);
        return index < gates.size() ? gates.at(index) : nullptr;
    };
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);
    QSignalSpy finishedSpy(hub.get(), &BackgroundTaskHub::taskFinished);
    SubAgentWaitTool waitTool;

    auto spawn = [&](const QString& goal) {
        return fixture.tool->execute(QJsonObject{
            {QStringLiteral("goal"), goal},
            {QStringLiteral("background"), true}});
    };
    auto idOf = [](const ToolResult& result) {
        static const QRegularExpression re(QStringLiteral("id=(sa-\\d+)"));
        return re.match(result.output).captured(1);
    };
    const ToolResult ack1 = spawn(QStringLiteral("one"));
    const ToolResult ack2 = spawn(QStringLiteral("two"));
    QVERIFY2(ack1.success, qPrintable(ack1.error));
    QVERIFY2(ack2.success, qPrintable(ack2.error));
    const QString id1 = idOf(ack1);
    const QString id2 = idOf(ack2);
    QVERIFY(!id1.isEmpty());
    QVERIFY(!id2.isEmpty());
    QVERIFY(id1 != id2);

    // Both workers are parked; release exactly one so "any" has something.
    QTRY_VERIFY_WITH_TIMEOUT(gateAt(1) != nullptr, 5000);
    gateAt(0)->release();

    const ToolResult any = waitTool.execute(QJsonObject{
        {QStringLiteral("ids"), QJsonArray{id1, id2}},
        {QStringLiteral("mode"), QStringLiteral("any")},
        {QStringLiteral("timeout_ms"), 5000}});
    QVERIFY2(any.success, qPrintable(any.error));
    QVERIFY2(any.output.contains(QStringLiteral("1 of 2 finished")),
             qPrintable(any.output));
    QVERIFY(any.output.contains(QStringLiteral("pending:")));
    QCOMPARE(any.metadata.value(QStringLiteral("subagentReports")).toArray().size(), 1);

    gateAt(1)->release();
    const ToolResult all = waitTool.execute(QJsonObject{
        {QStringLiteral("ids"), QJsonArray{id1, id2}},
        {QStringLiteral("mode"), QStringLiteral("all")},
        {QStringLiteral("timeout_ms"), 5000}});
    QVERIFY2(all.success, qPrintable(all.error));
    QVERIFY2(all.output.contains(QStringLiteral("2 of 2 finished")),
             qPrintable(all.output));
    QCOMPARE(all.metadata.value(QStringLiteral("subagentReports")).toArray().size(), 2);

    // B7: 后台终态快照与 taskFinished 摘要都暴露 attempts（本次均 1 次）。
    const QVariantList snapshots = hub->snapshots();
    QCOMPARE(snapshots.size(), 2);
    for (const QVariant& value : snapshots) {
        const QVariantMap snapshot = value.toMap();
        QCOMPARE(snapshot.value(QStringLiteral("attempts")).toInt(), 1);
        QCOMPARE(snapshot.value(QStringLiteral("usage")).toMap()
                     .value(QStringLiteral("attempts")).toInt(), 1);
    }
    QCOMPARE(finishedSpy.count(), 2);
    for (const QList<QVariant>& args : finishedSpy) {
        const QVariantMap summary = args.at(0).toMap();
        QCOMPARE(summary.value(QStringLiteral("attempts")).toInt(), 1);
        QCOMPARE(summary.value(QStringLiteral("usage")).toMap()
                     .value(QStringLiteral("attempts")).toInt(), 1);
    }
}

// docs/43 O3: the gate reports running/queued pressure so the model knows a
// further spawn would only wait.
void TestAgent::subagentSlotPressureIsVisible() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setMaxParallel(1);
    auto gate = std::make_shared<QSemaphore>();
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(
            &fixture.mutex, &fixture.subSeenTools, &fixture.subSeenMessages);
        service->gate = gate.get();
        service->script.append(textResponse(QStringLiteral("slot report")));
        return service;
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);
    SubAgentStatusTool statusTool;
    // P1-3: 注入 spawn 工具引用，status 输出的槽位压力段保持等价。
    statusTool.setSpawnTool(fixture.tool);

    auto spawn = [&](const QString& goal) {
        return fixture.tool->execute(QJsonObject{
            {QStringLiteral("goal"), goal},
            {QStringLiteral("background"), true}});
    };
    auto idOf = [](const ToolResult& result) {
        static const QRegularExpression re(QStringLiteral("id=(sa-\\d+)"));
        return re.match(result.output).captured(1);
    };
    QVERIFY2(spawn(QStringLiteral("first")).success, "first spawn failed");
    const QString second = idOf(spawn(QStringLiteral("second")));
    QVERIFY(!second.isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        const ToolResult status = statusTool.execute(
            QJsonObject{{QStringLiteral("id"), second}});
        return status.success
            && status.output.contains(QStringLiteral("1/1 running"))
            && status.output.contains(QStringLiteral("1 queued"));
    })(), 5000);

    gate->release(2);
    QTRY_COMPARE_WITH_TIMEOUT(hub->activeCount(), 0, 5000);
}

// docs/43 P2-7: 未被 subagent_wait 收割的后台完成也必须留下审计行
// （finishTask → taskFinished → Queued → GUI 线程 subagent_finish）。
void TestAgent::subagentFinishAuditRowWritten() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    // RAII：断言提前返回也必须摘掉 memory——迟到的跨会话 taskFinished
    // 落在已析构的局部库上是 UB（P2-5 保留任务跨用例存活后暴露）。
    struct RestoreMemory {
        AIAgent& agent;
        ~RestoreMemory() { agent.setMemory(nullptr); }
    } restoreMemory{agent};

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse report = textResponse(QStringLiteral("bg audit report"));
        report.tokensUsed = 9;
        report.promptTokens = 6;
        report.completionTokens = 3;
        sub.script.append(report);
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("audit me")},
                    {"background", true}}));
    llm.script.append(textResponse(QStringLiteral("kicked off")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("background with audit"));
    QTRY_COMPARE(spy.count(), 1);

    // 只认本用例自己的任务：跨用例存活的旧任务迟到的 subagent_finish
    // 也会写进本用例的 :memory: 库（P2-5 语义下 memory 是长寿命对象，
    // 生产无此问题；测试按 taskId 过滤）。
    const QVariantList tasks = agent.backgroundSubAgents();
    QCOMPARE(tasks.size(), 1);
    const QString taskId =
        tasks.first().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!taskId.isEmpty());

    bool found = false;
    QJsonObject finishDetail;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const auto& entry : memory.auditLog(50)) {
            const QJsonObject row = entry.toObject();
            if (row.value("event").toString()
                    != QLatin1String("subagent_finish"))
                continue;
            const QJsonObject detail = QJsonDocument::fromJson(
                row.value("detail").toString().toUtf8()).object();
            if (detail.value("taskId").toString() != taskId) continue;
            finishDetail = detail;
            found = true;
        }
        return found;
    }(), 5000);
    QVERIFY2(found, "subagent_finish audit row never appeared");
    QVERIFY(!finishDetail.value("taskId").toString().isEmpty());
    QCOMPARE(finishDetail.value("state").toString(),
             QStringLiteral("completed"));
    QCOMPARE(finishDetail.value("steps").toInt(), 1);
    QCOMPARE(finishDetail.value("tokens").toInt(), 9);
    QCOMPARE(finishDetail.value("usage").toObject()
                 .value("input").toInt(), 6);
    QCOMPARE(finishDetail.value("usage").toObject()
                 .value("reported").toBool(), true);
}

// docs/43 P2-5: 会话切换不再误杀后台任务——运行中任务保留（按会话
// 过滤不串台，TTL 兜底），切走后仍能跑完并留下 state=completed 的
// subagent_finish 审计行（旧行为 cancelAll 全灭，审计只见 cancelled）。
void TestAgent::subagentSurvivesSessionSwitch() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    // RAII：断言提前返回也摘掉 memory（见 subagentFinishAuditRowWritten）。
    struct RestoreMemory {
        AIAgent& agent;
        ~RestoreMemory() { agent.setMemory(nullptr); }
    } restoreMemory{agent};

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    QSemaphore gate;
    fixture.tool = registerSpawnTool(fixture, [&](RecordingFakeService& sub) {
        sub.gate = &gate;
        sub.script.append(textResponse(QStringLiteral("survivor report")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("long survey")},
                    {"background", true}}));
    llm.script.append(textResponse(QStringLiteral("kicked off")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("switch mid-run"));
    QTRY_COMPARE(spy.count(), 1);

    agent.resetSession();
    // 新会话面板不串台：旧会话任务不出现在当前列表里。
    QCOMPARE(agent.backgroundSubAgents().size(), 0);
    // 放行：任务在池线程跑完 → completed 审计（被误杀则是 cancelled）。
    gate.release();
    bool completed = false;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const auto& entry : memory.auditLog(50)) {
            const QJsonObject row = entry.toObject();
            if (row.value("event").toString()
                    != QLatin1String("subagent_finish"))
                continue;
            const QJsonObject detail = QJsonDocument::fromJson(
                row.value("detail").toString().toUtf8()).object();
            if (detail.value("state").toString()
                    == QLatin1String("completed"))
                completed = true;
        }
        return completed;
    }(), 10000);
    QVERIFY2(completed,
             "background task was killed by the session switch (P2-5)");

}

// docs/43 P3-18: 槽位排队有墙钟上限——两个后台任务占满默认 gate 后，
// 第三个排队超时（注入 300ms），stopCode=slot_timeout 且文案可读；
// 占位的两个任务不受影响。
void TestAgent::subagentSlotQueueTimesOut() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    QSemaphore gate;
    fixture.tool = registerSpawnTool(fixture, [&](RecordingFakeService& sub) {
        sub.gate = &gate;
        sub.script.append(textResponse(QStringLiteral("occupied report")));
    });
    fixture.tool->setMaxSlotWaitMs(300);

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);

    for (int i = 0; i < 2; ++i) {
        const ToolResult ack = fixture.tool->execute(QJsonObject{
            {QStringLiteral("goal"),
             QStringLiteral("occupy %1").arg(i)},
            {QStringLiteral("background"), true}});
        QVERIFY2(ack.success, qPrintable(ack.error));
    }
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const ToolResult third = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("queue then time out")},
        {QStringLiteral("background"), true}});
    QVERIFY2(third.success, qPrintable(third.error));
    const QString id = idRe.match(third.output).captured(1);
    QVERIFY(!id.isEmpty());

    SubAgentWaitTool waitTool;
    const ToolResult report = waitTool.execute(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("timeout_ms"), 10000}});
    const QString text = report.success ? report.output : report.error;
    QVERIFY2(text.contains(QStringLiteral("Timed out after")),
             qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral(
                  "waiting for a free sub-agent slot")),
             qPrintable(text));
    const QJsonObject meta = report.metadata
        .value(QStringLiteral("subagentReport")).toObject();
    QCOMPARE(meta.value(QStringLiteral("stopCode")).toString(),
             QStringLiteral("slot_timeout"));
    // B7: 槽位超时没有真正运行 attempt，报告与终态快照都应为 0。
    QCOMPARE(meta.value(QStringLiteral("attempts")).toInt(), 0);
    bool zeroAttemptSeen = false;
    for (const QVariant& value : hub->snapshots()) {
        const QVariantMap snapshot = value.toMap();
        if (snapshot.value(QStringLiteral("id")).toString() != id) continue;
        QCOMPARE(snapshot.value(QStringLiteral("attempts")).toInt(), 0);
        zeroAttemptSeen = true;
    }
    QVERIFY2(zeroAttemptSeen, "timed-out task missing from snapshots");

    gate.release(2);
    QTRY_COMPARE_WITH_TIMEOUT(hub->activeCount(), 0, 5000);
}

// docs/43 P3-15: 单 id 的 subagent_wait 接受 todo_id，报告头回显——
// 后台链路与同步 spawn 的计划对齐提醒同口径（批量路径不回显）。
void TestAgent::subagentWaitEchoesTodoId() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("echoed report")));
    });

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);
    const ToolResult ack = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("todo echo")},
        {QStringLiteral("background"), true}});
    QVERIFY2(ack.success, qPrintable(ack.error));
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const QString id = idRe.match(ack.output).captured(1);
    QVERIFY(!id.isEmpty());

    SubAgentWaitTool waitTool;
    const ToolResult report = waitTool.execute(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("todo_id"), QStringLiteral("t9")},
        {QStringLiteral("timeout_ms"), 10000}});
    QVERIFY2(report.success, qPrintable(report.error));
    QVERIFY2(report.output.contains(QStringLiteral("todo: t9")),
             qPrintable(report.output));
    // 不带 todo_id 的收割不回显。
    const ToolResult plain = waitTool.execute(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("timeout_ms"), 10000}});
    QVERIFY2(plain.success, qPrintable(plain.error));
    QVERIFY(!plain.output.contains(QStringLiteral("todo: ")));
}

// docs/43 P3-17: topFindings 按严重度取前 3（旧行为按报告顺序切片，
// 「前 3」未必是「最严重 3」）；同权重保持报告顺序。
void TestAgent::subagentTopFindingsSortedBySeverity() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    // RAII：断言提前返回也摘掉 memory（见 subagentFinishAuditRowWritten）。
    struct RestoreMemory {
        AIAgent& agent;
        ~RestoreMemory() { agent.setMemory(nullptr); }
    } restoreMemory{agent};

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse finalReport = textResponse(QStringLiteral(
            "Mixed severity.\n\n"
            "## FINDINGS\n"
            "- [low] src/d.cpp:1 - cosmetic\n"
            "- [critical] src/e.cpp:5 - data loss\n"
            "- [high] src/f.cpp:9 - race\n"
            "- [medium] src/g.cpp:2 - leak\n"));
        sub.script.append(finalReport);
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("rank the findings")}}));
    llm.script.append(textResponse(QStringLiteral("synthesized")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("rank findings"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    QJsonObject light;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject candidate =
            entry.toObject().value("subagentReport").toObject();
        if (candidate.contains(QStringLiteral("stopCode")))
            light = candidate;
    }
    QVERIFY2(!light.isEmpty(), "trace node missing subagentReport");
    QCOMPARE(light.value("findingCount").toInt(), 4);
    const QJsonArray top = light.value("topFindings").toArray();
    QCOMPARE(top.size(), 3);
    QCOMPARE(top.at(0).toObject().value("severity").toString(),
             QStringLiteral("critical"));
    QCOMPARE(top.at(1).toObject().value("severity").toString(),
             QStringLiteral("high"));
    QCOMPARE(top.at(2).toObject().value("severity").toString(),
             QStringLiteral("medium"));

}

// docs/43 P3-14: provider 未上报分类用量时（reported=false），审计行
// 不带 subagent_usage——消费方读 subagent_tokens，不误读全 0 为免费。
void TestAgent::subagentAuditOmitsUnreportedUsage() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);
    // RAII：断言提前返回也摘掉 memory（见 subagentFinishAuditRowWritten）。
    struct RestoreMemory {
        AIAgent& agent;
        ~RestoreMemory() { agent.setMemory(nullptr); }
    } restoreMemory{agent};

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        // 不带 token 计量：usageReported 保持 false。
        sub.script.append(textResponse(QStringLiteral("no usage payload")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("silent cost")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn without usage"));
    QTRY_COMPARE(spy.count(), 1);

    bool found = false;
    for (const auto& entry : memory.auditLog(50)) {
        const QJsonObject row = entry.toObject();
        if (row.value("toolName").toString()
                != QLatin1String("spawn_agent"))
            continue;
        const QJsonObject detail = QJsonDocument::fromJson(
            row.value("detail").toString().toUtf8()).object();
        if (!detail.contains(QStringLiteral("subagent_tokens"))) continue;
        QVERIFY2(!detail.contains(QStringLiteral("subagent_usage")),
                 "unreported usage must not land in the audit row");
        found = true;
    }
    QVERIFY2(found, "spawn_agent audit row missing");

}

// docs/43 O3: one batch wait aggregates the cost of every collected run into
// a single audit row, and live task state reaches the system prompt.
void TestAgent::subagentBatchWaitAuditAggregates() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard spawnGuard;
    ToolNameGuard waitGuard{QString::fromLatin1(SubAgentWaitTool::kName)};
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        LLMResponse report = textResponse(QStringLiteral("batch sub report"));
        report.tokensUsed = 7;
        report.promptTokens = 4;
        report.completionTokens = 3;
        sub.script.append(report);
    });
    ToolRegistry::instance().registerTool(std::make_shared<SubAgentWaitTool>());

    BatchWaitFakeService dynamicLlm;
    struct RestoreLlm {
        AIAgent& agent;
        FakeAIService* original;
        ~RestoreLlm() { agent.setLLMService(original); }
    } restoreLlm{agent, &llm};
    agent.setLLMService(&dynamicLlm);

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("two background jobs"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    const QString lastTool = lastToolMessageContent(agent.messages());
    QVERIFY2(lastTool.contains(QStringLiteral("2 of 2 finished")),
             qPrintable(lastTool));

    // Tasks stay in the hub after collection, so the prompt reflects them.
    const QString prompt = agent.effectiveSystemPrompt();
    QVERIFY2(prompt.contains(QStringLiteral("Background sub-agents")),
             qPrintable(prompt.right(600)));

    bool found = false;
    for (const auto& entry : memory.auditLog(50)) {
        const QJsonObject row = entry.toObject();
        if (row.value("toolName").toString() != QLatin1String("subagent_wait"))
            continue;
        const QJsonObject detail = QJsonDocument::fromJson(
            row.value("detail").toString().toUtf8()).object();
        if (!detail.contains(QStringLiteral("subagent_batch_size"))) continue;
        QCOMPARE(detail.value("subagent_batch_size").toInt(), 2);
        QCOMPARE(detail.value("subagent_tokens").toInt(), 14);
        QCOMPARE(detail.value("subagent_usage").toObject()
                     .value("input").toInt(), 8);
        found = true;
    }
    QVERIFY2(found, "batch wait audit row missing");
    agent.setMemory(nullptr);
}

// B4: 批量 wait 中只有一个任务上报分类用量时，聚合结果保留 reported=OR
// （仍可展示分类），同时给出 reportedCount/totalCount/partial，避免把未
// 上报项当成 0 成本；input/output 仍按现有求和口径（未上报项贡献 0）。
// 聚合还要传播 estimated（任一项缺分类用量或用了估算回退）与
// cacheRead/cacheCreation 原值。
void TestAgent::subagentBatchWaitMarksPartialUsage() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    SubAgentFixture fixture;
    SpawnToolGuard spawnGuard;
    ToolNameGuard waitGuard{QString::fromLatin1(SubAgentWaitTool::kName)};
    auto dispatches = std::make_shared<std::atomic<int>>(0);
    fixture.tool = registerSpawnTool(fixture,
        [dispatches](RecordingFakeService& sub) {
            const int index = dispatches->fetch_add(1);
            LLMResponse report = textResponse(
                QStringLiteral("partial batch report"));
            report.tokensUsed = 7;
            if (index == 0) {
                // 第一个任务上报分类用量（含 Anthropic 缓存明细）；
                // 第二个只有聚合 tokens。
                report.promptTokens = 4;
                report.completionTokens = 3;
                report.cachedTokens = 1;
                report.cacheReadTokens = 6;
                report.cacheCreationTokens = 2;
            }
            sub.script.append(report);
        });
    ToolRegistry::instance().registerTool(std::make_shared<SubAgentWaitTool>());

    BatchWaitFakeService dynamicLlm;
    struct RestoreLlm {
        AIAgent& agent;
        FakeAIService* original;
        ~RestoreLlm() { agent.setLLMService(original); }
    } restoreLlm{agent, &llm};
    agent.setLLMService(&dynamicLlm);

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("two jobs, one reports usage"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    QJsonObject light;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject candidate =
            entry.toObject().value("subagentReport").toObject();
        if (candidate.value("batch").toBool()) {
            light = candidate;
            break;
        }
    }
    QVERIFY2(!light.isEmpty(), "trace node missing batch subagentReport");
    const QJsonObject usage = light.value("usage").toObject();
    QVERIFY2(usage.value("reported").toBool(),
             "reported 应保持 OR（有任一任务上报即可展示分类）");
    QCOMPARE(usage.value("reportedCount").toInt(), 1);
    QCOMPARE(usage.value("totalCount").toInt(), 2);
    QCOMPARE(usage.value("partial").toBool(), true);
    QCOMPARE(usage.value("input").toInt(), 4);
    QCOMPARE(usage.value("output").toInt(), 3);
    // Runner 的展示 cached 合并了 OpenAI cached + Anthropic cacheRead。
    QCOMPARE(usage.value("cached").toInt(), 7);
    QCOMPARE(usage.value("cacheRead").toInt(), 6);
    QCOMPARE(usage.value("cacheCreation").toInt(), 2);
    // 第二个任务没上报分类用量 → 聚合结果必须标记 estimated。
    QVERIFY2(usage.value("estimated").toBool(),
             "batch aggregation lost the estimated marker");
    QCOMPARE(light.value("tokensUsed").toInt(), 14);

    agent.setMemory(nullptr);
}

// docs/43 O4-2: 失败的后台任务用保存的原始 spawn 参数重派为新任务——
// 运行中拒绝、未知 id 拒绝、原任务保留、新任务跑完并产出报告。
void TestAgent::subagentRetryFailedBackgroundTask() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();

    // 第 1 次派发先被 gate 停住（覆盖「运行中拒绝重试」），释放后失败；
    // 第 2 次派发是 SubAgentTool 内的自动重试（attempt 2），同样失败 →
    // 任务终态 failed。第 3 次起（用户重试）返回成功脚本。
    auto gate1 = std::make_shared<QSemaphore>();
    auto dispatches = std::make_shared<std::atomic<int>>(0);
    fixture.tool->setServiceFactory([&, gate1, dispatches]() -> IAIService* {
        auto* service = new RecordingFakeService(
            &fixture.mutex, &fixture.subSeenTools, &fixture.subSeenMessages);
        const int index = dispatches->fetch_add(1);
        if (index == 0) {
            service->gate = gate1.get();
            LLMResponse failure;
            failure.success = false;
            failure.error = QStringLiteral("transient boom");
            service->script.append(failure);
        } else if (index == 1) {
            LLMResponse failure;
            failure.success = false;
            failure.error = QStringLiteral("still boom");
            service->script.append(failure);
        } else {
            service->script.append(
                textResponse(QStringLiteral("recovered on retry")));
        }
        return service;
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("retry survey")},
                    {"background", true}}));
    llm.script.append(textResponse(QStringLiteral("task done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn background then retry"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    // 任务在跑（gate 停住）→ 拒绝重试。
    QString runningId;
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        for (const QVariant& value : agent.backgroundSubAgents()) {
            const QVariantMap task = value.toMap();
            if (task.value(QStringLiteral("state")).toString()
                    == QLatin1String("running")) {
                runningId = task.value(QStringLiteral("id")).toString();
                return true;
            }
        }
        return false;
    })(), 10000);
    QVERIFY(!runningId.isEmpty());
    QVERIFY(!agent.retryBackgroundSubAgent(runningId));
    QVERIFY(!agent.retryBackgroundSubAgent(QStringLiteral("sa-999")));

    // 释放 gate → attempt 1 失败 → attempt 2 失败 → 终态 failed。
    gate1->release();
    QString failedId;
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        for (const QVariant& value : agent.backgroundSubAgents()) {
            const QVariantMap task = value.toMap();
            if (task.value(QStringLiteral("id")).toString() == runningId
                    && task.value(QStringLiteral("state")).toString()
                           == QLatin1String("failed")) {
                failedId = runningId;
                return true;
            }
        }
        return false;
    })(), 10000);
    QCOMPARE(dispatches->load(), 2);

    // 一键重试：同参数派新任务（第 3 次工厂调用 → 成功脚本）。
    QVERIFY(agent.retryBackgroundSubAgent(failedId));
    QString newId;
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        for (const QVariant& value : agent.backgroundSubAgents()) {
            const QVariantMap task = value.toMap();
            if (task.value(QStringLiteral("id")).toString() != failedId
                    && task.value(QStringLiteral("state")).toString()
                           == QLatin1String("completed")) {
                newId = task.value(QStringLiteral("id")).toString();
                return true;
            }
        }
        return false;
    })(), 10000);
    QVERIFY(!newId.isEmpty());
    QVERIFY(agent.backgroundSubAgentReport(newId)
                .contains(QStringLiteral("recovered on retry")));
    // 原任务保留在列表里（failed + completed 共 2 条）。
    QCOMPARE(agent.backgroundSubAgents().size(), 2);
    agent.dismissFinishedBackgroundSubAgents();
    QVERIFY(agent.backgroundSubAgents().isEmpty());
}

// docs/43 O4-3: backgroundSubAgentFindings 把已解析的 findings 逐条结构化
// 返回（severity/path/line/message）；无报告/无 findings/未知 id 均为空。
void TestAgent::subagentFindingsQueryReturnsStructuredList() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        auto* service = new RecordingFakeService(
            &fixture.mutex, &fixture.subSeenTools, &fixture.subSeenMessages);
        service->script.append(textResponse(QStringLiteral(
            "review done\n\n"
            "## FINDINGS\n"
            "- [high] src/a.cpp:12 - null deref after the check\n"
            "- [low] src/b.cpp:40 - stale comment\n")));
        return service;
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("review the patch")},
                    {"background", true}}));
    llm.script.append(textResponse(QStringLiteral("ok")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("background review"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    // 等任务终态并取 id。
    QString taskId;
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        for (const QVariant& value : agent.backgroundSubAgents()) {
            const QVariantMap task = value.toMap();
            if (task.value(QStringLiteral("state")).toString()
                    == QLatin1String("completed")) {
                taskId = task.value(QStringLiteral("id")).toString();
                return true;
            }
        }
        return false;
    })(), 10000);
    QVERIFY(!taskId.isEmpty());

    const QVariantList findings = agent.backgroundSubAgentFindings(taskId);
    QCOMPARE(findings.size(), 2);
    const QVariantMap first = findings.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("severity")).toString(),
             QStringLiteral("high"));
    QCOMPARE(first.value(QStringLiteral("path")).toString(),
             QStringLiteral("src/a.cpp"));
    QCOMPARE(first.value(QStringLiteral("line")).toInt(), 12);
    QVERIFY(first.value(QStringLiteral("message")).toString()
                .contains(QStringLiteral("null deref")));
    const QVariantMap second = findings.at(1).toMap();
    QCOMPARE(second.value(QStringLiteral("severity")).toString(),
             QStringLiteral("low"));

    // 未知 id → 空。
    QVERIFY(agent.backgroundSubAgentFindings(QStringLiteral("sa-999")).isEmpty());
    agent.dismissFinishedBackgroundSubAgents();
    QVERIFY(agent.backgroundSubAgents().isEmpty());
}

// docs/33 E1: 子代理完整过程落盘为证据工件——报告头带路径，主代理可
// file_read 细读；7 天保留期由每次派生时的清理执行。
void TestAgent::subagentEvidenceArtifactWrittenAndPruned() {
    QTemporaryDir artifacts(testDirTemplate());
    QVERIFY(artifacts.isValid());
    SubAgentRunner::setArtifactsRootOverride(artifacts.path());
    struct ResetOverride {
        ~ResetOverride() { SubAgentRunner::setArtifactsRootOverride(QString()); }
    } resetOverride;

    // 预置一个过期的旧工件：本次派生应把它清掉。
    QDir(artifacts.path()).mkpath(QStringLiteral("."));
    const QString stalePath = artifacts.filePath(QStringLiteral("stale.md"));
    {
        // Qt6: setFileTime 是实例方法，须在文件打开时调用。
        QFile stale(stalePath);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("old");
        QVERIFY(stale.setFileTime(QDateTime::currentDateTime().addDays(-8),
                                  QFileDevice::FileModificationTime));
    }

    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
        sub.script.append(textResponse(QStringLiteral("findings with probe-evidence")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("leave evidence")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn with artifacts"));
    QTRY_COMPARE(spy.count(), 1);

    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY(report.contains(QStringLiteral("[Sub-agent report]")));
    QVERIFY(report.contains(QStringLiteral("evidence: ")));
    // 从报告头提取路径并校验内容。
    const int keyIndex = report.indexOf(QStringLiteral("evidence: "));
    const int lineEnd = report.indexOf(QLatin1Char('\n'), keyIndex);
    QString artifactPath = report.mid(keyIndex + QStringLiteral("evidence: ").length(),
                                      lineEnd - keyIndex
                                          - QStringLiteral("evidence: ").length());
    artifactPath = artifactPath.trimmed().remove(QLatin1Char(')'));
    QVERIFY(QFileInfo::exists(artifactPath));
    QFile artifact(artifactPath);
    QVERIFY(artifact.open(QIODevice::ReadOnly));
    const QString contents = QString::fromUtf8(artifact.readAll());
    QVERIFY(contents.contains(QStringLiteral("Sub-agent evidence")));
    QVERIFY(contents.contains(QStringLiteral("leave evidence")));
    QVERIFY(contents.contains(QStringLiteral("probe-evidence")));
    // 过期工件被清理；本次工件仍在（且落在覆盖的工件根目录内）。
    QVERIFY(!QFileInfo::exists(stalePath));
    QVERIFY2(artifactPath.startsWith(artifacts.path()),
             qPrintable(QStringLiteral("artifact outside override root: %1").arg(artifactPath)));

    // UI 入口数据源：spawn_agent 的 tool_result 节点带 evidencePath 字段。
    bool nodeHasEvidence = false;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value("type").toString() == QLatin1String("tool_result")
                && node.value("title").toString() == QLatin1String("spawn_agent")) {
            QCOMPARE(node.value("evidencePath").toString(), artifactPath);
            nodeHasEvidence = true;
        }
    }
    QVERIFY(nodeHasEvidence);

    // isEvidencePath：根目录内的真实文件放行；根外/不存在的路径拒绝
    // （UI 侧用它拦截伪造路径交给系统打开）。
    QVERIFY(SubAgentRunner::isEvidencePath(artifactPath));
    QVERIFY(!SubAgentRunner::isEvidencePath(stalePath));   // 已被清理
    QVERIFY(!SubAgentRunner::isEvidencePath(
        artifacts.filePath(QStringLiteral("../escape.md"))));
    QVERIFY(!SubAgentRunner::isEvidencePath(QString()));
    QVERIFY(!SubAgentRunner::isEvidencePath(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)));
}

// ── docs/33 E2: 角色档案 ──────────────────────────────────────────────

namespace {
// spawn_agent 参数 schema 里 role 属性的 enum 数组（主循环第一轮 schema）。
QJsonArray roleEnumInMainSchema(const QJsonArray& tools) {
    for (const auto& entry : tools) {
        const QJsonObject fn = entry.toObject().value("function").toObject();
        if (fn.value("name").toString() != QLatin1String("spawn_agent")) continue;
        return fn.value("parameters").toObject()
            .value("properties").toObject()
            .value("role").toObject().value("enum").toArray();
    }
    return {};
}
} // namespace

void TestAgent::subagentRolePresetShapesPromptAndSchema() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
        sub.script.append(textResponse(QStringLiteral("review done")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("review the diff")},
                    {"role", QStringLiteral("review")}}));
    llm.script.append(textResponse(QStringLiteral("integrated")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn a reviewer"));
    QTRY_COMPARE(spy.count(), 1);

    // 预设 review 的专注语进入子代理系统提示词。
    QMutexLocker lock(&fixture.mutex);
    const QJsonArray subMessages = fixture.subSeenMessages.first();
    QVERIFY(subMessages.first().toObject().value("content").toString()
                .contains(QStringLiteral("code review")));
    lock.unlock();

    // 主 schema 的 role 枚举 = 预设清单（explore/research/review/coder）。
    QVERIFY(!llm.seenTools.isEmpty());
    const QJsonArray roleEnum = roleEnumInMainSchema(llm.seenTools.first());
    QCOMPARE(roleEnum.size(), 4);
    QStringList names;
    for (const auto& v : roleEnum) names.append(v.toString());
    QVERIFY(names.contains(QStringLiteral("explore")));
    QVERIFY(names.contains(QStringLiteral("research")));
    QVERIFY(names.contains(QStringLiteral("review")));
    QVERIFY(names.contains(QStringLiteral("coder")));
}

void TestAgent::subagentUserRoleOverridesAndFiltersTools() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(toolResponse(QStringLiteral("test_sub_probe"), {}));
        sub.script.append(textResponse(QStringLiteral("audit done")));
    });
    fixture.tool->setUserRoles(SubAgentTool::rolesFromVariant({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("auditor")},
                    {QStringLiteral("prompt"),
                     QStringLiteral("AUDITOR-ROLE-MARKER: audit everything.")},
                    {QStringLiteral("tools"), QVariantList{
                        QStringLiteral("test_sub_probe")}}},
        // 与预设同名 → 覆盖预设，且出现在 schema 枚举里仍只有一个 explore。
        QVariantMap{{QStringLiteral("name"), QStringLiteral("explore")},
                    {QStringLiteral("prompt"),
                     QStringLiteral("CUSTOM-EXPLORE-MARKER")}},
    }));
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("audit the module")},
                    {"role", QStringLiteral("auditor")}}));
    llm.script.append(textResponse(QStringLiteral("integrated")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn an auditor"));
    QTRY_COMPARE(spy.count(), 1);

    QMutexLocker lock(&fixture.mutex);
    // 白名单把子代理工具面收窄到只剩 probe（目录里其他只读工具全部滤除）。
    const QJsonArray subTools = fixture.subSeenTools.first();
    QCOMPARE(subTools.size(), 1);
    QVERIFY(toolsArrayContains(subTools, QStringLiteral("test_sub_probe")));
    // 自定义档案的专注语进入系统提示词。
    QVERIFY(fixture.subSeenMessages.first().first().toObject()
                .value("content").toString()
                .contains(QStringLiteral("AUDITOR-ROLE-MARKER")));
    lock.unlock();

    // 用户 explore 覆盖预设：枚举按唯一角色名展开（auditor + 四个预设名），
    // explore 只出现一次。
    const QJsonArray roleEnum = roleEnumInMainSchema(llm.seenTools.first());
    QCOMPARE(roleEnum.size(), 5);
    int exploreCount = 0;
    for (const auto& v : roleEnum) {
        if (v.toString() == QLatin1String("explore")) ++exploreCount;
    }
    QCOMPARE(exploreCount, 1);

    // 覆盖生效：role=explore 用的是用户提示词。
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("explore again")},
                    {"role", QStringLiteral("explore")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn custom explorer"));
    QTRY_COMPARE(spy2.count(), 1);
    QMutexLocker lock2(&fixture.mutex);
    QVERIFY(fixture.subSeenMessages.last().first().toObject()
                .value("content").toString()
                .contains(QStringLiteral("CUSTOM-EXPLORE-MARKER")));
}

void TestAgent::subagentUnknownRoleFailsReadable() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("should not run")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("anything")},
                    {"role", QStringLiteral("no-such-role")}}));
    llm.script.append(textResponse(QStringLiteral("recovered")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn with bad role"));
    QTRY_COMPARE(spy.count(), 1);

    const QString report = lastToolMessageContent(agent.messages());
    QVERIFY(report.contains(QStringLiteral("Unknown role")));
    QVERIFY(report.contains(QStringLiteral("Available roles")));
    // 子代理一次都没跑（剧本只有一条文本，若跑了 subSeenMessages 会非空）。
    QMutexLocker lock(&fixture.mutex);
    QVERIFY(fixture.subSeenMessages.isEmpty());
}

void TestAgent::subagentRolesFromVariantParsesAndDedupes() {
    const QVector<SubAgentRole> roles = SubAgentTool::rolesFromVariant({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("a")},
                    {QStringLiteral("prompt"), QStringLiteral("P-A")},
                    {QStringLiteral("profileId"), QStringLiteral("  fast-model  ")},
                    {QStringLiteral("effort"), QStringLiteral(" high ")},
                    {QStringLiteral("background"), true},
                    {QStringLiteral("tools"), QVariantList{
                        QStringLiteral("read_file"), QString(),
                        QStringLiteral("  grep  ")}}},
        QVariantMap{{QStringLiteral("prompt"), QStringLiteral("no name")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("b")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("A")},
                    {QStringLiteral("prompt"), QStringLiteral("P-A2")}},
    });
    // 无名/无提示词的条目剔除；同名（大小写不敏感）保留首个。
    QCOMPARE(roles.size(), 1);
    QCOMPARE(roles.first().name, QStringLiteral("a"));
    QCOMPARE(roles.first().prompt, QStringLiteral("P-A"));
    // docs/35 P4.6: 角色级模型配置随档案解析（首尾空白剔除）。
    QCOMPARE(roles.first().profileId, QStringLiteral("fast-model"));
    QCOMPARE(roles.first().effort, QStringLiteral("high"));
    // docs/35：角色默认后台随档案解析；缺失字段 = false（旧档案兼容）。
    QVERIFY(roles.first().background);
    const QVector<SubAgentRole> legacy = SubAgentTool::rolesFromVariant({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("legacy")},
                    {QStringLiteral("prompt"), QStringLiteral("P")}}});
    QCOMPARE(legacy.size(), 1);
    QVERIFY(!legacy.first().background);
    const QStringList expectedTools{QStringLiteral("read_file"), QStringLiteral("grep")};
    QCOMPARE(roles.first().tools, expectedTools);

    // 预设清单固定为 explore/research/review/coder。
    const QVector<SubAgentRole> presets = SubAgentTool::presetRoles();
    QCOMPARE(presets.size(), 4);
    QCOMPARE(presets.at(0).name, QStringLiteral("explore"));
    QCOMPARE(presets.at(1).name, QStringLiteral("research"));
    QCOMPARE(presets.at(2).name, QStringLiteral("review"));
    QCOMPARE(presets.at(3).name, QStringLiteral("coder"));
    QVERIFY(presets.at(2).prompt.contains(QStringLiteral("code review")));
    QVERIFY(presets.at(3).prompt.contains(QStringLiteral("never apply")));
}

// docs/35：角色级默认后台。用户档案把 explore 设为默认后台后：
//   * 省略 background（甚至省略 role）→ 走后台 fork，立即 ack；
//   * 显式 background=false → 同步执行（模型决定优先于角色默认）。
void TestAgent::subagentRoleDefaultBackgroundStartsAsync() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("role-bg report")));
    });
    fixture.tool->setUserRoles(SubAgentTool::rolesFromVariant({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("explore")},
                    {QStringLiteral("prompt"),
                     QStringLiteral("CUSTOM-EXPLORE-DEFAULT-BG")},
                    {QStringLiteral("background"), true}}}));

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);

    // 省略 role 与 background：默认 explore 解析成功，且按角色默认转后台。
    const ToolResult ack = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("default-background survey")}});
    QVERIFY2(ack.success, qPrintable(ack.error));
    QVERIFY(ack.output.contains(QStringLiteral("[Sub-agent started]")));
    QVERIFY(!ack.output.contains(QStringLiteral("[Sub-agent report]")));
    QCOMPARE(hub->snapshots().size(), 1);
    QCOMPARE(hub->snapshots().first().toMap()
                 .value(QStringLiteral("role")).toString(),
             QStringLiteral("explore"));
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const QRegularExpressionMatch idMatch = idRe.match(ack.output);
    QVERIFY(idMatch.hasMatch());
    SubAgentWaitTool waitTool;
    const ToolResult report = waitTool.execute(QJsonObject{
        {QStringLiteral("id"), idMatch.captured(1)},
        {QStringLiteral("timeout_ms"), 10000}});
    QVERIFY2(report.success, qPrintable(report.error));
    QVERIFY(report.output.contains(QStringLiteral("role-bg report")));

    // 模型显式 false：角色默认被覆盖，回到同步 fork，不新增后台任务。
    const ToolResult sync = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("force sync")},
        {QStringLiteral("background"), false}});
    QVERIFY2(sync.success, qPrintable(sync.error));
    QVERIFY(sync.output.contains(QStringLiteral("[Sub-agent report]")));
    QVERIFY(!sync.output.contains(QStringLiteral("[Sub-agent started]")));
    QCOMPARE(hub->snapshots().size(), 1);
}

// 2026-11-03：设置页的内置角色默认后台开关。预设角色统一继承该开关；
// 用户同名档案（shadow）不继承，自己的 background 仍然优先。
void TestAgent::subagentPresetBackgroundSettingAppliesToBuiltins() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("preset-bg report")));
    });
    fixture.tool->setPresetBackground(true);

    auto hub = std::make_shared<BackgroundTaskHub>();
    BackgroundTaskHubRouter router(hub);

    // 内置 research 未在档案里配置 background，但全局开关打开 → 后台。
    const ToolResult ack = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("preset background")},
        {QStringLiteral("role"), QStringLiteral("research")}});
    QVERIFY2(ack.success, qPrintable(ack.error));
    QVERIFY(ack.output.contains(QStringLiteral("[Sub-agent started]")));
    QCOMPARE(hub->snapshots().size(), 1);
    QCOMPARE(hub->snapshots().first().toMap()
                 .value(QStringLiteral("role")).toString(),
             QStringLiteral("research"));
    static const QRegularExpression idRe(QStringLiteral("id=(sa-\\d+)"));
    const QRegularExpressionMatch idMatch = idRe.match(ack.output);
    QVERIFY(idMatch.hasMatch());
    SubAgentWaitTool waitTool;
    const ToolResult report = waitTool.execute(QJsonObject{
        {QStringLiteral("id"), idMatch.captured(1)},
        {QStringLiteral("timeout_ms"), 10000}});
    QVERIFY2(report.success, qPrintable(report.error));
    QVERIFY(report.output.contains(QStringLiteral("preset-bg report")));

    // 用户同名档案遮蔽预设：没有 background 字段 = false → 同步执行。
    fixture.tool->setUserRoles(SubAgentTool::rolesFromVariant({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("research")},
                    {QStringLiteral("prompt"),
                     QStringLiteral("CUSTOM-RESEARCH-MARKER")}}}));
    const ToolResult shadowed = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("shadowed role is sync")},
        {QStringLiteral("role"), QStringLiteral("research")}});
    QVERIFY2(shadowed.success, qPrintable(shadowed.error));
    QVERIFY(shadowed.output.contains(QStringLiteral("[Sub-agent report]")));
    QVERIFY(!shadowed.output.contains(QStringLiteral("[Sub-agent started]")));
    QCOMPARE(hub->snapshots().size(), 1);

    // 关掉全局开关并从遮蔽档案恢复预设 → 内置角色回到同步默认。
    fixture.tool->setUserRoles({});
    fixture.tool->setPresetBackground(false);
    const ToolResult presetSync = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("presets are sync again")},
        {QStringLiteral("role"), QStringLiteral("research")}});
    QVERIFY2(presetSync.success, qPrintable(presetSync.error));
    QVERIFY(presetSync.output.contains(QStringLiteral("[Sub-agent report]")));
    QCOMPARE(hub->snapshots().size(), 1);
}

// docs/35 P4.6: 角色点名的 profileId / effort 一路传到服务工厂，上下文窗口
// 也按同一 profile 解析（不再全局固定）。
void TestAgent::subagentRoleModelProfileReachesFactory() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->refreshCatalog(false);

    SubAgentRole role;
    role.name = QStringLiteral("fast");
    role.prompt = QStringLiteral("quick, cheap exploration");
    role.tools = QStringList{QStringLiteral("test_sub_probe")};
    role.profileId = QStringLiteral("profile-fast");
    role.effort = QStringLiteral("low");
    fixture.tool->setUserRoles({role});

    QMutex seenMutex;
    QString seenProfile;
    QString seenEffort;
    QString limitProfile;
    fixture.tool->setServiceFactory(
        [&](const QString& profileId, const QString& effort) -> IAIService* {
            QMutexLocker lock(&seenMutex);
            seenProfile = profileId;
            seenEffort = effort;
            auto* service = new RecordingFakeService(
                &fixture.mutex, &fixture.subSeenTools, &fixture.subSeenMessages);
            service->script.append(textResponse(QStringLiteral("fast report")));
            return service;
        });
    fixture.tool->setContextLimitResolver(
        [&](const QString& profileId) -> int {
            QMutexLocker lock(&seenMutex);
            limitProfile = profileId;
            return 4321;
        });

    const ToolResult result = fixture.tool->execute(QJsonObject{
        {QStringLiteral("goal"), QStringLiteral("quick probe")},
        {QStringLiteral("role"), QStringLiteral("fast")}});
    QVERIFY2(result.success, qPrintable(result.error));
    QVERIFY(result.output.contains(QStringLiteral("fast report")));
    QMutexLocker lock(&seenMutex);
    QCOMPARE(seenProfile, QStringLiteral("profile-fast"));
    QCOMPARE(seenEffort, QStringLiteral("low"));
    QCOMPARE(limitProfile, QStringLiteral("profile-fast"));
}

// docs/35 P4.6: 流式增量按 200ms 节流成 stream/reasoning 进度事件，而不是
// 每个 chunk 一条。
void TestAgent::subagentStreamsProgressThrottled() {
    StreamingFakeService service;
    SubAgentRequest request;
    request.goal = QStringLiteral("stream a report");
    request.maxSteps = 1;   // forced final: no tools, just an answer
    QVector<SubAgentProgress> events;
    request.progress = [&events](const SubAgentProgress& p) {
        events.append(p);
    };

    const SubAgentReport report = SubAgentRunner::run(&service, request);
    QVERIFY2(report.ok, qPrintable(report.text));
    QCOMPARE(report.text, QStringLiteral("final answer"));

    int streamEvents = 0;
    int reasoningEvents = 0;
    bool sawChunk = false;
    bool sawReasoning = false;
    for (const SubAgentProgress& event : events) {
        if (event.phase == QLatin1String("stream")) {
            ++streamEvents;
            sawChunk |= event.detail.contains(QStringLiteral("chunk"));
        } else if (event.phase == QLatin1String("reasoning")) {
            ++reasoningEvents;
            sawReasoning |= event.detail.contains(QStringLiteral("pondering"));
        }
    }
    // Five chunks arrive in the first 50ms → one throttled tick.
    QVERIFY2(streamEvents >= 1 && streamEvents <= 2,
             qPrintable(QStringLiteral("stream events: %1").arg(streamEvents)));
    QVERIFY(streamEvents <= 2);
    QCOMPARE(reasoningEvents, 1);
    QVERIFY(sawChunk);
    QVERIFY(sawReasoning);
}

// docs/35 P4.6: 长流式输出不得把 subagentLog 挤出上限——同一阶段的增量
// 事件在原地更新末条，而不是每次追加一行。
void TestAgent::subagentStreamingLogCollapses() {
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    ensureSubAgentProbe();
    fixture.tool = std::make_shared<SubAgentTool>();
    fixture.tool->setServiceFactory([&]() -> IAIService* {
        return new StreamingFakeService();   // never gated; answers in 350ms
    });
    fixture.tool->refreshCatalog(false);
    ToolRegistry::instance().registerTool(fixture.tool);

    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{QStringLiteral("goal"), QStringLiteral("stream it")}}));
    llm.script.append(textResponse(QStringLiteral("done")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("watch streaming"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);

    QJsonArray log;
    for (const auto& entry : agent.traceModel()) {
        const QJsonObject node = entry.toObject();
        if (node.value("title").toString().startsWith(QLatin1String("spawn_agent"))
                && node.value("type").toString() == QLatin1String("tool_call")) {
            const QJsonArray candidate = node.value("subagentLog").toArray();
            if (!candidate.isEmpty()) log = candidate;
        }
    }
    QVERIFY2(!log.isEmpty(), "流式运行没有留下过程日志");
    int streamEntries = 0;
    for (const auto& entry : log) {
        if (entry.toObject().value("phase").toString() == QLatin1String("stream"))
            ++streamEntries;
    }
    QVERIFY2(streamEntries <= 1,
             qPrintable(QStringLiteral("stream log entries: %1").arg(streamEntries)));
    QVERIFY(log.size() <= 50);
}

// docs/33 E3 批判环：review 预设是执行端（自圈改动集 + 严重度排序的
// findings list），spawn_agent 描述是主循环侧的引导（批次后派审、逐条
// 核实、不辩论）。
void TestAgent::reviewCriticLoopGuidanceInPresetAndSchema() {
    const QVector<SubAgentRole> presets = SubAgentTool::presetRoles();
    const QString review = presets.at(2).prompt;
    // 自圈改动集：review 子代理自己跑 git_status/git_diff（或按 context 点名）。
    QVERIFY(review.contains(QStringLiteral("git_status")));
    QVERIFY(review.contains(QStringLiteral("git_diff")));
    // 产出形态：严重度排序 + 具体位置；干净时明说，不硬凑。
    QVERIFY(review.contains(QStringLiteral("findings list")));
    QVERIFY(review.contains(QStringLiteral("path:line")));
    QVERIFY(review.contains(QStringLiteral("do not invent issues")));

    SubAgentTool tool;
    const QString desc = tool.definition().description;
    QVERIFY(desc.contains(QStringLiteral("Critic loop")));
    QVERIFY(desc.contains(QStringLiteral("'review'")));
    QVERIFY(desc.contains(QStringLiteral("findings")));
}

// docs/32 修正：子代理工具面必须包含网络只读检索（web_search/http_fetch
// 是 RequiresConfirm，旧目录过滤按 Safe 一刀切把它们静默漏掉了），且
// localOnly 门镜像 AIAgent::toolAllowedForModel：web_search/mcp__ 摘除、
// http_fetch 保留。
void TestAgent::subagentCatalogCarriesNetworkTools() {
    ensureNetworkTools();
    SubAgentFixture fixture;
    SpawnToolGuard guard;
    WebToolsGuard webGuard;
    fixture.tool = registerSpawnTool(fixture, [](RecordingFakeService& sub) {
        sub.script.append(textResponse(QStringLiteral("network report")));
    });
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("search the web")}}));
    llm.script.append(textResponse(QStringLiteral("integrated")));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn for web"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);

    QMutexLocker lock(&fixture.mutex);
    const QJsonArray subTools = fixture.subSeenTools.first();
    QVERIFY(toolsArrayContains(subTools, QStringLiteral("web_search")));
    QVERIFY(toolsArrayContains(subTools, QStringLiteral("http_fetch")));
    lock.unlock();

    // localOnly=true：web_search 摘除，http_fetch 保留（同主循环门）。
    fixture.tool->refreshCatalog(true);
    llm.reset();
    llm.script.append(toolResponse(QStringLiteral("spawn_agent"),
        QJsonObject{{"goal", QStringLiteral("fetch local docs")}}));
    llm.script.append(textResponse(QStringLiteral("integrated 2")));
    QSignalSpy spy2(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("spawn localOnly"));
    QTRY_COMPARE_WITH_TIMEOUT(spy2.count(), 1, 15000);
    QMutexLocker lock2(&fixture.mutex);
    const QJsonArray subTools2 = fixture.subSeenTools.last();
    QVERIFY(!toolsArrayContains(subTools2, QStringLiteral("web_search")));
    QVERIFY(toolsArrayContains(subTools2, QStringLiteral("http_fetch")));
}

QTEST_GUILESS_MAIN(TestAgent)
#include "test_agent.moc"
