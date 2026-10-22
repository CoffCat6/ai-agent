// AIAgent main-loop branch coverage, driven by a scripted fake LLM service.
// Exercises: direct completion, dangerous-tool exclusion, safe-tool feedback,
// confirmation approve/decline, unknown tools, max-iteration guard, stop and
// the context budget compression hook.
#include "core/agent/AIAgent.h"
#include "core/agent/AgentState.h"
#include "core/agent/EpisodicService.h"
#include "core/tools/ToolRegistry.h"
#include "core/memory/SqliteMemory.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "infrastructure/llm/IAIService.h"
#include "core/utils/TokenEstimator.h"
#include "viewmodels/AgentViewModel.h"

#include <QSet>
#include <QSemaphore>
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QElapsedTimer>

#include <atomic>
#include <chrono>
#include <thread>

using namespace NexusAI;

namespace {

class FakeAIService : public IAIService {
    Q_OBJECT
public:
    QVector<QJsonArray> seenMessages;   // history snapshot per chatComplete()
    QVector<QJsonArray> seenTools;
    QVector<LLMResponse> script;
    bool repeatLast{false};
    quint64 requestCounter{0};          // requestStarted()/requestId echo

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
    QString model() const override { return QStringLiteral("fake-model"); }

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

// Every assistant message announcing tool_calls must be followed by tool
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
        agent.setContextLimit(8192);
        agent.setMemory(nullptr);
        // docs/25 B3: the episodic fixture is test-local — never leak it
        // into the neighbours (the async launch changes fresh-session flow).
        agent.setEpisodicService(nullptr);
        // 权限模式用例会改动本实例（QtTest 全程共用一个测试对象），
        // 每个用例前回落默认，防止泄漏到后续用例。
        agent.setPermissionMode(PermissionMode::ConfirmChanges);
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
    void traceRestoredOnResume();
    // F1: todo list lifetime is per session/tab, never carried across resets.
    void todoListClearedOnSessionResetAndResume();
    // F1: the todo list is persisted per session — reopening the SAME session
    // restores its list, and a different session yields its own (or empty).
    void todoListPersistsAcrossSessionResume();
    // 思考强度：思考正文只落 thinking 节点，不混进答案、不回灌模型、可恢复
    void reasoningTextLandsOnThinkingNode();
    void stopDuringConfirmationIsNoOp();
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
    // Batch 13: cooperative tool cancellation (SEC-1301..1304)
    void toolCancelledOnStop();
    void shellProcessKilledOnStop();
    // Batch 14: local-only mode knowledge-tool gating (SEC-1401..1404)
    void localOnlyModeHidesKnowledgeTool();
    void localOnlyModeRejectsResidualCall();
    // Batch 14 review fix: isToolEnabled predicate semantics
    void toolRegistryEnablementSemantics();
    void toolExecutionBudgetForcesFinal();
    void forcedFinalControlMessageNotPersisted();
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
    // docs/16 §8.2: 会话累计 token（逐请求累加 / 清零 / 续会话恢复）
    void testSessionTokensUsed();
    // docs/16 §13 AC-7: 配额检查（超限 emit quotaWarning，strict 模式拒绝）
    void testCheckQuota();
    // docs/16 §3.3: 估算器滚动校准（API 真实 promptTokens 反馈 → EWMA 系数）
    void testEstimateScaleCalibration();
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
    QCOMPARE(seen.size(), 11);
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
    // 预算已可配置（agent/maxSteps，默认 30），本用例断言的是"预算耗尽"这条
    // 路径本身，所以显式钉一个 10，与脚本长度对齐——否则脚本第 11 轮起会重复
    // 最后一条响应，被"同轮重复调用"守卫提前收尾，测不到预算耗尽。
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

// After several completed tool rounds, older rounds must be collapsed into
// a note (only the newest two stay verbatim) while the wire format stays
// valid - a long tail of tool_call/tool messages reads like an "always call
// tools" script and made the model fire the tool on greetings.
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

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("read all four"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(agent.state(), AgentState::Completed);

    // The final LLM call must carry only the newest 2 tool rounds verbatim;
    // older ones are replaced by the collapsed-rounds note. Pairing must
    // stay valid after the replacement.
    const QJsonArray last = llm.seenMessages.last();
    int toolMsgs = 0;
    QString collapsedNote;
    for (const auto& entry : last) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QStringLiteral("tool")) ++toolMsgs;
        if (msg.value("content").toString().contains(QStringLiteral("Earlier tool calls")))
            collapsedNote = msg.value("content").toString();
    }
    QCOMPARE(toolMsgs, 2);
    QVERIFY(!collapsedNote.isEmpty());
    // FIX-504: the note must not name any specific tool - a hardcoded
    // "e.g. search_knowledge_base" injects KB bias into every conversation
    // no matter which tools were actually used.
    QVERIFY2(!collapsedNote.contains(QStringLiteral("search_knowledge_base")),
             "collapse note hardcodes the knowledge tool");
    QVERIFY2(noDanglingToolCalls(last), "collapse broke tool_call/tool pairing");

