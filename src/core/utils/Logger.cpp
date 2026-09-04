#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QtDebug>

#include <cstdio>

namespace NexusAI {

namespace {

QMutex g_mutex;
QFile g_file;
QString g_filePath;
QtMsgType g_minLevel = QtInfoMsg;

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

bool parseLevel(const QString& name, QtMsgType& out) {
    if (name.compare(QLatin1String("debug"), Qt::CaseInsensitive) == 0) { out = QtDebugMsg; return true; }
    if (name.compare(QLatin1String("info"), Qt::CaseInsensitive) == 0)  { out = QtInfoMsg; return true; }
    if (name.compare(QLatin1String("warning"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("warn"), Qt::CaseInsensitive) == 0) { out = QtWarningMsg; return true; }
    if (name.compare(QLatin1String("critical"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("crit"), Qt::CaseInsensitive) == 0) { out = QtCriticalMsg; return true; }
    return false;
}

// Daily rotation keeps name order == time order; drop everything but the
// newest files so the log directory cannot grow without bound.
void pruneOldLogs(const QDir& dir, int keep = 7) {
    const QFileInfoList files = dir.entryInfoList(
        QStringList() << "nexus-*.log", QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = keep; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    // Level filter (Fatal can never be below the threshold)
    if (type < g_minLevel) return;

    QMutexLocker locker(&g_mutex);

    QString line = QString("[%1] [%2]")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"),
             QString::fromLatin1(levelName(type)));
    if (context.category && *context.category)
        line += QString(" [%1]").arg(QString::fromLatin1(context.category));
    line += " " + message;
    if ((type == QtWarningMsg || type == QtCriticalMsg) && context.file)
        line += QString(" (%1:%2)").arg(context.file).arg(context.line);

    // (Re)open the target file when the day changed or on first message.
    const QString dirPath = Logger::logDir();
    const QString wanted = dirPath + "/nexus-" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".log";
    if (g_filePath != wanted) {
        if (g_file.isOpen())
            g_file.close();
        QDir().mkpath(dirPath);
        g_file.setFileName(wanted);
        if (g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            g_filePath = wanted;
            pruneOldLogs(QDir(dirPath));
        } else {
            g_filePath.clear();
        }
    }

    if (g_file.isOpen()) {
        g_file.write(line.toUtf8());
        g_file.write("\n");
        g_file.flush();
    }

    // Mirror to stderr so terminal runs and CI still show live output.
    std::fprintf(stderr, "%s\n", qPrintable(line));
    std::fflush(stderr);

    if (type == QtFatalMsg)
        abort();
}

} // namespace

QString Logger::logDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
}

QString Logger::logFilePath() {
    QMutexLocker locker(&g_mutex);
    return g_filePath;
}

void Logger::setMinLevel(QtMsgType level) {
    QMutexLocker locker(&g_mutex);
    g_minLevel = level;
}

void Logger::install() {
    // Minimum level from settings (logging/level: debug|info|warning|critical,
    // default "info"), overridable per run via the NEXUSAI_LOG_LEVEL env var.
    QSettings settings(QStringLiteral("NexusAI"), QStringLiteral("NexusAI"));
    QtMsgType level = QtInfoMsg;
    parseLevel(settings.value("logging/level", QStringLiteral("info")).toString(), level);
    const QString env = qEnvironmentVariable("NEXUSAI_LOG_LEVEL");
    if (!env.isEmpty())
        parseLevel(env, level);
    g_minLevel = level;

    qInstallMessageHandler(messageHandler);
}

} // namespace NexusAI
