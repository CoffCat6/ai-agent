import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Per-line attribution for one file at HEAD: which commit and author last
// touched each line. Read-only — clicking a row opens that commit's details,
// which is the whole point of blaming a line. Lines that only exist in the
// worktree are marked as uncommitted rather than attributed to a real commit.
Dialog {
    id: dialog

    property string path: ""
    property var lines: []
    property bool loading: false
    property string errorText: ""
    property string requestToken: ""

    readonly property int lineCount: lines.length
    readonly property string authorSummary: {
        const names = []
        for (let i = 0; i < lines.length; ++i) {
            const name = String(lines[i].author || "")
            if (name.length > 0 && names.indexOf(name) < 0) names.push(name)
        }
        return names.length > 0 ? names.join("、") : qsTr("暂无归属")
    }

    signal commitSelected(var commit)

    function reload() {
        lines = []
        errorText = ""
        requestToken = ""
        if (path.length === 0) return
        loading = true
        requestToken = GitVM.requestBlame(path)
        if (requestToken.length === 0) loading = false
    }

    function openFor(path) {
        dialog.path = path && path.length > 0 ? path : ""
        reload()
        open()
    }

    function lineLabel(entry) {
        if (entry.uncommitted === true) return qsTr("未提交")
        return entry.shortSha
    }

    function lineDate(entry) {
        if (entry.uncommitted === true || Number(entry.timestamp || 0) <= 0)
            return ""
        return Qt.formatDateTime(new Date(Number(entry.timestamp) * 1000),
                                 "yyyy-MM-dd")
    }

    // A blame row carries exactly what the commit detail dialog needs to fetch
    // its own files and diff.
    function commitOf(entry) {
        return {
            "sha": entry.sha,
            "shortSha": entry.shortSha,
            "subject": entry.summary,
            "author": entry.author,
            "timestamp": Number(entry.timestamp || 0)
        }
    }

    Connections {
        target: GitVM

        function onBlameReady(requestId, blamePath, lines, error) {
            if (requestId !== dialog.requestToken) return
            dialog.lines = lines
            dialog.errorText = error
            dialog.loading = false
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(900, parent ? parent.width - 48 : 900)
    height: Math.min(700, parent ? parent.height - 80 : 700)
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
                icon: "blame"
                size: 14
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("逐行归属 · %1").arg(dialog.path)
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
                tooltip: qsTr("重新读取逐行归属")
                onClicked: dialog.reload()
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭逐行归属")
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: !dialog.loading && dialog.lineCount > 0
            text: qsTr("%1 行 · 作者：%2").arg(dialog.lineCount).arg(dialog.authorSummary)
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.loading
            text: qsTr("正在读取逐行归属…")
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
            objectName: "gitBlameList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            visible: !dialog.loading && dialog.lineCount > 0
            clip: true
            model: dialog.lines
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: blameRow
                objectName: "gitBlameRow"
                required property var modelData

                width: ListView.view ? ListView.view.width : 0
                height: 24
                radius: 4
                color: blameHover.hovered ? Theme.surfaceHover : "transparent"

                HoverHandler { id: blameHover }

                MouseArea {
                    objectName: "blameRowArea"
                    anchors.fill: parent
                    enabled: blameRow.modelData.uncommitted !== true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: dialog.commitSelected(dialog.commitOf(blameRow.modelData))
                    Accessible.name: qsTr("查看该行的归属提交")
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 8

                    Text {
                        text: String(blameRow.modelData.lineNo).padStart(4, " ")
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 32
                    }

                    Text {
                        text: dialog.lineLabel(blameRow.modelData)
                        color: blameRow.modelData.uncommitted === true
                            ? Theme.statusWarning : Theme.brandPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: 9
                        Layout.preferredWidth: 56
                    }

                    Text {
                        text: blameRow.modelData.author
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: 9
                        elide: Text.ElideRight
                        Layout.preferredWidth: 96
                    }

                    Text {
                        text: dialog.lineDate(blameRow.modelData)
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: 9
                        Layout.preferredWidth: 72
                    }

                    Text {
                        Layout.fillWidth: true
                        text: blameRow.modelData.text
                        color: Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
