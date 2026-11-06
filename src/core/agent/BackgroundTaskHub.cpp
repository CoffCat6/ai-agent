#include "BackgroundTaskHub.h"


#include <QCoreApplication>
#include <QDateTime>
#include <QRunnable>
#include <QThread>
#include <QTimer>

namespace NexusAI {

namespace {

// The panel shows a live progress timeline while a task runs, so keep a
// deeper tail than the 3 lines the mobile-era tool result summary needed.
constexpr int kProgressTail = 24;

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
    // Ownership contract: constructed on the GUI thread, never re-parented.
    // No QObject parent on purpose: hub teardown must be able to detach the
    // pool (parenting would make QObject's destructor delete it and block).
    m_pool = new QThreadPool();
    // A private pool: background runs must not starve the global pool used by
    // the primary agent's tools. One thread per allowed task.
    m_pool->setMaxThreadCount(kMaxTasks);
    m_pool->setExpiryTimeout(30000);
}

// Teardown order matters: cancel first (flips the runners' shared tokens),
// then drop queued runnables, then wait BOUNDED. Waiting is only legal on the
// owning thread; a pool worker that released the last reference would
// deadlock inside waitForDone(), and QThreadPool's own destructor blocks the
// same way. In that (pathological) case detach the pool and let its threads
// drain/expire instead of hanging the process.
BackgroundTaskHub::~BackgroundTaskHub() {
    {
        QMutexLocker lock(&m_mutex);
        m_shuttingDown = true;
    }
    cancelAll();
    if (!m_pool) return;
    m_pool->clear();
    if (QThread::currentThread() == thread()
            && m_pool->waitForDone(kShutdownWaitMs)) {
        delete m_pool;
        m_pool = nullptr;
        return;
    }
    // Uncooperative task or off-thread destruction: never block teardown.
    // The pool is intentionally detached (leaked) here; its runnables only
    // touch this object before entering user work (see createTask wrapper).
    m_pool = nullptr;
}

bool BackgroundTaskHub::shutdown(int timeoutMs) {
    // Called by AIAgent (GUI thread). From any other thread there is no safe
    // wait: a pool worker must not wait for itself.
    if (QThread::currentThread() != thread()) return false;
    {
        QMutexLocker lock(&m_mutex);
        m_shuttingDown = true;
    }
    cancelAll();
    if (!m_pool) return true;
    m_pool->clear();
    const bool done = m_pool->waitForDone(qMax(0, timeoutMs));
    return done;
}

QString BackgroundTaskHub::currentSessionScope() {
    return BackgroundTaskHubRouter::currentSessionId();
}

bool BackgroundTaskHub::inSession(const Entry& entry, const QString& sessionId) {
    return entry.sessionId == sessionId;
}

QString BackgroundTaskHub::createTask(const QString& goal, const QString& role,
                                      const ITool::Cancellation& cancelled,
                                      Work work, const QString& sessionId,
                                      const QJsonObject& spawnArgs) {
    QString id;
    {
        QMutexLocker lock(&m_mutex);
        if (m_shuttingDown || !m_pool) return {};
        int active = 0;
        for (const auto& entry : m_tasks) {
            if (!isTerminal(entry.state)) ++active;
        }
        if (active >= kMaxTasks) return {};
        id = QStringLiteral("sa-%1").arg(++m_seq);
        Entry entry;
        entry.id = id;
        // Bind the task to the dispatching session: a late worker must never
        // surface in the next session's panel/trace/history.
        entry.sessionId = sessionId.isEmpty() ? currentSessionScope() : sessionId;
        entry.goal = goal;
        entry.role = role;
        entry.state = QStringLiteral("queued");
        entry.startedAtMs = QDateTime::currentMSecsSinceEpoch();
        entry.cancelled = cancelled;
        entry.spawnArgs = spawnArgs;
        m_tasks.insert(id, entry);
        m_order.append(id);
    }
    emit changed();
    m_pool->start(QRunnable::create([this, id, work = std::move(work)]() {
        if (!markRunning(id)) return;   // cancelled while queued
        work(id);
    }));
    // TTL is armed on the hub's thread (createTask may run on a pool thread;
    // QTimer::singleShot with a receiver is thread-safe). The hub's ownership
    // contract is the GUI/main thread; on an adopted std::thread (test-only
    // host) there is no event loop, so skip instead of warning.
    if (QCoreApplication::instance()
            && this->thread() == QCoreApplication::instance()->thread()) {
        QTimer::singleShot(kTaskTtlMs, this, [this, id]() { expire(id); });
    }
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
            entry->cancelling = false;
            entry->slotQueued = false;
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
                                       const QString& text, int step,
                                       int maxSteps) {
    {
        QMutexLocker lock(&m_mutex);
        Entry* entry = findEntry(id);
        if (!entry || isTerminal(entry->state)) return;
        const bool queuedPhase = (phase == QLatin1String("queued"));
        // acquireSlot 每 250ms 回调一次 queued；面板只需要第一条，重复的
        // 同文案事件不必反复入队/发信号（否则 24 条尾巴瞬间被刷满）。
        const bool append =
            !queuedPhase || entry->progress.isEmpty()
            || entry->progress.last().first != phase
            || entry->progress.last().second != text;
        bool changed = false;
        if (append) {
            entry->progress.append({phase, text});
            while (entry->progress.size() > kProgressTail)
                entry->progress.removeFirst();
            changed = true;
        }
        if (step > 0 && entry->steps != step) {
            entry->steps = step;
            changed = true;
        }
        if (maxSteps > 0 && entry->maxSteps != maxSteps) {
            entry->maxSteps = maxSteps;
            changed = true;
        }
        // P4.5 "queued" 事件表示 Runner 在 acquireSlot 里排队；hub 状态此时
        // 已是 running（池线程已启动）。保留独立标记给面板统计。
        if (entry->slotQueued != queuedPhase) {
            entry->slotQueued = queuedPhase;
            changed = true;
        }
        if (!changed) return;
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
        entry->cancelling = false;
        entry->slotQueued = false;
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
    return cancelImpl(id, currentSessionScope());
}

bool BackgroundTaskHub::cancelForSession(const QString& sessionId,
                                         const QString& id) {
    return cancelImpl(id, sessionId);
}

bool BackgroundTaskHub::cancelImpl(const QString& id,
                                   const QString& sessionScope,
                                   bool anySession) {
    bool cancelledNow = false;
    {
        QMutexLocker lock(&m_mutex);
        Entry* entry = findEntry(id);
        if (!entry || isTerminal(entry->state)) return false;
        // Tool-thread queries are session-scoped: an id from a previous
        // session is "unknown", never cancelled through the current one.
        if (!anySession && !inSession(*entry, sessionScope)) return false;
        if (entry->cancelled) entry->cancelled->store(true);
        // A queued task has no runner yet: finish it here so the panel and
        // subagent_wait see a terminal state without waiting for the pool.
        if (entry->state == QLatin1String("queued")) {
            entry->state = QStringLiteral("cancelled");
            entry->error = QStringLiteral("cancelled before start");
            entry->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
            entry->slotQueued = false;
        } else {
            // A running task keeps its state until the runner unwinds and
            // calls finishTask(); the token is what it polls. The flag gives
            // the panel immediate "正在停止…" feedback instead of appearing
            // to ignore the click.
            entry->cancelling = true;
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
    for (const QString& id : ids) cancelImpl(id, {}, /*anySession=*/true);
}

QVariantList BackgroundTaskHub::snapshots() const {
    QVariantList out;
    QMutexLocker lock(&m_mutex);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& id : m_order) {
        const auto it = m_tasks.constFind(id);
        if (it == m_tasks.cend()) continue;
        out.append(snapshotOf(it.value(), now));
    }
    return out;
}

QVariantList BackgroundTaskHub::snapshotsForSession(const QString& sessionId) const {
    QVariantList out;
    QMutexLocker lock(&m_mutex);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& id : m_order) {
        const auto it = m_tasks.constFind(id);
        if (it == m_tasks.cend() || !inSession(it.value(), sessionId)) continue;
        out.append(snapshotOf(it.value(), now));
    }
    return out;
}

// docs/43 O4: the panel now shows what a run actually cost. Values come from
// the stored report (0 while running); usage stays empty when the provider
// reported only an aggregate, so the UI falls back to tokensUsed.
QVariantMap BackgroundTaskHub::snapshotOf(const Entry& entry, qint64 nowMs) const {
    QVariantList tail;
    for (const auto& item : entry.progress)
        tail.append(QVariantMap{{QStringLiteral("phase"), item.first},
                                {QStringLiteral("text"), item.second}});
    const qint64 end = entry.finishedAtMs > 0 ? entry.finishedAtMs : nowMs;
    int tokensUsed = 0;
    int findingCount = 0;
    QString stopCode;
    QVariantMap usage;
    if (entry.hasReport) {
        tokensUsed = entry.report.tokensUsed;
        findingCount = entry.report.findings.size();
        stopCode = entry.report.stopCode;
        usage = QVariantMap{
            {QStringLiteral("input"), entry.report.usage.inputTokens},
            {QStringLiteral("output"), entry.report.usage.outputTokens},
            {QStringLiteral("cached"), entry.report.usage.cachedTokens},
            {QStringLiteral("reported"), entry.report.usage.usageReported},
            {QStringLiteral("model"), entry.report.usage.model},
            {QStringLiteral("profileId"), entry.report.usage.profileId}};
    }
    return QVariantMap{
        {QStringLiteral("id"), entry.id},
        {QStringLiteral("sessionId"), entry.sessionId},
        {QStringLiteral("goal"), entry.goal},
        {QStringLiteral("role"), entry.role},
        {QStringLiteral("state"), entry.state},
        {QStringLiteral("startedAt"), entry.startedAtMs},
        {QStringLiteral("elapsedMs"), qMax<qint64>(0, end - entry.startedAtMs)},
        {QStringLiteral("steps"), entry.steps},
        {QStringLiteral("maxSteps"), entry.maxSteps},
        {QStringLiteral("proposalCount"), entry.proposalCount},
        {QStringLiteral("cancelling"), entry.cancelling},
        {QStringLiteral("slotQueued"), entry.slotQueued},
        {QStringLiteral("progressTail"), tail},
        {QStringLiteral("error"), entry.error},
        {QStringLiteral("tokensUsed"), tokensUsed},
        {QStringLiteral("usage"), usage},
        {QStringLiteral("findingCount"), findingCount},
        {QStringLiteral("stopCode"), stopCode}};
}

int BackgroundTaskHub::activeCount() const {
    QMutexLocker lock(&m_mutex);
    int active = 0;
    for (const auto& entry : m_tasks) {
        if (!isTerminal(entry.state)) ++active;
    }
    return active;
}

int BackgroundTaskHub::activeCountFor(const QString& sessionId) const {
    QMutexLocker lock(&m_mutex);
    int active = 0;
    for (const auto& entry : m_tasks) {
        if (!isTerminal(entry.state) && inSession(entry, sessionId)) ++active;
    }
    return active;
}

bool BackgroundTaskHub::hasTask(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    return it != m_tasks.cend() && inSession(it.value(), currentSessionScope());
}

QString BackgroundTaskHub::stateLabel(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    if (it == m_tasks.cend() || !inSession(it.value(), currentSessionScope()))
        return QString();
    return it->state;
}

QString BackgroundTaskHub::stateLabelForSession(const QString& sessionId,
                                                const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    if (it == m_tasks.cend() || !inSession(it.value(), sessionId)) return QString();
    return it->state;
}

std::optional<SubAgentReport> BackgroundTaskHub::reportFor(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    if (it == m_tasks.cend() || !it->hasReport
            || !inSession(it.value(), currentSessionScope()))
        return std::nullopt;
    return it->report;
}

std::optional<SubAgentReport> BackgroundTaskHub::reportForSession(
    const QString& sessionId, const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    if (it == m_tasks.cend() || !it->hasReport || !inSession(it.value(), sessionId))
        return std::nullopt;
    return it->report;
}

QString BackgroundTaskHub::sessionIdFor(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    return it == m_tasks.cend() ? QString() : it->sessionId;
}

// docs/43 O4-2: retry reads the stored spawn arguments of a TERMINAL task.
// Empty object = unknown id, another session's task, or a task created before
// the snapshot existed (nothing to re-dispatch).
QJsonObject BackgroundTaskHub::spawnArgsForSession(const QString& sessionId,
                                                   const QString& id) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tasks.constFind(id);
    if (it == m_tasks.cend() || !inSession(it.value(), sessionId))
        return {};
    if (!isTerminal(it->state)) return {};
    return it->spawnArgs;
}

QString BackgroundTaskHub::waitFor(const QString& id, int timeoutMs,
                                   const ITool::Cancellation& waiterCancelled) {
    QElapsedTimer timer;
    timer.start();
    QMutexLocker lock(&m_mutex);
    while (true) {
        const auto it = m_tasks.constFind(id);
        // A task id from another session is indistinguishable from unknown:
        // waiting on it would leak an old session's report into this one.
        if (it == m_tasks.cend() || !inSession(it.value(), currentSessionScope()))
            return QStringLiteral("missing");
        if (isTerminal(it->state)) return it->state;
        if (waiterCancelled && waiterCancelled->load()) return it->state;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) return it->state;
        m_cv.wait(&m_mutex, static_cast<unsigned long>(qMin(remaining, 250)));
    }
}

