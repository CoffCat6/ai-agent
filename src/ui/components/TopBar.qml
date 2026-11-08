import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Top bar - sidebar toggle, centered run-state indicator, token usage
Rectangle {
    id: root

    color: Theme.bgSecondary

    // 紧凑 token 计数（顶栏小字与明细浮层共用）：1234 → 1.2k，56000 → 56k。
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

    // ── State Status Indicator ───────────────────
    // 运行状态（Ready/思考中/执行工具…）钉在顶栏正中——顶栏即对话区宽度，
    // 这里就是对话区的视觉中心。放在布局流外（不占 RowLayout 槽位）：
    // 忙碌指示属于整个对话区，不该跟右侧 token 计量挤在一处。
    // 会话标题的最大宽度止于中线（见 sessionTitleLabel），不会从下面穿过。
    RowLayout {
        id: stateIndicator
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
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
            objectName: "topBarStateText"
            text: AgentVM.stateText
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
        }

        // 运行时长（busy 时才出现）：● 执行工具 · 1m32s。时长是"进行中"
        // 的属性，Ready/Completed 后随状态一起隐藏，空会话零噪音。
        // QML 侧记时零 C++ 改动，秒级显示精度足够；起点靠 stateChanged
        // 边沿检测（prevBusy），忙碌内部迁移（思考→执行工具）不重置。
        // TopBar 晚于任务出现（热重载/后进会话）时 startedAt=0 直接不
        // 显示——宁缺毋错。
        Text {
            id: stateElapsed
            property bool prevBusy: false
            property real startedAt: 0
            property string elapsed: ""
            visible: AgentVM.stateBusy && startedAt > 0
            text: "· " + elapsed
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
        }
        Connections {
            target: AgentVM
            function onStateChanged() {
                if (AgentVM.stateBusy && !stateElapsed.prevBusy)
                    stateElapsed.startedAt = Date.now()
                stateElapsed.prevBusy = AgentVM.stateBusy
            }
        }
        Timer {
            interval: 1000
            running: AgentVM.stateBusy && stateElapsed.startedAt > 0
            repeat: true
            triggeredOnStart: true
            onTriggered: {
                const secs = Math.max(0,
                    Math.floor((Date.now() - stateElapsed.startedAt) / 1000))
                stateElapsed.elapsed = secs < 60
                    ? String(secs) + "s"
                    : Math.floor(secs / 60) + "m"
                      + String(secs % 60).padStart(2, "0") + "s"
            }
        }
    }

    // Bottom border
    Rectangle {
        width: parent.width
        height: 1
        anchors.bottom: parent.bottom
        color: Theme.borderSubtle
    }

    // ── token 明细浮层的统一明细行（用法见下方 tokenPopup）──────
    // 四列结构：[缩进] 色块 · 标签 · token 数 · 百分比。全弹层的六类行
    // （组头/行/二级子项/剩余可用/会话方向/会话来源）共用这一套实现与
    // 列宽，保证每列跨行严格对齐：色块槽固定 7px（小号点居其中），
    // 标签列起点一致，token/百分比两列右缘锚在行尾同一条线上。
    // · 缩进只作用于行首（Item 占位），不移动右侧数字列——不同层级
    //   （组/行/子项）的 token 列仍彼此对齐，"分母 − 合计"可以竖向心算。
    // · 标签 fillWidth 承担全部弹性，空间不足先 elide 标签，两列数字
    //   恒定宽。由此产生的空隙在标签 item 内部，不会触发测试的
    //   "标签与数字间空档"断言（该断言量的是兄弟 item 边缘间距）。
    // · token 数走 root.formatTokens（紧凑 K/M，与顶栏小字同口径），
    //   百分比统一一位小数（tokenPopup.pctText）。
    component BreakdownRow: RowLayout {
        id: breakdownRow
        property string label: ""
        property real tokens: 0
        // 百分比分母：容量区默认窗口容量（maxTokens）；会话累计区传
        // totalTokens / totalInputTokens。
        property real pctBase: tokenPopup.maxTokens
        // "transparent" 时色块槽留白（剩余可用/会话方向行无色块语义）。
        property color dotColor: "transparent"
        property int dotSize: 7
        property int indent: 0
        property bool bold: false
        property color labelColor: Theme.textSecondary

        spacing: 6
        Layout.fillWidth: true

        // 色块槽固定 7×7：小号点（dotSize 4）居其中，保证标签列起点
        // 不随点的大小漂移。
        Item {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                anchors.centerIn: parent
                width: breakdownRow.dotSize
                height: breakdownRow.dotSize
                radius: Math.max(1, Math.round(breakdownRow.dotSize / 2) - 1)
                color: breakdownRow.dotColor
            }
        }
        Text {
            Layout.fillWidth: true
            text: breakdownRow.label
            color: breakdownRow.labelColor
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
            font.bold: breakdownRow.bold
            elide: Text.ElideRight
        }
        Text {
            Layout.preferredWidth: tokenPopup.colNumW
            horizontalAlignment: Text.AlignRight
            text: root.formatTokens(breakdownRow.tokens)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
        }
        Text {
            Layout.preferredWidth: tokenPopup.colPctW
            horizontalAlignment: Text.AlignRight
            text: tokenPopup.pctText(breakdownRow.tokens,
                                     breakdownRow.pctBase) + "%"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 0

        // ── Sidebar Toggle ───────────────────────────
        // 全局唯一的侧栏开关（原先浮在侧栏右缘的圆形把手已移除）。
        // 图标随状态切换：展开态画"侧栏填实 + 向内箭头"（点它=收起），
        // 收起态画"侧栏留空 + 向外箭头"（点它=展开）。
        IconButton {
            id: sidebarToggle
            icon: AgentVM.isSidebarExpanded ? "sidebar-collapse" : "sidebar-expand"
            iconSize: 20
            restIconColor: Theme.textPrimary
            // 两个面互为正反，翻转一下能看出"这是同一个开关的两态"。
            flipOnIconChange: true
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
                // 再点一次关闭：必须在 onPressed 判断。项目弹层是
                // CloseOnReleaseOutside，鼠标释放时它先把弹层关掉，
                // onClicked/onReleased 读到的 opened 恒为 false，toggle 会又打开。
                onPressed: projectPopup.opened ? projectPopup.close() : projectPopup.open()
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
                width: 380
                height: projectContent.implicitHeight + 12
                padding: 6
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside

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
                                        font.pixelSize: Theme.fontSizeMicro
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

        // ── 当前会话标题（docs/30）───────────────────
        // 标签栏删除后，这里是**唯一**常显的会话标题位：侧栏只有高亮没有文字
        // 归属，用户在多条会话间切换时需要一眼确认"我在看哪条"。
        // 双重截断：先按字数截到 10 字 + "…"（顶栏只做"我在哪"的定位，
        // 不做全文阅读），宽度 elide 仅作窄窗口兜底。截断时悬停看全文
        // （与侧栏同一口径）。最大宽度到顶栏中线为止：运行状态钉在正中
        // （见 root 下方的 stateIndicator），标题不能再越过中线跟它抢位；
        // busy 时状态行还带时长（● 执行工具 · 1m32s），在中线两侧各伸约
        // 68px，因此预留 96 而不是 16。
        Text {
            id: sessionTitleLabel
            objectName: "topBarSessionTitle"
            // 标题可用起点：RowLayout 左内边距(8) + 侧栏按钮宽度 + 标题
            // 左内边距(12)；项目徽标可见时用它的布局几何，避免读自身 x
            // 形成 `width -> x -> width` 绑定环。
            readonly property real leadingEdge: projectBadge.visible
                ? projectBadge.x + projectBadge.width + 12
                : 8 + sidebarToggle.width + 12
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.maximumWidth: Math.max(80, root.width / 2 - leadingEdge - 96)
            // 完整标题：ToolTip 与无障碍名称取它，展示层才做字数截断。
            readonly property string fullTitle: AgentVM.currentSessionId.length > 0
                ? (chatTitle || qsTr("新会话"))
                : qsTr("新对话")
            // 字数上限 10：中英文一视同仁按 code unit 数（标题是短标签，
            // 不必按显示宽度折算省/全角）。超出部分以 "…" 收尾。
            text: fullTitle.length > 10
                ? fullTitle.slice(0, 10) + "…" : fullTitle
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter

            // 标题不在 AgentVM 里单独暴露（tabModel 才有），从会话列表按 id 找；
            // 列表变化时绑定自动重算。
            readonly property string chatTitle: {
                const list = AgentVM.sessionList
                const id = AgentVM.currentSessionId
                if (id.length === 0) return ""
                for (let i = 0; i < list.length; ++i)
                    if (list[i].id === id) return list[i].title || ""
                return ""
            }

            HoverHandler { id: titleHover }
            // 两种截断任一发生都给全文：字数截断（text ≠ fullTitle）或
            // 窄窗口宽度 elide（truncated）。
            ToolTip.visible: titleHover.hovered
                             && (sessionTitleLabel.truncated
                                 || sessionTitleLabel.text !== fullTitle)
            ToolTip.delay: 600
            ToolTip.text: fullTitle
            Accessible.role: Accessible.StaticText
            Accessible.name: fullTitle
        }

        // 模板入口已移除：侧栏窄条与聊天空态仍有入口，TopBar 这份属冗余。

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
            // 测试锚点：悬停浮层的可用性回归用例通过它定位触发器并取 popup。
            objectName: "topBarTokenArea"
            spacing: 6
            Layout.rightMargin: 8

            // 窗口占用色阶：占用条与百分比文字同源，≥80% 转警示色、
            // ≥90% 转危险色，一眼可读，无需数字细节。
            // S1: 文字/条/配色共用一个取整后的百分比——旧实现文字走
            // Math.round(rate*100)、配色走未取整的 rate，79.6% 会显示
            // "80%" 却仍是主色。
            readonly property int usagePct: AgentVM.maxTokens > 0
                ? Math.min(100, Math.round(AgentVM.usedTokens * 100 / AgentVM.maxTokens))
                : 0
            // 窄宽度下退化，避免右侧计量块顶掉居中的运行状态指示器——
            // 指示器锚在窗口正中、不占布局流，右侧内容变宽只会从它下面
            // 穿过去。阈值 1200 = main.qml 的默认窗口宽：全量形态（占用
            // 绝对值/分母 + 已压缩角标 + ¥/轮）按最坏组合（busy 时长 +
            // 角标 + 累计 + 费用 + 5 个按钮）约 537px，与中线 ±80px 的
            // 状态行在 ~1234px 以下开始相撞；退化后（占用只留百分比、
            // 角标与费用收进浮层）右侧约 314px，到窗口最小宽度 900 仍有
            // 余量。历史注记：旧值 900 与 minimumWidth 相等，分支永不
            // 生效（死分支）。
            readonly property bool wideEnough: root.width >= 1200
            readonly property color usageColor:
                usagePct >= 90 ? Theme.statusDanger
                : usagePct >= 80 ? Theme.statusWarning
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

            // docs/16 §8.2: 窗口占用 = 绝对值 / 分母 · 百分比。只有百分比
            // 时用户看不出"从 45k 涨到 46k"，也看不出分母是哪个模型的窗口；
            // 明细仍在浮层里。
            Text {
                visible: AgentVM.maxTokens > 0
                text: tokenArea.wideEnough
                    ? qsTr("%1 / %2 · %3%")
                          .arg(formatTokens(AgentVM.usedTokens))
                          .arg(formatTokens(AgentVM.maxTokens))
                          .arg(tokenArea.usagePct)
                    : tokenArea.usagePct + "%"
                color: tokenArea.usageColor
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                font.bold: tokenArea.usagePct >= 80
            }

            // docs/16 §8.3（S1）: 历史被压缩过时常驻一个小角标——压缩本身只在
            // trace 里留节点，用户看到百分比从 95% 掉到 30% 会以为数据出错。
            Row {
                visible: tokenArea.wideEnough
                         && (AgentVM.contextCompositionMap.compressionCount || 0) > 0
                spacing: 3

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "archive"
                    size: 12
                    iconColor: Theme.statusWarning
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("已压缩")
                    color: Theme.statusWarning
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
                HoverHandler { id: compressHover }
                ToolTip.visible: compressHover.hovered
                ToolTip.delay: 400
                ToolTip.text: {
                    const c = AgentVM.contextCompositionMap
                    return qsTr("历史已压缩 %1 次 · 累计省略 %2 条消息%3")
                        .arg(c.compressionRounds || 0)
                        .arg(c.omittedMessages || 0)
                        .arg(c.contextTruncated ? qsTr("（含原地截断）") : "")
                }
            }

            // 模型短名已移除：模型归属在设置页/模型选择器全量可见（provider
            // · id），顶栏这一份没有实际作用；分母口径由浮层标题行交代。
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

            // docs/16 §8.2: per-request cost (only shown when non-zero)。
            // 窄宽度下收进浮层（footer 永远可见）——它是 wideEnough 退化
            // 算术的一部分，见上。
            Text {
                visible: tokenArea.wideEnough && AgentVM.lastCostCents > 0
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
            objectName: "topBarTokenPopup"
            parent: tokenArea
            // 不用 Layout 属性——Popup 不是 RowLayout 的 item，Layout.* 在
            // 这里无效（qmllint 已告警）。y 在触发器下方。
            // x 右对齐触发器并钳制在窗口内：浮层有固定宽度，窄窗口下单纯的
            // `tokenArea.width - width` 会把浮层推到左边界之外，左边那截标签
            // 就看不见了。
            readonly property real areaXInRoot: tokenArea.mapToItem(root, 0, 0).x
            x: Math.max(8 - areaXInRoot,
                        Math.min(tokenArea.width - width,
                                 root.width - areaXInRoot - width - 8))
            y: tokenArea.height + 8
            // 宽度跟字号缩放（字号 11px 时 = 300）：300 容得下最宽的
            // "缩进 + 色块 + 标签 + token 列 + 百分比列"，又不留空档。
            // 字号调大时按比例放大（fontScale），列宽/缩进与弹层宽度同一
            // 缩放源（见下方 colNumW/colPctW/indent*），避免文字变大后被压窄。
            // 极窄窗口下退让到窗口可用宽度，避免贴边溢出。
            width: Math.min(Math.round(300 * fontScale),
                            Math.max(200, root.width - 16))
            padding: 10
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            background: DialogBackground {}

            // S1: composition 改为绑定（随 tokenUsageChanged 自动刷新）；会话
            // 累计仍是 SQLite 聚合，只在打开浮层时查一次。
            property var composition: AgentVM.contextCompositionMap
            property var sessionTotals: ({})

            function refreshAndOpen() {
                sessionTotals = AgentVM.sessionTokenBreakdown()
                open()
            }

            readonly property int totalTokens: composition.totalTokens || 0
            readonly property int maxTokens: composition.maxTokens || 0

            // 列宽/缩进与弹层宽度共用同一字号缩放源：字号调大时数字列等比
            // 放大，"45.2k"（5 字符）与 "100.0%"（6 字符）在 mono 下的宽度
            // 预算不会被压破。
            readonly property real fontScale: Math.max(1.0, Theme.fontSizeCaption / 11)
            // token 列 40px：紧凑格式最长 5 字符（"45.2k"/"12.3M"）。
            readonly property int colNumW: Math.round(40 * fontScale)
            // 百分比列 42px：最长 6 字符（"100.0%"）。
            readonly property int colPctW: Math.round(42 * fontScale)
            // 缩进只作用于行首色块+标签（BreakdownRow 内 Item 占位），
            // token/百分比两列不随层级移动。取值与旧版视觉缩进一致：
            // 组 0 / 行 10 / 子项 32。
            readonly property int indentRow: Math.round(10 * fontScale)
            readonly property int indentChild: Math.round(32 * fontScale)

            // Non-zero buckets in capacity order; colors match the stacked
            // bar segments. Percentages are shares of the window capacity.
            // S1: 后端只返回行 key + tokens（不含翻译），文案/配色集中在这里，
            // 便于 lupdate 扫描与主题统一。二级子项按 "父.子" 命名，取色时
            // 只认父键，保证同一族的色块一致。
            function labelFor(key) {
                switch (key) {
                case "instructions":           return qsTr("指令")
                case "tools":                  return qsTr("工具")
                case "context":                return qsTr("素材与会话")
                case "systemPrompt":           return qsTr("系统提示词")
                case "skills":                 return qsTr("技能")
                case "memory":                 return qsTr("记忆")
                case "memory.episodic":        return qsTr("历史摘要")
                case "memory.knowledge":       return qsTr("知识库命中")
                case "projectRules":           return qsTr("项目规则")
                case "runtimeState":           return qsTr("运行状态")
                case "compressed":             return qsTr("压缩标记")
                case "toolsSchema":            return qsTr("工具定义")
                case "toolsSchema.builtin":    return qsTr("内置工具")
                case "toolsSchema.mcp":        return qsTr("MCP 工具")
                case "toolCalls":              return qsTr("工具调用")
                case "toolResults":            return qsTr("工具结果")
                case "tool.fileRead":          return qsTr("文件读取")
                case "tool.command":           return qsTr("命令执行")
                case "tool.search":            return qsTr("检索/网络")
                case "tool.git":               return qsTr("Git 查询")
                case "tool.subagent":          return qsTr("子代理报告")
                case "tool.knowledge":         return qsTr("知识库")
                case "tool.mcp":               return qsTr("MCP 结果")
                case "tool.other":             return qsTr("其他工具")
                case "files":                  return qsTr("文件附件")
                case "images":                 return qsTr("图片")
                case "conversation":           return qsTr("会话内容")
                case "conversation.user":      return qsTr("用户")
                case "conversation.assistant": return qsTr("助手")
                }
                return key
            }
            function colorFor(key) {
                const base = String(key).split(".")[0]
                switch (base) {
                case "instructions": return Theme.textSecondary
                case "systemPrompt": return Theme.textSecondary
                case "skills":       return Theme.textTertiary
                case "memory":       return Theme.brandPrimary
                case "projectRules": return Theme.textTertiary
                case "runtimeState": return Theme.textTertiary
                case "compressed":   return Theme.statusWarning
                case "tools":        return Theme.statusWarning
                case "toolsSchema":  return Theme.statusWarning
                case "toolCalls":    return Theme.textTertiary
                case "toolResults":  return Theme.statusSuccess
                case "context":      return Theme.brandPrimary
                case "conversation": return Theme.brandPrimary
                case "files":        return Theme.textTertiary
                case "images":       return Theme.statusDanger
                }
                return Theme.textTertiary
            }

            // 只渲染非零行/非零子项：十二行里通常一半是 0，全画出来只会
            // 淹没真正的大头。分组小计仍用后端给的 tokens（= 子项之和）。
            readonly property var visibleGroups: {
                const groups = composition.groups || []
                const out = []
                for (let i = 0; i < groups.length; ++i) {
                    const g = groups[i]
                    const rows = []
                    const srcRows = g.rows || []
                    for (let j = 0; j < srcRows.length; ++j) {
                        const r = srcRows[j]
                        if ((r.tokens || 0) <= 0) continue
                        const kids = []
                        const srcKids = r.children || []
                        for (let k = 0; k < srcKids.length; ++k)
                            if ((srcKids[k].tokens || 0) > 0) kids.push(srcKids[k])
                        rows.push({ key: r.key, tokens: r.tokens, children: kids })
                    }
                    if (rows.length > 0)
                        out.push({ key: g.key, tokens: g.tokens, rows: rows })
                }
                return out
            }

            // 容量条按叶子行分段（有子项就用子项），颜色与图例同源。
            readonly property var segments: {
                const out = []
                const groups = visibleGroups
                for (let i = 0; i < groups.length; ++i) {
                    const rows = groups[i].rows
                    for (let j = 0; j < rows.length; ++j) {
                        const kids = rows[j].children || []
                        if (kids.length > 0) {
                            for (let k = 0; k < kids.length; ++k)
                                out.push({ label: labelFor(kids[k].key),
                                           tokens: kids[k].tokens,
                                           color: colorFor(kids[k].key) })
                        } else {
                            out.push({ label: labelFor(rows[j].key),
                                       tokens: rows[j].tokens,
                                       color: colorFor(rows[j].key) })
                        }
                    }
                }
                return out
            }
            // 百分比统一一位小数（不含 % 号，调用处拼接）：0.1% 也能读出
            // 量级，各行竖向相加可核对到 ~0.1%。base 为分母（窗口容量或
            // 会话累计总额/输入总额），无分母时回 "0.0"。
            function pctText(tokens, base) {
                if (!(base > 0)) return "0.0"
                return Math.min(100, Math.max(0, tokens * 100 / base)).toFixed(1)
            }

            readonly property bool hasSessionTotals:
                (sessionTotals.totalCalls || 0) > 0

            // 会话累计摘要行：总额 + 输入/输出/命中率，同统计页口径。
            readonly property string sessionSummary: {
                const t = sessionTotals
                const total = t.totalTokens || 0
                const rate = t.cacheHitRate || 0
                // 口径说明不写进标题：会话累计本来就含子代理、本来就是
                // provider 真实值，写出来只是占宽度。真实/估算的差异放在
                // 上方容量区（那里才有被误读的风险）。
                return qsTr("会话累计 · %1 tokens · 命中率 %2%")
                    .arg(root.formatTokens(total))
                    .arg(Math.min(100, rate * 100).toFixed(1))
            }

            // 输入(未命中)/缓存命中/输出：与统计页方向卡同一互斥口径。
            // 百分比由行组件现算（pctBase = 会话累计总额），模型只留标签与数值。
            readonly property var sessionDirectionRows: {
                const t = sessionTotals
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
                        out.push({ label: rows[i][0], tokens: rows[i][1] })
                }
                return out
            }

            // 来源拆分（输入侧）：只列非零项，按占比降序。
            readonly property var sessionSourceRows: {
                const t = sessionTotals
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
                        rows.push({ label: defs[i][1], tokens: v })
                }
                rows.sort(function(a, b) { return b.tokens - a.tokens })
                return rows
            }

            // S3: 行数随技能/记忆/工具分类增长，无限高会在小屏把整个窗口撑爆，
            // 因此用 ScrollView 兜住上限（超过 400 再滚动）。
            //
            // 高度必须由 contentItem 自己的 implicitHeight 决定：把
            // contentHeight 绑到 contentItem.implicitHeight 会形成
            // "ScrollView 尺寸 ↔ Popup.contentHeight" 的绑定环，实测取到 0，
            // Popup 高度退化成 padding×2，内容整块被裁掉（悬停看起来"没反应"）。
            contentItem: ScrollView {
                id: tokenPopupScroll
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                // Popup 只用 implicitWidth/implicitHeight 决定内容区尺寸，而
                // ScrollView 的 implicitWidth 是 0（它不派生自内容宽度）——
                // 结果内容区宽度恒为 0，RowLayout 的 fillWidth 全部退化成 0，
                // 标签与数字挤在一起，中间留出一大片空白。
                implicitWidth: tokenPopup.availableWidth
                implicitHeight: Math.min(tokenPopupContent.implicitHeight, 400)

                contentWidth: availableWidth

                ColumnLayout {
                    id: tokenPopupContent
                    objectName: "topBarTokenPopupContent"
                    // ScrollView 的可用宽度要等布局后才确定，直接绑
                    // parent.width 在首帧是 0；用 availableWidth 让内容随
                    // 弹出框宽度收敛。
                    width: tokenPopupScroll.availableWidth
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
                    // S3: 明确"估算"——窗口占用是启发式估算 × 滚动校准，与会话
                    // 累计（provider 真实值）不是同一口径，不标出来就会被当成
                    // 两个互相矛盾的数字。数字格式与图例行同口径：紧凑 K/M +
                    // 一位小数百分比。
                    text: tokenPopup.maxTokens > 0
                        ? qsTr("上下文构成 · %1 / %2 · %3%").arg(
                              root.formatTokens(tokenPopup.totalTokens))
                              .arg(root.formatTokens(tokenPopup.maxTokens))
                              .arg(tokenPopup.pctText(tokenPopup.totalTokens,
                                                      tokenPopup.maxTokens))
                        : qsTr("上下文构成 · %1").arg(root.formatTokens(tokenPopup.totalTokens))
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                // 校准系数只在真正观测过（≠1.0）时出现；避免给未校准的会话
                // 增加一行噪音。
                Text {
                    readonly property real scale:
                        tokenPopup.composition.estimateScale || 1.0
                    visible: tokenPopup.segments.length > 0
                             && Math.abs(scale - 1.0) >= 0.005
                    text: qsTr("已按实测校准 ×%1").arg(scale.toFixed(2))
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
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

                // ── Legend（S1）: 组 → 行 → 二级子项 ──
                //   组/行小计来自后端（= 子项之和），百分比是"占窗口容量"，
                //   与上方容量条同一分母，用户可以直接相加核对。行样式统一
                //   走文件根上的 BreakdownRow：四列跨行对齐，token 数紧凑
                //   K/M 格式，百分比一位小数。
                Repeater {
                    model: tokenPopup.visibleGroups
                    delegate: ColumnLayout {
                        id: groupDelegate
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.topMargin: 3
                        spacing: 2

                        BreakdownRow {
                            label: tokenPopup.labelFor(groupDelegate.modelData.key)
                            tokens: groupDelegate.modelData.tokens || 0
                            dotColor: tokenPopup.colorFor(groupDelegate.modelData.key)
                            bold: true
                        }

                        Repeater {
                            model: groupDelegate.modelData.rows
                            delegate: ColumnLayout {
                                id: rowDelegate
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2

                                BreakdownRow {
                                    label: tokenPopup.labelFor(rowDelegate.modelData.key)
                                    tokens: rowDelegate.modelData.tokens || 0
                                    dotColor: tokenPopup.colorFor(rowDelegate.modelData.key)
                                    labelColor: Theme.textPrimary
                                    indent: tokenPopup.indentRow
                                }

                                // 二级子项（工具结果按工具分类 / 工具定义拆内置·MCP /
                                // 会话内容拆用户·助手）：只有父行展开时才值得看。
                                // 缩进由 BreakdownRow 的 indent 承担（只推行首
                                // 色块+标签，数字列留在全局同缘）。
                                Repeater {
                                    model: rowDelegate.modelData.children || []
                                    delegate: BreakdownRow {
                                        required property var modelData
                                        label: tokenPopup.labelFor(modelData.key)
                                        tokens: modelData.tokens || 0
                                        dotColor: tokenPopup.colorFor(modelData.key)
                                        dotSize: 4
                                        indent: tokenPopup.indentChild
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Free capacity row ──
                // 无色块语义：dotColor 缺省透明，只留 7px 槽与图例列对齐。
                // 数字与"分母 − 合计"可竖向核对。
                BreakdownRow {
                    visible: tokenPopup.segments.length > 0
                             && tokenPopup.maxTokens > 0
                             && tokenPopup.maxTokens > tokenPopup.totalTokens
                    label: qsTr("剩余可用")
                    tokens: tokenPopup.maxTokens - tokenPopup.totalTokens
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
                // 百分比分母 = 会话累计总额（pctBase）。
                Repeater {
                    model: tokenPopup.sessionTotals.totalCalls > 0
                        ? tokenPopup.sessionDirectionRows : []
                    delegate: BreakdownRow {
                        required property var modelData
                        label: modelData.label
                        tokens: modelData.tokens
                        pctBase: tokenPopup.sessionTotals.totalTokens || 0
                        labelColor: Theme.textPrimary
                    }
                }

                // 来源拆分：系统提示词/对话/工具调用/图片等累计占比。
                // 百分比分母 = 输入总额（pctBase），小号点与图例子项同规格。
                Repeater {
                    model: tokenPopup.sessionSourceRows
                    delegate: BreakdownRow {
                        required property var modelData
                        label: modelData.label
                        tokens: modelData.tokens
                        pctBase: tokenPopup.sessionTotals.totalInputTokens || 0
                        dotColor: Theme.textSecondary
                        dotSize: 4
                        labelColor: Theme.textPrimary
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
        }

        // ── Process Toggle ────────────────────────────
        // 折叠/展开过程节点（思考、工具调用、工具结果、context）。
        //
        // 原先这个开关是悬浮在对话区右上角的胶囊按钮：它控制的是整个会话
        // 的显示方式，却摆得像"某条消息的展开按钮"；而且浮在滚动区之上，
        // 窄窗口下正好压住助手正文。挪到这里与任务清单/审阅/面板同排——
        // 它们都是"会话级视图开关"，语义一致且永不遮挡内容。
        //
        // 只在有过程节点时出现：空会话里它点了没有任何效果。
        IconButton {
            objectName: "processToggleButton"
            readonly property int processCount: AgentVM.intermediateCount
            visible: processCount > 0
            icon: AgentVM.showProcess ? "process-collapse" : "process-expand"
            iconSize: 18
            restIconColor: Theme.textPrimary
            flipOnIconChange: true
            tooltip: AgentVM.showProcess
                ? qsTr("折叠思考与工具调用过程（%1 项）").arg(processCount)
                : qsTr("显示思考与工具调用过程（%1 项）").arg(processCount)
            onClicked: AgentVM.showProcess = !AgentVM.showProcess
        }

        // ── Task List Toggle ──────────────────────────
        // The list itself lives in TodoDialog (main.qml) — the sidebar was
        // too narrow for it. The badge counts tasks that are not finished
        // yet, so an in-flight plan is visible without opening anything.
        // The button stays available while any list exists (including an
        // all-completed one): the plan is part of the session record and must
        // stay reviewable after the badge count drops to zero.
        // Reads AgentVM.todoList directly: TopBar cannot see the dialog
        // instance, and this way the count follows todoListChanged.
        IconButton {
            id: todoToggle
            readonly property int todoCount: {
                const list = AgentVM.todoList
                return list ? list.length : 0
            }
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
            visible: todoCount > 0
            tooltip: unfinishedCount > 0
                ? qsTr("任务清单（%1 项未完成）").arg(unfinishedCount)
                : qsTr("任务清单（全部完成）")
            Accessible.name: unfinishedCount > 0
                ? qsTr("打开任务清单，%1 项未完成").arg(unfinishedCount)
                : qsTr("打开任务清单，全部完成")
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
                    font.pixelSize: Theme.fontSizeNano
                    font.family: Theme.fontMono
                    font.bold: true
                }
            }
        }

        // ── Background Sub-agents (docs/35 P4.3) ───────
        // 子代理功能开启即显示（不再只限「有进行中任务」）：面板同时是
        // 已结束任务的查看入口，只按 activeCount 显隐会让任务一完成入口
        // 就消失，用户再也点不开历史报告。角标仍只表示进行中的数量。
        IconButton {
            id: backgroundAgentsToggle
            objectName: "backgroundAgentsToggle"
            readonly property int activeCount: AgentVM.backgroundSubAgentActiveCount

            visible: Settings.subagentEnabled || activeCount > 0
            icon: "agents"
            iconSize: 16
            restIconColor: activeCount > 0 ? Theme.brandPrimary : Theme.textPrimary
            tooltip: activeCount > 0
                ? qsTr("后台子代理（%1 个进行中）").arg(activeCount)
                : qsTr("后台子代理面板")
            Accessible.name: activeCount > 0
                ? qsTr("打开后台子代理面板，%1 个进行中").arg(activeCount)
                : qsTr("打开后台子代理面板")
            onClicked: backgroundTasksDialog.open()

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 1
                anchors.topMargin: 1
                visible: backgroundAgentsToggle.activeCount > 0
                width: Math.max(13, backgroundBadgeLabel.implicitWidth + 6)
                height: 13
                radius: 7
                color: Theme.statusWarning

                Text {
                    id: backgroundBadgeLabel
                    anchors.centerIn: parent
                    text: backgroundAgentsToggle.activeCount > 99
                        ? "99+" : backgroundAgentsToggle.activeCount
                    color: Theme.bgPrimary
                    font.pixelSize: Theme.fontSizeNano
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
                    font.pixelSize: Theme.fontSizeNano
                    font.family: Theme.fontMono
                    font.bold: true
                }
            }
        }

        // ── Panel Toggle Button ──────────────────────
        // 与左侧侧栏按钮同一套视觉语法（窗口轮廓 + 面板分隔线 + 面板本体
        // 填实/留空 + 指向箭头），只是镜像到右边。收起态加一层细边框，
        // 让"这里有东西可以展开"比纯靠图标更容易被发现。
        IconButton {
            icon: AgentVM.isPanelVisible ? "panel-collapse" : "panel-expand"
            // 与左侧侧栏按钮同为 20：两个开关是同一类控件，尺寸不该
            // 因为历史原因（旧的 chevron 偏小）而不一致。
            iconSize: 20
            restIconColor: Theme.textPrimary
            // 与左侧一致：两侧是同类控件，动效也不该一边有一边没有。
            flipOnIconChange: true
            border.width: AgentVM.isPanelVisible ? 0 : 1
            border.color: Theme.borderSubtle
            tooltip: AgentVM.isPanelVisible
                ? qsTr("收起右侧面板 (Ctrl+])") : qsTr("展开右侧面板 (Ctrl+])")
            onClicked: AgentVM.togglePanel()
        }

    }
}
