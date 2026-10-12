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
//
// CITE-801（docs/20 §5.1 引用溯源）：非流式散文里的 [文档名] 记号渲染为
// 可点击徽标 chip，点击按名打开知识库文档详情。记号的切分规则只有一份实现
// ——src/viewmodels/CitationParser.h（注册为 NexusAI.ViewModels 的
// CitationParser 单例），单测见 tests/test_highlighter.cpp。
Item {
    id: root

    required property var modelData
    required property int index

    readonly property string body: modelData.title !== undefined ? modelData.title : ""
    readonly property bool streaming: modelData.status === "running"
    // docs/20 §3.1（FORK-701）：末条助手回复（summary 类型）且非流式时可重新生成。
    // 注意不能用 index === traceCount-1：finishTask 会在 summary 之后追加
    // metrics 节点，整表末行恒为 metrics。traceCount 是 NOTIFY 属性（traceChanged），
    // 引用它使绑定随 trace 变化重新求值（Q_INVOKABLE 本身不建立依赖）。
    readonly property bool canRegenerate:
        !streaming && AgentVM.traceCount > 0
        && index === AgentVM.lastTraceIndexOfType("summary")
        && !AgentVM.stateBusy

    // 96 = 反引号。拼出三连标记，避免在源码里写裸反引号。
    readonly property string fenceTick: {
        const bt = String.fromCharCode(96)
        return bt + bt + bt
    }
    readonly property string fenceTilde: "~~~"

    // 是否含围栏（纯字符串查找，无正则）。
    readonly property bool hasFence:
        body.indexOf(fenceTick) !== -1 || body.indexOf(fenceTilde) !== -1
    // CITE-801: 便宜的前置探测（只找方括号，无正则），命中才走拆分路径；
    // 真正的记号识别与文档存在性过滤由 CitationParser.parse +
    // isKnownCitation 负责。
    readonly property bool mayHaveCitation:
        body.indexOf("[") !== -1 && body.indexOf("]") !== -1
    // 是否走「散文 / 代码」拆分路径（引用徽标也复用这条交替结构）。
    readonly property bool splitMode: (hasFence || mayHaveCitation) && !streaming

    // ── CITE-801: [文档名] 记号解析 ──
    // 唯一实现在 src/viewmodels/CitationParser.h（注册为 NexusAI.ViewModels
    // 的 CitationParser 单例），单测见 tests/test_highlighter.cpp。QML 侧
    // 不再保留 JS 镜像，规则改动只需改一处。

    // 当前知识库文档名（knowledgeChanged 驱动重算）—— 记号只有指向真实
    // 文档才渲染成徽标，[1]/[TODO] 之类普通方括号保持原样。
    readonly property var knownCitationNames: {
        const docs = AgentVM.knowledgeDocuments
        const names = []
        for (let k = 0; k < docs.length; ++k)
            names.push(docs[k].name)
        return names
    }
    function isKnownCitation(name) {
        const names = knownCitationNames
        for (let k = 0; k < names.length; ++k)
            if (names[k] === name)
                return true
        return false
    }

    // 点击徽标 → 打开知识库文档详情并定位到命中分块（docs/25）。走
    // AgentVM 的显式入口（openKnowledgeDocument），由 main.qml 连接到
    // KnowledgeDocDialog；不再按 contentData 反射式探测窗口子对象——那种
    // 写法会在对话框改名、换父级或换成非 ApplicationWindow 时静默失效，
    // 且没有编译期保护。chunk < 0 表示简写记号（无分块定位）。
    function openCitation(name, chunk) {
        AgentVM.openKnowledgeDocument(name, chunk)
    }

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
                    // CITE-801: 散文段先按 [文档名] 记号切片（C++ 单例，唯一
                    // 实现）；含真实文档的记号才进徽标流布局，普通方括号维持
                    // 原单 TextEdit 渲染。
                    readonly property var citeParts:
                        isCode ? [] : CitationParser.parse(modelData.text)
                    readonly property bool citeMode: {
                        if (isCode)
                            return false
                        for (let c = 0; c < citeParts.length; ++c) {
                            const p = citeParts[c]
                            if (p.citation && root.isKnownCitation(p.name))
                                return true
                        }
                        return false
                    }
                    implicitHeight: isCode
                        ? codeSeg.implicitHeight
                        : (citeMode ? citeFlow.implicitHeight
                                    : Math.max(1, textSeg.contentHeight))

                    TextEdit {
                        id: textSeg
                        visible: !segItem.isCode && !segItem.citeMode
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

                    // CITE-801: 引用徽标流 —— 文本片段与可点击 chip 交替。
                    // 文本片段宽度取「自然宽 clamp 到流宽」：短片段与 chip
                    // 同行，长片段在流宽内换行（TextEdit.implicitWidth 是
                    // 自然宽、不随 width 变化，与 width 绑定不会成环）。
                    Flow {
                        id: citeFlow
                        visible: !segItem.isCode && segItem.citeMode
                        width: parent.width
                        spacing: 4

                        Repeater {
                            model: segItem.citeMode ? segItem.citeParts : []

                            delegate: Item {
                                id: piece
                                readonly property bool isChip:
                                    modelData.citation
                                    && root.isKnownCitation(modelData.name)
                                width: isChip ? chipBg.width
                                              : Math.min(prosePiece.implicitWidth,
                                                         citeFlow.width)
                                height: isChip ? chipBg.height
                                               : Math.max(1, prosePiece.contentHeight)

                                TextEdit {
                                    id: prosePiece
                                    visible: !piece.isChip
                                    width: piece.width
                                    text: modelData.text
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

                                // 徽标 chip：淡品牌底 + 描边 + 圆角，点击按名
                                // 打开知识库文档详情。宽度 = 自然宽 + 内边距
                                // （clamp 到流宽），超长文档名中间省略。
                                Rectangle {
                                    id: chipBg
                                    visible: piece.isChip
                                    radius: height / 2
                                    color: Qt.rgba(Theme.brandPrimary.r,
                                                   Theme.brandPrimary.g,
                                                   Theme.brandPrimary.b, 0.12)
                                    border.width: 1
                                    border.color: Theme.brandPrimary
                                    width: Math.min(chipLabel.implicitWidth + 16,
                                                    citeFlow.width)
                                    height: chipLabel.implicitHeight + 8

                                    Text {
                                        id: chipLabel
                                        anchors.centerIn: parent
                                        width: Math.max(0, chipBg.width - 16)
                                        // docs/25: 完整记号显示分块序号。统一用 1 基，
                                        // 与详情对话框的「分块 N / M」、剪贴板导出及
                                        // 工具输出的人读行一致；原始记号里的 N 是 0 基
                                        // chunk_index，不直接展示给用户。
                                        text: modelData.chunk >= 0
                                            ? modelData.name + " #" + (modelData.chunk + 1)
                                            : modelData.name
                                        color: Theme.brandPrimary
                                        elide: Text.ElideMiddle
                                        font.pixelSize: 10
                                        font.family: Theme.fontMono
                                    }

                                    MouseArea {
                                        id: chipMA
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.openCitation(
                                            modelData.name, modelData.chunk)
                                    }

                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.chunk >= 0
                                        ? qsTr("打开引用文档 %1，定位到分块 %2")
                                              .arg(modelData.name).arg(modelData.chunk + 1)
                                        : qsTr("打开引用文档 %1").arg(modelData.name)
                                    Accessible.onPressAction: chipMA.clicked()
                                }
                            }
                        }
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
            visible: rootHover.hovered || regenerateBtn.visible

            CopyButton { text: root.body }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.modelData.model || ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }

            // docs/20 §3.1：重新生成（末条回复、非流式、非 busy）。
            Rectangle {
                id: regenerateBtn
                visible: root.canRegenerate && rootHover.hovered
                width: regenLabel.implicitWidth + 16
                height: 22
                radius: 4
                color: regenMA.containsMouse ? Theme.surfaceHover : "transparent"
                border.width: 1
                border.color: Theme.borderSubtle
                opacity: regenMA.containsMouse ? 1.0 : 0.55

                Behavior on opacity { NumberAnimation { duration: 150 } }

                Text {
                    id: regenLabel
                    anchors.centerIn: parent
                    text: qsTr("重新生成")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }

                MouseArea {
                    id: regenMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: AgentVM.regenerate()
                }

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("重新生成")
                Accessible.description: qsTr("丢弃当前回答并重新生成")
                Accessible.onPressAction: regenMA.clicked()
            }
        }
    }

    HoverHandler { id: rootHover }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("助手回复")
    Accessible.description: root.body.substring(0, 80)
}
