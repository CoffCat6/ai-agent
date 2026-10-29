import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Transient toast banner anchored to the top edge of the overlay.
// accentColor picks the semantic border/text color (Theme.statusDanger for
// errors, Theme.statusSuccess for notices); duration is the auto-hide delay
// in milliseconds. Click anywhere on it to dismiss early. An optional
// action button (actionText) fires actionTriggered on click — used by the
// session-delete undo toast (docs/23 G7).
Rectangle {
    id: root

    property string message: ""
    property color accentColor: Theme.statusSuccess
    property int duration: 3500
    property string actionText: ""
    signal actionTriggered()

    function show(msg) {
        message = msg
        opacity = 1
        hideTimer.restart()
    }

    function showWithAction(msg, action) {
        actionText = action
        show(msg)
    }

    parent: Overlay.overlay
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: parent.top
    anchors.topMargin: 24
    z: 100

    width: toastRow.implicitWidth + 32
    height: toastRow.implicitHeight + 20
    radius: 8
    color: Theme.bgSecondary
    border.width: 1
    border.color: root.accentColor
    opacity: 0
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }

    Row {
        id: toastRow
        // Above the dismiss MouseArea so the action button receives its
        // own clicks instead of the backdrop dismiss.
        z: 1
        anchors.centerIn: parent
        spacing: 12

        Text {
            id: toastText
            width: Math.min(implicitWidth, 460)
            text: root.message
            color: root.accentColor
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        AppButton {
            visible: root.actionText.length > 0
            text: root.actionText
            onClicked: {
                root.opacity = 0
                root.actionTriggered()
            }
            Accessible.name: root.actionText
        }
    }

    Timer {
        id: hideTimer
        interval: root.duration
        onTriggered: {
            root.opacity = 0
            root.actionText = ""
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.opacity = 0
            root.actionText = ""
        }
    }

    Accessible.role: Accessible.Notification
    Accessible.name: root.message
}
