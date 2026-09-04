#pragma once

#include <QString>

namespace NexusAI {

// Runtime logging: installs a Qt message handler that mirrors every
// qDebug/qInfo/qWarning/qCritical message to a daily log file under
// <AppLocalDataLocation>/logs/nexus-YYYYMMDD.log, pruning old files.
// Complements the SQLite audit trail, which records agent tool activity
// (visible in the in-app audit dialog); this captures application-level
// diagnostics, which a windowed (no-console) build would otherwise discard.
class Logger {
public:
    static void install();

    // Minimum level written to the log file; messages below it are dropped
    // (stderr mirror included). Levels: QtDebugMsg < QtInfoMsg <
    // QtWarningMsg < QtCriticalMsg < QtFatalMsg - Fatal is never filtered.
    static void setMinLevel(QtMsgType level);

    // Path of the log file currently being written (empty before the first
    // message or when the log directory is not writable).
    static QString logFilePath();

    // Directory holding the rotated log files.
    static QString logDir();
};

} // namespace NexusAI
