#pragma once

#include "core/schedule/ScheduleSpec.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

class QTimer;

namespace NexusAI {

class AIAgent;
class SqliteMemory;

// docs/50 §5-§6：定时任务调度器（H4）。core 层、GUI 线程单实例——全部外部
// 依赖注入，不感知设置服务/偏好键/QML：
//
//   · SqliteMemory*          两表 CRUD 与 run 历史（P1 数据层）
//   · Clock                  时间源（测试冻结时钟跑矩阵）
//   · AgentFactory           造无头 AIAgent（main.cpp 抽出的共享工厂）
//   · ProfileResolver        profileId → profile（空 map = 已删除/不可用）
//   · ModelConfigurator      按 profile 配置 LLM 服务/上下文（与 VM 同源闭包）
//   · MaxRunMinutesProvider  全局单次运行上限（分钟；触发时现读，改值即生效）
//
// 线程纪律：SQLite 访问全部留在 GUI 线程（tick 定时器、信号处理、CRUD 直通
// 都是 GUI 线程入口）；agent 的工具执行在池线程，终态经队列信号回这里。
//
// 执行载体（D1）：无头 AIAgent——独立 owned SQLite 连接（同 tab 模式，库
// 文件相同、busy_timeout 串行化），会话惰性创建落 sessions/messages/trace/
// audit 全套，侧栏可见；不占 tab 额度、不抢焦点、确认不进全局模态队列。
//
// 无人值守防挂起三层（D2）：任务权限档默认 plan_only（agent 侧天然不挂起）；
// AIAgent::setUnattended 把确认分支改为直接拒绝；setMaxRunMinutesProvider
// 注入的墙钟上限走既有 onTaskTimeout 封口（终态记 timeout）。
//
// 调度语义（D4/D5）：三种形态；30s tick + 2 分钟宽限；错过不补跑——把锚点
// 一次推进到最近应触发点并记一条 missed；全局同时最多 1 个定时 run，计划
// 触发撞闸记 skipped_busy、手动触发返回 false 不落行。
class ScheduledTasksManager : public QObject {
    Q_OBJECT
public:
    using Clock = std::function<QDateTime()>;
    using AgentFactory = std::function<AIAgent*()>;
    // 返回空 map 表示 profile 不存在（删除/失效）。
    using ProfileResolver = std::function<QVariantMap(const QString& profileId)>;
    // 与 AgentViewModel::setModelConfigurator 同源的配置闭包（会话临时覆盖
    // 传空——定时任务没有 UI 侧覆盖通道）。
    using ModelConfigurator = std::function<void(AIAgent*, const QVariantMap& profile)>;
    using MaxRunMinutesProvider = std::function<int()>;

    explicit ScheduledTasksManager(SqliteMemory* memory, Clock clock = {},
                                   QObject* parent = nullptr);
    // 析构即停机（停 tick + 收口在跑 run）；aboutToQuit 显式调 shutdown()
    // 可限时等待在跑任务收口。
    ~ScheduledTasksManager() override;

    void setAgentFactory(AgentFactory factory);
    void setProfileResolver(ProfileResolver resolver);
    void setModelConfigurator(ModelConfigurator configurator);
    void setMaxRunMinutesProvider(MaxRunMinutesProvider provider);

    // 30s tick 循环。构造即启动；CRUD 后可调 tick() 立即反映下次触发。
    void startTicking();
    void tick();

    // CRUD 直通。变更后自动 tick 一次并发 tasksChanged。
    // Q_INVOKABLE：P3 的对话框经上下文属性 ScheduleManager 直接调用。
    Q_INVOKABLE QVariantMap upsertTask(const QVariantMap& task);
    Q_INVOKABLE bool deleteTask(const QString& id);
    Q_INVOKABLE bool setTaskEnabled(const QString& id, bool enabled);
    Q_INVOKABLE QVariantList tasks() const;
    Q_INVOKABLE QVariantList runsForTask(const QString& taskId, int limit = 0) const;

    // 手动「立即运行」（trigger_source='manual'，不推进调度锚点）；闸占用
    // 返回 false（UI toast 提示，不落 run 行）。
    Q_INVOKABLE bool runNow(const QString& taskId);

    // UI 展示：该任务的下一次计划触发（未启用/规格非法 → 无效）。
    QDateTime nextDueFor(const QVariantMap& task) const;
    // QML 辅助：下次触发的 epoch ms（0 = 未启用/解析失败）与调度描述
    // （"每天 09:00"等，走 ScheduleSpec::displayText）。QDateTime 对 QML
    // 不友好，统一折算毫秒。
    Q_INVOKABLE qint64 nextDueMs(const QVariantMap& task) const;
    Q_INVOKABLE QString describeTask(const QVariantMap& task) const;

    // 退出停机：停 tick → 对在跑 run stop() 并记 stopped → deleteLater。
    // stop() 是同步封口（在飞工具协作取消），无需额外限时等待；AIAgent
    // 析构自带 1s 限时 hub 收口，落在 aboutToQuit 的 2s 预算内。
    void shutdown();

    static constexpr int kTickIntervalMs = 30 * 1000;
    static constexpr int kGraceMs = 120 * 1000;   // 2 分钟宽限（docs/50 §5.2）
    // 首条助手回答截断长度（run.summary / 通知正文）。
    static constexpr int kSummaryChars = 120;

signals:
    // 一次真实执行到达终态（success/failed/timeout）。skipped_busy 不发。
    // summary 是最终回答（或失败原因）的截断；taskId供「打开会话」。
    void runFinished(const QString& taskId, const QString& taskName,
                     bool success, const QString& sessionId,
                     const QString& summary);
    // 任务表/run 表有任何变化（对话框刷新）。
    void tasksChanged();
    // 有定时任务产生了会话（侧栏重载入口，main.cpp 接 AgentVM）。
    void sessionsMayHaveChanged();

private:
    struct ActiveRun {
        bool valid{false};
        qint64 runId{0};
        QString taskId;
        QString taskName;
        QString sessionId;
        bool notify{true};        // 任务级通知开关（docs/50 §6.4）
        bool terminal{false};     // 首个终态信号生效，其余忽略
        QMetaObject::Connection sessionConn;
    };

    // 触发一次执行。dueAt 仅在 triggerSource='schedule' 时用于推进锚点。
    void fireTask(const QVariantMap& task, const QString& triggerSource,
                  const QDateTime& dueAt);
    // 终态落库 + 信号 + agent 清理（幂等：terminal 已置位直接返回）。
    void finishActiveRun(const QString& status, const QString& sessionId,
                         const QString& error, const QString& summary);
    QString summarize(const QString& text) const;

    SqliteMemory* m_memory;
    Clock m_clock;
    AgentFactory m_agentFactory;
    ProfileResolver m_profileResolver;
    ModelConfigurator m_modelConfigurator;
    MaxRunMinutesProvider m_maxRunMinutes;
    QTimer* m_timer{nullptr};
    ActiveRun m_activeRun;
    AIAgent* m_activeAgent{nullptr};
    bool m_shuttingDown{false};
};

} // namespace NexusAI
