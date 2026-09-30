import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Top bar - sidebar toggle, status indicator and panel toggle
Rectangle {
    id: root

    color: Theme.bgSecondary

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

                // Pulsing animation for active states
                SequentialAnimation on opacity {
                    running: AgentVM.stateBusy
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.3; duration: 800 }
                    NumberAnimation { from: 0.3; to: 1.0; duration: 800 }
                }
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

            // Mini progress bar: usedTokens / maxTokens
            Rectangle {
                Layout.preferredWidth: 60
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
                    color: AgentVM.tokenUsageRate >= 0.9
                        ? Theme.statusDanger : Theme.brandPrimary
                }
            }

            Text {
                text: qsTr("tokens: %1 / %2").arg(AgentVM.usedTokens).arg(AgentVM.maxTokens)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
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
                            text: modelData.tokens
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
                        text: tokenPopup.maxTokens - tokenPopup.totalTokens
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

                // ── Footer: session cumulative (kept from the old view) ──
                Rectangle {
                    visible: (tokenPopup.sessionTotals.totalCalls || 0) > 0
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    Layout.bottomMargin: 2
                    Layout.preferredHeight: 1
                    color: Theme.borderSubtle
                }
                Text {
                    visible: (tokenPopup.sessionTotals.totalCalls || 0) > 0
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
