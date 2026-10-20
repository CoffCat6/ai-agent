import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// G4: read-only list of files the active session's write_file / edit_file
// calls touched. Data flows AgentVM.fileCheckpoints -> here; undo is routed
// back through AgentVM.undoFileCheckpoint. Path-only rows (oversized or
// unreadable originals) are shown as audit history and cannot be undone.
Item {
    id: root

    property var checkpoints: []
    property bool collapsed: false
    property string errorText: ""
    // Test/embedding hook: overrides AgentVM.fileCheckpointDetail(id).
    property var detailProvider: null
    // UI tests set this to false to inject git data without spawning git.
    property bool gitAutoLoad: true

    readonly property int itemCount: checkpoints ? checkpoints.length : 0
    readonly property bool hasItems: itemCount > 0
    readonly property bool listVisible: hasItems && !collapsed
    readonly property int renderedCount: changeList.count
    readonly property alias detailVisible: detailDialog.visible
    readonly property alias detailDialogItem: detailDialog

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

    visible: hasItems
    implicitHeight: hasItems ? content.implicitHeight : 0

    function statusText(entry) {
        if (root.activeCount(entry) === 0) return qsTr("已全部撤销")
        if (entry.truncated) return qsTr("仅记录路径，请用 git 恢复")
        const base = entry.existed
            ? qsTr("可回退本次会话的全部改动")
            : qsTr("将删除本会话新建的文件")
        if (root.activeCount(entry) > 1)
            return qsTr("%1 · 共 %2 次改动").arg(base).arg(root.activeCount(entry))
        return base
    }

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

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 6

        // Header doubles as the collapse toggle.
        Item {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 22

            HoverHandler { id: headerHover }

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
                    text: qsTr("文件改动 · %1").arg(root.itemCount)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Item { Layout.fillWidth: true }

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: root.collapsed ? "chevron-down" : "chevron-up"
                    size: 11
                    iconColor: Theme.textSecondary
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.collapsed = !root.collapsed
                Accessible.name: qsTr("展开或折叠文件改动列表")
            }
        }

        ListView {
            id: changeList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 188)
            visible: root.listVisible
            clip: true
            model: root.checkpoints
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: changeRow
                objectName: "fileChangeRow"
                required property var modelData
                required property int index

                readonly property bool undone: modelData.undone
                readonly property bool truncated: modelData.truncated

                width: ListView.view ? ListView.view.width : 0
                height: 46
                radius: 6
                color: rowHover.hovered ? Theme.surfaceHover : "transparent"

                HoverHandler { id: rowHover }

                // Below the content row: clicks on text fall through here,
                // while the undo button consumes its own press.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openDetail(changeRow.modelData)
                    Accessible.name: qsTr("查看文件改动详情")
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 4
                    spacing: 8

                    AppIcon {
                        Layout.alignment: Qt.AlignVCenter
                        icon: "file"
                        size: 13
                        iconColor: root.statusColor(changeRow.modelData)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            text: changeRow.modelData.path
                            color: root.activeCount(changeRow.modelData) === 0
                                ? Theme.textSecondary : Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                            Accessible.name: changeRow.modelData.path
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.statusText(changeRow.modelData)
                                  + " · "
                                  + Qt.formatDateTime(
                                        new Date(changeRow.modelData.createdAt),
                                        "MM-dd HH:mm")
                            color: root.statusColor(changeRow.modelData)
                            font.pixelSize: 10
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        objectName: "fileChangeCountBadge"
                        Layout.alignment: Qt.AlignVCenter
                        visible: root.activeCount(changeRow.modelData) > 1
                        text: "×" + root.activeCount(changeRow.modelData)
                        color: Theme.textSecondary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        visible: changeRow.modelData.hasDiff === true
                                 && changeRow.modelData.addedLines > 0
                        text: "+" + changeRow.modelData.addedLines
                        color: Theme.statusSuccess
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        visible: changeRow.modelData.hasDiff === true
                                 && changeRow.modelData.removedLines > 0
                        text: "−" + changeRow.modelData.removedLines
                        color: Theme.statusDanger
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                    }

                    AppButton {
                        Layout.alignment: Qt.AlignVCenter
                        text: root.activeCount(changeRow.modelData) === 0
                            ? qsTr("已撤销") : qsTr("撤销")
                        enabled: root.activeCount(changeRow.modelData) > 0
                                 && !changeRow.modelData.truncated
                                 && !AgentVM.stateBusy
                        opacity: enabled ? 1.0 : 0.4
                        onClicked: root.requestUndo(changeRow.modelData)
                        Accessible.name: qsTr("撤销该文件在本会话的全部改动")
                    }
                }
            }
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
        autoLoadGit: root.gitAutoLoad
    }
}
