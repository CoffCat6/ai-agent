#pragma once

#include "core/utils/Logger.h"

#include <QAbstractListModel>
#include <QString>
#include <QTimer>
#include <QVector>

namespace NexusAI {

// List model behind the in-app log panel (日志开发 doc 6, M1/M3).
//
// Session-log semantics (日志增强二轮): the view is an APPEND-ONLY record of
// everything logged since application start — rows never disappear because
// the ring evicted them. Capture runs on a low-frequency timer for the
// whole process lifetime (logs logged while the panel is closed are still
// kept); UI row notifications only matter while the panel is visible.
// filterLevel shows only the given severity (-1 = all); it stores a
// severity RANK (0=Debug..4=Fatal), NOT a raw QtMsgType (whose declaration
// order is not a severity order in Qt 6). filterText is
// a case-insensitive substring over message+category, filterCategory a
// prefix match on the logging category. Filter changes rebuild the view
// (modelReset); appends are mirrored as row inserts.
//
// Memory bounds: the session mirror keeps at most MaxSessionRows entries
// (oldest trimmed); entries evicted from BOTH the ring and the mirror are
// gone for good — that is the physical limit of an in-process log.
class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    // Batch 12 前日志增强: event-class filter — "" shows everything,
    // otherwise entries whose category starts with the value (empty
    // category counts as "nexus.app").
    Q_PROPERTY(QString filterCategory READ filterCategory WRITE setFilterCategory NOTIFY filterChanged)
    Q_PROPERTY(bool polling READ polling WRITE setPolling NOTIFY pollingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        TimestampRole = Qt::UserRole + 1,
        LevelRole,
        CategoryRole,
        MessageRole,
        SeqRole,
    };

    explicit LogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Severity gate: -1 = show ALL (no severity filter); otherwise a
    // severity rank (0=Debug,1=Info,2=Warning,3=Critical,4=Fatal) — only
    // entries whose rank == filterLevel are shown. NOT a raw QtMsgType value.
    int filterLevel() const;
    void setFilterLevel(int level);
    QString filterText() const;
    void setFilterText(const QString& text);
    QString filterCategory() const { return m_filterCategory; }
    void setFilterCategory(const QString& category);

    bool polling() const;
    void setPolling(bool on);

    int count() const { return m_visible.size(); }

    // Drop every row and re-anchor at the current ring head; later pulls
    // only surface entries newer than the clear.
    Q_INVOKABLE void clearView();

    // Visible rows formatted "[time] [LEVEL] message" for clipboard export.
    Q_INVOKABLE QString toClipboardText() const;

signals:
    void filterChanged();
    void pollingChanged();
    void countChanged();

private slots:
    void pull();

private:
    bool passesFilter(const Logger::LogEntry& entry) const;
    // Row of |seq| inside the filtered view (-1 when filtered out).
    int rowOfSeq(quint64 seq) const;

    // Append-only session capture: low-frequency timer runs for the whole
    // process lifetime (panel-open polling just accelerates it for the UI).
    QTimer m_captureTimer;
    QTimer m_pollTimer;
    quint64 m_lastSeq{0};
    QVector<Logger::LogEntry> m_all;      // session mirror (append-only, capped)
    QVector<Logger::LogEntry> m_visible;  // filtered rows, seq-ordered
    int m_filterLevel{-1};  // severity rank: -1 = ALL (default: show every level)
    QString m_filterText;
    QString m_filterCategory;
    bool m_polling{false};

    static constexpr int MaxSessionRows = 5000;
    static constexpr int CaptureIntervalMs = 1000;   // background capture
    void trimSessionMirror();
};

} // namespace NexusAI
