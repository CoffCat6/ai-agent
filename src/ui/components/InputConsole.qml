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

    objectName: "inputConsole"
    property int maxHeight: 240
    // Batch 30 (F6): [{kind:"image", dataUrl}, {kind:"file", name, content}]
    property var pendingAttachments: []
    property bool restoringDraft: false
    onPendingAttachmentsChanged: {
        if (!restoringDraft) AgentVM.draftAttachments = pendingAttachments
    }

    // ── G2: '@' project file mentions ────────────────────
    property bool mentionOpen: false
    property int mentionStart: -1
    property int mentionIndex: 0
    property string mentionQuery: ""
    property var mentionResults: []
    // Guards the two-step text replacement in applyMention(): remove() emits
    // textChanged synchronously, and the unguarded updateMention() would reset
    // mentionStart to -1 before insert() runs, losing the completion.
    property bool applyingMention: false
    readonly property bool mentionNoProject: mentionOpen && AgentVM.activeProjectRoot === ""
    readonly property bool mentionScanning: mentionOpen && AgentVM.projectFilesScanning
    readonly property int mentionPopupHeight: {
        if (!mentionOpen) return 0
        if (mentionNoProject) return 56
        if (mentionResults.length === 0) return 42
        return Math.min(mentionResults.length * 30 + 8, 240)
    }

    function closeMention() {
        if (mentionPopup.opened) mentionPopup.close()
        mentionOpen = false
        mentionStart = -1
        mentionIndex = 0
        mentionQuery = ""
        mentionResults = []
    }

    // Shared token scanner lives in C++ (ProjectFileIndex::mentionAt) so the
    // popup and the submitted-reference resolver cannot drift.
    function updateMention() {
        if (applyingMention) return
        if (restoringDraft || isBusy) {
            closeMention()
            return
        }
        const m = AgentVM.activeFileMention(textArea.text, textArea.cursorPosition)
        if (!m.active) {
            closeMention()
            return
        }
        if (m.query !== mentionQuery) {
            mentionIndex = 0
            mentionQuery = m.query
        }
        mentionStart = m.start
        mentionResults = AgentVM.activeProjectRoot === ""
            ? [] : AgentVM.listProjectFiles(m.query, 50)
        if (mentionIndex >= mentionResults.length)
            mentionIndex = Math.max(0, mentionResults.length - 1)
        mentionOpen = true
        if (!mentionPopup.opened) mentionPopup.open()
    }

    function applyMention(path) {
        if (mentionStart < 0) return
        const start = mentionStart
        const end = textArea.cursorPosition
        const token = (path.indexOf(" ") >= 0 || path.indexOf("\t") >= 0)
            ? '@"' + path + '"' : '@' + path
        const inserted = token + " "
        applyingMention = true
        textArea.remove(start, end)
        textArea.insert(start, inserted)
        textArea.cursorPosition = start + inserted.length
        applyingMention = false
        closeMention()
        textArea.forceActiveFocus()
    }

    function moveMention(delta) {
        const count = mentionResults.length
        if (count === 0) return
        mentionIndex = (mentionIndex + delta + count) % count
        mentionList.positionViewAtIndex(mentionIndex, ListView.Contain)
    }

    // ── G2: live reference status ────────────────────────
    // Path-only validation (no file contents), so this stays cheap enough to
    // run on every keystroke. Chips show exactly which '@' tokens will be
    // attached when the message is sent, and why the others will not.
    property var referenceChecks: []
    readonly property bool referenceVisible: referenceChecks.length > 0

    function updateReferences() {
        referenceChecks = AgentVM.inspectFileReferences(textArea.text)
    }

    function restoreDraft() {
        restoringDraft = true
        closeMention()
        textArea.text = AgentVM.draftText
        pendingAttachments = AgentVM.draftAttachments
        historyBrowsing = false
        restoringDraft = false
        updateReferences()
    }
    Component.onCompleted: restoreDraft()
    Connections {
        target: AgentVM
        function onActiveTabChanged() { root.restoreDraft() }
        function onDraftTextChanged() {
            if (textArea.text !== AgentVM.draftText) root.restoreDraft()
        }
        function onDraftAttachmentsChanged() { root.restoreDraft() }
        function onProjectFilesChanged() {
            if (root.mentionOpen) root.updateMention()
        }
        function onActiveProjectIdChanged() {
            root.closeMention()
            root.updateReferences()
        }
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
                    + (referenceVisible ? referenceBar.height + 6 : 0)
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
            const seen = {}
            for (let i = 0; i < root.pendingAttachments.length; ++i) {
                const a = root.pendingAttachments[i]
                if (a.kind === "image") {
                    images.push(a.dataUrl)
                } else {
                    files.push({kind: "file", name: a.name, content: a.content})
                    seen[a.name] = true
                }
            }
            // G2: '@path' tokens resolve to project files at submit time. The
            // C++ side caps per-file/total bytes and rejects escapes,
            // sensitive files and binaries; duplicate attachments win.
            const refs = AgentVM.resolveFileReferences(textArea.text)
            for (let i = 0; i < refs.length; ++i) {
                const ref = refs[i]
                const base = ref.name.split("/").pop()
                if (seen[ref.name] || seen[base]) continue
                seen[ref.name] = true
                seen[base] = true
                files.push({kind: "file", name: ref.name, content: ref.content})
            }
            if (!AgentVM.startTask(textArea.text, images, files)) return
            root.closeMention()
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

    // ── G2: referenced project files ────────────────────
    // One chip per '@' token in the draft. Green = resolves and will be
    // attached on send; red = will be skipped, with the reason on hover.
    ReferenceChipRow {
        id: referenceBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: inputRow.top
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.bottomMargin: 4
        height: 24
        checks: root.referenceChecks
        visible: root.referenceVisible
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
            placeholderText: qsTr("输入消息…（Enter 发送，Shift+Enter 换行，@ 引用项目文件）")
            placeholderTextColor: Theme.textSecondary
            selectByMouse: true
            renderType: Text.NativeRendering
            clip: true

            Accessible.name: qsTr("消息输入框")
            Accessible.description: qsTr("输入消息后按 Enter 发送，Shift+Enter 换行；输入 @ 可引用项目文件")

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
                root.updateMention()
                root.updateReferences()
            }

            onCursorPositionChanged: root.updateMention()

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
                if (root.mentionOpen) {
                    root.moveMention(-1)
                    event.accepted = true
                    return
                }
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
                if (root.mentionOpen) {
                    root.moveMention(1)
                    event.accepted = true
                    return
                }
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
            // G2: while the @ completion popup is open, Esc closes it and
            // Tab accepts the highlighted path.
            Keys.onPressed: function(event) {
                if (root.mentionOpen) {
                    if (event.key === Qt.Key_Escape) {
                        root.closeMention()
                        event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Tab && root.mentionResults.length > 0) {
                        root.applyMention(root.mentionResults[root.mentionIndex])
                        event.accepted = true
                        return
                    }
                }
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
                    return
                }
                if (root.mentionOpen && root.mentionResults.length > 0) {
                    root.applyMention(root.mentionResults[root.mentionIndex])
                } else {
                    if (root.mentionOpen) root.closeMention()
                    sendCurrentText()
                }
                event.accepted = true
            }

            Keys.onEnterPressed: function(event) {
                if (event.modifiers & Qt.ShiftModifier) {
                    event.accepted = false // default: insert newline
                    return
                }
                if (root.mentionOpen && root.mentionResults.length > 0) {
                    root.applyMention(root.mentionResults[root.mentionIndex])
                } else {
                    if (root.mentionOpen) root.closeMention()
                    sendCurrentText()
                }
                event.accepted = true
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

    // ── G2: '@' project file completion ──────────────────
    // Non-focus popup: the text area keeps the keyboard, so typing narrows
    // the query without the popup stealing focus. Navigation is handled by
    // the text area's key handlers above.
    Popup {
        id: mentionPopup
        objectName: "mentionPopup"
        parent: root
        x: 36
        y: -height - 6
        width: Math.min(520, Math.max(260, root.width - 88))
        height: root.mentionPopupHeight
        padding: 0
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            radius: 10
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderSubtle
        }

        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 12
                anchors.verticalCenter: parent.verticalCenter
                visible: root.mentionResults.length === 0
                text: root.mentionNoProject
                    ? qsTr("当前会话未绑定项目，无法引用项目文件。\n请先在侧栏选择或导入项目。")
                    : root.mentionScanning
                        ? qsTr("正在扫描项目文件…")
                        : qsTr("没有匹配的项目文件")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }

            ListView {
                id: mentionList
                objectName: "mentionList"
                anchors.fill: parent
                anchors.margins: 4
                visible: root.mentionResults.length > 0
                clip: true
                model: root.mentionResults
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: mentionRow
                    objectName: "mentionRow"
                    required property string modelData
                    required property int index

                    width: ListView.view ? ListView.view.width : 0
                    height: 30
                    radius: 6
                    color: index === root.mentionIndex
                        ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                  Theme.brandPrimary.b, 0.14)
                        : mentionRowHover.hovered ? Theme.surfaceHover : "transparent"

                    HoverHandler { id: mentionRowHover }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.applyMention(mentionRow.modelData)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        AppIcon {
                            Layout.alignment: Qt.AlignVCenter
                            icon: "file"
                            size: 13
                            iconColor: mentionRow.index === root.mentionIndex
                                ? Theme.brandPrimary : Theme.textSecondary
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: mentionRow.modelData
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }

    // No height animation: the console resizes on every wrapped line while
    // typing, and animating that makes the text trail the cursor.
}
