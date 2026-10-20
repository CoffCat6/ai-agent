import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Review section of the right panel: the session's write_file / edit_file
// checkpoints, one row per file, arranged in a collapsible directory tree.
// Data flows AgentVM.fileCheckpoints -> here; per-file undo is routed back
// through AgentVM.undoFileCheckpoint and row clicks open the wide review
// dialog (FileChangeDetailDialog). Path-only rows (oversized or unreadable
// originals) are shown as audit history and cannot be undone or diffed.
Item {
    id: root

    property var checkpoints: []
    property bool collapsed: false
    property string errorText: ""
    // Test/embedding hook: overrides AgentVM.fileCheckpointDetail(id).
    property var detailProvider: null
    // Active project root: used to display repo-relative paths when set.
    property string projectRoot: ""
    // Tree height cap in bounded contexts; 0 = fill the available height
    // (used when the panel is the body of the review tab).
    property int listMaxHeight: 260

    readonly property int itemCount: checkpoints ? checkpoints.length : 0
    readonly property bool hasItems: itemCount > 0
    readonly property bool listVisible: hasItems && !collapsed
    readonly property int renderedCount: changeTree.fileRowCount
    readonly property int treeRowCount: changeTree.rowCount
    readonly property int treeFolderRowCount: changeTree.folderRowCount
    readonly property alias detailVisible: detailDialog.visible
    readonly property alias detailDialogItem: detailDialog
    readonly property alias undoDialogVisible: undoDialog.visible
    readonly property int activeAdded: sumStat(true)
    readonly property int activeRemoved: sumStat(false)

    function activeCount(entry) {
        if (entry.activeCount !== undefined) return entry.activeCount
        return entry.undone ? 0 : 1
    }

    function changeCount(entry) {
        return entry.changeCount !== undefined ? entry.changeCount : 1
    }

    function statusColor(entry) {
        if (root.activeCount(entry) === 0) return Theme.textSecondary
        if (entry.truncated) return Theme.statusWarning
        return Theme.brandPrimary
    }

    function sumStat(added) {
        let total = 0
        for (let i = 0; i < itemCount; ++i) {
            const entry = checkpoints[i]
            if (root.activeCount(entry) === 0) continue
            total += added ? (entry.addedLines || 0) : (entry.removedLines || 0)
        }
        return total
    }

    function detailFor(entry) {
        if (root.detailProvider) return root.detailProvider(entry.id)
        return AgentVM.fileCheckpointDetail(entry.id)
    }

    function openDetail(entry) {
        if (!entry) return
        detailDialog.openFor(entry, root.detailFor(entry))
    }

    function closeDetail() {
        detailDialog.close()
    }

    function detailPath() {
        return detailDialog.checkpoint ? detailDialog.checkpoint.path : ""
    }

    function detailDiffLineCount() {
        return detailDialog.diffLineCount
    }

    // Test hooks for the directory tree.
    function toggleTreeFolder(path) {
        changeTree.toggleFolder(path)
    }

    function treeIsCollapsed(path) {
        return changeTree.isCollapsed(path)
    }

    function expandTree() {
        changeTree.expandAll()
    }

    function collapseTree() {
        changeTree.collapseAll()
    }

    function rowAction(entry) {
        const active = root.activeCount(entry)
        if (active > 0 && entry.truncated === true) {
            return { "text": qsTr("不可撤销"),
                     "tooltip": qsTr("原文件过大，仅记录了路径，请用 git 恢复"),
                     "enabled": false }
        }
        return {
            "text": active === 0 ? qsTr("已撤销") : qsTr("撤销"),
            "tooltip": active === 0
                ? qsTr("本会话对该文件的改动已撤销")
                : qsTr("撤销该文件在本会话的全部改动"),
            "enabled": active > 0
        }
    }

    function requestUndo(entry) {
        root.errorText = ""
        undoDialog.pending = entry
        undoDialog.open()
    }

    function performUndo(entry) {
        root.errorText = ""
        if (!AgentVM.undoFileCheckpoint(entry.id))
            root.errorText = qsTr("撤销失败：文件或检查点已不可用。")
    }

    visible: hasItems
    implicitHeight: hasItems ? content.implicitHeight : 0

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 6

        // Header doubles as the collapse toggle. The MouseArea is declared
        // before the row so the header's action buttons stay clickable; plain
        // text does not consume clicks and falls through to the toggle.
        Item {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 22

            HoverHandler { id: headerHover }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.collapsed = !root.collapsed
                Accessible.name: qsTr("展开或折叠审阅列表")
            }

            RowLayout {
                anchors.fill: parent
                spacing: 6

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: "edit"
                    size: 12
                    iconColor: Theme.brandPrimary
                }

                Text {
                    text: qsTr("审阅 · %1").arg(root.itemCount)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Item { Layout.fillWidth: true }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.activeAdded > 0
                    text: "+" + root.activeAdded
                    color: Theme.statusSuccess
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.activeRemoved > 0
                    text: "−" + root.activeRemoved
                    color: Theme.statusDanger
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }

                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.hasItems && !root.collapsed
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    radius: 4
                    icon: "chevron-up"
                    iconSize: 10
                    tooltip: qsTr("折叠全部目录")
                    onClicked: root.collapseTree()
                }

                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.hasItems && !root.collapsed
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    radius: 4
                    icon: "chevron-down"
                    iconSize: 10
                    tooltip: qsTr("展开全部目录")
                    onClicked: root.expandTree()
                }

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: root.collapsed ? "chevron-down" : "chevron-up"
                    size: 11
                    iconColor: Theme.textSecondary
                }
            }
        }

        ChangeTree {
            id: changeTree
            Layout.fillWidth: true
            Layout.preferredHeight: root.listMaxHeight > 0
                ? Math.min(changeTree.contentHeight, root.listMaxHeight)
                : changeTree.contentHeight
            Layout.fillHeight: root.listMaxHeight <= 0
            visible: root.listVisible
            items: root.checkpoints
            basePath: root.projectRoot
            actionsEnabled: !AgentVM.stateBusy
            actionFor: function(entry) { return root.rowAction(entry) }
            fileColorFor: function(entry) { return root.statusColor(entry) }
            onFileActivated: (item) => root.openDetail(item)
            onFileActionRequested: (item) => root.requestUndo(item)
        }

        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.statusWarning
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: undoDialog

        property var pending: null

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        modal: true
        closePolicy: Popup.CloseOnEscape

        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("撤销文件改动？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            Text {
                Layout.fillWidth: true
                text: undoDialog.pending
                    ? (undoDialog.pending.existed
                        ? qsTr("将把以下文件恢复为本次会话首次改动前的内容：\n%1\n此后对该文件的手工修改也会被覆盖。")
                            .arg(undoDialog.pending.path)
                        : qsTr("将删除模型在本会话新建的文件：\n%1")
                            .arg(undoDialog.pending.path))
                      + (root.activeCount(undoDialog.pending) > 1
                          ? "\n" + qsTr("该文件在本会话共有 %1 次改动，本次将全部撤销。")
                                .arg(root.activeCount(undoDialog.pending))
                          : "")
                    : ""
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WrapAnywhere
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                AppButton {
                    text: qsTr("取消")
                    onClicked: undoDialog.close()
                }
                AppButton {
                    text: qsTr("撤销")
                    textColor: Theme.statusWarning
                    highlighted: true
                    onClicked: {
                        const entry = undoDialog.pending
                        undoDialog.close()
                        if (entry) root.performUndo(entry)
                    }
                }
            }
        }
    }

    FileChangeDetailDialog {
        id: detailDialog
        checkpoints: root.checkpoints
        detailFor: root.detailFor
        projectRoot: root.projectRoot
        onUndoRequested: (entry) => root.requestUndo(entry)
    }
}