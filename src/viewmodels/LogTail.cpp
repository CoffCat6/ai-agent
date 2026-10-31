#include "LogTail.h"

#include "core/utils/LogTailReader.h"
#include "core/utils/Logger.h"

#include <QJSEngine>

namespace NexusAI {

LogTail::LogTail(QObject* parent) : QObject(parent) {}
LogTail* LogTail::instance() {
    static LogTail* tail = new LogTail;
    return tail;
}
LogTail* LogTail::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

QVariantMap LogTail::read(const QString& path, int maxLines,
                          const QString& minLevel, const QString& keyword) const {
    LogTailOptions options;
    options.maxLines = maxLines;
    options.keyword = keyword.trimmed();

    // 级别阈值：空 = 全部；未知 token 也按"全部"处理，绝不因为 UI 传错
    // 一个字符串就把所有日志静默过滤掉。
    const QString level = minLevel.trimmed().toLower();
    if (level == QLatin1String("debug")) {
        options.minSeverityRank = Logger::severityRank(QtDebugMsg);
    } else if (level == QLatin1String("info")) {
        options.minSeverityRank = Logger::severityRank(QtInfoMsg);
    } else if (level == QLatin1String("warning") || level == QLatin1String("warn")) {
        options.minSeverityRank = Logger::severityRank(QtWarningMsg);
    } else if (level == QLatin1String("critical") || level == QLatin1String("crit")) {
        options.minSeverityRank = Logger::severityRank(QtCriticalMsg);
    }

    const LogTailResult result = readLogTail(path, options);
    return {{QStringLiteral("ok"), result.ok},
            {QStringLiteral("path"), result.path},
            {QStringLiteral("error"), result.error},
            {QStringLiteral("lines"), result.lines},
            {QStringLiteral("matched"), result.matched},
            {QStringLiteral("truncated"), result.truncated},
            {QStringLiteral("bytesRead"), result.bytesRead}};
}

} // namespace NexusAI
