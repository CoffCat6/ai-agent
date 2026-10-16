import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 代码块展示组件（docs/19 §5.2）：可选标题栏（语言标签 + 复制）+ 高亮正文。
// - 语言标签优先用调用方传入的 languageLabel（Markdown 围栏的信息串），
//   否则回落到 CodeHighlighter 自动检测出的语言名；都为空则隐藏整条标题栏。
// - radiusMd + codeBackground，与气泡/卡片的分层一致。
// - 语法高亮仍由 CodeHighlighter 承担（未知语言/超大块回落纯文本）。
Rectangle {
    id: root

    property string text: ""
    // 调用方显式指定的语言标签（如围栏信息串 "python"）。
    property string languageLabel: ""
    property color bgColor: Theme.codeBackground
    property color textColor: Theme.textPrimary
    property bool copyable: true

    readonly property string displayLanguage:
        root.languageLabel !== "" ? root.languageLabel : highlighter.languageName
    readonly property bool hasHeader: root.displayLanguage !== ""

    // TextEdit 对自动换行内容没有隐式高度，须手动补
    readonly property int bodyHeight: codeText.contentHeight + 16

    implicitHeight: (hasHeader ? headerBar.height : 0) + bodyHeight
    radius: Theme.radiusMd
    color: bgColor
    border.width: 1
    border.color: Theme.cardBorder
    clip: true

    // Passive hover detection (does not steal mouse events from text)
    HoverHandler {
        id: codeHover
    }

    // ── Header: language label + copy ─────────────────
    Rectangle {
        id: headerBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.hasHeader
        height: 26
        color: Theme.cardHeaderBackground

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.displayLanguage
            color: Theme.textSecondary
            font.family: Theme.fontMono
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        CopyButton {
            id: copyBtn
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            // docs/25 D2: 聚焦时保持可见——CopyButton 已在 Tab 链内（docs/25 D1），
            // 鼠标移开后不因 hover 消失而丢失焦点。
            visible: root.copyable && root.text !== ""
                     && (codeHover.hovered || copyBtn.activeFocus)
            text: root.text
            bgColor: "transparent"
        }

        // 标题栏底部分隔线
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.cardBorder
        }
    }

    // ── Body ──────────────────────────────────────────
    TextEdit {
        id: codeText
        anchors.top: root.hasHeader ? headerBar.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        anchors.leftMargin: 12
        text: root.text
        readOnly: true
        cursorVisible: false
        activeFocusOnPress: false
        activeFocusOnTab: false
        selectByMouse: true
        color: root.textColor
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontSizeCaption
        wrapMode: TextEdit.WrapAnywhere
        renderType: TextEdit.NativeRendering
        textFormat: TextEdit.PlainText

        CodeHighlighter {
            id: highlighter
            document: codeText.textDocument
            text: root.text
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: root.hasHeader
        ? qsTr("代码块（%1）").arg(root.displayLanguage)
        : qsTr("代码块")
    // 用 JS 的 substring()：QML 字符串没有 QString.left()，调用会抛
    // TypeError（每次渲染代码块都会打到日志）。
    Accessible.description: root.text.substring(0, 80)
}
