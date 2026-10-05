import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 用户消息气泡（docs/19 §4.3）：右对齐、品牌分层底色、无衬线正文，
// 下方右对齐时间戳与悬停操作条。正文一律 fontSans（修正 D3）。
Item {
    id: root

    required property var modelData

    readonly property string body: modelData.title !== undefined ? modelData.title : ""
    readonly property var images: modelData.images !== undefined ? modelData.images : []
    readonly property var files: modelData.files !== undefined ? modelData.files : []
    readonly property bool hasAttachments: images.length > 0 || files.length > 0
    // 气泡最大宽 = 本 delegate 宽 × 比例；正文可用宽再扣掉水平内边距。
    readonly property real maxBubbleWidth: Math.max(120, width * Theme.bubbleMaxRatio)
    readonly property real maxContentWidth: Math.max(80, maxBubbleWidth - Theme.spaceMd * 2)
    // 内容自然宽（标题 + 附件取大者），再夹到上限。
    readonly property real contentWidth: Math.min(
        Math.max(bodyText.implicitWidth, attachRow.implicitWidth), maxContentWidth)

    implicitHeight: bubble.height + footer.height + Theme.spaceXs
    height: implicitHeight
    // width 由调用方（TraceNodeRouter）显式赋为内容区宽；气泡自身靠右定位，
    // 剩余留白落在左侧。

    // ── Bubble ────────────────────────────────────────
    Rectangle {
        id: bubble
        anchors.right: parent.right
        color: Theme.bubbleUser
        // 右下角小圆角，形成朝向头像侧的"对话尾"（Qt 6.7+ 支持分角圆角）。
        topLeftRadius: Theme.radiusBubble
        topRightRadius: Theme.radiusBubble
        bottomLeftRadius: Theme.radiusBubble
        bottomRightRadius: Theme.radiusBubbleTail
        width: root.contentWidth + Theme.spaceMd * 2
        height: contentCol.implicitHeight + Theme.spaceSm * 2
        border.width: 1
        border.color: Theme.isDark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.04)

        Column {
            id: contentCol
            anchors.centerIn: parent
            width: root.contentWidth
            spacing: Theme.spaceSm

            Text {
                id: bodyText
                text: root.body
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                renderType: Text.NativeRendering
                textFormat: Text.PlainText
                width: parent.width
            }

            Row {
                id: attachRow
                visible: root.hasAttachments
                spacing: Theme.spaceXs

                Repeater {
                    model: root.images
                    delegate: AttachmentChip { kind: "image"; source: modelData }
                }
                Repeater {
                    model: root.files
                    delegate: AttachmentChip {
                        kind: "file"
                        name: modelData.name !== undefined ? modelData.name : ""
                    }
                }
            }
        }
    }

    // ── Timestamp + hover actions ─────────────────────
    Item {
        id: footer
        anchors.top: bubble.bottom
        anchors.topMargin: Theme.spaceXs
        anchors.right: parent.right
        height: 18
        // 与气泡同宽，时间戳/操作贴在气泡下沿。
        width: bubble.width

        Text {
            id: timeText
            anchors.right: actions.left
            anchors.rightMargin: Theme.spaceSm
            anchors.verticalCenter: parent.verticalCenter
            visible: root.modelData.timestamp !== undefined
            text: root.modelData.timestamp !== undefined
                ? Qt.formatDateTime(new Date(root.modelData.timestamp), "HH:mm")
                : ""
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spaceXs
            visible: rootHover.hovered

            CopyButton { text: root.body }
        }
    }

    HoverHandler { id: rootHover }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("你的消息")
    Accessible.description: root.body.substring(0, 80)
}
