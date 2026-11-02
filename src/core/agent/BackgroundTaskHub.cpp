#include "BackgroundTaskHub.h"

#include <QDateTime>
#include <QRunnable>
#include <QTimer>

namespace NexusAI {

namespace {

constexpr int kProgressTail = 3;

QString terminalStateFor(const SubAgentReport& report) {
    if (report.stop == SubAgentReport::Stop::Cancelled)
        return QStringLiteral("cancelled");
    return report.ok ? QStringLiteral("completed") : QStringLiteral("failed");
}

bool isTerminal(const QString& state) {
    return state == QLatin1String("completed")
        || state == QLatin1String("failed")
        || state == QLatin1String("cancelled");
}

} // namespace

BackgroundTaskHub::BackgroundTaskHub(QObject* parent) : QObject(parent) {
    // A private pool: background runs must not starve the global pool used by
    // the primary agent's tools. One thread per allowed task.
    m_pool.setMaxThreadCount(kMaxTasks);
    m_pool.setExpiryTimeout(30000);
}

BackgroundTaskHub::~BackgroundTaskHub() {
    cancelAll();
    m_pool.waitForDone();
}

QString BackgroundTaskHub::createTask(const QString& goal, const QString& role,
                                      const ITool::Cancellation& cancelled,
                                      Work work) {
    QString id;
    {
        QMutexLocker lock(&m_mutex);
        int active = 0;
        for (const auto& entry : m_tasks) {
            if (!isTerminal(entry.state)) ++active;
        }
        if (active >= kMaxTasks) return {};
        id = QStringLiteral("sa-%1").arg(++m_seq);
        Entry entry;
        entry.id = id;
        entry.goal = goal;
        entry.role = role;
        entry.state = QStringLiteral("queued");
        entry.startedAtMs = QDateTime::currentMSecsSinceEpoch();
        entry.cancelled = cancelled;
        m_tasks.insert(id, entry);
        m_order.append(id);
    }
    emit changed();
    m_pool.start(QRunnable::create([this, id, work = std::move(work)]() {
        if (!markRunning(id)) return;   // cancelled while queued
        work(id);
    }));
    // TTL is armed on the hub's thread (createTask may run on a pool thread;
    // QTimer::singleShot with a receiver is thread-safe).
    QTimer::singleShot(kTaskTtlMs, this, [this, id]() { expire(id); });
    return id;
}

void BackgroundTaskHub::expire(const QString& id) {
    {
        QMutexLocker lock(&m_mutex);
        Entry* entry = findEntry(id);
        if (!entry || isTerminal(entry->state)) return;
        entry->expired = true;
        if (entry->cancelled) entry->cancelled->store(true);
        if (entry->state == QLatin1String("queued")) {
            entry->state = QStringLiteral("failed");
            entry->error = QStringLiteral(
                "Background task exceeded the 30 minute time limit.");
            entry->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
        }
        m_cv.wakeAll();
    }
    emit changed();
}

bool BackgroundTaskHub::markRunning(const QString& id) {
    {
        QMutexLocker lock(&m_mutex);
        Entry* entry = findEntry(id);
        if (!entry || entry->state != QLatin1String("queued")) return false;
        entry->state = QStringLiteral("running");
    }
    emit changed();
    return true;
}

void BackgroundTaskHub::updateProgress(const QString& id, const QString& phase,
                                       const QString& text) {
    {
        QMutexLocker lock(&m_mutex);
        Entry* entry = findEntry(id);
        if (!entry || isTerminal(entry->state)) return;
        entry->progress.append({phase, text});
        while (entry->progress.size() > kProgressTail)
            entry->progress.removeFirst();
    }
    emit changed();
}

void BackgroundTaskHub::finishTask(const QString& id, const SubAgentReport& report) {
    {
        QMutexLocker lock(&m_mutex);
        Entry* entry = findEntry(id);
        if (!entry) return;
        entry->state = entry->expired
            ? QStringLiteral("failed") : terminalStateFor(report);
        entry->report = report;
        entry->hasReport = true;
        entry->steps = report.steps;
        entry->proposalCount = report.proposals.size();
        entry->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
        if (entry->expired) {
            entry->error = QStringLiteral(
                "Background task exceeded the 30 minute time limit.");
        } else if (!report.ok && report.stop != SubAgentReport::Stop::Cancelled) {
            entry->error = report.text;
        }
        m_cv.wakeAll();
    }
    emit changed();
}

bool BackgroundTaskHub::cancel(const QString& id) {
    bool cancelledNow = false;
    {
        QMutexLocker lock(&m_mutex);
        Entry* entry = findEntry(id);
        if (!entry || isTerminal(entry->state)) return false;
        if (entry->cancelled) entry->cancelled->store(true);
        // A queued task has no runner yet: finish it here so the panel and
        // subagent_wait see a terminal state without waiting for the pool.
        if (entry->state == QLatin1String("queued")) {
            entry->state = QStringLiteral("cancelled");
            entry->error = QStringLiteral("cancelled before start");
            entry->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
        }
        // A running task keeps its state until the runner unwinds and calls
        // finishTask(); the token is what it polls.
        cancelledNow = true;
        m_cv.wakeAll();
    }
    if (cancelledNow) emit changed();
    return cancelledNow;
}

void BackgroundTaskHub::cancelAll() {
    QStringList ids;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_tasks.cbegin(); it != m_tasks.cend(); ++it) {
            if (!isTerminal(it->state)) ids.append(it.key());
        }
    }
    for (const QString& id : ids) cancel(id);
}

