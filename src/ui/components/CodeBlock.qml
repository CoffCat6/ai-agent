import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Code block display component for showing JSON, code, tool arguments.
// Hover reveals a copy button. Batch 23 (F11): a read-only TextEdit hosts
// the CodeHighlighter so code gets theme-aware coloring; the highlighter
// falls back to plain text on unknown languages and oversized blocks.
Rectangle {
    id: root

    property string text: ""
    property color bgColor: Theme.surfaceHover
    property color textColor: Theme.textSecondary
    property bool copyable: true

    // TextEdit has no auto implicit height for wrapped content
    implicitHeight: codeText.contentHeight + 16
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

    TextEdit {
        id: codeText
        anchors.fill: parent
        anchors.margins: 8
        anchors.leftMargin: 12
        text: root.text
        readOnly: true
        cursorVisible: false
        activeFocusOnPress: false
        activeFocusOnTab: false
        selectByMouse: true
        color: textColor
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontSizeCaption
        wrapMode: TextEdit.WrapAnywhere
        renderType: TextEdit.NativeRendering
        textFormat: TextEdit.PlainText

        CodeHighlighter {
            document: codeText.textDocument
            text: root.text
        }
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

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("代码块")
    // 用 JS 的 substring()：QML 字符串没有 QString.left()，调用会抛
    // TypeError（每次渲染代码块都会打到日志）。
    Accessible.description: root.text.substring(0, 80)
}
