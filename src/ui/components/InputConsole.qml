import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Input console - multi-line text input with send / stop buttons.
// Enter sends, Shift+Enter inserts a newline, Ctrl+Enter also sends.
Rectangle {
    id: root

    property int maxHeight: 180

    // Sending is allowed while Idle, after completion, or after an error
    readonly property bool canSend: textArea.length > 0 &&
        (AgentVM.state === 0 || AgentVM.state === 5 || AgentVM.state === 6)

    // Agent busy: Thinking / AwaitingToolConfirm / ExecutingTool / Summarizing
    readonly property bool isBusy: AgentVM.state >= 1 && AgentVM.state <= 4

    implicitHeight: Math.min(textArea.implicitHeight + 20, maxHeight)
    height: implicitHeight
    color: Theme.bgSecondary
    radius: 8

    // Focus border emphasis
    border.width: textArea.activeFocus ? 2 : 1
    border.color: textArea.activeFocus ? Theme.brandPrimary : Theme.borderSubtle

    // Subtle glow rectangle (dark mode only)
    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        radius: parent.radius + 2
        color: "transparent"
        border.width: 1
        border.color: Theme.brandPrimary
        opacity: textArea.activeFocus && Theme.isDark ? 0.3 : 0.0
        z: -1

        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // Focus click area
    MouseArea {
        anchors.fill: parent
        onClicked: textArea.forceActiveFocus()
    }

    // Text input area
    TextArea {
        id: textArea
        anchors.fill: parent
        anchors.margins: 10
        anchors.rightMargin: 50 // Leave space for send/stop button

        color: Theme.textPrimary
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        wrapMode: TextArea.Wrap
        placeholderText: qsTr("输入消息... (Enter 发送, Shift+Enter 换行)")
        placeholderTextColor: Theme.textSecondary
        selectByMouse: true
        renderType: Text.NativeRendering

        // Custom cursor
        cursorDelegate: Rectangle {
            visible: textArea.activeFocus
            color: Theme.brandPrimary
            width: 2
            SequentialAnimation on opacity {
                running: textArea.activeFocus
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.0; duration: 500 }
                NumberAnimation { from: 0.0; to: 1.0; duration: 500 }
            }
        }

        onTextChanged: AgentVM.draftText = text

        function sendCurrentText() {
            if (root.canSend) {
                AgentVM.startTask(textArea.text)
                textArea.text = ""
            }
        }

        Keys.onReturnPressed: function(event) {
            if (event.modifiers & Qt.ShiftModifier) {
                event.accepted = false // default: insert newline
            } else {
                sendCurrentText()
                event.accepted = true
            }
        }

        Keys.onEnterPressed: function(event) {
            if (event.modifiers & Qt.ShiftModifier) {
                event.accepted = false // default: insert newline
            } else {
                sendCurrentText()
                event.accepted = true
            }
        }
    }

    // Send button (shown while idle)
    Rectangle {
        id: sendButton
        visible: !root.isBusy
        width: 36
        height: 36
        radius: 18
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 7

        color: sendButtonMA.containsMouse ? Theme.surfaceHover :
               (sendButtonEnabled ? Theme.brandPrimary : Theme.borderSubtle)
        opacity: sendButtonEnabled ? 1.0 : 0.5

        readonly property bool sendButtonEnabled: root.canSend

        AppIcon {
            anchors.centerIn: parent
            icon: "send"
            size: 16
            iconColor: Theme.bgPrimary
        }

        MouseArea {
            id: sendButtonMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: parent.sendButtonEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: parent.sendButtonEnabled ? textArea.sendCurrentText() : undefined
        }

        Behavior on color { ColorAnimation { duration: 150 } }
    }

    // Stop button (shown while the agent is working)
    Rectangle {
        id: stopButton
        visible: root.isBusy
        width: 36
        height: 36
        radius: 18
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 7

        color: stopButtonMA.containsMouse
            ? Qt.lighter(Theme.statusDanger, 1.25)
            : Theme.statusDanger
        opacity: 0.92

        AppIcon {
            anchors.centerIn: parent
            icon: "stop"
            size: 15
            iconColor: "#ffffff"
        }

        MouseArea {
            id: stopButtonMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: AgentVM.stop()
        }

        Behavior on color { ColorAnimation { duration: 150 } }
    }

    // Smooth height animation
    Behavior on height {
        NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
    }
}
