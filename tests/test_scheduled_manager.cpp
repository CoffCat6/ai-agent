// docs/50 P2：ScheduledTasksManager 全链——冻结时钟矩阵（到期/宽限/错过/
// 未到期/停用）、假 LLM 全链（fire→success）、unattended 确认拒绝、超时封口、
// profile 失效、并发闸（skipped_busy / runNow=false）、手动运行、通知开关、
// 停机记 stopped。
//
// 时钟冻结：manager 的 Clock 注入可变成员 `now`，测试推进时钟后手动 tick()。
// 任务锚点用 lastFiredAt 显式给定（createdAt 是真实墙钟，不可控）。
#include "core/schedule/ScheduledTasksManager.h"

#include "core/agent/AIAgent.h"
#include "core/agent/LLMTypes.h"
#include "core/memory/SqliteMemory.h"
#include "core/tools/ToolRegistry.h"
#include "infrastructure/llm/IAIService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace NexusAI;

namespace {

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

// 永不响应的假服务：chatComplete 什么都不发——任务只能靠墙钟超时封口，
// 或作为占位让并发闸一直占用。
class StuckService : public IAIService {
    Q_OBJECT
public:
    void chatComplete(const QJsonArray&, const QJsonArray&) override {}
    void cancel() override {}
    void setApiKey(const QString&) override {}
    void setModel(const QString&) override {}
    void setBaseUrl(const QString&) override {}
    QString model() const override { return QStringLiteral("stuck-model"); }
};

// 与 test_agent 的 FakeAIService 同型：脚本化同步响应。
class FakeService : public IAIService {
    Q_OBJECT
public:
    QVector<LLMResponse> script;
    QVector<QJsonArray> seenMessages;

    void chatComplete(const QJsonArray& messages, const QJsonArray&) override {
        seenMessages.append(messages);
        LLMResponse response;
        if (script.isEmpty()) {
            emit errorOccurred(QStringLiteral("FakeService: empty script"));
            return;
        }
        response = script.takeFirst();
        emit requestStarted(1);
        if (response.success)
            emit responseReady(response);
        else
            emit errorOccurred(response.error);
    }
    void cancel() override {}
    void setApiKey(const QString&) override {}
    void setModel(const QString&) override {}
    void setBaseUrl(const QString&) override {}
    QString model() const override { return QStringLiteral("fake-model"); }
};

// 2026-11-10 是周二；daily 用例与星期无关。
QDateTime T(int day, int h, int m, int s = 0) {
    return QDateTime(QDate(2026, 11, day), QTime(h, m, s),
                     QTimeZone::systemTimeZone());
}

QVariantMap taskFixture(const QString& name, qint64 lastFiredAtMs = 0) {
    QVariantMap task;
    task.insert(QStringLiteral("name"), name);
    task.insert(QStringLiteral("prompt"), QStringLiteral("run"));
    task.insert(QStringLiteral("profileId"), QStringLiteral("p1"));
    task.insert(QStringLiteral("profileName"), QStringLiteral("P1"));
    task.insert(QStringLiteral("scheduleKind"), QStringLiteral("daily"));
    task.insert(QStringLiteral("scheduleValue"),
                QStringLiteral(R"({"kind":"daily","time":"09:00"})"));
    if (lastFiredAtMs > 0)
        task.insert(QStringLiteral("lastFiredAt"), lastFiredAtMs);
    return task;
}

QString latestRunStatus(SqliteMemory& mem, const QString& taskId) {
    const QVariantList runs = mem.listScheduledTaskRuns(taskId, 1);
    return runs.isEmpty() ? QString()
                          : runs.at(0).toMap().value(QStringLiteral("status")).toString();
}

qint64 lastFiredOf(SqliteMemory& mem, const QString& taskId) {
    for (const QVariant& row : mem.listScheduledTasks()) {
        if (row.toMap().value(QStringLiteral("id")).toString() == taskId)
            return row.toMap().value(QStringLiteral("lastFiredAt")).toLongLong();
    }
    return -1;
}

// 在最后一条 wire 请求里找 tool 角色消息的回灌正文。
QString lastToolFeedback(FakeService& llm) {
    const QJsonArray messages = llm.seenMessages.last();
    QString found;
    for (const auto& entry : messages) {
        const QJsonObject msg = entry.toObject();
        if (msg.value("role").toString() == QLatin1String("tool"))
            found = msg.value("content").toString();
    }
    return found;
}

} // namespace