// docs/43 O3: wait-any / wait-all over a set of ids. One condition-variable
// loop covers both modes so a completed task wakes every waiter immediately.
QHash<QString, QString> BackgroundTaskHub::waitForMany(
        const QStringList& ids, bool waitAll, int timeoutMs,
        const ITool::Cancellation& waiterCancelled) {
    QElapsedTimer timer;
    timer.start();
    QMutexLocker lock(&m_mutex);
    const QString scope = currentSessionScope();
    const auto collect = [&]() {
        QHash<QString, QString> out;
        for (const QString& id : ids) {
            const auto it = m_tasks.constFind(id);
            out.insert(id, it == m_tasks.cend() || !inSession(it.value(), scope)
                               ? QStringLiteral("missing") : it->state);
        }
        return out;
    };
    while (true) {
        const QHash<QString, QString> states = collect();
        bool anyTerminal = false;
        bool allTerminal = true;
        for (const QString& id : ids) {   // ids order keeps results stable
            const QString state = states.value(id);
            const bool terminal = isTerminal(state)
                || state == QLatin1String("missing");
            anyTerminal = anyTerminal || terminal;
            allTerminal = allTerminal && terminal;
        }
        if (waitAll ? allTerminal : anyTerminal) return states;
        if (waiterCancelled && waiterCancelled->load()) return states;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) return states;
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

void BackgroundTaskHub::removeFinishedInSession(const QString& sessionId) {
    bool any = false;
    {
        QMutexLocker lock(&m_mutex);
        QVector<QString> kept;
        kept.reserve(m_order.size());
        for (const QString& id : m_order) {
            const auto it = m_tasks.constFind(id);
            if (it == m_tasks.cend()) continue;
            if (inSession(it.value(), sessionId) && isTerminal(it->state)) {
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