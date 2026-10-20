import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Wide review dialog for one file checkpoint. Review is scoped to what the
// agent wrote in this session: the write-time diff is the only view. The live
// working tree and git history live in the Git tab instead. The left column
// lists every file of the session (same tree as the panel) so review does not
// need closing/reopening per file; viewMode switches the diff between unified
// and side-by-side renderings.
Dialog {
    id: dialog

    property var checkpoint: null
    // All session entries (feeding the left tree); empty -> tree hidden.
    property var checkpoints: []
    // function(entry) -> detail map; injected by FileChangesPanel.
    property var detailFor: null
    // Active project root, used for tree display paths.
    property string projectRoot: ""

    property string diffText: ""
    property bool diffTruncated: false
    // The stored diff was missing (row written before diffs were persisted)
    // and was rebuilt from the current file content.
    property bool diffFallback: false
    property string viewMode: "unified"

    readonly property bool hasDiff: diffText.length > 0
    readonly property bool undone: checkpoint ? checkpoint.undone === true : false
    readonly property bool truncated: checkpoint ? checkpoint.truncated === true : false
    readonly property int addedLines: checkpoint ? (checkpoint.addedLines || 0) : 0
    readonly property int removedLines: checkpoint ? (checkpoint.removedLines || 0) : 0
    readonly property alias diffLineCount: detailDiffView.renderedCount

    readonly property var entries: (checkpoints && checkpoints.length > 0)
        ? checkpoints : (checkpoint ? [checkpoint] : [])
    readonly property int entryIndex: indexOfEntry(checkpoint)
    readonly property bool treeVisible: entries.length > 1
    // The tree column must not stretch: without a maximum the layout lets it
    // consume the whole row and collapses the diff pane.
    readonly property real treePaneWidth:
        Math.min(260, Math.max(140, width * 0.32))
    readonly property bool canStepBack: entryIndex > 0
    readonly property bool canStepForward: entryIndex >= 0
        && entryIndex < entries.length - 1
    readonly property bool undoOffered: checkpoint !== null
        && !undone && !truncated

    signal undoRequested(var entry)

    function indexOfEntry(entry) {
        if (!entry) return -1
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i] && entries[i].id === entry.id) return i
        }
        return -1
    }

    function detailForEntry(entry) {
        if (!entry) return {}
        if (dialog.detailFor) return dialog.detailFor(entry)
        return AgentVM.fileCheckpointDetail(entry.id)
    }

    function applyDetail(detail) {
        const d = detail || {}
        diffText = d.diff !== undefined ? d.diff : ""
        diffTruncated = d.diffTruncated === true
        diffFallback = d.diffFallback === true
    }

    function openFor(entry, detail) {
        checkpoint = entry || null
        if (detail === undefined || detail === null)
            applyDetail(detailForEntry(entry))
        else
            applyDetail(detail)
        open()
    }

    // Switching files inside the dialog.
    function selectEntry(entry) {
        if (!entry) return
        if (checkpoint && entry.id === checkpoint.id) return
        checkpoint = entry
        applyDetail(detailForEntry(entry))
    }

    function stepEntry(delta) {
        const next = entryIndex + delta
        if (next < 0 || next >= entries.length) return
        selectEntry(entries[next])
    }

    function statusText() {
        if (!checkpoint) return ""
        if (undone) return qsTr("已撤销")
        if (truncated) return qsTr("仅记录路径，内容未保存")
        return checkpoint.existed ? qsTr("已修改") : qsTr("新建文件")
    }

    function statusColor() {
        if (!checkpoint) return Theme.textSecondary
        if (undone) return Theme.textSecondary
        if (truncated) return Theme.statusWarning
        return Theme.brandPrimary
    }

    function emptyText() {
        if (truncated) return qsTr("原文件超过单文件上限，仅记录了路径，无法展示内容差异；请用 git 查看或恢复。")
        if (!checkpoint || checkpoint.existed === false)
            return qsTr("该新建文件没有可读取的内容：它可能已被删除或改名。")
        return qsTr("此记录没有保存内容差异。")
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1120, parent ? parent.width - 48 : 1120)
    height: Math.min(760, parent ? parent.height - 80 : 760)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            spacing: 8

            AppIcon {
                icon: "file"
                size: 14
                iconColor: dialog.statusColor()
            }

            Text {
                Layout.fillWidth: true
                text: dialog.checkpoint ? dialog.checkpoint.path : ""
                color: Theme.textPrimary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideMiddle
                Accessible.name: text
            }

            Text {
                visible: dialog.addedLines > 0
                text: "+" + dialog.addedLines
                color: Theme.statusSuccess
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
            }

            Text {
                visible: dialog.removedLines > 0
                text: "−" + dialog.removedLines
                color: Theme.statusDanger
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
            }

            IconButton {
                objectName: "prevFileButton"
                visible: dialog.treeVisible
                icon: "chevron-left"
                enabled: dialog.canStepBack
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("上一个文件")
                onClicked: dialog.stepEntry(-1)
            }

            IconButton {
                objectName: "nextFileButton"
                visible: dialog.treeVisible
                icon: "chevron-right"
                enabled: dialog.canStepForward
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("下一个文件")
                onClicked: dialog.stepEntry(1)
            }

            AppButton {
                objectName: "unifiedModeButton"
                visible: dialog.hasDiff
                text: qsTr("统一")
                implicitHeight: 24
                highlighted: dialog.viewMode === "unified"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "unified"
            }

            AppButton {
                objectName: "splitModeButton"
                visible: dialog.hasDiff
                text: qsTr("并排")
                implicitHeight: 24
                highlighted: dialog.viewMode === "split"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "split"
            }

            AppButton {
                objectName: "undoButton"
                visible: dialog.undoOffered
                text: qsTr("撤销")
                implicitHeight: 24
                enabled: !AgentVM.stateBusy
                opacity: enabled ? 1.0 : 0.4
                onClicked: dialog.undoRequested(dialog.checkpoint)
                Accessible.name: qsTr("撤销该文件在本会话的全部改动")
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭文件改动详情")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            Text {
                text: dialog.statusText()
                color: dialog.statusColor()
                font.family: Theme.fontSans
                font.pixelSize: 10
            }

            Text {
                Layout.fillWidth: true
                text: dialog.checkpoint
                    ? Qt.formatDateTime(new Date(dialog.checkpoint.createdAt),
                                        "yyyy-MM-dd HH:mm:ss")
                    : ""
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: 10
            }

            Text {
                visible: dialog.checkpoint
                         && (dialog.checkpoint.changeCount || 1) > 1
                text: qsTr("本次会话共 %1 次改动").arg(dialog.checkpoint
                                                     ? dialog.checkpoint.changeCount : 1)
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: 10
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 10

            // ── Session file tree ──────────────────────
            ColumnLayout {
                objectName: "reviewFileTree"
                visible: dialog.treeVisible
                Layout.preferredWidth: dialog.treePaneWidth
                Layout.maximumWidth: dialog.treePaneWidth
                Layout.fillHeight: true
                spacing: 4

                Text {
                    text: qsTr("本会话文件 · %1").arg(dialog.entries.length)
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                    font.family: Theme.fontMono
                }

                ChangeTree {
                    id: entriesTree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    items: dialog.entries
                    basePath: dialog.projectRoot
                    rowHeight: 24
                    fileColorFor: function(entry) {
                        return entry.undone === true ? Theme.textSecondary
                                                     : Theme.brandPrimary
                    }
                    onFileActivated: (item) => dialog.selectEntry(item)
                }
            }

            Rectangle {
                visible: dialog.treeVisible
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.borderSubtle
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    visible: dialog.diffFallback
                    text: qsTr("该记录未保存差异，以下按当前文件内容重建（文件未再改动时即为当时写入的内容）。")
                    color: Theme.statusWarning
                    font.family: Theme.fontSans
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }

                DiffView {
                    id: detailDiffView
                    objectName: "detailDiffView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    diffText: dialog.diffText
                    diffTruncated: dialog.diffTruncated
                    viewMode: dialog.viewMode
                    emptyText: dialog.emptyText()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("关闭")
                onClicked: dialog.close()
            }
        }
    }
}
