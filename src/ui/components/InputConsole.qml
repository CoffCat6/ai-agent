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
    color: Theme.bgSecondary
    radius: 8

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

    function sendCurrentText() {
        if (root.canSend) {
            const images = []
            const files = []
            for (let i = 0; i < root.pendingAttachments.length; ++i) {
                const a = root.pendingAttachments[i]
                if (a.kind === "image") images.push(a.dataUrl)
                else files.push({name: a.name, content: a.content})
            }
            AgentVM.startTask(textArea.text, images, files)
            textArea.text = ""
            root.pendingAttachments = []
        }
    }

    // Focus border emphasis
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

    // ── Attachment preview strip (Batch 30) ────────────
    Rectangle {
        id: previewBar
        visible: root.previewVisible
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        height: 72
        color: Theme.bgPrimary
        radius: 6

        Row {
            id: thumbs
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Repeater {
                model: root.pendingAttachments

                delegate: Rectangle {
                    width: modelData.kind === "image" ? 56
                         : (fileNameText.width + 20)
                    height: 56
                    radius: 4
                    color: Theme.surfaceHover
                    clip: true

                    Image {
                        visible: modelData.kind === "image"
                        anchors.fill: parent
                        source: modelData.kind === "image" ? modelData.dataUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }

                    Row {
                        id: fileNameText
                        visible: modelData.kind === "file"
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, 140)
                        spacing: 4

                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "export"
                            size: 13
                            iconColor: Theme.textSecondary
                        }

                        Text {
                            text: modelData.kind === "file" ? modelData.name : ""
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                            width: parent.width - 17
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 2
                        width: 16
                        height: 16
                        radius: 8
                        color: removeArea.containsMouse ? Theme.statusDanger : Theme.bgPrimary
                        opacity: 0.9

                        Text {
                            anchors.centerIn: parent
                            text: "\u00d7"
                            color: "#ffffff"
                            font.pixelSize: 11
                        }

                        MouseArea {
                            id: removeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.pendingAttachments =
                                root.pendingAttachments.filter((_, i) => i !== index)
                        }
                    }
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

            onTextChanged: AgentVM.draftText = text

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
            radius: 16
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            color: actionMA.containsMouse ? Theme.surfaceHover :
                   (root.canSend ? Theme.brandPrimary : Theme.borderSubtle)
            opacity: root.canSend ? 1.0 : 0.5

            AppIcon {
                anchors.centerIn: parent
                icon: root.isBusy ? "stop" : "send"
                size: root.isBusy ? 15 : 16
                iconColor: root.isBusy ? "#ffffff" : Theme.bgPrimary
            }

            MouseArea {
                id: actionMA
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: root.canSend ? Qt.PointingHandCursor : Qt.ArrowCursor
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
                anchors.verticalCenter: parent.verticalCenter
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
