import QtQuick
import NexusAI
import NexusAI.ViewModels

// 助手回复（docs/19 §4.3/§5.1）：左侧头像列 + Markdown 正文 + 流式光标 + 复制。
//
// V5（docs/19 §5.2）：回复含围栏代码块时，把正文拆成「散文 / 代码」交替片段——
// 散文走 TextEdit.MarkdownText，代码片段交给 CodeBlock（语言标签 + 语法高亮）。
//
// 实现约束（上次该文件曾让 qmlcachegen 内存失控，事后定位到「正则字面量里含
// 反引号」——反引号是 JS 模板字符串定界符，QML 词法器会误判）：
//   本文件不得出现任何正则字面量，也不得出现裸反引号字符；
//   围栏标记一律用 String.fromCharCode(96) 动态拼出。
// 另外仅在「非流式」时才拆分：流式期间维持单 TextEdit 的增量更新，
// 既避免每 token 重建片段，也躲开未闭合围栏的半成品状态。
Item {
    id: root

    required property var modelData

    readonly property string body: modelData.title !== undefined ? modelData.title : ""
    readonly property bool streaming: modelData.status === "running"

    // 96 = 反引号。拼出三连标记，避免在源码里写裸反引号。
    readonly property string fenceTick: {
        const bt = String.fromCharCode(96)
        return bt + bt + bt
    }
    readonly property string fenceTilde: "~~~"

    // 是否含围栏（纯字符串查找，无正则）。
    readonly property bool hasFence:
        body.indexOf(fenceTick) !== -1 || body.indexOf(fenceTilde) !== -1
    // 是否走「散文 / 代码」拆分路径。
    readonly property bool splitMode: hasFence && !streaming

    // 逐行扫描，切成 {kind:"text"|"code", text/lang/code} 片段。
    readonly property var segments: {
        if (!splitMode)
            return []
        const lines = body.split("\n")
        const out = []
        let buf = []
        let inFence = false
        let lang = ""
        const pushText = function() {
            const t = buf.join("\n")
            if (t !== "") out.push({ kind: "text", text: t })
            buf = []
        }
        for (let i = 0; i < lines.length; ++i) {
            const raw = lines[i]
            // 行首允许空白：手工跳过，避免用正则。
            let s = 0
            while (s < raw.length && raw.charCodeAt(s) <= 32) ++s
            const head = raw.substring(s)
            const isTick = head.indexOf(fenceTick) === 0
            const isTilde = head.indexOf(fenceTilde) === 0
            if (isTick || isTilde) {
                const markerLen = isTick ? fenceTick.length : fenceTilde.length
                if (!inFence) {
                    pushText()
                    inFence = true
                    // 围栏信息串：取到第一个空白为止（手工扫描，无正则）。
                    let rest = head.substring(markerLen)
                    let e = 0
                    while (e < rest.length && rest.charCodeAt(e) > 32) ++e
                    lang = rest.substring(0, e)
                } else {
                    out.push({ kind: "code", lang: lang, code: buf.join("\n") })
                    buf = []
                    inFence = false
                    lang = ""
                }
                continue
            }
            buf.push(raw)
        }
        if (inFence) {
            // 未闭合围栏（异常/截断）：已收内容仍按代码段落渲染，避免丢字。
            out.push({ kind: "code", lang: lang, code: buf.join("\n") })
        } else {
            pushText()
        }
        return out
    }

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

    // ── Body ──────────────────────────────────────────
    Item {
        id: mdItem
        anchors.left: avatar.right
        anchors.leftMargin: Theme.spaceSm
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: splitMode ? segmentCol.implicitHeight : mdText.contentHeight

        // 单 TextEdit 路径：无围栏，或仍在流式。
        TextEdit {
            id: mdText
            anchors.left: parent.left
            anchors.right: parent.right
            visible: !root.splitMode
            text: root.body
            textFormat: TextEdit.MarkdownText
            readOnly: true
            // 悬停时才允许鼠标选中：避免整块 TextEdit 抢占滚轮/拖拽，
            // 同时保留回复的选中复制能力。
            selectByMouse: rootHover.hovered
            persistentSelection: true
            activeFocusOnTab: false
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            wrapMode: TextEdit.Wrap
            onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            // 流式光标要跟在文本末尾；只读 TextEdit 的 cursorPosition 默认在 0。
            onTextChanged: if (root.streaming) cursorPosition = length

            // 流式竖条光标（docs/19 §4.3）：不再用字符 “▌”。
            Rectangle {
                id: caret
                visible: root.streaming && !root.splitMode
                width: 2
                height: mdText.font.pixelSize + 4
                color: Theme.brandPrimary
                x: mdText.cursorRectangle.x + mdText.cursorRectangle.width + 1
                y: mdText.cursorRectangle.y

                SequentialAnimation on opacity {
                    running: root.streaming && !root.splitMode
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.0; duration: 500 }
                    NumberAnimation { from: 0.0; to: 1.0; duration: 500 }
                }
            }
        }

        // 片段路径：散文 / 代码交替。
        Column {
            id: segmentCol
            width: parent.width
            visible: root.splitMode
            spacing: Theme.spaceSm

            Repeater {
                model: root.splitMode ? root.segments : []

                delegate: Item {
                    id: segItem
                    width: segmentCol.width
                    readonly property bool isCode: modelData.kind === "code"
                    implicitHeight: isCode
                        ? codeSeg.implicitHeight
                        : Math.max(1, textSeg.contentHeight)

                    TextEdit {
                        id: textSeg
                        visible: !segItem.isCode
                        width: parent.width
                        text: segItem.isCode ? "" : modelData.text
                        textFormat: TextEdit.MarkdownText
                        readOnly: true
                        selectByMouse: rootHover.hovered
                        persistentSelection: true
                        activeFocusOnTab: false
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        wrapMode: TextEdit.Wrap
                        onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                    }

                    CodeBlock {
                        id: codeSeg
                        visible: segItem.isCode
                        width: parent.width
                        languageLabel: segItem.isCode ? (modelData.lang || "") : ""
                        text: segItem.isCode ? modelData.code : ""
                    }
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
