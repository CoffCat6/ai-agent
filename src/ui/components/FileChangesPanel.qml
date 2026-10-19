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

    readonly property int itemCount: checkpoints ? checkpoints.length : 0
    readonly property bool hasItems: itemCount > 0
    readonly property bool listVisible: hasItems && !collapsed
    readonly property int renderedCount: changeList.count

    visible: hasItems
    implicitHeight: hasItems ? content.implicitHeight : 0

    function statusText(entry) {
        if (entry.undone) return qsTr("已撤销")
        if (entry.truncated) return qsTr("仅记录路径，请用 git 恢复")
        return entry.existed
            ? qsTr("可恢复改动前内容")
            : qsTr("将删除本次新建的文件")
    }

    function statusColor(entry) {
        if (entry.undone) return Theme.textSecondary
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
                height: 42
                radius: 6
                color: rowHover.hovered ? Theme.surfaceHover : "transparent"

                HoverHandler { id: rowHover }

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
                            color: changeRow.modelData.undone
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

                    AppButton {
                        Layout.alignment: Qt.AlignVCenter
                        text: changeRow.modelData.undone
                            ? qsTr("已撤销") : qsTr("撤销")
                        enabled: !changeRow.modelData.undone
                                 && !changeRow.modelData.truncated
                                 && !AgentVM.stateBusy
                        opacity: enabled ? 1.0 : 0.4
                        onClicked: root.requestUndo(changeRow.modelData)
                        Accessible.name: qsTr("撤销该文件的本次改动")
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
                        ? qsTr("将把以下文件恢复为改动前内容：\n%1\n此后对该文件的手工修改也会被覆盖。")
                            .arg(undoDialog.pending.path)
                        : qsTr("将删除模型本次新建的文件：\n%1")
                            .arg(undoDialog.pending.path))
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
}
