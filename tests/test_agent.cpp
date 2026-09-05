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
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QStandardPaths>

using namespace NexusAI;

namespace {

class FakeAIService : public IAIService {
    Q_OBJECT
public:
    QVector<QJsonArray> seenMessages;   // history snapshot per chatComplete()
    QVector<QJsonArray> seenTools;
    QVector<LLMResponse> script;
    bool repeatLast{false};

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        seenMessages.append(messages);
        seenTools.append(tools);

        LLMResponse response;
        if (script.isEmpty()) {
            emit errorOccurred(QStringLiteral("FakeAIService: empty script"));
            return;
        }
        response = idx < script.size() ? script.at(idx) : script.last();
        ++idx;

        if (response.success) emit responseReady(response);
        else emit errorOccurred(response.error.isEmpty()
             ? QStringLiteral("FakeAIService: scripted error") : response.error);
    }
    void cancel() override {}
    void setApiKey(const QString&) override {}
    void setModel(const QString&) override {}
    void setBaseUrl(const QString&) override {}

    void reset() {
        seenMessages.clear();
        seenTools.clear();
        script.clear();
        repeatLast = false;
        idx = 0;
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

} // namespace

class TestAgent : public QObject {
    Q_OBJECT

    AIAgent agent;
    FakeAIService llm;

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
    // 5 built-in tools are registered, but rm_rf (Dangerous) must be filtered
    QCOMPARE(llm.seenTools.first().size(), 4);
}

void TestAgent::safeToolFeedbackLoop() {
    QTemporaryDir dir;
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
    QTemporaryDir dir;
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
    QTemporaryDir dir;
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

    QTemporaryDir dir;
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
    QTemporaryDir dir;
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
    bool collapsedNote = false;
    for (const auto& entry : last) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QStringLiteral("tool")) ++toolMsgs;
        if (msg.value("content").toString().contains(QStringLiteral("Earlier tool calls")))
            collapsedNote = true;
    }
    QCOMPARE(toolMsgs, 2);
    QVERIFY(collapsedNote);
    QVERIFY2(noDanglingToolCalls(last), "collapse broke tool_call/tool pairing");

    // All four reads really executed before collapsing kicked in
    QVERIFY(traceHas(agent.traceModel(), "tool_result", "payload"));
}

void TestAgent::traceRestoredOnResume() {
    SqliteMemory memory(QStringLiteral(":memory:"));
    agent.setMemory(&memory);

    QTemporaryDir dir;
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
    QTemporaryDir dir;
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
    QTemporaryDir dir;
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
    QCOMPARE(audit.size(), 1);
    QCOMPARE(audit.at(0).toObject().value("event").toString(), QStringLiteral("tool_declined"));
    QCOMPARE(audit.at(0).toObject().value("toolName").toString(), QStringLiteral("write_file"));
    QVERIFY(!audit.at(0).toObject().value("success").toBool());

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
    QCOMPARE(audit2.size(), 2);
    QCOMPARE(audit2.at(0).toObject().value("event").toString(), QStringLiteral("tool_execute"));
    QCOMPARE(audit2.at(0).toObject().value("toolName").toString(), QStringLiteral("read_file"));
    QVERIFY(audit2.at(0).toObject().value("success").toBool());

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
    QCOMPARE(memory.auditLog(50).size(), 1); // exactly one tool_execute

    // Task 2 (SAME session): ordinary question. A clean provider returns no
    // tool calls, so the agent must answer in a single LLM round and must
    // not execute any tool.
    const int callsBefore = llm.seenMessages.size();
    llm.script.append(textResponse(
        QStringLiteral("Python 装饰器是一种在运行时扩展函数行为的语法。")));
    agent.startTask(QStringLiteral("Python 装饰器是什么？"));
    QTRY_COMPARE(spy.count(), 2);
    QCOMPARE(llm.seenMessages.size(), callsBefore + 1);
    QCOMPARE(memory.auditLog(50).size(), 1); // no new tool execution
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
    QCOMPARE(memory.auditLog(50).size(), 1);
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
    QCOMPARE(memory.auditLog(50).size(), 2); // one execution per KB task
    QVERIFY2(noDanglingToolCalls(llm.seenMessages.last()),
             "Knowledge->Normal->Knowledge left an illegal history");

    agent.setMemory(nullptr);
}

QTEST_GUILESS_MAIN(TestAgent)
#include "test_agent.moc"
