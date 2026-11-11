// docs/50 P1：ScheduleSpec——解析 / 校验 / nextOccurrence 的纯函数矩阵。
// DST 语义按 QDateTime 的规范化结果固化（方案 §5.1：不自造规则）。
#include "core/schedule/ScheduleSpec.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QtTest>

using namespace NexusAI;

namespace {
// DST 用例的固定时区；Qt 缺时区数据时跳过（Windows 一般随系统带）。
QTimeZone newYorkZone() { return QTimeZone("America/New_York"); }

QDateTime localDateTime(int y, int m, int d, int hh, int mm) {
    return QDateTime(QDate(y, m, d), QTime(hh, mm),
                     QTimeZone::systemTimeZone());
}
} // namespace

class TestSchedule : public QObject {
    Q_OBJECT

private slots:
    // ── 解析与往返 ──
    void jsonRoundTripAllKinds();
    void parseRejectsUnknownKind();
    void lenientParseDelegatesSemanticsToValidation();
    // ── 校验矩阵 ──
    void validationMatrix();
    // ── daily ──
    void dailySameDayLaterTime();
    void dailyEqualTimeRollsToNextDay();
    void dailyCrossesMidnight();
    void dailyDstSpringForwardGap();
    void dailyDstFallBackOverlap();
    // ── weekly ──
    void weeklySameDayLaterTime();
    void weeklySameDayEarlierTimeRollsToNextWeek();
    void weeklyWrapsToNextWeek();
    // ── interval ──
    void intervalAdvancesFromAnchor();
    void intervalAnchorInFutureReturnsAnchor();
    void intervalExactMultipleStepsOneFurther();
    void intervalIsTimezoneAgnostic();
    // ── 防御与展示 ──
    void invalidSpecYieldsInvalidOccurrence();
    void displayTextFormats();
};

// ── 解析与往返 ────────────────────────────────────────────

void TestSchedule::jsonRoundTripAllKinds() {
    ScheduleSpec daily;
    daily.kind = ScheduleSpec::Daily;
    daily.time = QTime(9, 0);

    ScheduleSpec weekly;
    weekly.kind = ScheduleSpec::Weekly;
    weekly.time = QTime(21, 30);
    weekly.weekday = 5;   // 周五

    ScheduleSpec interval;
    interval.kind = ScheduleSpec::Interval;
    interval.hours = 6;

    const QList<QPair<ScheduleSpec, QJsonObject>> cases = {
        {daily,   QJsonObject{{"kind", "daily"}, {"time", "09:00"}}},
        {weekly,  QJsonObject{{"kind", "weekly"}, {"weekday", 5}, {"time", "21:30"}}},
        {interval, QJsonObject{{"kind", "interval"}, {"hours", 6}}},
    };
    // 序列化串比较：同时钉住落库的 JSON 形态（紧凑串）。
    const auto compact = [](const QJsonObject& json) {
        return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
    };
    for (const auto& it : cases) {
        const QJsonObject json = it.first.toJson();
        QCOMPARE(compact(json), compact(it.second));
        const auto parsed = ScheduleSpec::fromJson(json);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->kind, it.first.kind);
        QCOMPARE(parsed->time, it.first.time);
        QCOMPARE(parsed->weekday, it.first.weekday);
        QCOMPARE(parsed->hours, it.first.hours);
        QVERIFY(parsed->isValid());
    }
}

void TestSchedule::parseRejectsUnknownKind() {
    // 未知 kind（cron 之类）与缺失 kind 都无法归类。
    QVERIFY(!ScheduleSpec::fromJson(
        QJsonObject{{"kind", "cron"}, {"expr", "* * *"}}).has_value());
    QVERIFY(!ScheduleSpec::fromJson(QJsonObject{{"time", "09:00"}}).has_value());
    QVERIFY(!ScheduleSpec::fromJson(QJsonObject{}).has_value());
}

void TestSchedule::lenientParseDelegatesSemanticsToValidation() {
    // 字段缺失宽松填充，语义问题交给 validationError。
    const auto noTime = ScheduleSpec::fromJson(QJsonObject{{"kind", "daily"}});
    QVERIFY(noTime.has_value());
    QVERIFY(!noTime->isValid());

    const auto noWeekday =
        ScheduleSpec::fromJson(QJsonObject{{"kind", "weekly"}, {"time", "09:00"}});
    QVERIFY(noWeekday.has_value());
    QCOMPARE(noWeekday->weekday, 1);   // 默认周一
    QVERIFY(noWeekday->isValid());

    const auto noHours = ScheduleSpec::fromJson(QJsonObject{{"kind", "interval"}});
    QVERIFY(noHours.has_value());
    QCOMPARE(noHours->hours, 1);       // 默认 1 小时
    QVERIFY(noHours->isValid());
}

