#include "TraceModel.h"

#include <utility>

namespace NexusAI {

TraceModel::TraceModel(QObject* parent) : QAbstractListModel(parent) {}

int TraceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_nodes.size();
}

QVariant TraceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_nodes.size()) return {};
    const QVariantMap& node = m_nodes.at(index.row());
    switch (role) {
    case IdRole:        return node.value("id");
    case TypeRole:      return node.value("type");
    case TitleRole:     return node.value("title");
    case StatusRole:    return node.value("status");
    case ArgsRole:      return node.value("args");
    case ResultRole:    return node.value("result");
    case TimestampRole: return node.value("timestamp");
    case ModelDataRole: return node;
    }
    return {};
}

QHash<int, QByteArray> TraceModel::roleNames() const {
    return {
        {IdRole,        "nodeId"},   // "id" reserved-ish; avoids delegate ambiguity
        {TypeRole,      "type"},
        {TitleRole,     "title"},
        {StatusRole,    "status"},
        {ArgsRole,      "args"},
        {ResultRole,    "result"},
        {TimestampRole, "timestamp"},
        {ModelDataRole, "modelData"}, // TraceNodeRouter 子组件的 required property
    };
}

void TraceModel::resetNodes(const QVariantList& nodes) {
    beginResetModel();
    m_nodes.clear();
    for (const auto& entry : nodes)
        m_nodes.append(entry.toMap());
    endResetModel();
}

void TraceModel::applyDelta(const QVariantList& nodes) {
    QHash<QString, int> oldRows;
    oldRows.reserve(m_nodes.size());
    for (int i = 0; i < m_nodes.size(); ++i)
        oldRows.insert(m_nodes.at(i).value("id").toString(), i);

    QVector<std::pair<int, QVariantMap>> changed;
    QVector<QVariantMap> appended;
    QSet<QString> seen;
    bool structural = false;
    bool appendsStarted = false;

    for (const auto& entry : nodes) {
        const QVariantMap node = entry.toMap();
        const QString id = node.value("id").toString();
        if (seen.contains(id)) { structural = true; break; }
        seen.insert(id);

        const auto it = oldRows.constFind(id);
        if (it != oldRows.constEnd()) {
            // An update after an append would mean reordering - not a delta
            if (appendsStarted) { structural = true; break; }
            if (m_nodes.at(*it) != node)
                changed.append({*it, node});
        } else {
            appendsStarted = true;
            appended.append(node);
        }
    }
    // Any vanished old id means removals - only a full reset represents
    // those (checking old ids, not counts: brand-new nodes also sit in seen)
    if (!structural) {
        for (auto it = oldRows.constBegin(); it != oldRows.constEnd(); ++it) {
            if (!seen.contains(it.key())) { structural = true; break; }
        }
    }

    if (structural) {
        resetNodes(nodes);
        return;
    }

    for (const auto& [row, node] : changed) {
        m_nodes[row] = node;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx);
    }
    if (!appended.isEmpty()) {
        beginInsertRows({}, m_nodes.size(), m_nodes.size() + appended.size() - 1);
        for (const auto& node : appended)
            m_nodes.append(node);
        endInsertRows();
    }
}

void TraceModel::appendNode(const QVariantMap& node) {
    beginInsertRows({}, m_nodes.size(), m_nodes.size());
    m_nodes.append(node);
    endInsertRows();
}

void TraceModel::updateNode(int row, const QVariantMap& fields) {
    if (row < 0 || row >= m_nodes.size()) return;
    QVariantMap node = m_nodes.at(row);
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        node[it.key()] = it.value();
    if (node == m_nodes.at(row)) return;
    m_nodes[row] = node;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void TraceModel::updateLastNode(const QVariantMap& fields) {
    if (m_nodes.isEmpty()) return;
    updateNode(m_nodes.size() - 1, fields);
}

void TraceModel::clearNodes() {
    resetNodes({});
}

QVariantMap TraceModel::get(int row) const {
    return (row >= 0 && row < m_nodes.size()) ? m_nodes.at(row) : QVariantMap{};
}

int TraceModel::rowOfId(const QString& id) const {
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes.at(i).value("id").toString() == id) return i;
    return -1;
}

int TraceModel::rowOfMessageId(qint64 messageId) const {
    if (messageId <= 0) return -1;
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes.at(i).value("messageId").toLongLong() == messageId) return i;
    return -1;
}

int TraceModel::lastIndexOfType(const QString& type) const {
    for (int i = m_nodes.size() - 1; i >= 0; --i)
        if (m_nodes.at(i).value("type").toString() == type) return i;
    return -1;
}

int TraceModel::rankOfTypeAt(int row, const QString& type) const {
    if (row < 0 || row >= m_nodes.size()) return 0;
    if (m_nodes.at(row).value("type").toString() != type) return 0;
    int rank = 0;
    for (int i = 0; i <= row; ++i)
        if (m_nodes.at(i).value("type").toString() == type) ++rank;
    return rank;
}

int TraceModel::intermediateCount() const {
    int count = 0;
    for (const auto& node : m_nodes)
        if (isIntermediate(node)) ++count;
    return count;
}

QVariantList TraceModel::userMessageIndex(int previewChars) const {
    QVariantList out;
    const int cap = qMax(1, previewChars);
    for (int i = 0; i < m_nodes.size(); ++i) {
        const QVariantMap& node = m_nodes.at(i);
        if (node.value("type").toString() != QLatin1String("user_message"))
            continue;

        // 正文就是节点 title（AIAgent::startTask 写的就是用户原文）。
        const QString body = node.value("title").toString();
        // preview 保留原始长度信息不动，只做展示层的压缩：
        // 换行/连续空白折成单空格，否则多行消息的悬停提示会占满屏幕。
        QString preview = body.simplified();
        if (preview.size() > cap)
            preview = preview.left(cap) + QStringLiteral("…");

        QVariantMap entry;
        entry.insert(QStringLiteral("row"), i);
        entry.insert(QStringLiteral("messageId"),
                     node.value("messageId").toLongLong());
        entry.insert(QStringLiteral("preview"), preview);
        entry.insert(QStringLiteral("length"), body.size());
        entry.insert(QStringLiteral("timestamp"),
                     node.value("timestamp").toLongLong());
        out.append(entry);
    }
    return out;
}

bool TraceModel::isIntermediate(const QVariantMap& node) {
    const QString type = node.value("type").toString();
    return type == QLatin1String("thinking") || type == QLatin1String("tool_call")
        || type == QLatin1String("tool_result") || type == QLatin1String("context");
}

} // namespace NexusAI
