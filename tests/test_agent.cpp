// AIAgent main-loop branch coverage, driven by a scripted fake LLM service.
// Exercises: direct completion, dangerous-tool exclusion, safe-tool feedback,
// confirmation approve/decline, unknown tools, max-iteration guard, stop and
// the context budget compression hook.
#include "core/agent/AIAgent.h"
#include "core/agent/AgentState.h"
#include "core/tools/ToolRegistry.h"
#include "core/memory/SqliteMemory.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "infrastructure/llm/IAIService.h"

#include <QSet>
#include <QSemaphore>
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
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
    return tool;
}

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
    void initTestCase() {
        agent.setLLMService(&llm);
        agent.setToolRegistry(&ToolRegistry::instance());
        agent.setMemory(nullptr);
    }

    void init() {
        llm.reset();
        agent.setContextLimit(8192);
        agent.setMemory(nullptr);
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

    void directCompletion();
    void dangerousToolsNotExposed();
    void safeToolFeedbackLoop();
    void confirmApproveWritesFile();
    void confirmDeclineFeedsRejection();
    void unknownToolFeedsError();
    void maxIterationsStops();
    void repeatedCallWithAnswerFinishes();
    void oldToolRoundsCollapsed();
    void traceRestoredOnResume();
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
    llm.script.append(textResponse("ok"));
    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask("hi");
    QTRY_COMPARE(spy.count(), 1);
    // 6 built-in tools are registered (Batch 19 added edit_file), but
    // rm_rf (Dangerous) must be filtered
    QCOMPARE(llm.seenTools.first().size(), 5);
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

    const QString id = confirmSpy.first().first().toString();
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
    llm.script.append(toolResponse("shell_exec",
        QJsonObject{{"command",
#ifdef Q_OS_WIN
            QStringLiteral("ping -n 31 127.0.0.1 > nul")
#else
            QStringLiteral("sleep 30")
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

    QElapsedTimer clock;
    clock.start();
    agent.stop();
    QTRY_VERIFY_WITH_TIMEOUT(
        lastToolCallStatus(agent.traceModel(), QStringLiteral("shell_exec"))
            == QStringLiteral("cancelled"), 3000);
    QVERIFY2(clock.elapsed() < 2500, "shell cancel took longer than 2 s");
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
void TestAgent::localOnlyModeRejectsResidualCall() {
    registerKnowledgeTool();
    auto* registry = &ToolRegistry::instance();
    registry->setToolEnabled(QStringLiteral("search_knowledge_base"), false);

    llm.script.append(toolResponse("search_knowledge_base",
        QJsonObject{{"query", QStringLiteral("anything")}}));
    llm.script.append(textResponse(QStringLiteral("final answer")));

    QSignalSpy spy(&agent, &AIAgent::responseReady);
    agent.startTask(QStringLiteral("residual kb call"));
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("final answer"));

    QVERIFY2(toolMessageContent(agent.messages(), QStringLiteral("call_t0"))
                 .contains(QStringLiteral("local-only")),
             "disabled tool call was not rejected with a local-only error");
    QVERIFY2(noDanglingToolCalls(agent.messages()),
             "disabled-tool rejection left dangling tool_calls");
    // Never executed: the call node failed, and no tool_result node exists.
    QCOMPARE(lastToolCallStatus(agent.traceModel(),
                                QStringLiteral("search_knowledge_base")),
             QStringLiteral("failed"));
    QVERIFY2(!traceHas(agent.traceModel(), QStringLiteral("tool_result"),
                       QStringLiteral("search_knowledge_base")),
             "gated KB tool was executed despite being disabled");
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
    // cleanup() also restores the singleton for later tests.
}

// A single response announcing more tool calls than the execution budget
// allows must execute the budget, seal the rest, and converge into one
// final no-tools answer instead of running unbounded work.
void TestAgent::toolExecutionBudgetForcesFinal() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

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
    QCOMPARE(executions, AIAgent::kMaxToolExecutions);
    QVERIFY(traceHas(agent.traceModel(), "context", "budget"));
    QVERIFY2(noDanglingToolCalls(llm.seenMessages.last()),
             "budget path left dangling tool_calls");

    agent.setMemory(nullptr);
}

// The "[System] Step budget exhausted..." control message drives the forced
// final answer but belongs to the REQUEST, not the conversation: it must be
// injected into the forced call and must never leak into later turns.
void TestAgent::forcedFinalControlMessageNotPersisted() {
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
    QVERIFY(readPos >= 0 && anomalyPos > readPos);
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

QTEST_GUILESS_MAIN(TestAgent)
#include "test_agent.moc"
