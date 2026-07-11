import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Input console - multi-line text input with send button
Rectangle {
    id: root

    property int maxHeight: 150

    implicitHeight: Math.min(textArea.implicitHeight + 20, maxHeight)
    height: implicitHeight
    color: Theme.bgSecondary
    radius: 8

    // Focus glow effect - using border width change for simplicity
    // (DropShadow requires Qt5Compat module; using border emphasis instead)
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
        anchors.rightMargin: 50 // Leave space for send button

        color: Theme.textPrimary
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        wrapMode: TextArea.Wrap
        placeholderText: qsTr("输入消息... (Ctrl+Enter 发送)")
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

        Keys.onReturnPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier) {
                if (textArea.length > 0 && AgentVM.state === 0) {
                    AgentVM.startTask(textArea.text)
                    textArea.text = ""
                }
                event.accepted = true
            }
        }
    }

    // Send button
    Rectangle {
        id: sendButton
        width: 36
        height: 36
        radius: 18
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 7

        color: sendButtonMA.containsMouse ? Theme.surfaceHover :
               (sendButtonEnabled ? Theme.brandPrimary : Theme.borderSubtle)
        opacity: sendButtonEnabled ? 1.0 : 0.5

        readonly property bool sendButtonEnabled: textArea.length > 0 && AgentVM.state === 0

        // Send icon (using text arrow as fallback)
        Text {
            anchors.centerIn: parent
            text: "➤"
            color: Theme.bgPrimary
            font.pixelSize: 14
        }

        MouseArea {
            id: sendButtonMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: parent.sendButtonEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (parent.sendButtonEnabled) {
                    AgentVM.startTask(textArea.text)
                    textArea.text = ""
                }
            }
        }

        Behavior on color { ColorAnimation { duration: 150 } }
    }

    // Smooth height animation
    Behavior on height {
        NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
    }
}