void TestSchedule::validationMatrix() {
    ScheduleSpec s;
    s.kind = ScheduleSpec::Daily;
    s.time = QTime();                  // 无效时刻
    QVERIFY(!s.validationError().isEmpty());

    s.time = QTime(9, 0);
    QVERIFY(s.validationError().isEmpty());

    s.kind = ScheduleSpec::Weekly;
    s.weekday = 0;
    QVERIFY(!s.validationError().isEmpty());
    s.weekday = 8;
    QVERIFY(!s.validationError().isEmpty());
    s.weekday = 7;
    QVERIFY(s.validationError().isEmpty());

    s.kind = ScheduleSpec::Interval;
    s.hours = 0;
    QVERIFY(!s.validationError().isEmpty());
    s.hours = -1;
    QVERIFY(!s.validationError().isEmpty());
    s.hours = 169;
    QVERIFY(!s.validationError().isEmpty());
    s.hours = 168;
    QVERIFY(s.validationError().isEmpty());
}

// ── daily ─────────────────────────────────────────────────

void TestSchedule::dailySameDayLaterTime() {
    ScheduleSpec s;
    s.time = QTime(9, 0);
    // 2026-11-10 是周二。
    QCOMPARE(s.nextOccurrence({}, localDateTime(2026, 11, 10, 8, 0)),
             localDateTime(2026, 11, 10, 9, 0));
}

void TestSchedule::dailyEqualTimeRollsToNextDay() {
    ScheduleSpec s;
    s.time = QTime(9, 0);
    // 严格晚于：恰在触发点上 → 下一天。
    QCOMPARE(s.nextOccurrence({}, localDateTime(2026, 11, 10, 9, 0)),
             localDateTime(2026, 11, 11, 9, 0));
}

void TestSchedule::dailyCrossesMidnight() {
    ScheduleSpec s;
    s.time = QTime(0, 15);
    QCOMPARE(s.nextOccurrence({}, localDateTime(2026, 11, 10, 23, 30)),
             localDateTime(2026, 11, 11, 0, 15));
}

void TestSchedule::dailyDstSpringForwardGap() {
    const QTimeZone ny = newYorkZone();
    if (!ny.isValid())
        QSKIP("America/New_York 时区数据不可用");

    ScheduleSpec s;
    s.time = QTime(2, 30);   // 2026-03-08 02:30 落在 spring-forward 空档
    const QDateTime after(QDate(2026, 3, 7), QTime(12, 0), ny);
    const QDateTime occ = s.nextOccurrence({}, after, ny);
    QVERIFY(occ.isValid());
    QVERIFY(occ > after);
    // QDateTime 对空档时刻用跳变前偏移（EST, UTC-5）落位：02:30 EST
    // == 07:30Z。固化该语义；Qt 若改变行为，此断言会先说话。
    QCOMPARE(occ.toUTC(),
             QDateTime(QDate(2026, 3, 8), QTime(7, 30), Qt::UTC));
}

void TestSchedule::dailyDstFallBackOverlap() {
    const QTimeZone ny = newYorkZone();
    if (!ny.isValid())
        QSKIP("America/New_York 时区数据不可用");

    ScheduleSpec s;
    s.time = QTime(1, 30);   // 2026-11-01 01:30 出现两次（EDT/EST）
    const QDateTime after(QDate(2026, 10, 31), QTime(12, 0), ny);
    const QDateTime occ = s.nextOccurrence({}, after, ny);
    QVERIFY(occ.isValid());
    QVERIFY(occ > after);
    // 重叠时刻按跳变前偏移（EDT, UTC-4）落位：01:30 EDT == 05:30Z。
    QCOMPARE(occ.toUTC(),
             QDateTime(QDate(2026, 11, 1), QTime(5, 30), Qt::UTC));
}

// ── weekly ────────────────────────────────────────────────

void TestSchedule::weeklySameDayLaterTime() {
    ScheduleSpec s;
    s.kind = ScheduleSpec::Weekly;
    s.weekday = Qt::Wednesday;   // 2026-11-11 是周三
    s.time = QTime(9, 0);
    QCOMPARE(s.nextOccurrence({}, localDateTime(2026, 11, 11, 8, 0)),
             localDateTime(2026, 11, 11, 9, 0));
}

void TestSchedule::weeklySameDayEarlierTimeRollsToNextWeek() {
    ScheduleSpec s;
    s.kind = ScheduleSpec::Weekly;
    s.weekday = Qt::Wednesday;
    s.time = QTime(9, 0);
    QCOMPARE(s.nextOccurrence({}, localDateTime(2026, 11, 11, 10, 0)),
             localDateTime(2026, 11, 18, 9, 0));
}

