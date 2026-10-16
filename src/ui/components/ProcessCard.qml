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
    readonly property bool bodyVisible: (expanded || waitingConfirm) && hasDetail

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
                if (xy === "??") { badge = "新"; color = Theme.statusSuccess }
                else if (xy[0] === "A" || xy[1] === "A") { badge = "增"; color = Theme.statusSuccess }
                else if (xy[0] === "D" || xy[1] === "D") { badge = "删"; color = Theme.statusDanger }
                else if (xy[0] === "M" || xy[1] === "M") { badge = "改"; color = Theme.statusWarning }
                else if (xy[0] === "R" || xy[1] === "R") { badge = "移"; color = Theme.brandPrimary }
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
                        width: Math.max(40, headerRow.width - x - 8)
                    }
                }

                AppIcon {
                    id: chevron
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.hasDetail && !root.waitingConfirm
                    icon: root.expanded ? "chevron-up" : "chevron-down"
                    size: 12
                    iconColor: headerMA.containsMouse
                        ? Theme.brandPrimary : Theme.textSecondary
                }

                MouseArea {
                    id: headerMA
                    anchors.fill: parent
                    enabled: root.hasDetail && !root.waitingConfirm
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.toggleRequested()
                }
            }

            // 折叠摘要
            Text {
                visible: root.hasDetail && !root.expanded && !root.waitingConfirm
                text: qsTr("已折叠 · %1 字符").arg(root.detailText.length)
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }

            // git_status 结构化文件列表
            Column {
                visible: root.bodyVisible && root.gitStatusEntries !== null
                width: parent.width
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
                                font.pixelSize: 9
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

            // 展开区
            CodeBlock {
                visible: root.bodyVisible && root.gitStatusEntries === null
                width: parent.width
                text: root.detailText
                textColor: root.nodeType === "tool_result"
                    ? Theme.statusSuccess
                    : (root.nodeType === "thinking"
                       ? Theme.textSecondary : Theme.textPrimary)
                bgColor: Theme.codeBackground
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
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: root.label
    Accessible.description: root.detailText.substring(0, 80)
}
