#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QtDebug>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace NexusAI {

namespace {

QMutex g_mutex;
QFile g_file;
QString g_filePath;
QtMsgType g_minLevel = QtInfoMsg;
QString g_logDirectory;
int g_maxFileSizeMb = 10;
int g_retentionFiles = 14;

// In-process ring buffer for the in-app log panel (doc 6 §3.1): bounded to
// Logger::RingCapacity, guarded by g_mutex, filled AFTER the level filter
// so filtered levels never consume ring space.
std::vector<Logger::LogEntry> g_ring;
quint64 g_nextSeq = 1;

const char* levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO";
    case QtWarningMsg:  return "WARN";
    case QtCriticalMsg: return "CRIT";
    case QtFatalMsg:    return "FATAL";
    }
    return "LOG";
}

// Severity ranking: QtMsgType's declaration order is NOT a severity order
// (QtInfoMsg = 4 ranks above QtFatalMsg = 3), so threshold comparisons must
// go through this mapping. Exposed by the doc-6 log-panel tests: with
// minLevel = Warning, qInfo lines passed `type < g_minLevel`.
int severityRank(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:    return 0;
    case QtInfoMsg:     return 1;
    case QtWarningMsg:  return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg:    return 4;
    }
    return 4;
}

bool parseLevel(const QString& name, QtMsgType& out) {
    if (name.compare(QLatin1String("debug"), Qt::CaseInsensitive) == 0) { out = QtDebugMsg; return true; }
    if (name.compare(QLatin1String("info"), Qt::CaseInsensitive) == 0)  { out = QtInfoMsg; return true; }
    if (name.compare(QLatin1String("warning"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("warn"), Qt::CaseInsensitive) == 0) { out = QtWarningMsg; return true; }
    if (name.compare(QLatin1String("critical"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("crit"), Qt::CaseInsensitive) == 0) { out = QtCriticalMsg; return true; }
    return false;
}

QString defaultLogDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs");
}

QString configuredLogDir() {
    return g_logDirectory.trimmed().isEmpty() ? defaultLogDir() : g_logDirectory;
}

qint64 maxFileBytes() {
    return static_cast<qint64>(g_maxFileSizeMb) * 1024 * 1024;
}

int nextLogIndex(const QDir& dir, const QString& date) {
    const QFileInfoList files = dir.entryInfoList(
        QStringList() << QStringLiteral("nexus-%1-*.log").arg(date), QDir::Files);
    int maxIndex = 0;
    static const QRegularExpression indexedName(QStringLiteral("-(\\d+)\\.log$"));
    for (const QFileInfo& info : files) {
        const auto match = indexedName.match(info.fileName());
        if (match.hasMatch())
            maxIndex = qMax(maxIndex, match.captured(1).toInt());
    }
    return maxIndex + 1;
}

QString indexedLogPath(const QDir& dir, const QString& date, int index) {
    return dir.filePath(QStringLiteral("nexus-%1-%2.log")
                            .arg(date, 4, QLatin1Char('0'))
                            .arg(index, 4, 10, QLatin1Char('0')));
}

void pruneOldLogs(const QDir& dir) {
    QFileInfoList files = dir.entryInfoList(
        QStringList() << "nexus-*.log", QDir::Files);
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified() > b.lastModified();
    });
    QSet<QString> retained;
    for (int i = 0; i < qMin(g_retentionFiles, files.size()); ++i)
        retained.insert(files.at(i).absoluteFilePath());
    const QString activePath = QFileInfo(g_filePath).absoluteFilePath();
    if (!g_filePath.isEmpty() && !retained.contains(activePath)) {
        if (retained.size() >= g_retentionFiles && !files.isEmpty())
            retained.remove(files.at(qMin(g_retentionFiles, files.size()) - 1).absoluteFilePath());
        retained.insert(activePath);
    }
    for (const QFileInfo& file : files) {
        if (!retained.contains(file.absoluteFilePath()))
            QFile::remove(file.absoluteFilePath());
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QMutexLocker locker(&g_mutex);
    // QtMsgType is not ordered by severity. Check while holding the same lock
    // used by setMinLevel() to avoid a data race during live settings changes.
    if (severityRank(type) < severityRank(g_minLevel)) return;

    QString line = QString("[%1] [%2]")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"),
             QString::fromLatin1(levelName(type)));
    if (context.category && *context.category)
        line += QString(" [%1]").arg(QString::fromLatin1(context.category));
    line += " " + message;
    if ((type == QtWarningMsg || type == QtCriticalMsg) && context.file)
        line += QString(" (%1:%2)").arg(context.file).arg(context.line);

    // Ring buffer for the in-app log panel (doc 6 §3.1). Best effort for
    // Fatal: the entry lands here before abort() below.
    {
        Logger::LogEntry entry;
        entry.seq = g_nextSeq++;
        entry.timestamp = QDateTime::currentMSecsSinceEpoch();
        entry.level = static_cast<int>(type);
        entry.category = (context.category && *context.category)
            ? QString::fromLatin1(context.category) : QString();
        entry.message = message;
        g_ring.push_back(entry);
        while (static_cast<int>(g_ring.size()) > Logger::RingCapacity)
            g_ring.erase(g_ring.begin());
    }

    const QString dirPath = configuredLogDir();
    const QString date = QDateTime::currentDateTime().toString("yyyyMMdd");
    const QString dailyPath = QDir(dirPath).filePath(QStringLiteral("nexus-%1.log").arg(date));
    QByteArray bytes = line.toUtf8() + '\n';
    if (bytes.size() > maxFileBytes())
        bytes = bytes.left(static_cast<int>(maxFileBytes()));

    if (g_filePath.isEmpty() || QFileInfo(g_filePath).absolutePath() != QDir(dirPath).absolutePath()
            || !QFileInfo(g_filePath).fileName().startsWith(QStringLiteral("nexus-%1").arg(date))) {
        if (g_file.isOpen()) g_file.close();
        QDir().mkpath(dirPath);

        QDir logDirectory(dirPath);
        QString wanted = dailyPath;
        const int latestIndex = nextLogIndex(logDirectory, date) - 1;
        if (latestIndex > 0)
            wanted = indexedLogPath(logDirectory, date, latestIndex);
        else if (QFileInfo::exists(dailyPath)
                 && QFileInfo(dailyPath).size() >= maxFileBytes())
            wanted = indexedLogPath(logDirectory, date, 1);

        if (QFileInfo(wanted).exists() && QFileInfo(wanted).size() >= maxFileBytes())
            wanted = indexedLogPath(logDirectory, date, nextLogIndex(logDirectory, date));
        g_file.setFileName(wanted);
        if (g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            g_filePath = wanted;
            pruneOldLogs(logDirectory);
        } else {
            g_filePath.clear();
        }
    }

    if (g_file.isOpen()) {
        if (g_file.size() > 0 && g_file.size() + bytes.size() > maxFileBytes()) {
            g_file.close();
            const QString nextPath = indexedLogPath(QDir(dirPath), date,
                                                    nextLogIndex(QDir(dirPath), date));
            g_file.setFileName(nextPath);
            if (g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                g_filePath = nextPath;
                pruneOldLogs(QDir(dirPath));
            } else {
                g_filePath.clear();
            }
        }
        if (g_file.isOpen()) {
            g_file.write(bytes);
            g_file.flush();
        }
    }

    // Mirror to stderr so terminal runs and CI still show live output.
    std::fprintf(stderr, "%s\n", qPrintable(line));
    std::fflush(stderr);

    if (type == QtFatalMsg)
        abort();
}

} // namespace