void TestSchedule::weeklyWrapsToNextWeek() {
    ScheduleSpec s;
    s.kind = ScheduleSpec::Weekly;
    s.weekday = Qt::Monday;
    s.time = QTime(9, 0);
    QCOMPARE(s.nextOccurrence({}, localDateTime(2026, 11, 10, 12, 0)),
             localDateTime(2026, 11, 16, 9, 0));
}

// ── interval ──────────────────────────────────────────────

void TestSchedule::intervalAdvancesFromAnchor() {
    ScheduleSpec s;
    s.kind = ScheduleSpec::Interval;
    s.hours = 2;
    const QDateTime anchor = localDateTime(2026, 11, 10, 0, 0);
    QCOMPARE(s.nextOccurrence(anchor, localDateTime(2026, 11, 10, 5, 30)),
             localDateTime(2026, 11, 10, 6, 0));
}

void TestSchedule::intervalAnchorInFutureReturnsAnchor() {
    ScheduleSpec s;
    s.kind = ScheduleSpec::Interval;
    s.hours = 2;
    const QDateTime anchor = localDateTime(2026, 11, 10, 10, 0);
    QCOMPARE(s.nextOccurrence(anchor, localDateTime(2026, 11, 10, 8, 0)),
             anchor);
}

void TestSchedule::intervalExactMultipleStepsOneFurther() {
    ScheduleSpec s;
    s.kind = ScheduleSpec::Interval;
    s.hours = 2;
    const QDateTime anchor = localDateTime(2026, 11, 10, 0, 0);
    // after 恰为第 3 次触发点 → 返回第 4 次（严格晚于）。
    QCOMPARE(s.nextOccurrence(anchor, localDateTime(2026, 11, 10, 6, 0)),
             localDateTime(2026, 11, 10, 8, 0));
}

void TestSchedule::intervalIsTimezoneAgnostic() {
    const QTimeZone ny = newYorkZone();
    if (!ny.isValid())
        QSKIP("America/New_York 时区数据不可用");

    ScheduleSpec s;
    s.kind = ScheduleSpec::Interval;
    s.hours = 2;
    const QDateTime anchor(QDate(2026, 11, 10), QTime(0, 0), ny);
    // after 用 UTC 表达同一时刻 +90 分钟：结果仍是锚点 +2h（毫秒算术）。
    const QDateTime after =
        anchor.toUTC().addMSecs(90 * 60 * 1000);
    const QDateTime occ = s.nextOccurrence(anchor, after);
    QCOMPARE(occ.toMSecsSinceEpoch(),
             anchor.toMSecsSinceEpoch() + 2LL * 60 * 60 * 1000);
}

// ── 防御与展示 ────────────────────────────────────────────

void TestSchedule::invalidSpecYieldsInvalidOccurrence() {
    const QDateTime now = localDateTime(2026, 11, 10, 12, 0);

    ScheduleSpec badTime;
    badTime.time = QTime();
    QVERIFY(!badTime.nextOccurrence({}, now).isValid());

    ScheduleSpec badHours;
    badHours.kind = ScheduleSpec::Interval;
    badHours.hours = 0;
    QVERIFY(!badHours.nextOccurrence(now, now).isValid());

    ScheduleSpec ok;
    ok.time = QTime(9, 0);
    QVERIFY(!ok.nextOccurrence({}, QDateTime()).isValid());   // after 无效

    ScheduleSpec interval;
    interval.kind = ScheduleSpec::Interval;
    interval.hours = 6;
    QVERIFY(!interval.nextOccurrence(QDateTime(), now).isValid());   // anchor 无效
}

void TestSchedule::displayTextFormats() {
    ScheduleSpec daily;
    daily.time = QTime(9, 0);
    QCOMPARE(daily.displayText(), QStringLiteral("每天 09:00"));

    ScheduleSpec weekly;
    weekly.kind = ScheduleSpec::Weekly;
    weekly.weekday = 3;
    weekly.time = QTime(9, 0);
    // 星期名随系统语言，只钉结构与时刻。
    QVERIFY(weekly.displayText().startsWith(QStringLiteral("每周")));
    QVERIFY(weekly.displayText().contains(QStringLiteral("09:00")));

    ScheduleSpec interval;
    interval.kind = ScheduleSpec::Interval;
    interval.hours = 6;
    QCOMPARE(interval.displayText(), QStringLiteral("每 6 小时"));

    ScheduleSpec bad;
    bad.time = QTime();
    QVERIFY(bad.displayText().isEmpty());
}

QTEST_GUILESS_MAIN(TestSchedule)
#include "test_schedule.moc"
