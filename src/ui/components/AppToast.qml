import QtQuick
import NexusAI

// Transient toast banner anchored to the top edge of the overlay.
// accentColor picks the semantic border/text color (Theme.statusDanger for
// errors, Theme.statusSuccess for notices); duration is the auto-hide delay
// in milliseconds. Click anywhere on it to dismiss early.
Rectangle {
    id: root

    property string message: ""
    property color accentColor: Theme.statusSuccess
    property int duration: 3500

    function show(msg) {
        message = msg
        opacity = 1
        hideTimer.restart()
    }

    parent: Overlay.overlay
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: parent.top
    anchors.topMargin: 24
    z: 100

    width: Math.min(toastText.implicitWidth, 460) + 32
    height: toastText.implicitHeight + 20
    radius: 8
    color: Theme.bgSecondary
    border.width: 1
    border.color: root.accentColor
    opacity: 0
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 200 } }

    Text {
        id: toastText
        anchors.centerIn: parent
        width: Math.min(implicitWidth, 460)
        text: root.message
        color: root.accentColor
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
        wrapMode: Text.Wrap
    }

    Timer {
        id: hideTimer
        interval: root.duration
        onTriggered: root.opacity = 0
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.opacity = 0
    }
}
