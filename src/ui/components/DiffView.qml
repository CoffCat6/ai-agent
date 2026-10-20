import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Read-only unified-diff renderer for G4 checkpoint details. The diff text is
// produced at write time and already size-capped server-side, so this view
// only handles presentation: add/remove/hunk line coloring plus a truncation
// footer.
Item {
    id: root

    property string diffText: ""
    property bool diffTruncated: false
    property string emptyText: qsTr("此记录没有保存内容差异。")

    readonly property var lines: diffText.length > 0 ? diffText.split("\n") : []
    readonly property bool hasDiff: diffText.length > 0
    readonly property int renderedCount: lineList.count

    function isFileHeader(line) {
        return line.startsWith("+++") || line.startsWith("---")
    }

    function lineColor(line) {
        if (isFileHeader(line)) return Theme.textSecondary
        if (line.startsWith("+")) return Theme.statusSuccess
        if (line.startsWith("-")) return Theme.statusDanger
        if (line.startsWith("@@")) return Theme.brandPrimary
        return Theme.textPrimary
    }

    function lineBackground(line) {
        if (isFileHeader(line) || line.startsWith("@@")) return "transparent"
        const alpha = Theme.isDark ? 0.18 : 0.10
        if (line.startsWith("+"))
            return Qt.rgba(Theme.statusSuccess.r, Theme.statusSuccess.g,
                           Theme.statusSuccess.b, alpha)
        if (line.startsWith("-"))
            return Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                           Theme.statusDanger.b, alpha)
        return "transparent"
    }

    Column {
        anchors.fill: parent
        spacing: 4

        Rectangle {
            width: parent.width
            height: root.hasDiff ? parent.height - (root.diffTruncated ? 22 : 0) : 0
            visible: root.hasDiff
            radius: 6
            color: Theme.codeBackground
            border.color: Theme.borderSubtle
            clip: true

            ListView {
                id: lineList
                objectName: "diffView"
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.lines
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    objectName: "diffLine"
                    required property string modelData

                    width: ListView.view ? ListView.view.width : 0
                    height: Math.max(16, lineText.implicitHeight + 2)
                    color: root.lineBackground(modelData)

                    Text {
                        id: lineText
                        x: 4
                        width: parent.width - 8
                        text: modelData.length > 0 ? modelData : " "
                        color: root.lineColor(modelData)
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                }
            }
        }

        Text {
            width: parent.width
            visible: !root.hasDiff
            text: root.emptyText
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            topPadding: 12
        }

        Text {
            width: parent.width
            visible: root.diffTruncated
            height: visible ? implicitHeight : 0
            text: qsTr("差异过大，仅显示截断后的部分；撤销仍使用完整的改动前内容。")
            color: Theme.statusWarning
            font.family: Theme.fontSans
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }
    }
}
