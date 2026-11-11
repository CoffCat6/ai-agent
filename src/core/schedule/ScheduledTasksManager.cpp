#include "core/schedule/ScheduledTasksManager.h"

#include "core/agent/AIAgent.h"
#include "core/agent/PermissionMode.h"
#include "core/memory/SqliteMemory.h"

#include <QJsonDocument>
#include <QTimer>
#include <QVariantList>
#include <QtGlobal>

namespace NexusAI {
namespace {

// audit_log.session_id 是 NOT NULL 而调度级事件（missed/skipped/CRUD）没有
// 会话——用合成标记行，同 searchMessages 的 "__search_notice__" 先例。
QString schedulerAuditSession() { return QStringLiteral("__scheduler__"); }

QDateTime fallbackClock() { return QDateTime::currentDateTime(); }

// schedule_value → ScheduleSpec；坏行静默跳过（tick 每 30s 一次，qWarning
// 会刷屏；编辑器打开时 validationError 会暴露问题）。
std::optional<ScheduleSpec> specOf(const QVariantMap& task) {
    const QJsonObject json = QJsonDocument::fromJson(
        task.value(QStringLiteral("scheduleValue")).toString().toUtf8()).object();
    auto spec = ScheduleSpec::fromJson(json);
    if (!spec || !spec->isValid())
        return std::nullopt;
    return spec;
}

QDateTime anchorOf(const QVariantMap& task) {
    const qint64 lastFired = task.value(QStringLiteral("lastFiredAt")).toLongLong();
    if (lastFired > 0)
        return QDateTime::fromMSecsSinceEpoch(lastFired);
    const qint64 created = task.value(QStringLiteral("createdAt")).toLongLong();
    return created > 0 ? QDateTime::fromMSecsSinceEpoch(created) : QDateTime();
}

} // namespace

ScheduledTasksManager::ScheduledTasksManager(SqliteMemory* memory, Clock clock,
                                             QObject* parent)
    : QObject(parent),
      m_memory(memory),
      m_clock(clock ? std::move(clock) : fallbackClock) {
    Q_ASSERT(m_memory);
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::CoarseTimer);
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ScheduledTasksManager::tick);
}

ScheduledTasksManager::~ScheduledTasksManager() {
    // 收口在跑 run（不依赖 aboutToQuit 被显式接线）；timer 随父子对象销毁。
    shutdown();
}

void ScheduledTasksManager::setAgentFactory(AgentFactory factory) {
    m_agentFactory = std::move(factory);
}

void ScheduledTasksManager::setProfileResolver(ProfileResolver resolver) {
    m_profileResolver = std::move(resolver);
}

void ScheduledTasksManager::setModelConfigurator(ModelConfigurator configurator) {
    m_modelConfigurator = std::move(configurator);
}

void ScheduledTasksManager::setMaxRunMinutesProvider(MaxRunMinutesProvider provider) {
    m_maxRunMinutes = std::move(provider);
}

void ScheduledTasksManager::startTicking() {
    if (!m_timer->isActive())
        m_timer->start();
}

