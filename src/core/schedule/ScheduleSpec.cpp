#include "core/schedule/ScheduleSpec.h"

#include <QLocale>

namespace NexusAI {
namespace {

constexpr int kMaxIntervalHours = 168;

QString kindToString(ScheduleSpec::Kind kind) {
    switch (kind) {
        case ScheduleSpec::Daily:    return QStringLiteral("daily");
        case ScheduleSpec::Weekly:   return QStringLiteral("weekly");
        case ScheduleSpec::Interval: return QStringLiteral("interval");
    }
    return QStringLiteral("daily");
}

} // namespace

std::optional<ScheduleSpec> ScheduleSpec::fromJson(const QJsonObject& json) {
    const QString kind = json.value(QLatin1String("kind")).toString();
    ScheduleSpec spec;
    if (kind == QLatin1String("daily")) {
        spec.kind = Daily;
    } else if (kind == QLatin1String("weekly")) {
        spec.kind = Weekly;
    } else if (kind == QLatin1String("interval")) {
        spec.kind = Interval;
    } else {
        return std::nullopt;   // 缺失/未知 kind：无法归类
    }
    spec.time = QTime::fromString(json.value(QLatin1String("time")).toString(),
                                  QStringLiteral("HH:mm"));
    spec.weekday = json.value(QLatin1String("weekday")).toInt(1);
    spec.hours = json.value(QLatin1String("hours")).toInt(1);
    return spec;
}

QJsonObject ScheduleSpec::toJson() const {
    QJsonObject json;
    json.insert(QLatin1String("kind"), kindToString(kind));
    if (kind == Interval) {
        json.insert(QLatin1String("hours"), hours);
        return json;
    }
    json.insert(QLatin1String("time"), time.toString(QStringLiteral("HH:mm")));
    if (kind == Weekly)
        json.insert(QLatin1String("weekday"), weekday);
    return json;
}

QString ScheduleSpec::validationError() const {
    switch (kind) {
        case Weekly:
            if (weekday < 1 || weekday > 7)
                return tr("星期必须在 1（周一）到 7（周日）之间");
            Q_FALLTHROUGH();
        case Daily:
            if (!time.isValid())
                return tr("请填写有效的触发时间（HH:mm）");
            break;
        case Interval:
            if (hours < 1 || hours > kMaxIntervalHours)
                return tr("间隔小时数必须在 1 到 168 之间");
            break;
    }
    return QString();
}

QDateTime ScheduleSpec::nextOccurrence(const QDateTime& anchor,
                                       const QDateTime& after,
                                       const QTimeZone& tz) const {
    if (!isValid() || !after.isValid())
        return QDateTime();

    switch (kind) {
        case Daily:
        case Weekly: {
            if (!time.isValid())
                return QDateTime();
            // after 的日期部分按 tz 解释（比较本身是时刻语义，不受表达
            // 时区影响）。Daily 在今天/明天内必有解；Weekly 的目标星期
            // 在 0..7 天内必再次出现，同日时刻已过则顺延到下周。
            const QDate baseDate = after.toTimeZone(tz).date();
            const int maxDays = (kind == Weekly) ? 7 : 1;
            for (int ahead = 0; ahead <= maxDays; ++ahead) {
                const QDate date = baseDate.addDays(ahead);
                if (kind == Weekly && date.dayOfWeek() != weekday)
                    continue;
                // DST 空档时刻按 QDateTime 规范化（跳变前偏移）落位，
                // 语义由测试矩阵固化。
                const QDateTime candidate(date, time, tz);
                if (candidate > after)
                    return candidate;
            }
            return QDateTime();   // 理论不可达：合法规格在窗口内必有解
        }
        case Interval: {
            if (!anchor.isValid() || hours < 1)
                return QDateTime();
            const qint64 stepMs = qint64(hours) * 60LL * 60LL * 1000LL;
            const qint64 anchorMs = anchor.toMSecsSinceEpoch();
            const qint64 afterMs = after.toMSecsSinceEpoch();
            if (anchorMs > afterMs)
                return anchor;    // 锚点在未来：下一次就是锚点本身
            // anchor + k*step > after 的最小值；恰好整除时 +1 步保证
            // 严格晚于（after 恰为一次触发点 → 下一个周期）。
            const qint64 elapsed = afterMs - anchorMs;
            const qint64 k = elapsed / stepMs + 1;
            return QDateTime::fromMSecsSinceEpoch(anchorMs + k * stepMs);
        }
    }
    return QDateTime();
}

QDateTime ScheduleSpec::latestOccurrenceAtOrBefore(const QDateTime& anchor,
                                                   const QDateTime& now,
                                                   const QTimeZone& tz) const {
    if (!isValid() || !now.isValid())
        return QDateTime();

    switch (kind) {
        case Daily:
        case Weekly: {
            if (!time.isValid())
                return QDateTime();
            // 从 now 所在日往回找：Daily 最多 1 天，Weekly 最多 7 天，
            // 必有解（DST 落位语义与 nextOccurrence 一致）。
            const QDate nowDate = now.toTimeZone(tz).date();
            const int maxBack = (kind == Weekly) ? 7 : 1;
            for (int back = 0; back <= maxBack; ++back) {
                const QDate date = nowDate.addDays(-back);
                if (kind == Weekly && date.dayOfWeek() != weekday)
                    continue;
                const QDateTime candidate(date, time, tz);
                if (candidate <= now)
                    return candidate;
            }
            return QDateTime();
        }
        case Interval: {
            if (!anchor.isValid() || hours < 1)
                return QDateTime();
            const qint64 stepMs = qint64(hours) * 60LL * 60LL * 1000LL;
            const qint64 anchorMs = anchor.toMSecsSinceEpoch();
            const qint64 nowMs = now.toMSecsSinceEpoch();
            if (nowMs < anchorMs)
                return QDateTime();   // 锚点在未来：还没有任何应触发点
            const qint64 k = (nowMs - anchorMs) / stepMs;
            if (k < 1)
                return QDateTime();   // 首个周期都还没到
            return QDateTime::fromMSecsSinceEpoch(anchorMs + k * stepMs);
        }
    }
    return QDateTime();
}

QString ScheduleSpec::displayText() const {
    if (!isValid())
        return QString();
    const QString hhmm = time.toString(QStringLiteral("HH:mm"));
    switch (kind) {
        case Daily:
            return tr("每天 %1").arg(hhmm);
        case Weekly: {
            const QString day =
                QLocale::system().dayName(weekday, QLocale::ShortFormat);
            return tr("每周%1 %2").arg(day, hhmm);
        }
        case Interval:
            return tr("每 %1 小时").arg(hours);
    }
    return QString();
}

} // namespace NexusAI
