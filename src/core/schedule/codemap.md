# src/core/schedule/ — 定时任务调度规格与调度器

## 职责

- docs/50（H4 定时任务）两个构件：
  - `ScheduleSpec`：三种调度形态（每天 HH:mm / 每周 X HH:mm / 每 N 小时，
    D4 决策不做 cron）的解析、校验、`nextOccurrence` / `latestOccurrenceAtOrBefore`
    纯函数。QtCore-only、无 QObject、无 IO、无系统时钟依赖——时间与锚点
    全部由调用方传入，时区可注入做 DST 单测。
  - `ScheduledTasksManager`（P2）：GUI 线程调度器——30s tick + 2 分钟宽限、
    错过不补跑（锚点经 latestOccurrenceAtOrBefore 一次推进）、无头 AIAgent
    执行编排、全局并发闸（计划撞闸记 skipped_busy、手动 runNow 返回 false）、
    `shutdown()` 退出停机（run 记 stopped）。依赖全注入（memory/clock/
    AgentFactory/ModelConfigurator/ProfileResolver/MaxRunMinutesProvider），
    不感知设置服务与 QML；`scheduled_tasks.schedule_value` 存 `toJson()`
    紧凑串。

## 关键约定

- **严格晚于**：`nextOccurrence` 返回的一律是严格晚于 `after` 的下一次；
  恰在触发点上 → 下一个周期。
- **Interval 锚点推进**：`anchor + k*step > after` 的最小值，UTC 毫秒算术，
  无时区语义；daily/weekly 按本地时区，DST 空档/重叠交给 QDateTime 规范化
  （跳变前偏移），行为由 `tests/test_schedule.cpp` 矩阵固化，不自造规则。
- **解析与校验分权**：`fromJson` 只拒绝缺失/未知 kind，字段语义问题
  （无效时刻、越界星期/小时）留给 `validationError()`，两处共用一套口径；
  坏行由调度器静默跳过（tick 每 30s 一次，qWarning 会刷屏）。
- **无人值守三层**（docs/50 §6.1）：任务权限档默认 plan_only；
  `AIAgent::setUnattended` 把确认分支改为直接拒绝；墙钟上限走
  `AIAgent::setTaskTimeoutMs` 既有封口，终态经 `taskTimedOut` 信号回
  manager（首个终态信号生效，timeout 先于 finishTask 的 responseReady）。
- **时间戳纪律**：manager 所有运行时时间取值走注入时钟（冻结时钟测试
  前提）；CRUD 的 created_at/updated_at 例外（真实墙钟，非调度语义）。
- **调度级审计**：`schedule_missed/skipped_busy/saved/deleted/finished` 行
  的 sessionId 用合成标记 `__scheduler__`（audit_log.session_id NOT NULL）。
- 装配在 `main.cpp`：与 tab 会话共用抽出的 `makeSessionAgent` 工厂与
  `configureAgentModel` 配置闭包；托盘通知与侧栏刷新接线在 P4。