class TestScheduledManager : public QObject {
    Q_OBJECT

private slots:
    void init() {
        m_mem = std::make_unique<SqliteMemory>(QStringLiteral(":memory:"));
        m_now = T(11, 8, 55);   // 基准：11 日 08:55
        m_mgr = std::make_unique<ScheduledTasksManager>(m_mem.get(), [this]() {
            return m_now;
        });
        m_serviceForNext = &m_llm;
        m_configuratorTimeoutMs = 0;
        m_maxRunMinutes = 10;
        m_agents.clear();   // QtTest 共用测试对象：上一用例的登记不带入本用例
        m_mgr->setAgentFactory([this]() -> AIAgent* {
            auto* agent = new AIAgent();
            agent->setToolRegistry(&ToolRegistry::instance());
            agent->setOwnedMemory(
                std::make_unique<SqliteMemory>(QStringLiteral(":memory:")));
            m_agents.append(agent);
            return agent;
        });
        m_mgr->setModelConfigurator([this](AIAgent* agent, const QVariantMap&) {
            agent->setLLMService(m_serviceForNext);
            agent->setContextLimit(8192);
            // 测试钩子：manager 层 setTaskTimeoutMs 之后的覆盖通道（超时
            // 用例把墙钟压到 30ms；0 = 不覆盖）。
            if (m_configuratorTimeoutMs > 0)
                agent->setTaskTimeoutMs(m_configuratorTimeoutMs);
        });
        m_mgr->setMaxRunMinutesProvider([this]() { return m_maxRunMinutes; });
        // 测试的配置器直接注入假服务，profile 本体只要求非空（空 = 失效）。
        m_mgr->setProfileResolver([](const QString&) {
            return QVariantMap{{QStringLiteral("id"), QStringLiteral("p1")}};
        });
    }

    void cleanup() {
        m_mgr->shutdown();
        m_mgr.reset();
        m_mem.reset();
        // 终态 agent 走 deleteLater；QTRY/qWait 已驱动过事件循环，这里再
        // 兜底转一圈让残余清理落地（不手动 delete，避免与 deleteLater 撞）。
        QTest::qWait(0);
    }

    // ── 调度矩阵（冻结时钟）──
    void firesWithinGraceWindow();
    void pastGraceRecordsMissedOnce();
    void notDueYetDoesNothing();
    void disabledTaskSkipped();
    // ── 执行链 ──
    void successChainPersistsSessionAndAnchor();
    void unattendedRejectsConfirmTool();
    void timeoutSealsRun();
    void profileMissingFailsReadable();
    // ── 并发与手动 ──
    void busyGateSkipsScheduleFiresAndBlocksRunNow();
    void runNowFiresWithoutConsumingSchedule();
    void notifyOffSilencesSignal();
    // ── 停机 ──
    void shutdownStopsRunningRun();

private:
    QString upsert(const QVariantMap& task) {
        const QVariantMap stored = m_mgr->upsertTask(task);
        return stored.value(QStringLiteral("id")).toString();
    }

    std::unique_ptr<SqliteMemory> m_mem;
    std::unique_ptr<ScheduledTasksManager> m_mgr;
    QDateTime m_now;
    FakeService m_llm;
    StuckService m_stuck;
    IAIService* m_serviceForNext{nullptr};
    int m_configuratorTimeoutMs{0};
    int m_maxRunMinutes{10};
    QVector<AIAgent*> m_agents;
};

// ── 调度矩阵 ──────────────────────────────────────────────

