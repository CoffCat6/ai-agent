import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Multi-branch commit graph. The picture is git's own `--graph` output rendered
// monospaced, so it cannot drift from what the repository really looks like;
// rows that carry a commit id are clickable and open that commit's details.
// Read-only and local: `--all` walks the refs already in the repository, no
// fetch and no remote contact.
Dialog {
    id: dialog

    property var lines: []
    property bool loading: false
    property bool allBranches: true
    property string errorText: ""
    property string requestToken: ""

    readonly property int lineCount: lines.length
    readonly property int commitCount: {
        let n = 0
        for (let i = 0; i < lines.length; ++i)
            if (lines[i].hasCommit === true) ++n
        return n
    }

    signal commitSelected(var commit)

    function reload() {
        lines = []
        errorText = ""
        requestToken = ""
        loading = true
        requestToken = GitVM.requestGraph(150, allBranches)
        if (requestToken.length === 0) loading = false
    }

    function openFor() {
        open()
        if (lineCount === 0)
            reload()
    }

    function commitOf(entry) {
        return {
            "sha": entry.sha,
            "shortSha": entry.sha,
            "subject": entry.subject,
            "author": "",
            "timestamp": 0
        }
    }

    Connections {
        target: GitVM

        function onGraphReady(requestId, lines, error) {
            if (requestId !== dialog.requestToken) return
            dialog.lines = lines
            dialog.errorText = error
            dialog.loading = false
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(820, parent ? parent.width - 48 : 820)
    height: Math.min(680, parent ? parent.height - 80 : 680)
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
                icon: "branch"
                size: 14
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("提交图")
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
            }

            AppButton {
                objectName: "graphScopeButton"
                text: dialog.allBranches
                    ? qsTr("全部分支") : qsTr("仅当前分支")
                implicitHeight: 24
                enabled: !dialog.loading
                opacity: enabled ? 1.0 : 0.4
                Accessible.name: qsTr("切换图的分支范围")
                onClicked: {
                    dialog.allBranches = !dialog.allBranches
                    dialog.reload()
                }
            }

            IconButton {
                icon: "refresh"
                enabled: !dialog.loading
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("重新读取提交图")
                onClicked: dialog.reload()
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭提交图")
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: qsTr("图形来自 git 自身的 --graph，只读本地引用，不会访问远端。")
            color: Theme.textTertiary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeMicro
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: !dialog.loading && dialog.commitCount > 0
            text: qsTr("%1 个提交 · 范围：%2")
                      .arg(dialog.commitCount)
                      .arg(dialog.allBranches ? qsTr("全部分支")
                                              : qsTr("仅当前分支"))
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeMicro
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.loading
            text: qsTr("正在读取提交图…")
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

        ListView {
            objectName: "gitGraphList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            visible: !dialog.loading && dialog.lineCount > 0
            clip: true
            model: dialog.lines
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            delegate: Rectangle {
                id: graphRow
                objectName: "gitGraphRow"
                required property var modelData

                width: ListView.view ? ListView.view.width : 0
                height: 22
                radius: 4
                color: graphHover.hovered && modelData.hasCommit
                    ? Theme.surfaceHover : "transparent"

                HoverHandler { id: graphHover }

                MouseArea {
                    objectName: "gitGraphRowArea"
                    anchors.fill: parent
                    enabled: graphRow.modelData.hasCommit === true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: dialog.commitSelected(dialog.commitOf(graphRow.modelData))
                    Accessible.name: qsTr("查看该提交")
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 6

                    Text {
                        // The graph punctuation is kept verbatim: lining it up is
                        // the whole point of the view.
                        text: graphRow.modelData.art
                        color: Theme.brandPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMicro
                        Layout.preferredWidth: 68
                    }

                    Text {
                        text: graphRow.modelData.sha
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeNano
                        visible: graphRow.modelData.hasCommit === true
                        Layout.preferredWidth: 56
                    }

                    Text {
                        text: graphRow.modelData.date
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeNano
                        visible: graphRow.modelData.hasCommit === true
                        Layout.preferredWidth: 70
                    }

                    Text {
                        Layout.fillWidth: true
                        text: graphRow.modelData.hasCommit
                              ? (graphRow.modelData.subject
                                 + (graphRow.modelData.refs.length > 0
                                    ? "  (" + graphRow.modelData.refs + ")" : ""))
                              : ""
                        color: Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeNano
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