void ScheduledTasksManager::tick() {
    if (m_shuttingDown) return;
    const QDateTime now = m_clock();
    const QVariantList tasks = m_memory->listScheduledTasks();
    bool changed = false;

    for (const QVariant& entry : tasks) {
        const QVariantMap task = entry.toMap();
        if (!task.value(QStringLiteral("enabled")).toBool())
            continue;
        const auto spec = specOf(task);
        if (!spec)
            continue;
        const QDateTime anchor = anchorOf(task);
        if (!anchor.isValid())
            continue;
        const QDateTime due = spec->nextOccurrence(anchor, anchor);
        if (!due.isValid() || due > now)
            continue;
        // 到期（含宽限窗）→ 触发；过期太深 → 错过收敛。
        if (now.toMSecsSinceEpoch() - due.toMSecsSinceEpoch() <= kGraceMs) {
            fireTask(task, QStringLiteral("schedule"), due);
            changed = true;
        } else {
            // 锚点一次推进到最近应触发点，只记一条 missed（D5 不补跑）。
            const QDateTime latest = spec->latestOccurrenceAtOrBefore(anchor, now);
            if (!latest.isValid())
                continue;
            const QString id = task.value(QStringLiteral("id")).toString();
            const qint64 runId = m_memory->addScheduledTaskRun(
                id, QStringLiteral("schedule"), latest.toMSecsSinceEpoch());
            if (runId > 0) {
                m_memory->finishScheduledTaskRun(
                    runId, QStringLiteral("missed"), QString(),
                    tr("应用未运行期间错过该次触发，未补跑"),
                    QString(), latest.toMSecsSinceEpoch());
            }
            m_memory->markScheduledTaskFired(id, latest.toMSecsSinceEpoch());
            m_memory->logAudit(schedulerAuditSession(),
                               QStringLiteral("schedule_missed"), QString(),
                               QString(), false, id, 0);
            changed = true;
        }
    }
    if (changed)
        emit tasksChanged();
}

QVariantMap ScheduledTasksManager::upsertTask(const QVariantMap& task) {
    const QVariantMap stored = m_memory->upsertScheduledTask(task);
    if (!stored.isEmpty())
        m_memory->logAudit(schedulerAuditSession(),
                           QStringLiteral("schedule_task_saved"), QString(),
                           QString(), true,
                           stored.value(QStringLiteral("id")).toString(), 0);
    tick();   // 下次触发时间即时反映，不等 30s
    emit tasksChanged();
    return stored;
}

bool ScheduledTasksManager::deleteTask(const QString& id) {
    // 在跑的就是这个任务 → 先收口为 stopped，再删（run 行随级联消失）。
    if (m_activeRun.valid && m_activeRun.taskId == id) {
        m_activeRun.terminal = true;
        if (m_activeAgent) {
            disconnect(m_activeAgent, nullptr, this, nullptr);
            m_activeAgent->stop();
            m_activeAgent->deleteLater();
            m_activeAgent = nullptr;
        }
        m_activeRun = ActiveRun{};
    }
    const bool ok = m_memory->deleteScheduledTask(id);
    if (ok)
        m_memory->logAudit(schedulerAuditSession(),
                           QStringLiteral("schedule_task_deleted"), QString(),
                           QString(), true, id, 0);
    emit tasksChanged();
    return ok;
}

bool ScheduledTasksManager::setTaskEnabled(const QString& id, bool enabled) {
    const bool ok = m_memory->setScheduledTaskEnabled(id, enabled);
    tick();
    emit tasksChanged();
    return ok;
}

QVariantList ScheduledTasksManager::tasks() const {
    return m_memory->listScheduledTasks();
}

QVariantList ScheduledTasksManager::runsForTask(const QString& taskId, int limit) const {
    return m_memory->listScheduledTaskRuns(taskId, limit);
}

bool ScheduledTasksManager::runNow(const QString& taskId) {
    if (m_shuttingDown)
        return false;
    // 手动触发撞闸：返回 false 让 UI toast，不落 run 行。
    if (m_activeRun.valid)
        return false;
    for (const QVariant& entry : m_memory->listScheduledTasks()) {
        const QVariantMap task = entry.toMap();
        if (task.value(QStringLiteral("id")).toString() == taskId) {
            fireTask(task, QStringLiteral("manual"), QDateTime());
            emit tasksChanged();
            return true;
        }
    }
    return false;
}

QDateTime ScheduledTasksManager::nextDueFor(const QVariantMap& task) const {
    const auto spec = specOf(task);
    if (!spec)
        return QDateTime();
    const QDateTime anchor = anchorOf(task);
    if (!anchor.isValid())
        return QDateTime();
    return spec->nextOccurrence(anchor, anchor);
}

qint64 ScheduledTasksManager::nextDueMs(const QVariantMap& task) const {
    const QDateTime due = nextDueFor(task);
    return due.isValid() ? due.toMSecsSinceEpoch() : 0;
}

