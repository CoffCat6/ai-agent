#include "LogModel.h"

#include <QDateTime>

#include <algorithm>

namespace NexusAI {

namespace {
QString levelName(int level) {
    switch (static_cast<QtMsgType>(level)) {
    case QtDebugMsg:    return QStringLiteral("DEBUG");
    case QtInfoMsg:     return QStringLiteral("INFO");
    case QtWarningMsg:  return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("CRIT");
    case QtFatalMsg:    return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG");
}
} // namespace

LogModel::LogModel(QObject* parent)
    : QAbstractListModel(parent) {
    // Session capture runs for the whole process lifetime (1 s): rows logged
    // while the panel is closed are still kept for later viewing.
    m_captureTimer.setInterval(CaptureIntervalMs);
    connect(&m_captureTimer, &QTimer::timeout, this, &LogModel::pull);
    m_captureTimer.start();

    // Panel-open polling accelerates the same pull for smooth live updates.
    m_pollTimer.setInterval(100);
    connect(&m_pollTimer, &QTimer::timeout, this, &LogModel::pull);
}

void LogModel::trimSessionMirror() {
    const int excess = m_all.size() - MaxSessionRows;
    if (excess <= 0) return;
    m_all.remove(0, excess);
}

int LogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (index.row() < 0 || index.row() >= m_visible.size())
        return {};
    const Logger::LogEntry& entry = m_visible.at(index.row());
    switch (role) {
    case TimestampRole: return entry.timestamp;
    case LevelRole:     return levelName(entry.level);
    case CategoryRole:  return entry.category;
    case MessageRole:   return entry.message;
    case SeqRole:       return qulonglong(entry.seq);
    }
    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const {
    static const QHash<int, QByteArray> roles = {
        {TimestampRole, "timestamp"},
        {LevelRole,     "level"},
        {CategoryRole,  "category"},
        {MessageRole,   "message"},
        {SeqRole,       "seq"},
    };
    return roles;
}

int LogModel::filterLevel() const { return m_filterLevel; }

void LogModel::setFilterLevel(int level) {
    if (m_filterLevel == level) return;
    m_filterLevel = level;
    emit filterChanged();
    // ≤1000 rows: a full rebuild is cheaper than incremental diffing.
    beginResetModel();
    m_visible.clear();
    for (const auto& entry : m_all) {
        if (passesFilter(entry)) m_visible.append(entry);
    }
    endResetModel();
    emit countChanged();
}

void LogModel::setFilterCategory(const QString& category) {
    if (m_filterCategory == category) return;
    m_filterCategory = category;
    emit filterChanged();
    beginResetModel();
    m_visible.clear();
    for (const auto& entry : m_all) {
        if (passesFilter(entry)) m_visible.append(entry);
    }
    endResetModel();
    emit countChanged();
}

QString LogModel::filterText() const { return m_filterText; }

void LogModel::setFilterText(const QString& text) {
    if (m_filterText == text) return;
    m_filterText = text;
    emit filterChanged();
    beginResetModel();
    m_visible.clear();
    for (const auto& entry : m_all) {
        if (passesFilter(entry)) m_visible.append(entry);
    }
    endResetModel();
    emit countChanged();
}

bool LogModel::polling() const { return m_polling; }

void LogModel::setPolling(bool on) {
    if (m_polling == on) return;
    m_polling = on;
    if (m_polling) {
        pull();                       // catch up immediately on open
        m_pollTimer.start();
    } else {
        // Capture keeps running at 1 s in the background — rows logged
        // while the panel is closed are kept for the next open.
        m_pollTimer.stop();
    }
    emit pollingChanged();
}

void LogModel::clearView() {
    beginResetModel();
    m_all.clear();
    m_visible.clear();
    m_lastSeq = Logger::revision();
    endResetModel();
    emit countChanged();
}

QString LogModel::toClipboardText() const {
    QStringList lines;
    lines.reserve(m_visible.size());
    for (const auto& entry : m_visible) {
        const QString ts = QDateTime::fromMSecsSinceEpoch(entry.timestamp)
                               .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        const QString cat = entry.category.isEmpty()
            ? QString() : QStringLiteral(" [%1]").arg(entry.category);
        lines.append(QStringLiteral("[%1] [%2]%3 %4")
                         .arg(ts, levelName(entry.level), cat, entry.message));
    }
    return lines.join(QLatin1Char('\n'));
}

bool LogModel::passesFilter(const Logger::LogEntry& entry) const {
    // "show this severity and above" (QtMsgType orders debug < info < …);
    // m_filterLevel < 0 means ALL — no severity gate.
    if (m_filterLevel >= 0 && entry.level < m_filterLevel) return false;
    if (!m_filterCategory.isEmpty()) {
        // Legacy/general lines have no logging category → treat as "app".
        const QString cat = entry.category.isEmpty()
            ? QStringLiteral("nexus.app") : entry.category;
        if (!cat.startsWith(m_filterCategory)) return false;
    }
    if (!m_filterText.isEmpty()) {
        if (!entry.message.contains(m_filterText, Qt::CaseInsensitive)
                && !entry.category.contains(m_filterText, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

int LogModel::rowOfSeq(quint64 seq) const {
    // m_visible is seq-ordered; binary search.
    auto it = std::lower_bound(
        m_visible.cbegin(), m_visible.cend(), seq,
        [](const Logger::LogEntry& e, quint64 s) { return e.seq < s; });
    if (it == m_visible.cend() || it->seq != seq) return -1;
    return static_cast<int>(it - m_visible.cbegin());
}

void LogModel::pull() {
    const auto fresh = Logger::entriesSince(m_lastSeq);
    if (fresh.isEmpty()) return;
    m_lastSeq = fresh.last().seq;

    // Append-only session log (日志增强二轮): rows already in the view stay
    // even when the ring evicts them — ring evictions no longer remove
    // anything. The session mirror itself is capped for memory safety.
    int inserted = 0;
    for (const auto& entry : fresh) {
        m_all.append(entry);
        if (passesFilter(entry)) {
            const int row = m_visible.size();
            beginInsertRows(QModelIndex(), row, row);
            m_visible.append(entry);
            endInsertRows();
            ++inserted;
        }
    }
    trimSessionMirror();
    if (inserted > 0) emit countChanged();
}

} // namespace NexusAI
