import QtQuick
import NexusAI
import NexusAI.ViewModels

// Small copy-to-clipboard button with "copied" feedback
Rectangle {
    id: root

    property string text: ""
    property color bgColor: "transparent"

    width: label.implicitWidth + 16
    height: 22
    radius: 4
    color: copyMA.containsMouse ? Theme.surfaceHover : root.bgColor
    border.width: 1
    border.color: root.copied ? Theme.statusSuccess : Theme.borderSubtle
    opacity: copyMA.containsMouse || root.copied ? 1.0 : 0.55

    property bool copied: false

    Behavior on opacity { NumberAnimation { duration: 150 } }
    Behavior on border.color { ColorAnimation { duration: 150 } }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.copied ? qsTr("已复制") : qsTr("复制")
        color: root.copied ? Theme.statusSuccess : Theme.textSecondary
        font.pixelSize: 10
        font.family: Theme.fontMono
    }

    MouseArea {
        id: copyMA
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (root.text.length > 0) {
                AgentVM.copyToClipboard(root.text)
                root.copied = true
                resetTimer.restart()
            }
        }
    }

    Timer {
        id: resetTimer
        interval: 1500
        onTriggered: root.copied = false
    }
}
