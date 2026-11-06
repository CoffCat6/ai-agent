#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QThreadStorage>
#include <QVariantList>
#include <QVector>
#include <QWaitCondition>
#include <functional>
#include <memory>
#include <optional>

#include "core/agent/SubAgentRunner.h"

namespace NexusAI {

// docs/35 P4.3: 会话级后台子代理任务表。AIAgent 持有（shared_ptr），
// spawn_agent(background=true) 经线程局部 BackgroundTaskHubRouter 取到它，
// 把运行放到 hub 自己的小线程池后立即 ack；subagent_wait/status/cancel
// 也从这里取任务。
//
// 所有权：hub 由 AIAgent 在 GUI 线程构造并持有，没有 QObject 父对象（父对象
// 会和 shared_ptr 争抢 delete）。shutdown()/析构必须回到属主线程；工作线程
// 只写任务表、发 changed()，连接方必须 QueuedConnection 回 GUI。
//
// 线程模型：createTask/updateProgress/finishTask/cancel 可在任意线程调用，
// 内部互斥保护；changed() 由工作线程发出。析构先取消/清空，再限时等待；
// 绝不在池线程上 waitForDone 自身（会死锁），超时则分离池而非阻塞退出。
//
// 会话绑定：createTask 从 BackgroundTaskHubRouter 读取当前会话 id 落库；
// 面向工具/面板的查询默认只看到 router 会话内的任务，避免旧会话任务写入
// 新会话的 trace/历史。AIAgent 另有显式 *ForSession 接口。
class BackgroundTaskHub : public QObject {
    Q_OBJECT
public:
    // 每会话同时存在的后台任务上限（排队 + 运行）。
    static constexpr int kMaxTasks = 4;
    // 等待类工具的默认/最大超时。
    static constexpr int kDefaultWaitMs = 60 * 1000;
    static constexpr int kMaxWaitMs = 10 * 60 * 1000;
    // 单任务墙钟上限（docs/35 §6.2）：超时自动取消并归类 failed，保留原因。
    static constexpr int kTaskTtlMs = 30 * 60 * 1000;
    // 析构/shutdown 等待工作线程的上限。协作式取消之外的顽固任务不能
    // 无限期挂住 teardown；超时后池被分离（见析构实现）。
    static constexpr int kShutdownWaitMs = 1000;

    // Invoked on a hub pool thread with the generated task id.
    using Work = std::function<void(const QString& taskId)>;

    explicit BackgroundTaskHub(QObject* parent = nullptr);
    ~BackgroundTaskHub() override;

    // 登记并排队一个后台任务；达到 kMaxTasks 时返回空 id。work 在 hub 的
    // 线程池里运行，负责最终调用 finishTask()。sessionId 为空时取当前
    // BackgroundTaskHubRouter 的会话（工具派发线程的常规路径）。
    // docs/43 O4-2: spawnArgs 保存原始 spawn_agent 调用参数（goal/role/
    // context/background/todo_id...），失败/取消后的「重试」用它原样重派。
    QString createTask(const QString& goal, const QString& role,
                       const ITool::Cancellation& cancelled, Work work,
                       const QString& sessionId = {},
                       const QJsonObject& spawnArgs = {});

    // Worker-thread callbacks from the sub-agent run. step/maxSteps are
// optional (0 = unknown) and let the panel show live step counts while the
// run is still active.
    void updateProgress(const QString& id, const QString& phase,
                        const QString& text, int step = 0, int maxSteps = 0);
    void finishTask(const QString& id, const SubAgentReport& report);

    // Flipping the shared token is the only cancellation mechanism; queued
    // tasks are finished immediately, running ones unwind at the runner's
    // next poll.
    bool cancel(const QString& id);
    bool cancelForSession(const QString& sessionId, const QString& id);
    void cancelAll();

    // Prefix/terminal-state queries (thread-safe copies).
    QVariantList snapshots() const;
    // 只返回指定会话的任务（面板/历史注入；空会话返回空表，不串台）。
    QVariantList snapshotsForSession(const QString& sessionId) const;
    int activeCount() const;
    int activeCountFor(const QString& sessionId) const;
    // 以下查询在 BackgroundTaskHubRouter 已安装时按会话过滤（工具线程），
    // 未安装 router 时按全局（直接建 hub 的测试/嵌入场景）。
    bool hasTask(const QString& id) const;
    QString stateLabel(const QString& id) const;
    std::optional<SubAgentReport> reportFor(const QString& id) const;
    // 显式会话版本（GUI 线程的面板 API；不给 router 留歧义）。
    QString stateLabelForSession(const QString& sessionId, const QString& id) const;
    std::optional<SubAgentReport> reportForSession(const QString& sessionId,
                                                   const QString& id) const;
    QString sessionIdFor(const QString& id) const;
    // docs/43 O4-2: 终态任务的重试用原始 spawn 参数；旧任务（无快照）或跨
    // 会话 id 返回空对象，调用方按不可重试处理。
    QJsonObject spawnArgsForSession(const QString& sessionId,
                                    const QString& id) const;

