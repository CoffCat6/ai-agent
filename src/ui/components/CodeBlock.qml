import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Code block display component for showing JSON, code, tool arguments.
// Hover reveals a copy button.
Rectangle {
    id: root

    property string text: ""
    property color bgColor: Theme.surfaceHover
    property color textColor: Theme.textSecondary
    property bool copyable: true

    implicitHeight: codeText.implicitHeight + 16
    radius: 4
    color: bgColor

    // Left accent bar
    Rectangle {
        width: 3
        height: parent.height
        color: Theme.brandPrimary
        opacity: 0.5
    }

    // Passive hover detection (does not steal mouse events from text)
    HoverHandler {
        id: codeHover
    }

    Text {
        id: codeText
        anchors.fill: parent
        anchors.margins: 8
        anchors.leftMargin: 12
        text: root.text
        color: textColor
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontSizeCaption
        wrapMode: Text.WrapAnywhere
        renderType: Text.NativeRendering
        textFormat: Text.PlainText
    }

    CopyButton {
        visible: root.copyable && codeHover.hovered && root.text !== ""
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 4
        text: root.text
        bgColor: root.bgColor
        z: 1
    }
}
