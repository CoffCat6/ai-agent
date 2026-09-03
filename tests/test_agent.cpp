// AIAgent main-loop branch coverage, driven by a scripted fake LLM service.
// Exercises: direct completion, dangerous-tool exclusion, safe-tool feedback,
// confirmation approve/decline, unknown tools, max-iteration guard, stop and
// the context budget compression hook.
#include "core/agent/AIAgent.h"
#include "core/agent/AgentState.h"
#include "core/tools/ToolRegistry.h"
#include "core/memory/SqliteMemory.h"
#include "infrastructure/llm/IAIService.h"

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
    void traceRestoredOnResume();
    void stopDuringConfirmationIsNoOp();
    void contextBudgetCompressesHistory();
    void auditTrailRecorded();
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
    const QString missing = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + "/nexusai_nonexistent_dir_xyz";
    llm.script.append(toolResponse("list_dir", QJsonObject{{"path", missing}}));
    llm.repeatLast = true;

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

QTEST_GUILESS_MAIN(TestAgent)
#include "test_agent.moc"