    // Block until the task is terminal, the wait deadline passes, or the
    // waiter's own cancellation fires. Returns the state label at exit.
    // 会话不匹配时返回 "missing"（旧会话 id 在新会话内等同未知任务）。
    QString waitFor(const QString& id, int timeoutMs,
                    const ITool::Cancellation& waiterCancelled);

    // docs/43 O3: multi-task wait. waitAll=false returns as soon as ANY id is
    // terminal (waitFor-any), waitAll=true waits for every id or the deadline.
    // Every requested id maps to its state label; unknown or foreign-session
    // ids are reported as "missing" and never leak another session's report.
    QHash<QString, QString> waitForMany(const QStringList& ids, bool waitAll,
                                        int timeoutMs,
                                        const ITool::Cancellation& waiterCancelled);

    // Drop terminal entries (panel "clear finished").
    void removeFinished();
    void removeFinishedInSession(const QString& sessionId);

    // GUI-thread teardown: cancel + drop queued work + bounded wait. Never
    // waits from a non-owner thread; returns false when the timeout fired.
    bool shutdown(int timeoutMs = kShutdownWaitMs);

signals:
    // Coalesced by AIAgent (250ms) before reaching QML; still emitted from
    // arbitrary threads, so receivers must connect queued.
    void changed();

private:
    struct Entry {
        QString id;
        QString sessionId;      // owning session; empty for router-less tests
        QString goal;
        QString role;
        QString state{QStringLiteral("queued")};
        // docs/43 O4-2: original spawn_agent arguments for one-click retry.
        QJsonObject spawnArgs;
        qint64 startedAtMs{0};
        qint64 finishedAtMs{0};
        int steps{0};
        int maxSteps{0};        // 0 = unbounded / unknown
        int proposalCount{0};
        bool cancelling{false}; // cancel() requested; runner still unwinding
        bool slotQueued{false}; // running but waiting for a sub-agent slot
        QString error;
        QVector<QPair<QString, QString>> progress;   // phase, text (tail)
        SubAgentReport report;
        bool hasReport{false};
        bool expired{false};   // TTL fired: finishTask() maps this to failed
        ITool::Cancellation cancelled;
    };

    bool markRunning(const QString& id);
    void expire(const QString& id);
    Entry* findEntry(const QString& id);   // caller holds m_mutex
    // docs/43 O4: one panel row (cost/findings included). Shared by the
    // global and per-session snapshot views.
    QVariantMap snapshotOf(const Entry& entry, qint64 nowMs) const;

    // Router 会话（未安装时为空 = 不过滤）。
    static QString currentSessionScope();
    // 会话严格匹配；空 sessionId 只能看到 router 缺失/空会话的历史任务。
    static bool inSession(const Entry& entry, const QString& sessionId);
    bool cancelImpl(const QString& id, const QString& sessionScope,
                    bool anySession = false);

    mutable QMutex m_mutex;
    QWaitCondition m_cv;
    QHash<QString, Entry> m_tasks;
    QVector<QString> m_order;   // creation order for stable panel rows
    // Heap-owned so teardown can detach instead of letting QThreadPool's
    // destructor block on a pool thread that ignores cancellation.
    QThreadPool* m_pool{nullptr};
    int m_seq{0};
    bool m_shuttingDown{false};
};

// docs/35 P4.3: 与 ProposalStoreRouter 同型的线程局部路由。AIAgent 在派发
// spawn_agent 的 lambda 里安装；子代理内部再 spawn 时拿不到（递归派生仍
// 禁止），测试可以直接安装。
class BackgroundTaskHubRouter {
public:
    explicit BackgroundTaskHubRouter(std::shared_ptr<BackgroundTaskHub> hub,
                                     QString sessionId = {}) {
        s_hub.localData() = std::move(hub);
        s_session.localData() = std::move(sessionId);
    }
    ~BackgroundTaskHubRouter() {
        s_hub.localData().reset();
        s_session.localData().clear();
    }
    BackgroundTaskHubRouter(const BackgroundTaskHubRouter&) = delete;
    BackgroundTaskHubRouter& operator=(const BackgroundTaskHubRouter&) = delete;

    static std::shared_ptr<BackgroundTaskHub> current() {
        return s_hub.localData();
    }
    // 当前派发线程绑定的会话；没有 router 时为空（测试/嵌入走全局视图）。
    static QString currentSessionId() {
        return s_session.localData();
    }

private:
    static inline QThreadStorage<std::shared_ptr<BackgroundTaskHub>> s_hub;
    static inline QThreadStorage<QString> s_session;

};

} // namespace NexusAI