#pragma once

#include "core/utils/Logger.h"

#include <QAbstractListModel>
#include <QString>
#include <QTimer>
#include <QVector>

namespace NexusAI {

// List model behind the in-app log panel (日志开发 doc 6, M1/M3).
// Pulls incremental entries from Logger's ring buffer on a 100 ms timer —
// the timer runs only while the panel is open (zero idle cost) — and keeps
// a filtered mirror of the ring: filterLevel shows the given severity and
// above, filterText is a case-insensitive substring over message+category.
// Filter changes rebuild the view (modelReset); ring evictions and appends
// are mirrored as row removals/inserts.
class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
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

    int filterLevel() const;
    void setFilterLevel(int level);
    QString filterText() const;
    void setFilterText(const QString& text);

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

    QTimer m_pollTimer;
    quint64 m_lastSeq{0};
    QVector<Logger::LogEntry> m_all;      // mirror of the ring (≤ capacity)
    QVector<Logger::LogEntry> m_visible;  // filtered rows, seq-ordered
    int m_filterLevel{QtInfoMsg};
    QString m_filterText;
    bool m_polling{false};
};

} // namespace NexusAI