QString Logger::logDir() {
    QMutexLocker locker(&g_mutex);
    return configuredLogDir();
}

void Logger::setLogDirectory(const QString& directory) {
    QString normalized = QDir::cleanPath(directory.trimmed());
    if (directory.trimmed().isEmpty()) normalized.clear();
    QMutexLocker locker(&g_mutex);
    if (g_logDirectory == normalized) return;
    if (g_file.isOpen()) g_file.close();
    g_filePath.clear();
    g_logDirectory = normalized;
}

int Logger::maxFileSizeMb() {
    QMutexLocker locker(&g_mutex);
    return g_maxFileSizeMb;
}

void Logger::setMaxFileSizeMb(int sizeMb) {
    QMutexLocker locker(&g_mutex);
    g_maxFileSizeMb = qBound(1, sizeMb, 1024);
}

int Logger::retentionFiles() {
    QMutexLocker locker(&g_mutex);
    return g_retentionFiles;
}

void Logger::setRetentionFiles(int count) {
    QMutexLocker locker(&g_mutex);
    g_retentionFiles = qBound(1, count, 100);
    const QString dir = configuredLogDir();
    if (QDir(dir).exists()) pruneOldLogs(QDir(dir));
}

QString Logger::logFilePath() {
    QMutexLocker locker(&g_mutex);
    return g_filePath;
}

void Logger::setMinLevel(QtMsgType level) {
    QMutexLocker locker(&g_mutex);
    g_minLevel = level;
}