void TestScheduledManager::firesWithinGraceWindow() {
    m_llm.script.append(textResponse(QStringLiteral("scheduled ok")));
    const QString id = upsert(taskFixture(
        QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch()));   // 锚点：昨天 09:00

    QSignalSpy finishedSpy(m_mgr.get(), &ScheduledTasksManager::runFinished);
    QSignalSpy sessionsSpy(m_mgr.get(), &ScheduledTasksManager::sessionsMayHaveChanged);

    m_now = T(11, 9, 0, 30);   // 到期 +30s，落在 2 分钟宽限窗内
    m_mgr->tick();

    QCOMPARE(latestRunStatus(*m_mem, id), QStringLiteral("success"));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toString(), id);
    QCOMPARE(finishedSpy.at(0).at(1).toString(), QStringLiteral("t"));
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), true);
    QCOMPARE(finishedSpy.at(0).at(4).toString(), QStringLiteral("scheduled ok"));
    QCOMPARE(sessionsSpy.count(), 1);
    // 锚点推进到 due（今天 09:00），而不是"现在"。
    QCOMPARE(lastFiredOf(*m_mem, id), T(11, 9, 0).toMSecsSinceEpoch());
}

void TestScheduledManager::pastGraceRecordsMissedOnce() {
    const QString id = upsert(taskFixture(
        QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch()));

    QSignalSpy finishedSpy(m_mgr.get(), &ScheduledTasksManager::runFinished);
    m_now = T(11, 9, 2, 1);   // 到期 +121s：刚出宽限窗
    m_mgr->tick();

    QCOMPARE(latestRunStatus(*m_mem, id), QStringLiteral("missed"));
    QCOMPARE(finishedSpy.count(), 0);   // skipped/missed 不发 runFinished
    // 锚点一次推进到最近应触发点（今天 09:00），下个 tick 不再补记。
    QCOMPARE(lastFiredOf(*m_mem, id), T(11, 9, 0).toMSecsSinceEpoch());
    m_now = m_now.addMSecs(60 * 1000);
    m_mgr->tick();
    QCOMPARE(m_mem->listScheduledTaskRuns(id).size(), 1);
}

void TestScheduledManager::notDueYetDoesNothing() {
    const QString id = upsert(taskFixture(
        QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch()));
    QSignalSpy changedSpy(m_mgr.get(), &ScheduledTasksManager::tasksChanged);

    m_now = T(11, 8, 30);   // 距 09:00 还早
    m_mgr->tick();

    QCOMPARE(m_mem->listScheduledTaskRuns(id).size(), 0);
    QCOMPARE(lastFiredOf(*m_mem, id), T(10, 9, 0).toMSecsSinceEpoch());
    QCOMPARE(changedSpy.count(), 0);
}

void TestScheduledManager::disabledTaskSkipped() {
    QVariantMap task = taskFixture(QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch());
    task.insert(QStringLiteral("enabled"), false);
    const QString id = upsert(task);

    m_now = T(11, 9, 1, 0);
    m_mgr->tick();

    QCOMPARE(m_mem->listScheduledTaskRuns(id).size(), 0);
    QCOMPARE(lastFiredOf(*m_mem, id), T(10, 9, 0).toMSecsSinceEpoch());
}

// ── 执行链 ────────────────────────────────────────────────