QString ScheduledTasksManager::describeTask(const QVariantMap& task) const {
    const auto spec = specOf(task);
    return spec ? spec->displayText() : QString();
}

void ScheduledTasksManager::fireTask(const QVariantMap& task,
                                     const QString& triggerSource,
                                     const QDateTime& dueAt) {
    if (m_shuttingDown)
        return;
    const QString id = task.value(QStringLiteral("id")).toString();
    const QString name = task.value(QStringLiteral("name")).toString();

    // 全局并发闸（D1）：同时最多 1 个定时 run。计划触发撞闸 → 消耗本次
    // 周期（推进锚点）并记 skipped_busy；手动撞闸已在 runNow 拦下。
    if (m_activeRun.valid) {
        const qint64 runId = m_memory->addScheduledTaskRun(
            id, QStringLiteral("schedule"),
            dueAt.isValid() ? dueAt.toMSecsSinceEpoch() : 0);
        if (runId > 0) {
            m_memory->finishScheduledTaskRun(
                runId, QStringLiteral("skipped_busy"), QString(),
                tr("已有定时任务在运行，本次触发跳过"), QString(),
                dueAt.isValid() ? dueAt.toMSecsSinceEpoch() : 0);
        }
        if (triggerSource == QLatin1String("schedule") && dueAt.isValid())
            m_memory->markScheduledTaskFired(id, dueAt.toMSecsSinceEpoch());
        m_memory->logAudit(schedulerAuditSession(),
                           QStringLiteral("schedule_skipped_busy"), QString(),
                           QString(), false, id, 0);
        emit tasksChanged();
        return;
    }

    // run 行先落（running）+ 推进锚点；手动触发不消耗调度周期。
    const qint64 runId = m_memory->addScheduledTaskRun(
        id, triggerSource,
        dueAt.isValid() ? dueAt.toMSecsSinceEpoch() : 0);
    if (triggerSource == QLatin1String("schedule") && dueAt.isValid())
        m_memory->markScheduledTaskFired(id, dueAt.toMSecsSinceEpoch());
    if (runId <= 0)
        return;   // 落行失败：本轮放弃（下个周期重来）

    m_activeRun = ActiveRun{};
    m_activeRun.valid = true;
    m_activeRun.runId = runId;
    m_activeRun.taskId = id;
    m_activeRun.taskName = name;
    m_activeRun.notify = task.value(QStringLiteral("notify"), true).toBool();

    // profile 解析失败：不建 agent，run 记 failed（可读原因）。
    const QString profileId = task.value(QStringLiteral("profileId")).toString();
    QVariantMap profile;
    if (m_profileResolver)
        profile = m_profileResolver(profileId);
    if (profile.isEmpty()) {
        finishActiveRun(QStringLiteral("failed"), QString(),
                        tr("模型配置已删除或不可用（%1）").arg(profileId), QString());
        return;
    }

    if (!m_agentFactory || !m_modelConfigurator) {
        finishActiveRun(QStringLiteral("failed"), QString(),
                        tr("调度器未装配执行环境"), QString());
        return;
    }
    auto* agent = m_agentFactory();
    if (!agent) {
        finishActiveRun(QStringLiteral("failed"), QString(),
                        tr("创建执行环境失败"), QString());
        return;
    }
    m_activeAgent = agent;

    // 与 tab 会话同源的配置链（服务/上下文上限/local 判定），再叠无人值守
    // 三层防挂起：权限档（默认 plan_only）+ unattended 拒绝 + 墙钟封口。
    m_modelConfigurator(agent, profile);
    agent->setPermissionMode(permissionModeFromString(
        task.value(QStringLiteral("permissionMode")).toString()));
    agent->setUnattended(true);
    const int maxMinutes = m_maxRunMinutes ? qMax(0, m_maxRunMinutes()) : 10;
    if (maxMinutes > 0)
        agent->setTaskTimeoutMs(maxMinutes * 60 * 1000);

    // 会话惰性创建（AIAgent::startTask 内）后捕获 id 供 run 行与侧栏跳转。
    m_activeRun.sessionConn = connect(agent, &AIAgent::sessionChanged, this,
        [this](const QString& sessionId) {
            if (m_activeRun.valid && m_activeRun.sessionId.isEmpty())
                m_activeRun.sessionId = sessionId;
        });

    // 首个终态信号生效（taskTimedOut 先于 finishTask 的 responseReady 到达）。
    connect(agent, &AIAgent::responseReady, this, [this](const QString& text) {
        finishActiveRun(QStringLiteral("success"),
                        m_activeRun.sessionId, QString(), text);
    });
    connect(agent, &AIAgent::errorOccurred, this, [this](const QString& error) {
        finishActiveRun(QStringLiteral("failed"),
                        m_activeRun.sessionId, error, QString());
    });
    connect(agent, &AIAgent::taskTimedOut, this, [this]() {
        finishActiveRun(QStringLiteral("timeout"), m_activeRun.sessionId,
                        tr("运行超过上限，已强制封口"), QString());
    });
    connect(agent, &QObject::destroyed, this, [this]() {
        m_activeAgent = nullptr;
        finishActiveRun(QStringLiteral("failed"), m_activeRun.sessionId,
                        tr("执行环境异常退出"), QString());
    });

    if (!agent->startTask(task.value(QStringLiteral("prompt")).toString())) {
        finishActiveRun(QStringLiteral("failed"), m_activeRun.sessionId,
                        tr("任务启动失败（模型未就绪或配额已满）"), QString());
    }
}

