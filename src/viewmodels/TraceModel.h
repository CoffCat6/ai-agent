#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace NexusAI {

// Incremental list model behind AgentVM.traceModel. The agent core keeps its
// own QJsonArray trace and notifies wholesale; this model diffs by node id
// on every sync so the QML ListView only re-renders appended or changed rows
// instead of tearing down every delegate (streaming used to restart all
// Canvas icons and running animations several times per second).
class TraceModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        TitleRole,
        StatusRole,
        ArgsRole,
        ResultRole,
        TimestampRole,
        ModelDataRole,   // whole row as a QVariantMap
    };

    explicit TraceModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Full replacement (session resume / clear): emits modelReset.
    void resetNodes(const QVariantList& nodes);
    // Id-based sync: trailing appends become rowsInserted, in-place changes
    // become dataChanged; any structural change (reorder / removal /
    // duplicate id) falls back to resetNodes.
    void applyDelta(const QVariantList& nodes);
    void appendNode(const QVariantMap& node);              // rowsInserted
    void updateNode(int row, const QVariantMap& fields);   // dataChanged
    void updateLastNode(const QVariantMap& fields);
    void clearNodes();                                     // modelReset to empty

    Q_INVOKABLE QVariantMap get(int row) const;

    int count() const { return m_nodes.size(); }
    const QVariantMap& nodeAt(int row) const { return m_nodes.at(row); }
    int rowOfId(const QString& id) const;
    // A1 (hit-locate): the first row whose persisted messages.id (stored as
    // the node's `messageId`) matches; -1 when absent. Only user_message
    // nodes carry a messageId today, so assistant-only hits fall through.
    int rowOfMessageId(qint64 messageId) const;
    // 该类型的最后一行（无命中返回 -1）。docs/20 §3.1/§3.2：消息级操作按钮
    // 只应在「该类型的最后一条」上显示 —— finishTask 会在助手回复（summary）
    // 之后追加 metrics 节点，所以整表末行恒为 metrics，不能用 index==count-1。
    int lastIndexOfType(const QString& type) const;
    // 第 row 行在它那一类节点里排第几（1 起）；不命中返回 0。docs/21 分叉锚点用。
    int rankOfTypeAt(int row, const QString& type) const;
    // Nodes hidden by the "fold process" toggle in the chat page
    int intermediateCount() const;

private:
    static bool isIntermediate(const QVariantMap& node);

    QVector<QVariantMap> m_nodes;
};

} // namespace NexusAI
