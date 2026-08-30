#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtDebug>

#include <cstdio>

namespace NexusAI {

namespace {

QMutex g_mutex;
QFile g_file;
QString g_filePath;

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

// Daily rotation keeps name order == time order; drop everything but the
// newest files so the log directory cannot grow without bound.
void pruneOldLogs(const QDir& dir, int keep = 7) {
    const QFileInfoList files = dir.entryInfoList(
        QStringList() << "nexus-*.log", QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = keep; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
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

void Logger::install() {
    qInstallMessageHandler(messageHandler);
}

} // namespace NexusAI
