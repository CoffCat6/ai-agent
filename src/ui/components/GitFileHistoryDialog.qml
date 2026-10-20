import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Commits that touched one path, opened from the working-diff dialog. Rows are
// read-only here; selecting one hands the commit back so the panel can show its
// details in the same dialog the history tab uses. `followRenames` mirrors
// GitService::fileHistory, so a file that was renamed still shows its past.
Dialog {
    id: dialog

    property string path: ""
    property var commits: []
    property bool loading: false
    property string errorText: ""
    property string requestToken: ""

    readonly property int commitCount: commits.length

    signal commitSelected(var commit)

    function reload() {
        commits = []
        errorText = ""
        requestToken = ""
        if (path.length === 0) return
        loading = true
        requestToken = GitVM.requestFileHistory(path, 30)
        if (requestToken.length === 0) loading = false
    }

    function openFor(path) {
        dialog.path = path && path.length > 0 ? path : ""
        reload()
        open()
    }

    Connections {
        target: GitVM

        function onFileHistoryReady(requestId, historyPath, commits, error) {
            if (requestId !== dialog.requestToken) return
            dialog.commits = commits
            dialog.errorText = error
            dialog.loading = false
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 48 : 560)
    height: Math.min(560, parent ? parent.height - 80 : 560)
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
                icon: "history"
                size: 14
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("文件历史 · %1").arg(dialog.path)
                color: Theme.textPrimary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideMiddle
                Accessible.name: text
            }

            IconButton {
                icon: "refresh"
                enabled: !dialog.loading
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("重新读取该文件历史")
                onClicked: dialog.reload()
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭文件历史")
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.loading
            text: qsTr("正在读取文件历史…")
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: !dialog.loading && dialog.errorText.length > 0
            text: dialog.errorText
            color: Theme.statusWarning
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: !dialog.loading && dialog.errorText.length === 0
                     && dialog.commitCount === 0
            text: qsTr("该文件还没有提交记录（可能尚未 git add）。")
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.Wrap
        }

        // Flickable + Repeater on purpose: a ListView inside this layout gets
        // height 0 until its delegates exist, and the delegates never appear
        // because the view has no visible range.
        Flickable {
            objectName: "gitFileHistoryList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            visible: !dialog.loading && dialog.commitCount > 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentHeight: historyColumn.implicitHeight
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: historyColumn
                width: parent ? parent.width : 0
                spacing: 2

                Repeater {
                    model: dialog.commits

                    delegate: Rectangle {
                        id: historyRow
                        objectName: "gitFileHistoryRow"
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        height: 50
                        radius: 6
                        color: rowHover.hovered ? Theme.surfaceHover : "transparent"

                        HoverHandler { id: rowHover }

                        MouseArea {
                            objectName: "gitFileHistoryRowArea"
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.commitSelected(historyRow.modelData)
                            Accessible.name: qsTr("查看该提交")
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 6
                            anchors.topMargin: 5
                            anchors.bottomMargin: 5
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: historyRow.modelData.subject
                                color: Theme.textPrimary
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.fontSizeCaption
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: historyRow.modelData.shortSha + " · "
                                      + historyRow.modelData.author + " · "
                                      + Qt.formatDateTime(
                                            new Date(historyRow.modelData.timestamp * 1000),
                                            "yyyy-MM-dd HH:mm")
                                color: Theme.textSecondary
                                font.family: Theme.fontMono
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
