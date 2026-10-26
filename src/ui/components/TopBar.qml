import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Top bar - sidebar toggle, status indicator and panel toggle
Rectangle {
    id: root

    // 预设菜单里的「管理…」：经 AgentChatPage 中继到 main 的设置中心
    // （openCategory("prompts")）。TopBar 看不到对话框实例，走信号链。
    signal promptAssetsRequested()

    color: Theme.bgSecondary

    // 紧凑 token 计数（TopBar 小字用）：1234 → 1.2k，56000 → 56k。
    // 分档 k/M/B：先按当前档位取值，四舍五入若溢出当前档（≥ 999.5）
    // 则整体进到下一档，保证显示永不出现四位数字（999950 → "1.0M"
    // 而非 "1000k"），进位区间（99.95k → 100k）由 Math.round 自然收敛。
    // 历史注记：此前用特殊分支实现进位，曾把「提前返回」误置于 k≥100
    // 判断之前，导致 172000 也返回常量 "100k" —— 累计显示停死。
    function formatTokens(n) {
        if (n < 1000) return String(n);
        let unit = 1e3, suffix = "k";
        if (n >= 1e9)      { unit = 1e9; suffix = "B"; }
        else if (n >= 1e6) { unit = 1e6; suffix = "M"; }
        const v = n / unit;
        if (v >= 999.5) {
            unit *= 1000
            suffix = suffix === "k" ? "M" : (suffix === "M" ? "B" : "T")
            const v2 = n / unit
            return (v2 >= 100 ? Math.round(v2) : v2.toFixed(1)) + suffix
        }
        if (v >= 100) return Math.round(v) + suffix
        if (v >= 99.95) return "100" + suffix
        return v.toFixed(1) + suffix
    }

    // docs/25 C4: token 明细浮层的精确数字走千分位（与 UsageStatsPage
    // .fmtNumber 同一口径）；formatTokens 的紧凑显示仅用于顶栏小字。
    function fmtNumber(n) {
        return Number(n || 0).toLocaleString(Qt.locale(), 'f', 0)
    }

    // Bottom border
    Rectangle {
        width: parent.width
        height: 1
        anchors.bottom: parent.bottom
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 0

        // ── Sidebar Toggle ───────────────────────────
        IconButton {
            icon: "menu"
            iconSize: 20
            restIconColor: Theme.textPrimary
            tooltip: AgentVM.isSidebarExpanded
                ? qsTr("收起侧边栏 (Ctrl+[)") : qsTr("展开侧边栏 (Ctrl+[)")
            onClicked: AgentVM.toggleSidebar()
        }

        // ── 会话归属徽标（docs/18 §2.3）──────────────
        // 归属按标签持有，这里显示的永远是**当前标签**的项目，因此不可能
        // 与其他标签不一致。两种形态：
        //  · 标签还没有会话 → 可点击切换目标（把"归属不确定"收敛到发首条
        //    消息之前这个唯一需要它的时刻）
        //  · 已有会话 → 只读指示，点击不改变归属（避免它又变成一个隐藏开关）
        Rectangle {
            id: projectBadge
            readonly property string projectId: AgentVM.activeProjectId
            readonly property bool hasSession: AgentVM.currentSessionId.length > 0
            readonly property bool editable: !hasSession
            readonly property string projectName: {
                const pid = projectBadge.projectId
                if (!pid) return ""
                const list = AgentVM.projects
                for (let i = 0; i < list.length; ++i)
                    if (list[i].id === pid) return list[i].name
                return ""
            }
            // 已有会话且归属本地 → 无需任何指示，直接隐藏。
            visible: editable || projectName.length > 0
            // RowLayout 取 implicitWidth；Row 是定位器只会包住子项，
            // 不会反向撑开 parent，这里必须显式给。
            implicitWidth: badgeRow.implicitWidth + 16
            implicitHeight: 22
            Layout.preferredHeight: 22
            Layout.leftMargin: 10
            Layout.maximumWidth: 240
            radius: 4
            color: projectBadgeMA.containsMouse && editable
                ? Theme.surfaceHover : "transparent"
            border.width: 1
            border.color: projectPopup.opened ? Theme.brandPrimary : Theme.borderSubtle

            Row {
                id: badgeRow
                anchors.centerIn: parent
                spacing: 4

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "folder"
                    size: 11
                    iconColor: projectBadge.projectName.length > 0
                        ? Theme.brandPrimary : Theme.textSecondary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, 160)
                    text: projectBadge.projectName.length > 0
                        ? projectBadge.projectName : qsTr("选择项目")
                    color: projectBadge.projectName.length > 0
                        ? Theme.textPrimary : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    elide: Text.ElideRight
                }
                // 可切换时才给出"可展开"的提示；只读形态不显示。
                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: projectBadge.editable
                    icon: projectPopup.opened ? "chevron-up" : "chevron-down"
                    size: 11
                    iconColor: Theme.textSecondary
                }
            }

            MouseArea {
                id: projectBadgeMA
                anchors.fill: parent
                // 只读形态下整体不可交互：没有悬停反馈、不换光标、点击无效。
                enabled: projectBadge.editable
                hoverEnabled: true
                cursorShape: projectBadge.editable
                    ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: projectPopup.opened ? projectPopup.close() : projectPopup.open()
            }

            Accessible.role: Accessible.Button
            Accessible.name: projectBadge.editable
                ? (projectBadge.projectName.length > 0
                   ? qsTr("当前项目：%1（点击更换）").arg(projectBadge.projectName)
                   : qsTr("选择项目"))
                : qsTr("当前项目：%1").arg(projectBadge.projectName)

            // 项目选择器：空标签上把归属定下来。发出首条消息后归属即落定，
            // 徽标随之转为只读。
            Popup {
                id: projectPopup
                x: 0
                y: projectBadge.height + 6
                width: 280
                height: projectContent.implicitHeight + 12
                padding: 6
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: DialogBackground {}

                contentItem: ColumnLayout {
                    id: projectContent
                    spacing: 2

                    Repeater {
                        // 首项固定为"本地对话"，其后是已导入项目。
                        model: {
                            const entries = [{
                                id: "",
                                label: qsTr("无项目（本地对话）"),
                                desc: qsTr("普通对话，不关联任何文件夹")
                            }]
                            const list = AgentVM.projects
                            for (let i = 0; i < list.length; ++i)
                                entries.push({
                                    id: list[i].id,
                                    label: list[i].name,
                                    desc: list[i].rootPath
                                })
                            return entries
                        }

                        delegate: Rectangle {
                            id: projectItem
                            required property var modelData
                            readonly property bool isSelected:
                                modelData.id === projectBadge.projectId
                            Layout.fillWidth: true
                            Layout.leftMargin: 2
                            Layout.rightMargin: 2
                            Layout.preferredHeight: 34
                            radius: 5
                            color: projectItemMA.containsMouse
                                ? Theme.surfaceHover : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 6

                                AppIcon {
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    icon: "check"
                                    size: 12
                                    iconColor: Theme.brandPrimary
                                    visible: projectItem.isSelected
                                }
                                Item {
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    visible: !projectItem.isSelected
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: projectItem.isSelected
                                            ? Theme.brandPrimary : Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.desc
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                        font.family: Theme.fontMono
                                        elide: Text.ElideMiddle
                                    }
                                }
                            }

                            MouseArea {
                                id: projectItemMA
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    projectPopup.close()
                                    AgentVM.setActiveProjectId(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Spacer ────────────────────────────────────
        Item { Layout.fillWidth: true }

        // ── State Status Indicator ───────────────────
        RowLayout {
            spacing: 6

            // Status dot
            Rectangle {
                id: stateDot
                width: 8
                height: 8
                radius: 4

                color: {
                    var s = AgentVM.state
                    if (s === AgentVM.stateIdle) return Theme.textSecondary
                    if (s === AgentVM.stateThinking) return Theme.brandPrimary
                    if (s === AgentVM.stateAwaitingToolConfirm) return Theme.statusWarning
                    if (s === AgentVM.stateExecutingTool) return Theme.statusWarning
                    if (s === AgentVM.stateSummarizing) return Theme.brandPrimary
                    if (s === AgentVM.stateCompleted) return Theme.statusSuccess
                    if (s === AgentVM.stateError) return Theme.statusDanger
                    return Theme.textSecondary
                }

                // Pulsing animation for active states. Drives an intermediate
                // property instead of opacity itself: `Animation on <property>`
                // destroys the declarative binding and leaves the value stranded
                // at the last frame when the animation stops, so the indicator
                // would stay dimmed after a task ends.
                property real pulse: 1.0
                SequentialAnimation {
                    running: AgentVM.stateBusy
                    loops: Animation.Infinite
                    NumberAnimation { target: stateDot; property: "pulse"; from: 1.0; to: 0.3; duration: 800 }
                    NumberAnimation { target: stateDot; property: "pulse"; from: 0.3; to: 1.0; duration: 800 }
                }
                opacity: AgentVM.stateBusy ? pulse : 1.0
            }

            Text {
                text: AgentVM.stateText
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }

        Item { Layout.fillWidth: true }

        // ── Token Usage (Batch 15 / F5 + docs/16 §8.2) ───
        // Bound to the pre-existing AgentViewModel counters; live updates
        // arrive via tokenUsageChanged while a task is running. A mini
        // progress bar visualizes usedTokens/maxTokens and the per-request
        // cost (lastCostCents) is shown alongside.
        // docs/16 §8.3: hovering opens a popup with the current session's
        // token breakdown (per-source input/output splits, cache share).
        RowLayout {
            id: tokenArea
            spacing: 6
            Layout.rightMargin: 8

            // 窗口占用色阶：占用条与百分比文字同源，≥80% 转警示色、
            // ≥90% 转危险色，一眼可读，无需数字细节。
            readonly property color usageColor:
                AgentVM.tokenUsageRate >= 0.9 ? Theme.statusDanger
                : AgentVM.tokenUsageRate >= 0.8 ? Theme.statusWarning
                : Theme.brandPrimary

            // Mini progress bar (docs/16 §8.2/§8.3): 上下文窗口占用
            // (usedTokens/maxTokens)。会话累计是无上限计数器，不借用窗口
            // 容量画条（同刻度并排会显得比占用条还长，误导占用判断），
            // 只以图标+数值显示，明细见悬浮浮层。点击或悬停弹出占用细节。
            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 4
                radius: 2
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle
                Rectangle {
                    width: Math.max(0, Math.min(parent.width,
                        AgentVM.maxTokens > 0
                            ? parent.width * Math.min(1.0, AgentVM.usedTokens / AgentVM.maxTokens)
                            : 0))
                    height: parent.height
                    radius: parent.radius
                    color: tokenArea.usageColor
                }
            }

            // 上下文占用只显示百分比（精确值悬停可见，明细在浮层）；
            // 颜色跟占用色阶走。maxTokens 为 0（无任务）时不显示。
            Text {
                visible: AgentVM.maxTokens > 0
                text: Math.round(AgentVM.tokenUsageRate * 100) + "%"
                color: tokenArea.usageColor
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                font.bold: AgentVM.tokenUsageRate >= 0.8
            }

            // docs/16 §8.2: 会话累计消耗（input+output，跨任务累加）。
            // 区别于左侧的窗口占用 usedTokens/maxTokens —— 那是下一次
            // 请求的 prompt 规模，不会随历史重发而"翻倍增长"。用层叠
            // 图标替代"累计"文字前缀，数值走紧凑格式。
            Row {
                visible: AgentVM.sessionTokensUsed > 0
                spacing: 3

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "layers"
                    size: 12
                    iconColor: Theme.textSecondary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: formatTokens(AgentVM.sessionTokensUsed)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
            }

            // docs/16 §8.2: per-request cost (only shown when non-zero)
            Text {
                visible: AgentVM.lastCostCents > 0
                text: qsTr("¥%1/轮").arg((AgentVM.lastCostCents / 100.0).toFixed(2))
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }

            // Hover detector: opens the breakdown popup after a short dwell
            // so a passing cursor doesn't flash the popup. HoverHandler is a
            // non-Item, so the RowLayout doesn't try to manage its geometry.
            // Both timers are condition-driven (no imperative start/stop,
            // which would break their running bindings).
            HoverHandler { id: tokenHover }

            // 点击计数区/进度条同样打开细节浮层（再点关闭）；悬停逻辑
            // 由上方 timer 驱动，点击则立即生效。条上只留百分比后，
            // 精确值仍由明细浮层提供（"上下文构成 · x / y · z%"），
            // 不另设 ToolTip：hoverOpenTimer 350ms 先开浮层，ToolTip
            // 只会在浮层上叠一层，纯干扰。
            TapHandler {
                onTapped: tokenPopup.opened
                    ? tokenPopup.close() : tokenPopup.refreshAndOpen()
            }

            Timer {
                id: hoverOpenTimer
                interval: 350
                running: tokenHover.hovered && !tokenPopup.opened
                onTriggered: tokenPopup.refreshAndOpen()
            }
            // Auto-close once neither the trigger nor the popup is hovered;
            // the interval doubles as a grace period for moving between them.
            Timer {
                id: hoverCloseTimer
                interval: 300
                running: tokenPopup.opened && !tokenHover.hovered
                         && !tokenPopupHover.hovered
                onTriggered: tokenPopup.close()
            }
        }

        // docs/16 §8.3: context-capacity composition popup. Both sources are
        // queried at open time: the live per-role token buckets from the
        // active agent (composition) and the session cumulative totals for
        // the one-line footer (sessionTotals).
        Popup {
            id: tokenPopup
            parent: tokenArea
            x: tokenArea.width - width      // right-aligned with the trigger
            y: tokenArea.height + 8
            width: 300
            padding: 10
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            background: DialogBackground {}

            property var composition: ({})
            property var sessionTotals: ({})

            function refreshAndOpen() {
                composition = AgentVM.contextComposition()
                sessionTotals = AgentVM.sessionTokenBreakdown()
                open()
            }

            readonly property int totalTokens: composition.totalTokens || 0
            readonly property int maxTokens: composition.maxTokens || 0

            // Non-zero buckets in capacity order; colors match the stacked
            // bar segments. Percentages are shares of the window capacity.
            readonly property var segments: {
                const c = composition
                const defs = [
                    ["systemPrompt", qsTr("系统提示词"), Theme.textSecondary],
                    ["toolsSchema",  qsTr("工具定义"),   Theme.statusWarning],
                    ["conversation", qsTr("对话"),       Theme.brandPrimary],
                    ["toolResults",  qsTr("工具结果"),   Theme.statusSuccess],
                    ["images",       qsTr("图片"),       Theme.statusDanger]
                ]
                const rows = []
                for (let i = 0; i < defs.length; ++i) {
                    const v = c[defs[i][0]] || 0
                    if (v > 0)
                        rows.push({ label: defs[i][1], tokens: v, color: defs[i][2] })
                }
                return rows
            }
            function windowPct(tokens) {
                return maxTokens > 0 ? Math.round(tokens * 100 / maxTokens) : 0
            }

            readonly property bool hasSessionTotals:
                (sessionTotals.totalCalls || 0) > 0

            // 会话累计摘要行：总额 + 输入/输出/命中率，同统计页口径。
            readonly property string sessionSummary: {
                const t = sessionTotals
                const total = t.totalTokens || 0
                const rate = t.cacheHitRate || 0
                return qsTr("会话累计 · %1 tokens · 命中率 %2%")
                    .arg(total).arg(Math.round(rate * 100))
            }

            // 输入(未命中)/缓存命中/输出：与统计页方向卡同一互斥口径。
            readonly property var sessionDirectionRows: {
                const t = sessionTotals
                const total = t.totalTokens || 0
                const inHit = t.totalCacheHitTokens || 0
                const inMiss = Math.max(0, (t.totalInputTokens || 0) - inHit)
                const rows = [
                    [qsTr("输入（未命中）"), inMiss],
                    [qsTr("缓存命中"), inHit],
                    [qsTr("输出"), t.totalOutputTokens || 0]
                ]
                const out = []
                for (let i = 0; i < rows.length; ++i) {
                    if (rows[i][1] > 0)
                        out.push({ label: rows[i][0], tokens: rows[i][1],
                                   pct: total > 0
                                       ? Math.round(rows[i][1] * 100 / total) : 0 })
                }
                return out
            }

            // 来源拆分（输入侧）：只列非零项，按占比降序。
            readonly property var sessionSourceRows: {
                const t = sessionTotals
                const total = t.totalInputTokens || 0
                const defs = [
                    ["inputSystemPrompt", qsTr("系统提示词")],
                    ["inputConversation", qsTr("对话")],
                    ["inputToolCall",     qsTr("工具调用")],
                    ["inputMcp",          qsTr("MCP")],
                    ["inputSkill",        qsTr("技能")],
                    ["inputImage",        qsTr("图片")],
                    ["inputOther",        qsTr("其他")]
                ]
                const rows = []
                for (let i = 0; i < defs.length; ++i) {
                    const v = t[defs[i][0]] || 0
                    if (v > 0)
                        rows.push({ label: defs[i][1], tokens: v,
                                    pct: total > 0
                                        ? Math.round(v * 100 / total) : 0 })
                }
                rows.sort(function(a, b) { return b.tokens - a.tokens })
                return rows
            }

            contentItem: ColumnLayout {
                spacing: 3

                // Keep the popup open while the cursor is over it (paired
                // with hoverCloseTimer on the trigger).
                HoverHandler { id: tokenPopupHover }

                // Empty state: no agent attached (fresh tab, no task yet).
                Text {
                    visible: tokenPopup.segments.length === 0
                             && tokenPopup.totalTokens === 0
                    text: qsTr("暂无数据")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    visible: tokenPopup.segments.length > 0
                    text: tokenPopup.maxTokens > 0
                        ? qsTr("上下文构成 · %1 / %2 · %3%").arg(
                              tokenPopup.totalTokens).arg(tokenPopup.maxTokens)
                              .arg(tokenPopup.windowPct(tokenPopup.totalTokens))
                        : qsTr("上下文构成 · %1").arg(tokenPopup.totalTokens)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                // ── Stacked capacity bar: full width = context window,
                //    each segment sized by its share, remainder = free ──
                Rectangle {
                    visible: tokenPopup.segments.length > 0
                             && tokenPopup.maxTokens > 0
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    Layout.preferredHeight: 8
                    radius: 4
                    color: Theme.bgPrimary
                    border.width: 1
                    border.color: Theme.borderSubtle
                    clip: true

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        height: parent.height
                        Repeater {
                            model: tokenPopup.segments
                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width * Math.min(1.0,
                                    modelData.tokens / Math.max(1, tokenPopup.maxTokens))
                                height: parent.height
                                color: modelData.color
                            }
                        }
                    }
                }

                // ── Legend: color dot + label + tokens + share of window ──
                Repeater {
                    model: tokenPopup.segments
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true

                        Rectangle {
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                            radius: 2
                            color: modelData.color
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideRight
                        }
                        Text {
                            text: root.fmtNumber(modelData.tokens)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        Text {
                            text: tokenPopup.windowPct(modelData.tokens) + "%"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            Layout.preferredWidth: 36
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                // ── Free capacity row ──
                RowLayout {
                    visible: tokenPopup.segments.length > 0
                             && tokenPopup.maxTokens > 0
                             && tokenPopup.maxTokens > tokenPopup.totalTokens
                    Layout.fillWidth: true

                    Item {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("剩余可用")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                    }
                    Text {
                        text: root.fmtNumber(tokenPopup.maxTokens - tokenPopup.totalTokens)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                    }
                    Text {
                        text: tokenPopup.windowPct(
                                  tokenPopup.maxTokens - tokenPopup.totalTokens) + "%"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        Layout.preferredWidth: 36
                        horizontalAlignment: Text.AlignRight
                    }
                }

                // ── Session cumulative section (per docs/16 §8.3):
                //    合计 + 输入(未命中)/缓存命中/输出 互斥口径，再按来源
                //    拆分系统提示词/对话/工具调用/图片等 —— 与统计页一致。 ──
                Rectangle {
                    visible: tokenPopup.hasSessionTotals
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    Layout.bottomMargin: 2
                    Layout.preferredHeight: 1
                    color: Theme.borderSubtle
                }
                Text {
                    visible: tokenPopup.hasSessionTotals
                    text: tokenPopup.sessionSummary
                    color: Theme.textSecondary
                    font.bold: true
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }

                // 输入(未命中)/缓存命中/输出：三项互斥，和恰为累计总额。
                Repeater {
                    model: tokenPopup.sessionTotals.totalCalls > 0
                        ? tokenPopup.sessionDirectionRows : []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true

                        Item {
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideRight
                        }
                        Text {
                            text: root.fmtNumber(modelData.tokens)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        Text {
                            text: modelData.pct + "%"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            Layout.preferredWidth: 36
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                // 来源拆分：系统提示词/对话/工具调用/图片等累计占比。
                Repeater {
                    model: tokenPopup.sessionSourceRows
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true

                        Item {
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                        }
                        Rectangle {
                            Layout.preferredWidth: 4
                            Layout.preferredHeight: 4
                            radius: 1
                            color: Theme.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideRight
                        }
                        Text {
                            text: root.fmtNumber(modelData.tokens)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        Text {
                            text: modelData.pct + "%"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            Layout.preferredWidth: 36
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                // ── Footer: request count + cost (kept from the old view) ──
                Text {
                    visible: tokenPopup.hasSessionTotals
                    text: (tokenPopup.sessionTotals.totalCostCents > 0)
                        ? qsTr("本会话累计 %1 次请求 · ¥%2")
                            .arg(tokenPopup.sessionTotals.totalCalls || 0)
                            .arg(((tokenPopup.sessionTotals.totalCostCents || 0) / 100.0).toFixed(2))
                        : qsTr("本会话累计 %1 次请求")
                            .arg(tokenPopup.sessionTotals.totalCalls || 0)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
            }
        }

        // ── 提示词预设切换器 ──────────────────────────
        // 一键把预设套用到当前会话（写活动标签 agent 的 override），
        // 「无」清除覆盖回到全局设置；管理入口去设置中心的 prompts 分类。
        IconButton {
            id: presetToggle
            icon: "layers"
            iconSize: 16
            tooltip: qsTr("提示词预设（套用到当前会话）")
            Accessible.name: tooltip
            onClicked: presetMenu.popup(presetToggle, 0, presetToggle.height + 4)
        }

        Menu {
            id: presetMenu
            width: 240

            MenuItem {
                text: qsTr("无（使用全局设置）")
                Accessible.name: text
                onTriggered: AgentVM.applyPromptOverride("")
            }
            MenuSeparator {}
            Repeater {
                model: Settings.promptPresets
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    Accessible.name: text
                    onTriggered: AgentVM.applyPromptOverride(modelData.prompt)
                }
            }
            MenuItem {
                visible: Settings.promptPresets.length === 0
                enabled: false
                text: qsTr("还没有预设")
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("管理预设…")
                Accessible.name: text
                onTriggered: root.promptAssetsRequested()
            }
        }

        // ── Task List Toggle ──────────────────────────
        // The list itself lives in TodoDialog (main.qml) — the sidebar was
        // too narrow for it. The badge counts tasks that are not finished
        // yet, so an in-flight plan is visible without opening anything.
        // Reads AgentVM.todoList directly: TopBar cannot see the dialog
        // instance, and this way the count follows todoListChanged.
        IconButton {
            id: todoToggle
            readonly property int unfinishedCount: {
                const list = AgentVM.todoList
                if (!list || list.length === 0)
                    return 0
                let n = 0
                for (let i = 0; i < list.length; ++i) {
                    const item = list[i]
                    if (item && item.status !== "completed")
                        ++n
                }
                return n
            }

            icon: "check"
            iconSize: 16
            restIconColor: Theme.textPrimary
            visible: unfinishedCount > 0
            tooltip: qsTr("任务清单（%1 项未完成）").arg(unfinishedCount)
            Accessible.name: qsTr("打开任务清单，%1 项未完成").arg(unfinishedCount)
            onClicked: todoDialog.open()

            // Count badge on the top-right corner; hidden at zero so an
            // idle session shows no chrome at all. Reads the count off the
            // button's id rather than walking parent.parent — the chain
            // breaks the moment this Rectangle is wrapped in anything.
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 1
                anchors.topMargin: 1
                visible: todoToggle.unfinishedCount > 0
                width: Math.max(13, badgeLabel.implicitWidth + 6)
                height: 13
                radius: 7
                color: Theme.brandPrimary

                Text {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: todoToggle.unfinishedCount > 99
                        ? "99+" : todoToggle.unfinishedCount
                    color: Theme.bgPrimary
                    font.pixelSize: 8
                    font.family: Theme.fontMono
                    font.bold: true
                }
            }
        }

        // ── Review Toggle ─────────────────────────────
        // Opens ReviewDialog (main.qml). Always available: an empty session
        // gets the dialog's own empty state rather than a dead button.
        //
        // UE-1: the button lights up while the session has changed files the
        // user has not looked at yet, and carries the count - the same
        // treatment the task list button gets, so "something needs my
        // attention" reads the same way in both places. ReviewDialog calls
        // AgentVM.acknowledgeFileReview() when it closes, so opening the
        // review is what clears it.
        IconButton {
            id: reviewToggle
            objectName: "reviewToggleButton"
            readonly property int unseenCount: AgentVM.unreviewedFileCount
            readonly property bool highlighted: unseenCount > 0

            icon: "edit"
            iconSize: 16
            restIconColor: highlighted ? Theme.brandPrimary : Theme.textPrimary
            restBgColor: highlighted
                ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                          Theme.brandPrimary.b, Theme.isDark ? 0.18 : 0.10)
                : "transparent"
            tooltip: highlighted
                ? qsTr("审阅本次会话的文件改动（%1 个文件待查看）").arg(unseenCount)
                : qsTr("审阅本次会话的文件改动 (Ctrl+R)")
            Accessible.name: highlighted
                ? qsTr("打开文件改动审阅，%1 个文件待查看").arg(unseenCount)
                : qsTr("打开文件改动审阅")
            onClicked: reviewDialog.open()

            // Count badge, mirroring the task list button. Capped at 99+ so a
            // big sweep of files cannot stretch the toolbar row.
            Rectangle {
                objectName: "reviewUnseenBadge"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 1
                anchors.topMargin: 1
                visible: reviewToggle.highlighted
                width: Math.max(13, unseenLabel.implicitWidth + 6)
                height: 13
                radius: 7
                color: Theme.brandPrimary

                Text {
                    id: unseenLabel
                    anchors.centerIn: parent
                    text: reviewToggle.unseenCount > 99
                        ? "99+" : reviewToggle.unseenCount
                    color: Theme.bgPrimary
                    font.pixelSize: 8
                    font.family: Theme.fontMono
                    font.bold: true
                }
            }
        }

        // ── Panel Toggle Button ──────────────────────
        IconButton {
            icon: AgentVM.isPanelVisible ? "chevron-right" : "chevron-left"
            iconSize: 18
            restIconColor: Theme.textPrimary
            border.width: AgentVM.isPanelVisible ? 0 : 1
            border.color: Theme.borderSubtle
            tooltip: AgentVM.isPanelVisible
                ? qsTr("收起上下文面板 (Ctrl+B)") : qsTr("展开上下文面板 (Ctrl+B)")
            onClicked: AgentVM.togglePanel()
        }

    }
}
