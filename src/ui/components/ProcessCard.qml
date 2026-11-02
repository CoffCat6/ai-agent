import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 过程信息卡片（docs/19 §5.3）：思考 / 工具调用 / 工具结果 / context
// 四类共用。默认折叠，卡片头显示类型、状态徽章与折叠箭头；展开区显示
// 参数 / 结果 / 思考正文。waiting_confirm 时强制前置展开确认按钮。
Item {
    id: root

    required property var modelData
    property bool expanded: false
    // 折叠状态的唯一数据源在页面（TraceNodeRouter→AgentChatPage），这里只发信号。
    signal toggleRequested()

    readonly property string nodeType: modelData.type || ""
    readonly property string status: modelData.status || ""
    // 外部服务来源（web_search 的后端 key，如 "zhipu"）。空 = 本地工具或
    // 后端未自报，此时不显示徽章。
    readonly property string source: modelData.source || ""
    // docs/33 E1: 子代理证据工件路径（spawn_agent 的 tool_result）。非空时
    // 头部渲染「查看证据」芯片，点击交给系统默认 .md 处理器。
    readonly property string evidencePath: modelData.evidencePath || ""
    // docs/33 过程可视化：spawn_agent 节点的实时过程日志（元数据 subagentLog，
    // 条目 {phase, text}）。assistant = 模型可见文本（含最终答案），tool =
    // 单个工具调用的「名(参数) ⇒ 结果摘要」。
    readonly property bool isSpawnAgent: (modelData.title || "").startsWith("spawn_agent")
    readonly property var subagentLog: {
        if (!isSpawnAgent) return null
        const raw = modelData.subagentLog
        // 不能用 Array.isArray 判型：QVariantList 进 QML 后是 sequence
        // wrapper（length/索引访问正常），Array.isArray 对它返回 false，
        // 用它判空会让日志永远不渲染。
        return (raw !== undefined && raw !== null && raw.length > 0) ? raw : null
    }
    readonly property bool hasSubagentLog: subagentLog !== null
    // docs/34 P3.2: spawn_agent 结果节点上的改动提案索引
    // （[{path, rationale, count}]，正文仍只在报告/证据工件里）。卡片头
    // 显示「N 个提案」徽章，展开区列出路径、理由与条数。
    readonly property bool isSpawnResult: nodeType === "tool_result"
                                          && (modelData.title || "").startsWith("spawn_agent")
    readonly property var subagentProposals: {
        if (!isSpawnResult) return null
        const raw = modelData.subagentProposals
        // 同 subagentLog：QVariantList 进 QML 是 sequence wrapper，
        // Array.isArray 判型会永远为 false。
        return (raw !== undefined && raw !== null && raw.length > 0) ? raw : null
    }
    readonly property bool hasSubagentProposals: subagentProposals !== null
    readonly property int proposalTotal: {
        if (!hasSubagentProposals) return 0
        let n = 0
        for (let i = 0; i < subagentProposals.length; ++i)
            n += subagentProposals[i].count || 0
        return n
    }
    // 日志条目图标与配色：assistant 用品牌色、tool 用警示色、context
    // （docs/35 P4.1 上下文压缩事件）用次要色，与正常输出区分。
    function logGlyph(phase) {
        if (phase === "tool") return "⚙"
        if (phase === "context") return "↺"
        if (phase === "reasoning") return "◌"
        if (phase === "stream") return "…"
        return "✦"
    }
    function logColor(phase) {
        if (phase === "tool") return Theme.statusWarning
        if (phase === "context" || phase === "reasoning")
            return Theme.textSecondary
        return Theme.brandPrimary
    }
    // 来源的展示名。技术名词不翻译（与设置页的 label 一致）。
    function sourceLabel(key) {
        if (key === "zhipu") return "智谱"
        if (key === "deepseek") return "DeepSeek"
        if (key === "duckduckgo") return "DuckDuckGo"
        return key
    }
    readonly property string detailText: {
        if (nodeType === "thinking")
            return modelData.reasoning || ""
        if (nodeType === "tool_call") {
            const raw = modelData.args || ""
            if (raw === "") return modelData.result || ""
            const t = modelData.title || ""
            if (t === "shell_exec" || t === "git_commit") {
                try {
                    const obj = JSON.parse(raw)
                    if (t === "shell_exec" && obj.command)
                        return obj.command
                    if (t === "git_commit" && obj.message)
                        return obj.message
                } catch (e) {}
            }
            return raw
        }
        return (modelData.args || "") !== "" ? modelData.args : (modelData.result || "")
    }
    readonly property bool hasDetail: detailText !== ""
    readonly property bool waitingConfirm: status === "waiting_confirm"
    readonly property string previewText: waitingConfirm ? (modelData.preview || "") : ""
    readonly property bool hasPreview: previewText !== ""
    // 确认态必须展开，否则同意/拒绝按钮不可达。
    readonly property bool bodyVisible: (expanded || waitingConfirm)
                                        && (hasDetail || hasSubagentLog
                                            || hasSubagentProposals)

    // git_status 结构化展示：解析 porcelain v1 输出为文件状态列表
    readonly property bool isGitStatus: nodeType === "tool_result" && (modelData.title || "") === "git_status"
    readonly property var gitStatusEntries: isGitStatus ? parseGitStatus(modelData.result || "") : null

    function parseGitStatus(text) {
        const lines = text.split("\n")
        const entries = []
        for (let i = 0; i < lines.length; i++) {
            const line = lines[i]
            if (line.startsWith("## ")) {
                entries.push({status: "⎇", text: line.substring(3), color: Theme.textSecondary})
            } else if (line.length >= 3) {
                const xy = line.substring(0, 2)
                const path = line.substring(3)
                let badge = "?", color = Theme.textSecondary
                // docs/25 C3: git 徽标单字进翻译流程，英文界面不再裸露中文
                if (xy === "??") { badge = qsTr("新"); color = Theme.statusSuccess }
                else if (xy[0] === "A" || xy[1] === "A") { badge = qsTr("增"); color = Theme.statusSuccess }
                else if (xy[0] === "D" || xy[1] === "D") { badge = qsTr("删"); color = Theme.statusDanger }
                else if (xy[0] === "M" || xy[1] === "M") { badge = qsTr("改"); color = Theme.statusWarning }
                else if (xy[0] === "R" || xy[1] === "R") { badge = qsTr("移"); color = Theme.brandPrimary }
                entries.push({status: badge, text: path, color: color})
            }
        }
        return entries.length > 0 ? entries : null
    }

    readonly property color accent: {
        if (nodeType === "thinking") return Theme.brandPrimary
        if (nodeType === "tool_call")
            return status === "failed" ? Theme.statusDanger : Theme.statusWarning
        if (nodeType === "tool_result")
            return status === "failed" ? Theme.statusDanger : Theme.statusSuccess
        return Theme.textSecondary
    }

    readonly property string label: {
        if (nodeType === "thinking") return qsTr("思考")
        if (nodeType === "tool_call") return qsTr("工具调用")
        if (nodeType === "tool_result") return qsTr("工具结果")
        if (nodeType === "context") return qsTr("上下文")
        return nodeType
    }

    readonly property string badgeText: {
        if (status === "running") return qsTr("进行中")
        if (status === "failed") return qsTr("失败")
        if (status === "cancelled") return qsTr("已取消")
        if (status === "waiting_confirm") return qsTr("待确认")
        return ""
    }

    implicitHeight: card.height
    height: implicitHeight

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        // 左侧留出助手头像列宽度，与正文左边缘对齐。
        anchors.leftMargin: Theme.avatarSize + Theme.spaceSm
        radius: Theme.radiusMd
        color: Theme.cardBackground
        border.width: 1
        border.color: Theme.cardBorder
        height: contentCol.implicitHeight + Theme.spaceSm * 2

        Column {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spaceSm
            spacing: Theme.spaceSm

            // ── Header ────────────────────────────────
            Item {
                width: parent.width
                height: headerRow.implicitHeight

                Row {
                    id: headerRow
                    anchors.left: parent.left
                    anchors.right: chevron.left
                    spacing: Theme.spaceSm

                    // 状态点
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: root.accent

                        SequentialAnimation on opacity {
                            running: root.status === "running"
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.3; duration: 800 }
                            NumberAnimation { from: 0.3; to: 1.0; duration: 800 }
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.label
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        font.weight: Font.DemiBold
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.badgeText !== ""
                        text: root.badgeText
                        color: root.accent
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        // 工具名 / 参数摘要作为标题
                        text: root.modelData.title || ""
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        elide: Text.ElideRight
                        // 徽章隐藏时不占 Row 宽度，扣减也要跟着消失，否则
                        // 标题会白白缩掉一段。
                        width: Math.max(40, headerRow.width - x
                                        - (sourceBadge.visible ? sourceBadge.width + Theme.spaceSm : 0)
                                        - (stopChip.visible ? stopChip.width + Theme.spaceSm : 0)
                                        - (evidenceChip.visible ? evidenceChip.width + Theme.spaceSm : 0)
                                        - (proposalBadge.visible ? proposalBadge.width + Theme.spaceSm : 0)
                                        - 8)
                    }

                    // 检索来源徽章：让「这次查询发给了谁」在会话里直接可见，
                    // 失败时同样是重要信息（哪家失败）。
                    Rectangle {
                        id: sourceBadge
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.source !== ""
                        width: sourceLabelText.implicitWidth + 10
                        height: 15
                        radius: 3
                        color: Theme.brandPrimary
                        opacity: 0.14

                        Text {
                            id: sourceLabelText
                            anchors.centerIn: parent
                            text: root.sourceLabel(root.source)
                            color: Theme.brandPrimary
                            font.pixelSize: Theme.fontSizeNano
                            font.family: Theme.fontSans
                            font.bold: true
                        }
                    }

                    // docs/32 §9 / docs/35 P4.5：运行中的子代理可单独停止
                    // （主任务与其他子代理继续）。
                    Rectangle {
                        id: stopChip
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.isSpawnAgent && root.status === "running"
                        width: stopText.implicitWidth + 14
                        height: 17
                        radius: 3
                        color: stopMA.containsMouse
                            ? Qt.lighter(Theme.statusDanger, 1.4) : Theme.statusDanger
                        opacity: 0.16

                        Text {
                            id: stopText
                            anchors.centerIn: parent
                            text: qsTr("停止")
                            color: Theme.statusDanger
                            font.pixelSize: Theme.fontSizeNano
                            font.family: Theme.fontSans
                            font.bold: true
                        }

                        MouseArea {
                            id: stopMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: AgentVM.cancelSubAgent(root.modelData.id)
                        }
                    }

                    // docs/33 E1: 证据工件入口——子代理报告的完整 transcript
                    // 落在本地文件，报告头里的纯文本路径用户没法直接用。
                    Rectangle {
                        id: evidenceChip
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.evidencePath !== ""
                        width: evidenceText.implicitWidth + 14
                        height: 17
                        radius: 3
                        color: evidenceMA.containsMouse
                            ? Qt.lighter(Theme.brandPrimary, 1.4) : Theme.brandPrimary
                        opacity: 0.16

                        Text {
                            id: evidenceText
                            anchors.centerIn: parent
                            text: qsTr("查看证据")
                            color: Theme.brandPrimary
                            font.pixelSize: Theme.fontSizeNano
                            font.family: Theme.fontSans
                            font.bold: true
                        }

                        MouseArea {
                            id: evidenceMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: AgentVM.openSubagentEvidence(root.evidencePath)
                        }
                    }

                    // docs/34 P3.2: 改动提案计数徽章——折叠态也能一眼看出
                    // 这次子代理起草了几处修改。
                    Rectangle {
                        id: proposalBadge
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.hasSubagentProposals
                        width: proposalBadgeText.implicitWidth + 10
                        height: 15
                        radius: 3
                        color: Theme.brandPrimary
                        opacity: 0.14

                        Text {
                            id: proposalBadgeText
                            anchors.centerIn: parent
                            text: qsTr("%1 个提案").arg(root.proposalTotal)
                            color: Theme.brandPrimary
                            font.pixelSize: Theme.fontSizeNano
                            font.family: Theme.fontSans
                            font.bold: true
                        }
                    }
                }

                AppIcon {
                    id: chevron
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: (root.hasDetail || root.hasSubagentLog
                              || root.hasSubagentProposals) && !root.waitingConfirm
                    icon: root.expanded ? "chevron-up" : "chevron-down"
                    size: 12
                    iconColor: headerMA.containsMouse
                        ? Theme.brandPrimary : Theme.textSecondary
                }

                MouseArea {
                    id: headerMA
                    anchors.fill: parent
                    enabled: (root.hasDetail || root.hasSubagentLog
                              || root.hasSubagentProposals) && !root.waitingConfirm
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.toggleRequested()
                }
            }

            // 折叠摘要
            Text {
                visible: (root.hasDetail || root.hasSubagentLog
                          || root.hasSubagentProposals)
                         && !root.expanded && !root.waitingConfirm
                text: root.hasSubagentLog
                    ? qsTr("已折叠 · %1 条过程记录").arg(root.subagentLog.length)
                    : (root.hasSubagentProposals
                       ? qsTr("已折叠 · %1 个改动提案").arg(root.proposalTotal)
                       : qsTr("已折叠 · %1 字符").arg(root.detailText.length))
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }

            // 展开区（docs/25 C7: 折叠态不实例化重型内容——Loader 绑定
            // bodyVisible，超长会话中大量折叠的过程节点不再为不可见的
            // 文本布局付费；折叠态高度仅由头部+摘要行决定，天然稳定）。
            Loader {
                id: bodyLoader
                objectName: "bodyLoader"
                active: root.bodyVisible
                visible: active
                width: parent.width
                sourceComponent: root.gitStatusEntries !== null
                                  ? gitStatusComp
                                  : (root.hasSubagentLog
                                     ? subagentLogComp
                                     : (root.hasSubagentProposals
                                        ? proposalsComp : codeComp))
            }

            Component {
                id: gitStatusComp
                Column {
                    width: bodyLoader.width
                    spacing: 1

                    Repeater {
                        model: root.gitStatusEntries

                        delegate: Row {
                            width: parent.width
                            spacing: 6

                            Rectangle {
                                width: 22; height: 16; radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: modelData.color
                                opacity: 0.15

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.status
                                    color: modelData.color
                                    font.pixelSize: Theme.fontSizeNano
                                    font.family: Theme.fontSans
                                    font.bold: true
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.text
                                color: modelData.color
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                elide: Text.ElideRight
                                width: parent.width - 28
                            }
                        }
                    }
                }
            }

            Component {
                id: subagentLogComp
                Column {
                    objectName: "subagentLogBody"
                    width: bodyLoader.width
                    spacing: Theme.spaceXs

                    Repeater {
                        model: root.subagentLog

                        delegate: Row {
                            required property var modelData
                            width: parent.width
                            spacing: 6

                            Text {
                                id: glyphText
                                anchors.verticalCenter: entryText.verticalCenter
                                text: root.logGlyph(modelData.phase)
                                color: root.logColor(modelData.phase)
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                            }

                            Text {
                                id: entryText
                                width: parent.width - 20
                                text: modelData.text
                                color: modelData.phase === "tool"
                                    || modelData.phase === "context"
                                    || modelData.phase === "reasoning"
                                    ? Theme.textSecondary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: modelData.phase === "tool"
                                    ? Theme.fontMono : Theme.fontSans
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // 日志之后仍展示参数/结果正文（报告或 goal）。
                    CodeBlock {
                        visible: root.detailText !== ""
                        width: parent.width
                        text: root.detailText
                        textColor: root.nodeType === "tool_result"
                            ? Theme.statusSuccess : Theme.textPrimary
                        bgColor: Theme.codeBackground
                    }
                }
            }

            // docs/34 P3.2: 提案列表——每条一张小卡：路径 + 处数 + 一行理由，
            // 报告正文（含 diff）仍以代码块附在下方。
            Component {
                id: proposalsComp
                Column {
                    objectName: "subagentProposalsBody"
                    width: bodyLoader.width
                    spacing: Theme.spaceXs

                    Repeater {
                        model: root.subagentProposals

                        delegate: Column {
                            required property var modelData
                            width: parent.width
                            spacing: 1

                            Row {
                                width: parent.width
                                spacing: 6

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - countText.width - 6
                                    text: modelData.path || ""
                                    color: Theme.brandPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    id: countText
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("%1 处修改").arg(modelData.count || 0)
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSizeNano
                                    font.family: Theme.fontSans
                                }
                            }

                            Text {
                                visible: text !== ""
                                width: parent.width
                                text: modelData.rationale || ""
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // 报告全文（## PROPOSALS 段与 diff）附在列表之后。
                    CodeBlock {
                        visible: root.detailText !== ""
                        width: parent.width
                        text: root.detailText
                        textColor: Theme.statusSuccess
                        bgColor: Theme.codeBackground
                    }
                }
            }

            Component {
                id: codeComp
                CodeBlock {
                    width: bodyLoader.width
                    text: root.detailText
                    textColor: root.nodeType === "tool_result"
                        ? Theme.statusSuccess
                        : (root.nodeType === "thinking"
                           ? Theme.textSecondary : Theme.textPrimary)
                    bgColor: Theme.codeBackground
                }
            }

            // 变更预览（确认态且有 preview 时显示）
            Text {
                visible: root.hasPreview
                text: qsTr("变更预览:")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }

            CodeBlock {
                visible: root.hasPreview
                width: parent.width
                height: Math.min(200, implicitHeight)
                text: root.previewText
                bgColor: Theme.codeBackground
            }

            // 确认按钮
            Row {
                visible: root.waitingConfirm
                spacing: Theme.spaceSm

                AppButton {
                    text: qsTr("拒绝")
                    buttonColor: "transparent"
                    textColor: Theme.statusDanger
                    border.color: Theme.statusDanger
                    onClicked: AgentVM.approveToolCall(root.modelData.id, false)
                }

                AppButton {
                    text: qsTr("允许")
                    highlighted: true
                    onClicked: AgentVM.approveToolCall(root.modelData.id, true)
                }
            }

            // 展开态的底部收回入口：长内容滚到卡片底部就能收起，不必回到
            // 顶部点箭头。确认态不显示（那有允许/拒绝按钮收场）。
            Item {
                visible: root.expanded && !root.waitingConfirm
                         && (root.hasDetail || root.hasSubagentLog
                             || root.hasSubagentProposals)
                width: parent.width
                height: collapseRow.implicitHeight

                Row {
                    id: collapseRow
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4

                    Text {
                        text: qsTr("收起")
                        color: collapseMA.containsMouse
                            ? Theme.brandPrimary : Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        icon: "chevron-up"
                        size: 10
                        iconColor: collapseMA.containsMouse
                            ? Theme.brandPrimary : Theme.textTertiary
                    }
                }

                MouseArea {
                    id: collapseMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleRequested()
                }
            }
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: root.label
    Accessible.description: root.detailText.substring(0, 80)
}