QtMsgType Logger::minLevel() {
    QMutexLocker locker(&g_mutex);
    return g_minLevel;
}

QtMsgType Logger::levelFromName(const QString& name, QtMsgType fallback) {
    QtMsgType parsed = fallback;
    parseLevel(name, parsed);
    return parsed;
}

quint64 Logger::revision() {
    QMutexLocker locker(&g_mutex);
    return g_nextSeq - 1;
}

QVector<Logger::LogEntry> Logger::entriesSince(quint64 seq) {
    QMutexLocker locker(&g_mutex);
    QVector<LogEntry> out;
    for (const auto& entry : g_ring) {
        if (entry.seq > seq)
            out.append(entry);
    }
    return out;
}

void Logger::install() {
    // Values are loaded before any Qt messages are redirected to this handler.
    QSettings settings(QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
    QtMsgType level = QtInfoMsg;
    parseLevel(settings.value("logging/level", QStringLiteral("info")).toString(), level);
    const QString env = qEnvironmentVariable("NEXUSAI_LOG_LEVEL");
    if (!env.isEmpty()) parseLevel(env, level);

    QMutexLocker locker(&g_mutex);
    g_minLevel = level;
    g_logDirectory = settings.value("logging/directory").toString().trimmed();
    if (!g_logDirectory.isEmpty()) g_logDirectory = QDir::cleanPath(g_logDirectory);
    g_maxFileSizeMb = qBound(1, settings.value("logging/maxFileSizeMb", 10).toInt(), 1024);
    g_retentionFiles = qBound(1, settings.value("logging/retentionFiles", 14).toInt(), 100);
    locker.unlock();
    qInstallMessageHandler(messageHandler);
}

int Logger::backfillFromFile(int maxEntries) {
    if (maxEntries <= 0)
        maxEntries = RingCapacity / 2;  // DFX: cap at 50% (2500)

    const QString dirPath = logDir();
    QDir dir(dirPath);
    if (!dir.exists())
        return 0;

    // Collect log files sorted by name (newest last — nexus-YYYYMMDD.log).
    const QFileInfoList files = dir.entryInfoList(
        QStringList() << "nexus-*.log", QDir::Files, QDir::Name);

    // Read from the newest file(s) backwards until we have enough entries.
    QVector<LogEntry> parsed;
    for (int fi = files.size() - 1; fi >= 0 && parsed.size() < maxEntries; --fi) {
        QFile file(files[fi].absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        // Read tail of file to avoid loading huge files entirely.
        // Seek to the last 256 KB (sufficient for ~2500 entries).
        const qint64 tailSize = 256 * 1024;
        if (file.size() > tailSize)
            file.seek(file.size() - tailSize);

        const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
        file.close();

        // Parse lines in reverse order (newest first), prepend to parsed.
        for (int li = lines.size() - 1; li >= 0 && parsed.size() < maxEntries; --li) {
            const QString& line = lines[li].trimmed();
            if (line.isEmpty()) continue;

            // Format: [yyyy-MM-dd hh:mm:ss.zzz] [LEVEL] [category] message
            // Minimal regex: extract timestamp, level, optional category, message.
            static const QRegularExpression re(
                QStringLiteral("^\\[(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3})\\] "
                               "\\[(DEBUG|INFO|WARN|CRIT|FATAL)\\]"
                               "(?: \\[([^\\]]+)\\])?"
                               " (.+)$"));
            const QRegularExpressionMatch m = re.match(line);
            if (!m.hasMatch()) continue;

            LogEntry entry;
            entry.timestamp = QDateTime::fromString(
                m.captured(1), QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")).toMSecsSinceEpoch();
            entry.level = levelFromName(m.captured(2), QtInfoMsg);
            entry.category = m.captured(3);
            entry.message = m.captured(4);
            entry.historical = true;
            parsed.prepend(entry);
        }
    }

    if (parsed.isEmpty())
        return 0;

    // Inject into ring buffer with dedup (skip if message already present).
    QMutexLocker locker(&g_mutex);
    QSet<QString> existingMessages;
    for (const auto& e : g_ring)
        existingMessages.insert(e.message);

    int injected = 0;
    for (const auto& entry : parsed) {
        if (existingMessages.contains(entry.message))
            continue;
        LogEntry e = entry;
        e.seq = g_nextSeq++;
        g_ring.push_back(e);
        ++injected;
    }

    // Trim ring to capacity.
    while (static_cast<int>(g_ring.size()) > RingCapacity)
        g_ring.erase(g_ring.begin());

    return injected;
}

} // namespace NexusAI