    // All four reads really executed before collapsing kicked in
    QVERIFY(traceHas(agent.traceModel(), "tool_result", "payload"));
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

// A single response announcing more tool calls than the execution budget
// allows must execute the budget, seal the rest, and converge into one
// final no-tools answer instead of running unbounded work.
void TestAgent::toolExecutionBudgetForcesFinal() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    // 工具执行上限随步数预算缩放（max(16, 2×步数)）。钉一个小预算，让上限(16)
    // 低于本轮声明的 20 个调用，才测得到"执行预算耗尽"这条路径。
    agent.setMaxIterations(AIAgent::kMinMaxIterations);

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

    agent.setMaxIterations(AIAgent::kDefaultMaxIterations);   // 还原共享 agent
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

// ── docs/15 §6.1.2: model-switch context-window adaptation ──

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

// docs/16 §8.2: 会话累计 token —— 每次请求累加 input+output，多轮任务
// 持续增长；resetSession 清零；resumeSession 从 token_usage 恢复基数。
void TestAgent::testSessionTokensUsed() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QSignalSpy sessionTokensSpy(&agent, &AIAgent::sessionTokensChanged);
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
    // 消息段 = 总占用 - tools schema 预留（后者保持原始估算口径）。
    const int rawBefore = baseline
        - static_cast<int>(baseComp.value("toolsSchema").toLongLong());
    QVERIFY(rawBefore > 0);

    // ── 校准生效：API 报告的 promptTokens = 2× 原始消息估算 → 系数 2.0
    // （在 [0.5, 3.0] 钳制内）。下一次 tokensChanged 的消息部分被放大，
    // 但 tools schema 部分保持原始口径，增幅应小于 2×。
    LLMResponse cal = textResponse(QStringLiteral("calibrated"));
    cal.promptTokens = rawBefore * 2;
    cal.completionTokens = 10;
    llm.script.append(cal);
    agent.startTask(QStringLiteral("第二轮"));
    QTRY_COMPARE(responseSpy.count(), 2);
    const int scaled = tokensSpy.last().at(0).toInt();
    QVERIFY(scaled > baseline);         // 系数生效，总占用变大
    QVERIFY(scaled < baseline * 2 + 1); // schema 未同步放大，增幅 < 2×

    // contextComposition 与 notifyTokens 同口径；校准只重分配桶，
    // 不破坏“桶之和 == usedTokens”的合同。
    const QVariantMap comp = agent.contextComposition();
    QCOMPARE(comp.value("totalTokens").toLongLong(),
             static_cast<qint64>(scaled));
    QCOMPARE(comp.value("systemPrompt").toLongLong()
           + comp.value("conversation").toLongLong()
           + comp.value("toolResults").toLongLong()
           + comp.value("images").toLongLong(),
             comp.value("usedTokens").toLongLong());
    // resetSession 清零系数，新会话回到基线口径。
    agent.resetSession();
    QCOMPARE(static_cast<int>(agent.contextComposition().value("totalTokens").toLongLong()),
             static_cast<int>(TokenEstimator::estimateTokens(agent.messages())
                              + static_cast<int>(comp.value("toolsSchema").toLongLong())));
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

QTEST_GUILESS_MAIN(TestAgent)
#include "test_agent.moc"