void TestScheduledManager::successChainPersistsSessionAndAnchor() {
    m_llm.script.append(textResponse(QStringLiteral("scheduled ok")));
    const QString id = upsert(taskFixture(
        QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch()));

    m_now = T(11, 9, 0, 10);
    m_mgr->tick();

    const QVariantList runs = m_mem->listScheduledTaskRuns(id);
    QCOMPARE(runs.size(), 1);
    const QVariantMap run = runs.at(0).toMap();
    QCOMPARE(run.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(run.value(QStringLiteral("triggerSource")).toString(), QStringLiteral("schedule"));
    QVERIFY(!run.value(QStringLiteral("sessionId")).toString().isEmpty());
    QCOMPARE(run.value(QStringLiteral("summary")).toString(), QStringLiteral("scheduled ok"));
    QVERIFY(run.value(QStringLiteral("finishedAt")).toLongLong() >=
            run.value(QStringLiteral("startedAt")).toLongLong());
}

void TestScheduledManager::unattendedRejectsConfirmTool() {
    // ConfirmChanges 模式 + shell_exec（RequiresConfirm，非 autoEditSafe）：
    // 无人值守时确认分支必须直接拒绝回灌，任务照常走到终态。
    QVariantMap task = taskFixture(QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch());
    task.insert(QStringLiteral("permissionMode"), QStringLiteral("confirm"));
    const QString id = upsert(task);
    m_llm.script.append(toolResponse(QStringLiteral("shell_exec"),
                                     QJsonObject{{"command", "echo hi"}}));
    m_llm.script.append(textResponse(QStringLiteral("done without it")));

    m_now = T(11, 9, 0, 10);
    m_mgr->tick();

    QCOMPARE(latestRunStatus(*m_mem, id), QStringLiteral("success"));
    QVERIFY2(lastToolFeedback(m_llm).contains(QStringLiteral("unattended")),
             qPrintable(lastToolFeedback(m_llm)));
    QVERIFY2(lastToolFeedback(m_llm).contains(QStringLiteral("requires user confirmation")),
             qPrintable(lastToolFeedback(m_llm)));
    // 拒绝留审计痕（agent 自己的 owned memory）。
    QVERIFY(!m_agents.isEmpty());
    bool blockedAudited = false;
    const QJsonArray audit = m_agents.last()->memory()->auditLog(200);
    for (const auto& entry : audit) {
        if (entry.toObject().value("event").toString() == QLatin1String("tool_blocked")
                && entry.toObject().value("detail").toString()
                       .contains(QLatin1String("unattended")))
            blockedAudited = true;
    }
    QVERIFY(blockedAudited);
}

void TestScheduledManager::timeoutSealsRun() {
    // provider 返回 0 → manager 不设墙钟；configurator 压 30ms 到位——
    // 走 AIAgent 真实超时定时器 → taskTimedOut → run 记 timeout。
    m_maxRunMinutes = 0;
    m_configuratorTimeoutMs = 30;
    m_serviceForNext = &m_stuck;
    const QString id = upsert(taskFixture(
        QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch()));

    QSignalSpy finishedSpy(m_mgr.get(), &ScheduledTasksManager::runFinished);
    m_now = T(11, 9, 0, 5);
    m_mgr->tick();

    QTRY_COMPARE_WITH_TIMEOUT(latestRunStatus(*m_mem, id),
                              QStringLiteral("timeout"), 5000);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), false);
    const QVariantMap run = m_mem->listScheduledTaskRuns(id).at(0).toMap();
    QVERIFY(!run.value(QStringLiteral("error")).toString().isEmpty());
}

void TestScheduledManager::profileMissingFailsReadable() {
    // 不装配 resolver → profile 解析为空 → run failed（可读原因），不建 agent。
    m_mgr->setProfileResolver([](const QString&) { return QVariantMap{}; });
    const QString id = upsert(taskFixture(
        QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch()));

    QSignalSpy finishedSpy(m_mgr.get(), &ScheduledTasksManager::runFinished);
    m_now = T(11, 9, 0, 10);
    m_mgr->tick();

    QCOMPARE(latestRunStatus(*m_mem, id), QStringLiteral("failed"));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), false);
    QVERIFY(finishedSpy.at(0).at(4).toString().isEmpty());
    const QVariantMap run = m_mem->listScheduledTaskRuns(id).at(0).toMap();
    QVERIFY2(run.value(QStringLiteral("error")).toString()
                 .contains(QStringLiteral("模型配置")),
             qPrintable(run.value(QStringLiteral("error")).toString()));
    QVERIFY(run.value(QStringLiteral("sessionId")).toString().isEmpty());
    QVERIFY(m_agents.isEmpty());   // 失败路径不建 agent
}

// ── 并发与手动 ────────────────────────────────────────────

