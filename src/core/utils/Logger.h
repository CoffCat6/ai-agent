#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace NexusAI {

// Runtime logging: installs a Qt message handler that mirrors every
// qDebug/qInfo/qWarning/qCritical message to a daily log file under
// <AppLocalDataLocation>/logs/nexus-YYYYMMDD.log, pruning old files.
// Complements the SQLite audit trail, which records agent tool activity
// (visible in the in-app audit dialog); this captures application-level
// diagnostics, which a windowed (no-console) build would otherwise discard.
//
// In-app visibility (日志开发 doc 6, M1): every formatted line is also kept
// in a bounded in-process ring buffer (app-level level filter applies), so
// the UI can show live diagnostics without reading rotated files. The
// snapshot API is pull-based and thread-safe; consumers poll by revision.
class Logger {
public:
    // One buffered log line (fields kept separate; the UI formats them).
    struct LogEntry {
        quint64 seq{0};        // process-wide monotonic id
        qint64 timestamp{0};   // ms since epoch
        int level{0};          // QtMsgType value
        QString category;      // qLoggingCategory (may be empty)
        QString message;
    };

    // Ring capacity: oldest entries are evicted past it (≈200 KB memory cap).
    static constexpr int RingCapacity = 1000;

    static void install();

    // Minimum level written to the log file and ring buffer; messages below
    // it are dropped (stderr mirror included). Levels: QtDebugMsg <
    // QtInfoMsg < QtWarningMsg < QtCriticalMsg < QtFatalMsg - Fatal is
    // never filtered.
    static void setMinLevel(QtMsgType level);
    static QtMsgType minLevel();

    // "debug"/"info"/"warning"/"warn"/"critical"/"crit" (case-insensitive);
    // unknown names yield |fallback|.
    static QtMsgType levelFromName(const QString& name, QtMsgType fallback);

    // Path of the log file currently being written (empty before the first
    // message or when the log directory is not writable).
    static QString logFilePath();

    // Directory holding the rotated log files.
    static QString logDir();

    // ── Ring-buffer snapshot API (thread-safe) ────────────
    // Highest seq assigned so far; 0 before the first buffered line.
    static quint64 revision();
    // Buffered entries with seq > |seq|, oldest first (≤ RingCapacity).
    static QVector<LogEntry> entriesSince(quint64 seq);
};

} // namespace NexusAI

Q_DECLARE_METATYPE(NexusAI::Logger::LogEntry)
