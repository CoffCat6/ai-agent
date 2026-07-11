import QtQuick
import QtQuick.Layouts
import NexusAI

// Code block display component for showing JSON, code, tool arguments
Rectangle {
    id: root

    property string text: ""
    property color bgColor: Theme.surfaceHover
    property color textColor: Theme.textSecondary

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
}