void TestScheduledManager::busyGateSkipsScheduleFiresAndBlocksRunNow() {
    // 任务 A：stuck 服务 + 无墙钟 → run 一直占用闸。
    QVariantMap taskA = taskFixture(QStringLiteral("A"), T(10, 9, 0).toMSecsSinceEpoch());
    taskA.insert(QStringLiteral("prompt"), QStringLiteral("run A"));
    const QString idA = upsert(taskA);
    m_serviceForNext = &m_stuck;
    m_maxRunMinutes = 0;
    m_now = T(11, 9, 0, 5);
    m_mgr->tick();
    QCOMPARE(latestRunStatus(*m_mem, idA), QStringLiteral("running"));
    QVERIFY(!m_agents.isEmpty());
    QCOMPARE(m_agents.last()->state() >= AgentState::Thinking, true);

    // 任务 B 同时到期：计划触发撞闸 → skipped_busy + 锚点照常推进。
    const QString idB = upsert(taskFixture(
        QStringLiteral("B"), T(10, 9, 0).toMSecsSinceEpoch()));
    m_mgr->tick();
    QCOMPARE(latestRunStatus(*m_mem, idB), QStringLiteral("skipped_busy"));
    QCOMPARE(lastFiredOf(*m_mem, idB), T(11, 9, 0).toMSecsSinceEpoch());

    // 手动触发撞闸：直接拒绝，不落 run 行。
    QVERIFY(!m_mgr->runNow(idB));
    QCOMPARE(m_mem->listScheduledTaskRuns(idB).size(), 1);
}

void TestScheduledManager::runNowFiresWithoutConsumingSchedule() {
    m_llm.script.append(textResponse(QStringLiteral("manual ok")));
    const QString id = upsert(taskFixture(
        QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch()));

    m_now = T(11, 8, 0);   // 未到期
    QVERIFY(m_mgr->runNow(id));

    const QVariantList runs = m_mem->listScheduledTaskRuns(id);
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("success"));
    QCOMPARE(runs.at(0).toMap().value(QStringLiteral("triggerSource")).toString(),
             QStringLiteral("manual"));
    // 手动运行不消耗调度周期：锚点原封不动。
    QCOMPARE(lastFiredOf(*m_mem, id), T(10, 9, 0).toMSecsSinceEpoch());
    // 未知 id：false。
    QVERIFY(!m_mgr->runNow(QStringLiteral("nope")));
}

void TestScheduledManager::notifyOffSilencesSignal() {
    QVariantMap task = taskFixture(QStringLiteral("t"), T(10, 9, 0).toMSecsSinceEpoch());
    task.insert(QStringLiteral("notify"), false);
    const QString id = upsert(task);
    m_llm.script.append(textResponse(QStringLiteral("quiet ok")));

    QSignalSpy finishedSpy(m_mgr.get(), &ScheduledTasksManager::runFinished);
    QSignalSpy sessionsSpy(m_mgr.get(), &ScheduledTasksManager::sessionsMayHaveChanged);
    m_now = T(11, 9, 0, 10);
    m_mgr->tick();

    QCOMPARE(latestRunStatus(*m_mem, id), QStringLiteral("success"));
    QCOMPARE(finishedSpy.count(), 0);   // notify=0：不发通知信号
    QCOMPARE(sessionsSpy.count(), 1);   // 侧栏刷新不受影响
}

// ── 停机 ──────────────────────────────────────────────────

void TestScheduledManager::shutdownStopsRunningRun() {
    QVariantMap taskA = taskFixture(QStringLiteral("A"), T(10, 9, 0).toMSecsSinceEpoch());
    const QString idA = upsert(taskA);
    m_serviceForNext = &m_stuck;
    m_maxRunMinutes = 0;
    m_now = T(11, 9, 0, 5);
    m_mgr->tick();
    QCOMPARE(latestRunStatus(*m_mem, idA), QStringLiteral("running"));

    m_mgr->shutdown();
    QCOMPARE(latestRunStatus(*m_mem, idA), QStringLiteral("stopped"));
    // 二次 shutdown 幂等（析构会再调一次）。
    m_mgr->shutdown();
    QCOMPARE(latestRunStatus(*m_mem, idA), QStringLiteral("stopped"));
    // 停机后 tick 与 runNow 都不再触发。
    m_now = T(11, 9, 1, 0);
    m_mgr->tick();
    QVERIFY(!m_mgr->runNow(idA));
    QCOMPARE(m_mem->listScheduledTaskRuns(idA).size(), 1);
}

QTEST_GUILESS_MAIN(TestScheduledManager)
#include "test_scheduled_manager.moc"
