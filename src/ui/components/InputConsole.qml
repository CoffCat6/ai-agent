import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// Input console - multi-line text input with send / stop buttons.
// Enter sends, Shift+Enter inserts a newline, Ctrl+Enter also sends.
// Batch 30 (F6): the plus button opens an upward menu with two entries —
// image upload (vision) and file upload (text attachments folded into the
// request); attachments preview above the text area and travel with it.
Rectangle {
    id: root

    property int maxHeight: 240
    // Batch 30 (F6): [{kind:"image", dataUrl}, {kind:"file", name, content}]
    property var pendingAttachments: []
    property bool restoringDraft: false
    onPendingAttachmentsChanged: {
        if (!restoringDraft) AgentVM.draftAttachments = pendingAttachments
    }

    function restoreDraft() {
        restoringDraft = true
        textArea.text = AgentVM.draftText
        pendingAttachments = AgentVM.draftAttachments
        historyBrowsing = false
        restoringDraft = false
    }
    Component.onCompleted: restoreDraft()
    Connections {
        target: AgentVM
        function onActiveTabChanged() { root.restoreDraft() }
        function onDraftTextChanged() {
            if (textArea.text !== AgentVM.draftText) root.restoreDraft()
        }
        function onDraftAttachmentsChanged() { root.restoreDraft() }
    }
    // 由 AgentChatPage 注入：发送后请求轨迹视图跳底并恢复跟随。
    property var traceJump: null
    // docs/20 §3.3：输入历史浏览态。applyingHistory 用于区分「程序设值」与
    // 「用户编辑」，只在用户编辑时退出浏览并复位游标。
    property bool historyBrowsing: false
    property bool applyingHistory: false

    // Sending is allowed while Idle, after completion, or after an error,
    // with text or at least one attached file.
    readonly property bool canSend: (textArea.length > 0 || pendingAttachments.length > 0) &&
        (AgentVM.state === AgentVM.stateIdle
         || AgentVM.state === AgentVM.stateCompleted
         || AgentVM.state === AgentVM.stateError)

    // Agent busy: Thinking / AwaitingToolConfirm / ExecutingTool / Summarizing
    readonly property bool isBusy: AgentVM.stateBusy

    // Height is driven by the text content (capped) plus the preview strip.
    implicitHeight: (previewVisible ? previewBar.height + 6 : 0)
                    + inputRow.height + 16
    height: implicitHeight
    color: Theme.cardBackground
    radius: Theme.radiusMd

    readonly property bool previewVisible: pendingAttachments.length > 0

    function addImageDataUrl(dataUrl) {
        if (dataUrl !== "")
            root.pendingAttachments = root.pendingAttachments.concat(
                [{kind: "image", dataUrl: dataUrl}])
    }

    function addTextFile(path) {
        const file = AgentVM.loadTextFileAttachment(path)
        if (!file || Object.keys(file).length === 0) return
        root.pendingAttachments = root.pendingAttachments.concat([file])
    }

    // docs/20 §3.4（A4 拖拽附件）：把 C++ 侧解析好的附件描述符追加到预览。
    // 描述符为 {kind:"image", dataUrl} 或 {kind:"file", name, content}，
    // 与 addImageDataUrl / addTextFile 同形。路径解析（含 url 解码）在
    // AgentViewModel::handleDroppedFiles 完成，QML 只负责入列。
    function appendAttachments(attachments) {
        for (let i = 0; i < attachments.length; ++i)
            root.pendingAttachments = root.pendingAttachments.concat([attachments[i]])
    }

    function sendCurrentText() {
        if (root.canSend) {
            const images = []
            const files = []
            for (let i = 0; i < root.pendingAttachments.length; ++i) {
                const a = root.pendingAttachments[i]
                if (a.kind === "image") images.push(a.dataUrl)
                else files.push({kind: "file", name: a.name, content: a.content})
            }
            if (!AgentVM.startTask(textArea.text, images, files)) return
            textArea.text = ""
            root.pendingAttachments = []
            // docs/20 §3.3：发送后退出历史浏览态（游标已在 startTask 内复位）。
            root.historyBrowsing = false
            // 发送即「要看新回复」的明确意图：强制解除上一轮的已解钉状
            // 态并重置吸底意图。否则长会话里用户曾上滑回看后，视图停在
            // 顶部，pinnedToBottom=false 会让整个生成期间的跟随滚动
            // （onTraceChanged → requestScrollToBottom）全程失效，新回复
            // 在视口外默默生长。
            if (traceJump !== null && traceJump.requestFollow !== undefined)
                traceJump.requestFollow()
        }
    }

    // Focus border emphasis
    border.width: textArea.activeFocus ? 2 : 1
    border.color: textArea.activeFocus ? Theme.brandPrimary : Theme.cardBorder

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

    // docs/20 §3.4（A4 拖拽附件）：拖放由 main.qml 的窗口根级 DropArea 处理
    // （组件内部再放一个 DropArea 不会收到事件：QML 的拖放分派只把事件交给
    // 首个接受它的 item，根级已铺满整窗）。解析在 C++ 完成，经 AgentVM 的
    // attachmentsDropped 信号调用本组件的 appendAttachments。
    // 注意：QML 的 url 对象没有 toLocalFile()，路径解析必须交给 C++。

    // ── Attachment preview strip (Batch 30 / docs/19 §6) ──
    Rectangle {
        id: previewBar
        visible: root.previewVisible
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        height: 72
        color: Theme.bgPrimary
        radius: Theme.radiusSm

        Row {
            id: thumbs
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Repeater {
                model: root.pendingAttachments

                // 与气泡内附件共用 AttachmentChip：仅尺寸更小并打开删除按钮。
                delegate: AttachmentChip {
                    kind: modelData.kind === "image" ? "image" : "file"
                    source: modelData.kind === "image" ? modelData.dataUrl : ""
                    name: modelData.kind === "file" ? modelData.name : ""
                    imageWidth: 56
                    imageHeight: 56
                    removable: true
                    onRemoveRequested: root.pendingAttachments =
                        root.pendingAttachments.filter((_, i) => i !== index)
                }
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(220, parent.width - thumbs.width - 24)
            visible: width > 60
            text: qsTr("附件将随消息发送给当前模型服务")
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Input row: attach icon | text area | send/stop ──
    Item {
        id: inputRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        height: Math.min(textArea.implicitHeight, root.maxHeight)

        // Text input area: height follows the content so the placeholder
        // stays vertically centered; grows with wrapping up to the cap
        // (the text area scrolls internally past it).
        TextArea {
            id: textArea
            objectName: "messageInput"
            anchors.left: parent.left
            anchors.leftMargin: 36
            anchors.right: parent.right
            anchors.rightMargin: 44
            anchors.verticalCenter: parent.verticalCenter
            height: Math.min(implicitHeight, root.maxHeight)

            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            wrapMode: TextArea.Wrap
            placeholderText: qsTr("输入消息... (Enter 发送, Shift+Enter 换行)")
            placeholderTextColor: Theme.textSecondary
            selectByMouse: true
            renderType: Text.NativeRendering
            clip: true

            Accessible.name: qsTr("消息输入框")
            Accessible.description: qsTr("输入消息后按 Enter 发送，Shift+Enter 换行")

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

            onTextChanged: {
                if (!root.restoringDraft) AgentVM.draftText = text
                // 用户手动编辑（非历史程序设值）时退出浏览并复位游标。
                if (!root.restoringDraft && !root.applyingHistory && root.historyBrowsing) {
                    root.historyBrowsing = false
                    AgentVM.resetInputHistory()
                }
            }

            // docs/20 §3.3：输入历史浏览。↑ 前移（更旧），↓ 后移（更新）。
            // Shift+↑/↓ 保持默认（多行光标移动，不进入历史）。仅在无显式编辑
            // 且光标处于首/末行时接管，避免干扰多行编辑。用户一旦手动编辑，
            // 退出浏览并复位游标（applyHistoryText 中设 applyingHistory 抑制）。
            function applyHistoryText(next) {
                root.applyingHistory = true
                textArea.text = next
                textArea.cursorPosition = textArea.length
                root.applyingHistory = false
                AgentVM.draftText = next
            }

            Keys.onUpPressed: function(event) {
                if (event.modifiers & Qt.ShiftModifier) {
                    event.accepted = false
                    return
                }
                // 仅在空输入框或已处于历史浏览态时接管；否则保留用户已输入的
                // 草稿（不覆盖）并让光标正常上移。多行时只在首行才进入历史。
                if (!root.historyBrowsing && textArea.text !== "") {
                    event.accepted = false
                    return
                }
                const before = textArea.text.substring(0, textArea.cursorPosition)
                if (before.indexOf("\n") !== -1) {
                    event.accepted = false
                    return
                }
                const prev = AgentVM.historyPrev()
                if (prev !== "") {
                    root.historyBrowsing = true
                    applyHistoryText(prev)
                }
                event.accepted = true
            }

            Keys.onDownPressed: function(event) {
                if (event.modifiers & Qt.ShiftModifier || !root.historyBrowsing) {
                    event.accepted = false
                    return
                }
                // 多行时仅在末行才继续下滑历史。
                const after = textArea.text.substring(textArea.cursorPosition)
                if (after.indexOf("\n") !== -1) {
                    event.accepted = false
                    return
                }
                const next = AgentVM.historyNext()
                if (next === "") {
                    root.historyBrowsing = false
                    applyHistoryText("")
                } else {
                    applyHistoryText(next)
                }
                event.accepted = true
            }

            // Batch 30 (F6): Ctrl+V first tries the clipboard image; plain
            // text falls through to the default paste.
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_V && (event.modifiers & Qt.ControlModifier)) {
                    const dataUrl = AgentVM.clipboardImageDataUrl()
                    if (dataUrl !== "") {
                        root.addImageDataUrl(dataUrl)
                        event.accepted = true
                    }
                }
            }

            function sendCurrentText() {
                if (root.canSend) {
                    root.sendCurrentText()
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

        // Attach button: plus icon on the left, opens the upward menu.
        Item {
            id: attachButton
            width: 28
            height: 28
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            opacity: root.isBusy ? 0.35 : 1.0

            AppIcon {
                anchors.centerIn: parent
                icon: "add"
                size: 18
                iconColor: attachMA.containsMouse ? Theme.brandPrimary : Theme.textSecondary
            }

            MouseArea {
                id: attachMA
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: attachMenu.popup(attachButton, 0, -attachMenu.implicitHeight - 8)
            }

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("添加附件")
            Accessible.description: qsTr("上传图片或文件附件")
            Accessible.onPressAction: attachMenu.popup(attachButton, 0, -attachMenu.implicitHeight - 8)
        }

        // Send (idle) / stop (busy) button on the right.
        Rectangle {
            id: actionButton
            width: 32
            height: 32
            radius: width / 2
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            // 停止态（busy）用危险色；可发送用品牌主色；否则置灰。
            readonly property bool stopMode: root.isBusy
            readonly property color baseColor: stopMode ? Theme.statusDanger
                : (root.canSend ? Theme.brandPrimary : Theme.borderSubtle)

            color: actionMA.containsMouse ? Qt.lighter(baseColor, 1.15) : baseColor
            opacity: (root.canSend || stopMode) ? 1.0 : 0.5

            AppIcon {
                anchors.centerIn: parent
                // 必须以 id 限定：AppIcon 是独立组件，QML 的非限定名解析只
                // 走组件作用域（id/组件根/上下文），不会沿父 item 的属性链
                // 找到 actionButton.stopMode——未限定写法会抛 ReferenceError，
                // 三个绑定全部失效（停止态图标/尺寸/颜色不生效）。
                icon: actionButton.stopMode ? "stop" : "send"
                size: actionButton.stopMode ? 15 : 16
                iconColor: actionButton.stopMode ? "#ffffff"
                    : (root.canSend ? Theme.bgPrimary : Theme.textSecondary)
            }

            MouseArea {
                id: actionMA
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: (root.canSend || root.isBusy)
                    ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (root.isBusy) {
                        AgentVM.stop()
                    } else {
                        textArea.sendCurrentText()
                    }
                }
            }

            Behavior on color { ColorAnimation { duration: 150 } }

            Accessible.role: Accessible.Button
            Accessible.name: root.isBusy ? qsTr("停止") : qsTr("发送")
            Accessible.onPressAction: {
                if (root.isBusy) AgentVM.stop()
                else textArea.sendCurrentText()
            }
        }
    }

    // Upward attachment menu (Batch 30 follow-up) — themed for dark/light
    // through the shared DialogBackground chrome and Theme tokens.
    component AttachMenuItem: MenuItem {
        id: menuItem
        property string glyph: "image"

        height: 40
        leftPadding: 6
        rightPadding: 6

        contentItem: RowLayout {
            spacing: 10

            Rectangle {
                width: 28
                height: 28
                radius: 6
                color: menuItem.highlighted
                    ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                              Theme.brandPrimary.b, 0.15)
                    : Theme.surfaceHover

                AppIcon {
                    anchors.centerIn: parent
                    icon: menuItem.glyph
                    size: 14
                    iconColor: menuItem.highlighted
                        ? Theme.brandPrimary : Theme.textSecondary
                }
            }

            Text {
                text: menuItem.text
                color: menuItem.highlighted ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                // RowLayout 管理子项的几何：在受管 item 上写 anchors 是未定义
                // 行为（每次启动告警 "Detected anchors on an item that is
                // managed by a layout"），用 Layout.alignment 垂直居中。
                Layout.alignment: Qt.AlignVCenter
            }
        }

        background: Rectangle {
            radius: 6
            color: menuItem.highlighted ? Theme.surfaceHover : "transparent"
        }
    }

    Menu {
        id: attachMenu
        width: 190
        padding: 6

        background: DialogBackground {
            radius: 10
        }

        topMargin: 0
        bottomMargin: 0

        AttachMenuItem {
            glyph: "image"
            text: qsTr("上传图片")
            onTriggered: imageDialog.open()
        }
        AttachMenuItem {
            glyph: "export"
            text: qsTr("上传文件")
            onTriggered: fileDialog.open()
        }
    }

    FileDialog {
        id: imageDialog
        nameFilters: [qsTr("图片文件 (*.png *.jpg *.jpeg *.webp *.bmp *.gif)")]
        onAccepted: root.addImageDataUrl(AgentVM.loadImageDataUrl(selectedFile))
    }

    FileDialog {
        id: fileDialog
        nameFilters: [
            qsTr("文本文件 (*.txt *.md *.json *.csv *.log *.xml *.yml *.yaml *.ini *.cpp *.h *.py *.js *.ts)"),
            qsTr("全部文件 (*)")
        ]
        onAccepted: root.addTextFile(selectedFile)
    }

    // No height animation: the console resizes on every wrapped line while
    // typing, and animating that makes the text trail the cursor.
}
