import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 用户消息气泡（docs/19 §4.3）：右对齐、品牌分层底色、无衬线正文，
// 下方右对齐时间戳与悬停操作条。正文一律 fontSans（修正 D3）。
Item {
    id: root

    required property var modelData
    required property int index

    readonly property string body: modelData.title !== undefined ? modelData.title : ""
    readonly property var images: modelData.images !== undefined ? modelData.images : []
    readonly property var files: modelData.files !== undefined ? modelData.files : []
    readonly property bool hasAttachments: images.length > 0 || files.length > 0
    // Edit the last user turn while idle, retaining its existing attachments.
    property bool editing: false
    readonly property bool canEdit:
        !AgentVM.stateBusy
        && AgentVM.traceCount > 0
        && index === AgentVM.lastTraceIndexOfType("user_message")
        && nodeType === "user_message"
    readonly property string nodeType: modelData.type || ""
    // 内联编辑草稿：进入编辑态时初始化为当前正文。
    property string editDraft: body
    // 气泡最大宽 = 本 delegate 宽 × 比例；正文可用宽再扣掉水平内边距。
    readonly property real maxBubbleWidth: Math.max(120, width * Theme.bubbleMaxRatio)
    readonly property real maxContentWidth: Math.max(80, maxBubbleWidth - Theme.spaceMd * 2)
    // 编辑态摊到上限宽：收缩宽给不了输入框落点，且边打字边变宽会一路抖动。
    readonly property real contentWidth: editing
        ? maxContentWidth
        : Math.min(Math.max(bodyText.implicitWidth, attachRow.implicitWidth), maxContentWidth)

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

            TextEdit {
                id: bodyText
                text: root.editing ? editDraft : root.body
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: TextEdit.Wrap
                renderType: Text.NativeRendering
                textFormat: TextEdit.PlainText
                width: parent.width
                // docs/20 §3.2：编辑态可写，阅读态只读不选中（与气泡语义
                // 一致——用户消息无需选中复制，操作条已有复制按钮）。
                readOnly: !root.editing
                visible: !root.editing
            }

            // docs/20 §3.2：内联编辑框（替换气泡正文）。
            TextArea {
                id: editArea
                visible: root.editing
                width: parent.width
                text: root.editing ? editDraft : ""
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: TextArea.Wrap
                selectByMouse: true
                background: null
                onTextChanged: if (root.editing) editDraft = text
                Keys.onEscapePressed: {
                    root.editing = false
                    editDraft = root.body
                }
            }

            // 编辑态操作行：发送 / 取消。
            Row {
                visible: root.editing
                spacing: Theme.spaceXs
                layoutDirection: Qt.RightToLeft
                anchors.right: parent.right

                AppButton {
                    text: qsTr("发送")
                    enabled: editDraft.trim().length > 0
                        && editDraft.trim() !== root.body
                    onClicked: {
                        const next = editDraft.trim()
                        // 成功则该条被替换；失败留在编辑态保住草稿，原因由
                        // AgentVM 走 errorOccurred 呈现（原来失败是静默无响应）。
                        // 不回写 editDraft：body 此刻还是旧文本，模型重绑要到下一个
                        // tick，进入编辑态时自己会重置草稿。
                        if (AgentVM.editAndResend(next)) root.editing = false
                    }
                }
                AppButton {
                    text: qsTr("取消")
                    onClicked: {
                        root.editing = false
                        editDraft = root.body
                    }
                }
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
                ? Theme.formatTime(new Date(root.modelData.timestamp))
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
            visible: rootHover.hovered && !root.editing

            CopyButton { text: root.body }

            // docs/20 §3.2：编辑重发（末条 user 消息、非 busy、无附件）。
            Rectangle {
                id: editBtn
                visible: root.canEdit && rootHover.hovered
                width: editLabel.implicitWidth + 16
                height: 22
                radius: 4
                color: editMA.containsMouse ? Theme.surfaceHover : "transparent"
                border.width: 1
                border.color: Theme.borderSubtle
                opacity: editMA.containsMouse ? 1.0 : 0.55

                Behavior on opacity { NumberAnimation { duration: 150 } }

                Text {
                    id: editLabel
                    anchors.centerIn: parent
                    text: qsTr("编辑")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }

                MouseArea {
                    id: editMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.editDraft = root.body
                        root.editing = true
                        editArea.forceActiveFocus()
                    }
                }

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("编辑并重发")
                Accessible.description: qsTr("修改这条消息并重新发送")
                Accessible.onPressAction: editMA.clicked()
            }
        }
    }

    HoverHandler { id: rootHover }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("你的消息")
    Accessible.description: root.body.substring(0, 80)
}
