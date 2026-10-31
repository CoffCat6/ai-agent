#include "LogTailReader.h"

#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace NexusAI {
namespace {

QString tr(const char* source) {
    return QCoreApplication::translate("LogTailReader", source);
}

int levelRankOfToken(const QString& token) {
    if (token == QLatin1String("DEBUG")) return 0;
    if (token == QLatin1String("INFO")) return 1;
    if (token == QLatin1String("WARN")) return 2;
    if (token == QLatin1String("CRIT")) return 3;
    if (token == QLatin1String("FATAL")) return 4;
    return -1;
}

// Logger 的行头固定为 "[yyyy-MM-dd hh:mm:ss.zzz] [LEVEL] ..."。
// 解析不出级别的行视为续行（会继承上一行的级别）。
int parseLevelRank(const QString& line) {
    if (!line.startsWith(QLatin1Char('['))) return -1;
    const int levelStart = line.indexOf(QLatin1String("] ["), 1);
    if (levelStart < 0) return -1;
    const int levelEnd = line.indexOf(QLatin1Char(']'), levelStart + 3);
    if (levelEnd < 0) return -1;
    return levelRankOfToken(line.mid(levelStart + 3, levelEnd - levelStart - 3));
}

} // namespace

QString newestLogFile() {
    const QDir directory(Logger::logDir());
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("nexus-*.log")}, QDir::Files, QDir::Time);
    return files.isEmpty() ? QString() : files.first().absoluteFilePath();
}

LogTailResult readLogTail(const QString& path, const LogTailOptions& options) {
    LogTailResult result;
    QString target = path.trimmed();
    if (target.isEmpty()) {
        target = Logger::logFilePath();
        if (target.isEmpty()) target = newestLogFile();
    }
    result.path = target;
    if (target.isEmpty()) {
        result.error = tr("还没有可读取的日志文件");
        return result;
    }

    QFile file(target);
    if (!file.exists()) {
        result.error = tr("日志文件不存在：%1").arg(target);
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = tr("无法读取日志文件：%1").arg(file.errorString());
        return result;
    }

    const qint64 window = qBound<qint64>(qint64{64} * 1024, options.maxBytes,
                                         qint64{8} * 1024 * 1024);
    const qint64 offset = qMax<qint64>(0, file.size() - window);
    if (!file.seek(offset)) {
        result.error = tr("无法定位日志文件尾部：%1").arg(file.errorString());
        return result;
    }
    QByteArray data = file.read(window);
    if (offset > 0) {
        // 从窗口中间开始时，第一行可能是半行：丢弃到第一个换行之后。
        const int newline = data.indexOf('\n');
        if (newline < 0) data.clear();
        else data.remove(0, newline + 1);
    }
    result.bytesRead = data.size();

    const int maxLines = qBound(1, options.maxLines, 2000);
    QStringList matchedLines;
    int matched = 0;
    int currentRank = -1;
    const QStringList rawLines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    for (QString line : rawLines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line.isEmpty()) continue;
        const int parsed = parseLevelRank(line);
        if (parsed >= 0) currentRank = parsed;
        if (options.minSeverityRank >= 0 && currentRank >= 0
            && currentRank < options.minSeverityRank)
            continue;
        if (!options.keyword.isEmpty()
            && !line.contains(options.keyword, Qt::CaseInsensitive))
            continue;
        ++matched;
        if (options.maxLineLength > 0 && line.size() > options.maxLineLength)
            line = line.left(options.maxLineLength) + QStringLiteral("…");
        matchedLines.append(line);
    }

    result.ok = true;
    result.matched = matched;
    result.truncated = offset > 0 || matched > maxLines;
    result.lines = matchedLines.size() > maxLines
        ? matchedLines.mid(matchedLines.size() - maxLines)
        : matchedLines;
    return result;
}

} // namespace NexusAI
