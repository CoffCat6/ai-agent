import QtQuick
import NexusAI
import NexusAI.ViewModels

// 助手回复（docs/19 §4.3）：左侧头像列 + Markdown 正文 + 流式光标 + 复制。
// 正文占满内容区剩余宽度（不气泡式收窄，长文更易读）。
Item {
    id: root

    required property var modelData

    readonly property string body: modelData.title !== undefined ? modelData.title : ""
    readonly property bool streaming: modelData.status === "running"

    implicitHeight: Math.max(avatar.height, mdItem.implicitHeight + footer.height)
    height: implicitHeight

    // ── Avatar ────────────────────────────────────────
    Rectangle {
        id: avatar
        anchors.left: parent.left
        anchors.top: parent.top
        width: Theme.avatarSize
        height: Theme.avatarSize
        radius: Theme.radiusSm
        color: Theme.brandPrimary

        Text {
            anchors.centerIn: parent
            text: "N"
            color: Theme.bgPrimary
            font.pixelSize: Math.round(Theme.fontSizeBody * 0.85)
            font.bold: true
            font.family: Theme.fontMono
        }
    }

    // ── Markdown body (markdown + streaming caret) ────
    Item {
        id: mdItem
        anchors.left: avatar.right
        anchors.leftMargin: Theme.spaceSm
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: mdText.contentHeight

        TextEdit {
            id: mdText
            anchors.left: parent.left
            anchors.right: parent.right
            text: root.body
            textFormat: TextEdit.MarkdownText
            readOnly: true
            // 悬停时才允许鼠标选中：避免整块 TextEdit 抢占滚轮/拖拽，
            // 同时保留回复的选中复制能力（原实现是常开 selectByMouse）。
            selectByMouse: rootHover.hovered
            persistentSelection: true
            activeFocusOnTab: false
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            wrapMode: TextEdit.Wrap
            onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            // 流式光标要跟在文本末尾；只读 TextEdit 的 cursorPosition 默认在 0。
            // 用命令式赋值（非绑定）：绑定会干扰用户拖选时的光标；
            // 仅在流式且文本增长时把光标推到末尾。
            onTextChanged: if (root.streaming) cursorPosition = length

            // 流式竖条光标（docs/19 §4.3）：不再用字符 “▌”，避免底部额外行。
            Rectangle {
                id: caret
                visible: root.streaming
                width: 2
                height: mdText.font.pixelSize + 4
                color: Theme.brandPrimary
                x: mdText.cursorRectangle.x + mdText.cursorRectangle.width + 1
                y: mdText.cursorRectangle.y

                SequentialAnimation on opacity {
                    running: root.streaming
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.0; duration: 500 }
                    NumberAnimation { from: 0.0; to: 1.0; duration: 500 }
                }
            }
        }
    }

    // ── Hover actions ────────────────────────────────
    Item {
        id: footer
        anchors.left: mdItem.left
        anchors.right: parent.right
        anchors.top: mdItem.bottom
        anchors.topMargin: Theme.spaceXs
        height: 18

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spaceXs
            visible: rootHover.hovered

            CopyButton { text: root.body }
        }
    }

    HoverHandler { id: rootHover }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("助手回复")
    Accessible.description: root.body.substring(0, 80)
}
