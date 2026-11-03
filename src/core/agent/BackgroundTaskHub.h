#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
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
// 线程模型：createTask/updateProgress/finishTask/cancel 可在任意线程调用，
// 内部互斥保护；changed() 由工作线程发出，连接方必须用 QueuedConnection
// 回 GUI。工作线程只活到任务结束（hub 析构时 waitForDone），因此任务
// lambda 捕获裸 this 是安全的。
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

    // Invoked on a hub pool thread with the generated task id.
    using Work = std::function<void(const QString& taskId)>;

    explicit BackgroundTaskHub(QObject* parent = nullptr);
    ~BackgroundTaskHub() override;

    // 登记并排队一个后台任务；达到 kMaxTasks 时返回空 id。work 在 hub 的
    // 线程池里运行，负责最终调用 finishTask()。
    QString createTask(const QString& goal, const QString& role,
                       const ITool::Cancellation& cancelled, Work work);

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
    void cancelAll();

    // Prefix/terminal-state queries (thread-safe copies).
    QVariantList snapshots() const;
    int activeCount() const;
    bool hasTask(const QString& id) const;
    QString stateLabel(const QString& id) const;
    std::optional<SubAgentReport> reportFor(const QString& id) const;

    // Block until the task is terminal, the wait deadline passes, or the
    // waiter's own cancellation fires. Returns the state label at exit.
    QString waitFor(const QString& id, int timeoutMs,
                    const ITool::Cancellation& waiterCancelled);

    // Drop terminal entries (panel "clear finished").
    void removeFinished();

signals:
    // Coalesced by AIAgent (250ms) before reaching QML; still emitted from
    // arbitrary threads, so receivers must connect queued.
    void changed();

private:
    struct Entry {
        QString id;
        QString goal;
        QString role;
        QString state{QStringLiteral("queued")};
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

    mutable QMutex m_mutex;
    QWaitCondition m_cv;
    QHash<QString, Entry> m_tasks;
    QVector<QString> m_order;   // creation order for stable panel rows
    QThreadPool m_pool;
    int m_seq{0};
};

// docs/35 P4.3: 与 ProposalStoreRouter 同型的线程局部路由。AIAgent 在派发
// spawn_agent 的 lambda 里安装；子代理内部再 spawn 时拿不到（递归派生仍
// 禁止），测试可以直接安装。
class BackgroundTaskHubRouter {
public:
    explicit BackgroundTaskHubRouter(std::shared_ptr<BackgroundTaskHub> hub) {
        s_hub.localData() = std::move(hub);
    }
    ~BackgroundTaskHubRouter() { s_hub.localData().reset(); }
    BackgroundTaskHubRouter(const BackgroundTaskHubRouter&) = delete;
    BackgroundTaskHubRouter& operator=(const BackgroundTaskHubRouter&) = delete;

    static std::shared_ptr<BackgroundTaskHub> current() {
        return s_hub.localData();
    }

private:
    static inline QThreadStorage<std::shared_ptr<BackgroundTaskHub>> s_hub;
};

} // namespace NexusAI