QVariantList BackgroundTaskHub::snapshots() const {
    QVariantList out;
    QMutexLocker lock(&m_mutex);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& id : m_order) {
        const auto it = m_tasks.constFind(id);
        if (it == m_tasks.cend()) continue;
        const Entry& entry = it.value();
        QVariantList tail;
        for (const auto& item : entry.progress)
            tail.append(QVariantMap{{QStringLiteral("phase"), item.first},
                                    {QStringLiteral("text"), item.second}});
        const qint64 end = entry.finishedAtMs > 0 ? entry.finishedAtMs : now;
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("goal"), entry.goal},
            {QStringLiteral("role"), entry.role},
            {QStringLiteral("state"), entry.state},
            {QStringLiteral("startedAt"), entry.startedAtMs},
            {QStringLiteral("elapsedMs"), qMax<qint64>(0, end - entry.startedAtMs)},
            {QStringLiteral("steps"), entry.steps},
            {QStringLiteral("proposalCount"), entry.proposalCount},
            {QStringLiteral("progressTail"), tail},
            {QStringLiteral("error"), entry.error}});
    }
    return out;
}

int BackgroundTaskHub::activeCount() const {
    QMutexLocker lock(&m_mutex);
    int active = 0;
    for (const auto& entry : m_tasks) {
        if (!isTerminal(entry.state)) ++active;
    }
    return active;
}

bool BackgroundTaskHub::hasTask(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    return m_tasks.contains(id);
}

QString BackgroundTaskHub::stateLabel(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    return it == m_tasks.cend() ? QString() : it->state;
}

std::optional<SubAgentReport> BackgroundTaskHub::reportFor(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    if (it == m_tasks.cend() || !it->hasReport) return std::nullopt;
    return it->report;
}

QString BackgroundTaskHub::waitFor(const QString& id, int timeoutMs,
                                   const ITool::Cancellation& waiterCancelled) {
    QElapsedTimer timer;
    timer.start();
    QMutexLocker lock(&m_mutex);
    while (true) {
        const auto it = m_tasks.constFind(id);
        if (it == m_tasks.cend()) return QStringLiteral("missing");
        if (isTerminal(it->state)) return it->state;
        if (waiterCancelled && waiterCancelled->load()) return it->state;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) return it->state;
        m_cv.wait(&m_mutex, static_cast<unsigned long>(qMin(remaining, 250)));
    }
}

void BackgroundTaskHub::removeFinished() {
    bool any = false;
    {
        QMutexLocker lock(&m_mutex);
        QVector<QString> kept;
        kept.reserve(m_order.size());
        for (const QString& id : m_order) {
            const auto it = m_tasks.constFind(id);
            if (it == m_tasks.cend()) continue;
            if (isTerminal(it->state)) {
                m_tasks.erase(it);
                any = true;
            } else {
                kept.append(id);
            }
        }
        m_order = std::move(kept);
    }
    if (any) emit changed();
}

BackgroundTaskHub::Entry* BackgroundTaskHub::findEntry(const QString& id) {
    const auto it = m_tasks.find(id);
    return it == m_tasks.end() ? nullptr : &it.value();
}

} // namespace NexusAI