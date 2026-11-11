#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <optional>

namespace NexusAI {

// docs/50 §5.1：定时任务（H4）的调度规格。纯数据 + 纯函数——无 QObject、
// 无 IO、无系统时钟依赖（时间一律由调用方传入），QtCore-only，nexus_core
// 保持零 UI 依赖的边界不变。
//
// 三种形态（D4 决策，不做 cron）：
//   Daily     每天 HH:mm（本地时间）
//   Weekly    每周 X HH:mm（weekday 1-7，对齐 Qt::Monday..Qt::Sunday）
//   Interval  每 N 小时（1-168，从锚点起按绝对毫秒推进，无时区语义）
//
// JSON 形态（scheduled_tasks.schedule_value 列存其紧凑串）：
//   {"kind":"daily","time":"09:00"}
//   {"kind":"weekly","weekday":3,"time":"09:00"}
//   {"kind":"interval","hours":6}
//
// 时间语义（D5 之外的既定口径）：daily/weekly 一律按本地时区解释；DST
// 空档/重叠交给 QDateTime 的规范化结果，行为由 test_schedule 矩阵固化，
// 不自造规则。Interval 用 UTC 毫秒算术，天然无 DST 问题。
struct ScheduleSpec {
    enum Kind { Daily, Weekly, Interval };

    Kind kind{Daily};
    QTime time;       // Daily/Weekly：触发时刻
    int weekday{1};   // Weekly：1（周一）..7（周日）
    int hours{1};     // Interval：1..168

    // 解析：kind 缺失或未知返回 nullopt（无法归类）；其余字段宽松填充，
    // 语义问题（无效时刻/越界）交给 validationError()——解析与校验共用
    // 一套口径，坏行由消费方跳过并审计，数据层不做哑存储之外的事。
    static std::optional<ScheduleSpec> fromJson(const QJsonObject& json);
    QJsonObject toJson() const;

    // 空 = 合法；非空 = 面向用户的非法原因（编辑器行级校验与导入防御
    // 共用）。源串走 tr()（中文源 + en 翻译，同 SqliteMemory 先例）。
    QString validationError() const;
    bool isValid() const { return validationError().isEmpty(); }

    // 严格晚于 after 的下一次触发。anchor 是 Interval 的推进锚点（任务
    // last_fired_at；Daily/Weekly 忽略）。规格非法 / after 无效 / Interval
    // 的 anchor 无效时返回无效 QDateTime。
    // tz 缺省系统时区；测试注入固定时区跑 DST 矩阵。
    QDateTime nextOccurrence(const QDateTime& anchor, const QDateTime& after,
                             const QTimeZone& tz = QTimeZone::systemTimeZone()) const;

    // ≤ now 的最近一次触发（错过收敛用）：应用未运行期间可能错过多个周期，
    // 调度器用它把锚点一次推进到位，只记一条 missed 而不是逐 tick 补记。
    // Interval 从 anchor 起算（k = floor((now-anchor)/step) ≥ 1）；Daily/
    // Weekly 只看 now（向前最多找 1/7 天）。前置条件是 nextOccurrence(anchor,
    // anchor) 已 ≤ now——此时必有解；防御性返回无效 QDateTime。
    QDateTime latestOccurrenceAtOrBefore(const QDateTime& anchor, const QDateTime& now,
                                         const QTimeZone& tz = QTimeZone::systemTimeZone()) const;

    // 列表/历史的调度描述（"每天 09:00" / "每周三 09:00" / "每 6 小时"）。
    // 星期名走 QLocale 随系统语言；规格非法返回空串。
    QString displayText() const;

    Q_DECLARE_TR_FUNCTIONS(ScheduleSpec)
};

} // namespace NexusAI