void ScheduledTasksManager::finishActiveRun(const QString& status,
                                            const QString& sessionId,
                                            const QString& error,
                                            const QString& summary) {
    if (!m_activeRun.valid || m_activeRun.terminal)
        return;
    m_activeRun.terminal = true;

    const QString sid = !sessionId.isEmpty() ? sessionId : m_activeRun.sessionId;
    // 终态时间戳走注入时钟（§11：冻结时钟下不许混入真实墙钟）。
    m_memory->finishScheduledTaskRun(m_activeRun.runId, status, sid,
                                     error, summarize(summary),
                                     m_clock().toMSecsSinceEpoch());

    auto* agent = m_activeAgent;
    if (agent) {
        disconnect(agent, nullptr, this, nullptr);   // 摘掉终态/destroyed 监听
        agent->deleteLater();
        m_activeAgent = nullptr;
    }
    const QString taskName = m_activeRun.taskName;
    const QString taskId = m_activeRun.taskId;
    const bool notifyRun = m_activeRun.notify;
    const bool success = status == QLatin1String("success");
    m_activeRun = ActiveRun{};

    m_memory->logAudit(sid.isEmpty() ? schedulerAuditSession() : sid,
                       QStringLiteral("schedule_finished"),
                       QString(), QString(), success,
                       QStringLiteral("%1: %2").arg(taskName, status), 0);
    if (notifyRun)
        emit runFinished(taskId, taskName, success, sid, summarize(summary));
    if (!sid.isEmpty())
        emit sessionsMayHaveChanged();
    emit tasksChanged();
}

QString ScheduledTasksManager::summarize(const QString& text) const {
    QString flat = text.simplified();
    if (flat.size() > kSummaryChars)
        flat = flat.left(kSummaryChars) + QStringLiteral("…");
    return flat;
}

void ScheduledTasksManager::shutdown() {
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (m_timer)
        m_timer->stop();
    if (m_activeRun.valid && m_activeAgent) {
        // 同步封口：stop() 停 LLM/协作取消在飞工具并封历史；run 记 stopped。
        disconnect(m_activeAgent, nullptr, this, nullptr);
        m_activeAgent->stop();
        m_memory->finishScheduledTaskRun(
            m_activeRun.runId, QStringLiteral("stopped"),
            m_activeRun.sessionId, tr("应用退出，任务已停止"), QString());
        m_activeAgent->deleteLater();
        m_activeAgent = nullptr;
        m_activeRun = ActiveRun{};
    }
}

} // namespace NexusAI
