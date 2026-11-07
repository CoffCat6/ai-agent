import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// docs/35 P4.3: 后台子代理面板。
//
// 为什么是独立对话框而不是塞进右栏：后台任务属于**会话级**内容（本地会话
// 也可能有），而右栏 ContextPanel 只服务项目级内容；也不只靠 trace 卡片，
// 否则任务一多就要在长会话里翻找。沿用 TodoDialog 的宿主契约：
//   * main.qml 声明实例并设置 `parent: Overlay.overlay`；
//   * 数据由宿主注入：`tasks: AgentVM.backgroundSubAgents`（实时绑定）；
//   * 打开方式：`backgroundTasksDialog.open()`。
//
// 2026-11-03 两栏改版：
//   * 左栏 = 代理栏（进行中/排队/已结束分组 + 实时计数），点选即切换；
//   * 右栏 = 选中任务的详情：状态/等待或耗时/步数/提案 + 实时进度，
//     结束后显示完整报告。
//
// 关键回归：模型更新不能重置 ListView。`tasks` 是整表替换的 QVariantList，
// 直接绑定会让每个 250ms 的面板刷新重建 delegate——展开状态丢失、按下的
// 按钮在释放前被销毁（表现为「查看/停止点了没反应」）。这里用本地
// ListModel 按 id 合并（set/move），delegate 稳定，选择与点击都不再被打断。
Dialog {
    id: dialog

    property var tasks: []
    // 动作门面（停止/报告/清除）。可覆写的原因同 ProcessCard：本组件在
    // NexusAI 模块内被 qmlcachegen 预编译，`AgentVM` 在编译期解析为
    // QML_SINGLETON 类型，宿主 context property 遮蔽不了它；测试通过
    // 该属性注入桩来断言按钮真的调了门面。默认值仍是应用单例。
    property var agentFacade: AgentVM
    // 实时耗时：运行/排队中的任务按本地时钟推进，不依赖后端刷新频率。
    property double clockMs: Date.now()

    // 选中任务（右栏）。按 id 保存，列表更新/重排不会丢选择。
    property string selectedId: ""
    property string selectedReport: ""
    // docs/43 O4-3: 选中终态任务的 findings 逐条数据（refreshReport 按需
    // 查询，快照 250ms 刷新只带 count 不携带全文）。
    property var findings: []
    // 右栏页签：0 = 实时进度，1 = 完整报告。运行中只看进度，完成后可切换。
    property int detailTab: 0

    readonly property int taskCount: tasks ? tasks.length : 0

    function isTerminal(state) {
        return state === "completed" || state === "failed"
                || state === "cancelled"
    }
    function isActiveState(state) {
        return state === "queued" || state === "running"
                || state === "cancelling"
    }
    // 面板展示状态：hub 里一个等待槽位的任务状态仍是 running（池线程已
    // 启动，acquireSlot 在排队），cancelling 是「已请求停止、Runner 尚未
    // 收尾」。两者都要在界面上单独可见。
    function displayState(task) {
        if (!task) return ""
        const s = String(task.state || "")
        if (isTerminal(s)) return s
        if (task.cancelling === true) return "cancelling"
        if (s === "queued" || task.slotQueued === true) return "queued"
        return "running"
    }
    function stateLabel(state) {
        switch (state) {
        case "queued": return qsTr("排队中")
        case "running": return qsTr("运行中")
        case "cancelling": return qsTr("正在停止…")
        case "completed": return qsTr("已完成")
        case "failed": return qsTr("失败")
        case "cancelled": return qsTr("已取消")
        default: return String(state || "")
        }
    }
    function stateColor(state) {
        switch (state) {
        case "queued": return Theme.statusWarning
        case "running": return Theme.brandPrimary
        case "cancelling": return Theme.statusWarning
        case "completed": return Theme.statusSuccess
        case "failed": return Theme.statusDanger
        case "cancelled": return Theme.textSecondary
        default: return Theme.textSecondary
        }
    }
    function countDisplay(want) {
        let n = 0
        for (let i = 0; i < dialog.taskCount; ++i)
            if (dialog.displayState(dialog.tasks[i]) === want) ++n
        return n
    }
    readonly property int runningCount: countDisplay("running")
                                                    + countDisplay("cancelling")
    readonly property int queuedCount: countDisplay("queued")
    readonly property int cancellingCount: countDisplay("cancelling")
    readonly property int finishedCount: {
        let n = 0
        for (let i = 0; i < dialog.taskCount; ++i)
            if (dialog.isTerminal(dialog.displayState(dialog.tasks[i]))) ++n
        return n
    }
    readonly property int activeCount: runningCount + queuedCount
    readonly property bool hasTasks: taskCount > 0

    function findTask(id) {
        for (let i = 0; i < dialog.taskCount; ++i) {
            if (String(dialog.tasks[i].id) === id) return dialog.tasks[i]
        }
        return null
    }
    readonly property var selectedTask: dialog.findTask(dialog.selectedId)

    function elapsedTextFor(display, startedAt, elapsedMs) {
        const active = dialog.isActiveState(display)
        const ms = active
            ? Math.max(0, dialog.clockMs - Number(startedAt || 0))
            : Number(elapsedMs || 0)
        const secs = Math.floor(ms / 1000)
        if (secs < 60) return qsTr("%1 秒").arg(secs)
        const mins = Math.floor(secs / 60)
        if (mins < 60) return qsTr("%1 分 %2 秒").arg(mins).arg(secs % 60)
        return qsTr("%1 时 %2 分").arg(Math.floor(mins / 60)).arg(mins % 60)
    }
    function elapsedText(task) {
        if (!task) return ""
        return dialog.elapsedTextFor(dialog.displayState(task),
                                     task.startedAt, task.elapsedMs)
    }
    function stepsText(task) {
        if (!task) return "0"
        const steps = Number(task.steps || 0)
        const maxSteps = Number(task.maxSteps || 0)
        return maxSteps > 0 ? qsTr("%1 / %2").arg(steps).arg(maxSteps)
                            : String(steps)
    }
    // docs/43 O4: cost is only shown when the provider reported classified
    // usage; otherwise fall back to the aggregate token count (never show
    // zeros as "free").
    function usageText(task) {
        if (!task) return ""
        const usage = task.usage || {}
        const tokens = Number(task.tokensUsed || 0)
        const model = String(usage.model || "")
        let text = model !== "" ? model + " · " : ""
        if (usage.reported === true) {
            text += qsTr("输入 %1 / 输出 %2")
                        .arg(Number(usage.input || 0))
                        .arg(Number(usage.output || 0))
            const cached = Number(usage.cached || 0)
            if (cached > 0) text += qsTr(" / 缓存 %1").arg(cached)
            // B4: 批量/多次尝试里只有部分任务上报了分类用量时明确标注，
            // 避免把未上报项的 0 当成「免费」。
            if (usage.partial === true) text += qsTr(" · 部分上报")
        } else {
            // B9: 估算回退（provider 完全没给 usage）用「≈」标明，避免
            // 读者把启发式估算当成真实计量。
            text += usage.estimated === true
                ? qsTr("≈ tokens %1").arg(tokens)
                : qsTr("tokens %1").arg(tokens)
        }
        return text
    }
    // docs/43 O4-3: severity 着色与过程卡片同口径。
    function severityColor(severity) {
        if (severity === "critical" || severity === "high")
            return Theme.statusDanger
        if (severity === "medium") return Theme.statusWarning
        return Theme.textTertiary
    }
    function groupRank(display) {        if (display === "running" || display === "cancelling") return 0
        if (display === "queued") return 1
        return 2
    }
    function groupFor(task) {
        return ["active", "queued", "finished"][dialog.groupRank(
            dialog.displayState(task))]
    }
    function sectionLabel(group) {
        switch (group) {
        case "active": return qsTr("进行中 · %1").arg(dialog.runningCount)
        case "queued": return qsTr("排队中 · %1").arg(dialog.queuedCount)
        case "finished": return qsTr("已结束 · %1").arg(dialog.finishedCount)
        default: return String(group || "")
        }
    }
    // 新事件在前：进度表只保留最近 24 条，从尾部倒序展示最新一条无需滚动。
    function selectedProgressNewestFirst() {
        const task = dialog.selectedTask
        if (!task || !task.progressTail) return []
        const tail = task.progressTail
        const out = []
        for (let i = tail.length - 1; i >= 0; --i) out.push(tail[i])
        return out
    }
    function phaseLabel(phase) {
        switch (String(phase || "")) {
        case "thinking": return qsTr("思考")
        case "tools": return qsTr("工具")
        case "tool": return qsTr("工具")
        case "assistant": return qsTr("输出")
        case "stream": return qsTr("流式")
        case "reasoning": return qsTr("推理")
        case "context": return qsTr("压缩")
        case "queued": return qsTr("排队")
        default: return String(phase || "")
        }
    }

    // ── 稳定的列表镜像 ────────────────────────────────
    ListModel { id: taskModel }

    function rowFor(task) {
        const display = dialog.displayState(task)
        return {
            taskId: String(task.id || ""),
            group: dialog.groupFor(task),
            goal: String(task.goal || ""),
            role: String(task.role || "explore"),
            displayState: display,
            startedAt: Number(task.startedAt || 0),
            elapsedMs: Number(task.elapsedMs || 0),
            steps: Number(task.steps || 0),
            maxSteps: Number(task.maxSteps || 0),
            proposalCount: Number(task.proposalCount || 0),
            tokensUsed: Number(task.tokensUsed || 0),
            findingCount: Number(task.findingCount || 0),
            stopCode: String(task.stopCode || ""),
            error: String(task.error || "")
            // progressTail / cancelling 等复杂或派生字段不进 ListModel：
            // QVariantList/JS array 的 role 类型会在 append/set 之间冲突
            // （"different type [VariantMap -> List]"）；详情区直接读
            // dialog.selectedTask 的原始数据。
        }
    }

    function syncTasks() {
        if (!dialog.tasks) return
        const byId = ({})
        for (let i = 0; i < dialog.taskCount; ++i)
            byId[String(dialog.tasks[i].id)] = dialog.tasks[i]

        // 1) 删除已消失的任务
        for (let i = taskModel.count - 1; i >= 0; --i) {
            if (!(String(taskModel.get(i).taskId) in byId))
                taskModel.remove(i)
        }
        // 2) 新增/原地更新（不重建 delegate）
        for (let i = 0; i < dialog.taskCount; ++i) {
            const task = dialog.tasks[i]
            const id = String(task.id)
            let idx = -1
            for (let j = 0; j < taskModel.count; ++j) {
                if (String(taskModel.get(j).taskId) === id) { idx = j; break }
            }
            if (idx < 0) taskModel.append(dialog.rowFor(task))
            else taskModel.set(idx, dialog.rowFor(task))
        }
        // 3) 分组排序：进行中 → 排队 → 已结束，组内按启动时间。move 不重建行。
        const desired = []
        for (let i = 0; i < dialog.taskCount; ++i)
            desired.push(String(dialog.tasks[i].id))
        desired.sort(function(a, b) {
            const ta = byId[a]
            const tb = byId[b]
            const ga = dialog.groupRank(dialog.displayState(ta))
            const gb = dialog.groupRank(dialog.displayState(tb))
            if (ga !== gb) return ga - gb
            return Number(ta.startedAt || 0) - Number(tb.startedAt || 0)
        })
        for (let target = 0; target < desired.length; ++target) {
            let current = -1
            for (let j = target; j < taskModel.count; ++j) {
                if (String(taskModel.get(j).taskId) === desired[target]) {
                    current = j
                    break
                }
            }
            if (current > target) taskModel.move(current, target, 1)
        }
    }

    function pickDefaultId() {
        let queuedId = ""
        for (let i = 0; i < dialog.taskCount; ++i) {
            const display = dialog.displayState(dialog.tasks[i])
            if (display === "running" || display === "cancelling")
                return String(dialog.tasks[i].id)
            if (display === "queued" && queuedId === "")
                queuedId = String(dialog.tasks[i].id)
        }
        if (queuedId !== "") return queuedId
        return dialog.taskCount > 0 ? String(dialog.tasks[0].id) : ""
    }
    function ensureSelection() {
        if (dialog.selectedId !== "" && dialog.findTask(dialog.selectedId))
            return
        dialog.selectedId = dialog.pickDefaultId()
    }
    function refreshReport() {
        // 不能读 selectedTask 绑定：selectedId 刚变化时绑定尚未重算，
        // 这里会拿到上一个任务（已完成任务因此一直取不到报告）。
        const task = dialog.findTask(dialog.selectedId)
        if (!task || !dialog.isTerminal(dialog.displayState(task))) {
            dialog.selectedReport = ""
            dialog.findings = []
            if (dialog.detailTab === 1) dialog.detailTab = 0
            return
        }
        dialog.selectedReport =
            String(dialog.agentFacade.backgroundSubAgentReport(
                       String(task.id)) || "")
        // docs/43 O4-3: findings 同一时机按需查询；老宿主（无此方法）静默
        // 置空，不影响报告页签。
        if (typeof dialog.agentFacade.backgroundSubAgentFindings
                === "function") {
            dialog.findings =
                dialog.agentFacade.backgroundSubAgentFindings(String(task.id))
                    || []
        } else {
            dialog.findings = []
        }
        if (dialog.selectedReport === "" && dialog.detailTab === 1)
            dialog.detailTab = 0
    }
    function refresh() {
        dialog.syncTasks()
        dialog.ensureSelection()
        dialog.refreshReport()
    }

    onTasksChanged: dialog.refresh()
    onSelectedIdChanged: {
        dialog.refreshReport()
        // 选中已完成任务时直接打开报告；运行中/排队默认看实时进度。
        dialog.detailTab = dialog.selectedReport !== "" ? 1 : 0
    }
    onVisibleChanged: {
        if (visible) {
            dialog.clockMs = Date.now()
            dialog.refresh()
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: dialog.visible && dialog.activeCount > 0
        onTriggered: dialog.clockMs = Date.now()
    }

    anchors.centerIn: parent
    width: Math.min(980, parent ? parent.width - 64 : 980)
    height: Math.min(parent ? parent.height - 56 : 640, 700)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 0

        Accessible.name: qsTr("后台子代理面板")
        Accessible.description: qsTr("查看与管理后台运行的子代理任务")

        // ── Header ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            Layout.bottomMargin: 10
            Layout.preferredHeight: 32
            spacing: 8

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                icon: "agents"
                size: 14
                iconColor: Theme.brandPrimary
            }

            Text {
                objectName: "backgroundTasksTitle"
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: dialog.hasTasks
                    ? qsTr("后台子代理 · %1 进行中 · %2 排队")
                          .arg(dialog.runningCount).arg(dialog.queuedCount)
                    : qsTr("后台子代理")
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                elide: Text.ElideRight
                Accessible.name: text
            }

            IconButton {
                Layout.alignment: Qt.AlignVCenter
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭后台子代理面板")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            spacing: 10

            // ── Empty state ───────────────────────────
            Item {
                objectName: "backgroundTasksEmpty"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !dialog.hasTasks

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(420, parent.width - 64)
                    spacing: 10

                    AppIcon {
                        Layout.alignment: Qt.AlignHCenter
                        icon: "agents"
                        size: 22
                        iconColor: Theme.textTertiary
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("没有后台任务。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("模型对长耗时任务使用 background 模式时会出现在这里。")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ── Left: agent list ──────────────────────
            Rectangle {
                visible: dialog.hasTasks
                Layout.preferredWidth: Math.min(340, Math.round(dialog.width * 0.36))
                Layout.fillHeight: true
                radius: Theme.radiusMd
                color: Theme.codeBackground
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            objectName: "backgroundTasksRunningCount"
                            text: qsTr("进行中 %1").arg(dialog.runningCount)
                            color: dialog.runningCount > 0
                                ? Theme.brandPrimary : Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                        Text {
                            objectName: "backgroundTasksQueuedCount"
                            text: qsTr("排队 %1").arg(dialog.queuedCount)
                            color: dialog.queuedCount > 0
                                ? Theme.statusWarning : Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            objectName: "backgroundTasksFinishedCount"
                            text: qsTr("已结束 %1").arg(dialog.finishedCount)
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                    }

                    ListView {
                        id: taskList
                        objectName: "backgroundTasksList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: dialog.hasTasks
                        clip: true
                        spacing: 4
                        model: taskModel
                        boundsBehavior: Flickable.StopAtBounds
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("后台子代理任务列表")
                        section.property: "group"
                        section.criteria: ViewSection.FullString
                        section.delegate: Item {
                            width: taskList.width
                            height: 24
                            Text {
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                text: dialog.sectionLabel(section)
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                                font.bold: true
                            }
                        }

                        delegate: Rectangle {
                            id: rowCard
                            required property string taskId
                            required property string group
                            required property string role
                            required property string displayState
                            required property string goal
                            required property double startedAt
                            required property double elapsedMs
                            required property int steps
                            required property int maxSteps
                            required property int proposalCount
                            required property int tokensUsed
                            required property int findingCount
                            required property string stopCode

                            readonly property bool selected:
                                dialog.selectedId === taskId
                            width: taskList.width
                            implicitHeight: rowCol.implicitHeight + 14
                            radius: Theme.radiusField
                            color: selected ? Theme.surfaceHover : "transparent"
                            border.width: 1
                            border.color: selected ? Theme.brandPrimary
                                                    : "transparent"
                            objectName: "backgroundTaskRow_" + taskId

                            ColumnLayout {
                                id: rowCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 7
                                spacing: 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Rectangle {
                                        Layout.alignment: Qt.AlignVCenter
                                        width: 7
                                        height: 7
                                        radius: 4
                                        color: dialog.stateColor(rowCard.displayState)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · %2")
                                                  .arg(rowCard.role)
                                                  .arg(dialog.stateLabel(
                                                           rowCard.displayState))
                                        color: dialog.stateColor(
                                                   rowCard.displayState)
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: dialog.elapsedTextFor(rowCard.displayState,
                                                            rowCard.startedAt,
                                                            rowCard.elapsedMs)
                                        color: Theme.textTertiary
                                        font.pixelSize: Theme.fontSizeMicro
                                        font.family: Theme.fontMono
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: rowCard.goal
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: qsTr("步数 %1").arg(
                                                  dialog.stepsText(rowCard))
                                        color: Theme.textTertiary
                                        font.pixelSize: Theme.fontSizeMicro
                                        font.family: Theme.fontMono
                                    }
                                    Text {
                                        visible: rowCard.proposalCount > 0
                                        text: qsTr("%1 个提案").arg(
                                                  rowCard.proposalCount)
                                        color: Theme.brandPrimary
                                        font.pixelSize: Theme.fontSizeMicro
                                        font.family: Theme.fontMono
                                    }
                                    Text {
                                        objectName: "backgroundTaskRowTokens_" + rowCard.taskId
                                        visible: rowCard.tokensUsed > 0
                                        text: qsTr("tokens %1").arg(
                                                  rowCard.tokensUsed)
                                        color: Theme.textTertiary
                                        font.pixelSize: Theme.fontSizeMicro
                                        font.family: Theme.fontMono
                                    }
                                    Text {
                                        objectName: "backgroundTaskRowFindings_" + rowCard.taskId
                                        visible: rowCard.findingCount > 0
                                        text: qsTr("%1 条结论").arg(
                                                  rowCard.findingCount)
                                        color: Theme.brandPrimary
                                        font.pixelSize: Theme.fontSizeMicro
                                        font.family: Theme.fontMono
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dialog.selectedId = rowCard.taskId
                            }
                        }

                        ScrollBar.vertical: AppScrollBar {}
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        AppButton {
                            objectName: "backgroundTasksClearFinished"
                            visible: dialog.finishedCount > 0
                            text: qsTr("清除已结束")
                            onClicked: dialog.agentFacade
                                       .dismissFinishedBackgroundSubAgents()
                            Accessible.name: qsTr("清除已结束的后台任务")
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            objectName: "backgroundTasksCancelAll"
                            visible: dialog.activeCount > 0
                            text: qsTr("全部停止")
                            onClicked: dialog.agentFacade
                                       .cancelAllBackgroundSubAgents()
                            Accessible.name: qsTr("停止全部后台子代理")
                        }
                    }
                }
            }

            // ── Right: selected task detail ───────────
            Rectangle {
                objectName: "backgroundTaskDetail"
                visible: dialog.hasTasks
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusMd
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle
                clip: true

                Text {
                    objectName: "backgroundTaskDetailEmpty"
                    anchors.centerIn: parent
                    visible: dialog.selectedTask === null
                    text: qsTr("从左侧选择一个子代理查看详情。")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8
                    visible: dialog.selectedTask !== null

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: dialog.selectedTask
                                ? dialog.stateColor(
                                      dialog.displayState(dialog.selectedTask))
                                : Theme.textTertiary
                        }
                        Text {
                            objectName: "backgroundTaskDetailTitle"
                            Layout.fillWidth: true
                            text: dialog.selectedTask
                                ? qsTr("%1 · %2")
                                      .arg(String(dialog.selectedTask.role
                                                  || "explore"))
                                      .arg(dialog.stateLabel(
                                               dialog.displayState(
                                                   dialog.selectedTask)))
                                : ""
                            color: dialog.selectedTask
                                ? dialog.stateColor(
                                      dialog.displayState(dialog.selectedTask))
                                : Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            text: dialog.selectedTask
                                ? String(dialog.selectedTask.id || "") : ""
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontMono
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: dialog.selectedTask
                            ? String(dialog.selectedTask.goal || "") : ""
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: qsTr("状态")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                            Text {
                                objectName: "backgroundTaskDetailState"
                                text: dialog.selectedTask
                                    ? dialog.stateLabel(dialog.displayState(
                                          dialog.selectedTask)) : ""
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                font.bold: true
                                elide: Text.ElideRight
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: dialog.selectedTask
                                    && dialog.displayState(dialog.selectedTask)
                                       === "queued"
                                    ? qsTr("排队等待") : qsTr("耗时")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                            Text {
                                objectName: "backgroundTaskDetailElapsed"
                                text: dialog.selectedTask
                                    ? dialog.elapsedText(dialog.selectedTask) : ""
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: qsTr("步数")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                            Text {
                                objectName: "backgroundTaskDetailSteps"
                                text: dialog.selectedTask
                                    ? dialog.stepsText(dialog.selectedTask) : ""
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: qsTr("提案")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                            Text {
                                objectName: "backgroundTaskDetailProposals"
                                text: dialog.selectedTask
                                    ? String(Number(
                                          dialog.selectedTask.proposalCount || 0))
                                    : ""
                                color: Number(dialog.selectedTask
                                              ? dialog.selectedTask.proposalCount || 0
                                              : 0) > 0
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                            }
                        }
                        // B7: 自动重试/未开始时的尝试次数。常规单次尝试
                        // （attempts===1）不占位置，避免详情区噪音。
                        // attempts===0 只在终态才代表「未开始」；running/queued
                        // 还没有报告，0 是"尚未产生"，显示成「未开始」会误导。
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            visible: dialog.selectedTask !== null
                                     && (Number(dialog.selectedTask.attempts || 0) > 1
                                         || Number(dialog.selectedTask.attempts || 0) === 0)
                                     && dialog.selectedTask.state !== "running"
                                     && dialog.selectedTask.state !== "queued"
                            Text {
                                text: qsTr("尝试")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                            Text {
                                objectName: "backgroundTaskDetailAttempts"
                                text: {
                                    const n = Number(dialog.selectedTask
                                                     ? dialog.selectedTask.attempts || 0
                                                     : 0)
                                    if (n === 0) return qsTr("未开始")
                                    return qsTr("%1 次").arg(n)
                                }
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                            }
                        }
                    }

                    Text {
                        objectName: "backgroundTaskDetailUsage"
                        Layout.fillWidth: true
                        visible: dialog.selectedTask !== null
                                 && Number(dialog.selectedTask.tokensUsed || 0) > 0
                        text: dialog.usageText(dialog.selectedTask)
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontMono
                        elide: Text.ElideRight
                    }

                    Text {
                        objectName: "backgroundTaskDetailStopCode"
                        Layout.fillWidth: true
                        visible: dialog.selectedTask !== null
                                 && String(dialog.selectedTask.stopCode || "") !== ""
                                 && String(dialog.selectedTask.stopCode || "")
                                    !== "completed"
                        text: dialog.selectedTask
                                  ? qsTr("stop: %1").arg(
                                        String(dialog.selectedTask.stopCode || ""))
                                  : ""
                        color: Theme.statusWarning
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontMono
                    }

                    // docs/43 O4-3: findings 逐条列表（severity 徽章 + 位置 +
                    // 说明）。快照只带 count，这里选中终态任务时按需查询；
                    // 无 findings 的任务不渲染。
                    ColumnLayout {
                        id: findingsSection
                        objectName: "backgroundTaskDetailFindings"
                        Layout.fillWidth: true
                        spacing: 3
                        visible: dialog.findings.length > 0

                        Text {
                            objectName: "backgroundTaskDetailFindingsTitle"
                            Layout.fillWidth: true
                            text: qsTr("结论（%1 条）").arg(dialog.findings.length)
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }

                        Repeater {
                            model: dialog.findings

                            delegate: RowLayout {
                                id: findingRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 6

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: findingSev.implicitWidth + 10
                                    height: 14
                                    radius: 3
                                    color: dialog.severityColor(
                                        String(findingRow.modelData.severity
                                               || "info"))
                                    opacity: 0.16

                                    Text {
                                        id: findingSev
                                        anchors.centerIn: parent
                                        text: String(findingRow.modelData.severity
                                                     || "info")
                                        color: dialog.severityColor(
                                            String(findingRow.modelData.severity
                                                   || "info"))
                                        font.pixelSize: Theme.fontSizeNano
                                        font.family: Theme.fontSans
                                        font.bold: true
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: Number(findingRow.modelData.line || 0) > 0
                                        ? qsTr("%1:%2")
                                              .arg(String(findingRow.modelData.path
                                                          || ""))
                                              .arg(Number(findingRow.modelData.line))
                                        : String(findingRow.modelData.path || "")
                                    color: Theme.brandPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: -1
                                    text: String(findingRow.modelData.message || "")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        AppButton {
                            objectName: "backgroundTaskDetailStop"
                            visible: dialog.selectedTask !== null
                                && dialog.isActiveState(dialog.displayState(
                                       dialog.selectedTask))
                                && dialog.selectedTask.cancelling !== true
                            text: qsTr("停止")
                            onClicked: dialog.agentFacade
                                       .cancelBackgroundSubAgent(String(
                                           dialog.selectedTask.id))
                            Accessible.name: qsTr("停止这个后台子代理")
                        }
                        // docs/43 O4-2: 失败/取消的终态任务可按原始参数一键
                        // 重派（新任务独立出现在列表与 trace）。
                        AppButton {
                            objectName: "backgroundTaskDetailRetry"
                            visible: dialog.selectedTask !== null
                                && (dialog.displayState(dialog.selectedTask)
                                        === "failed"
                                    || dialog.displayState(dialog.selectedTask)
                                        === "cancelled")
                            text: qsTr("重试")
                            onClicked: dialog.agentFacade
                                       .retryBackgroundSubAgent(String(
                                           dialog.selectedTask.id))
                            Accessible.name: qsTr("按原始参数重试这个后台子代理")
                        }
                        Text {
                            objectName: "backgroundTaskDetailStopping"
                            visible: dialog.selectedTask !== null
                                && dialog.selectedTask.cancelling === true
                            text: qsTr("正在停止…")
                            color: Theme.statusWarning
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: dialog.selectedTask !== null
                            && String(dialog.selectedTask.error || "") !== ""
                        text: dialog.selectedTask
                            ? String(dialog.selectedTask.error || "") : ""
                        color: Theme.statusDanger
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    // ── Live progress / full report ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        AppButton {
                            objectName: "backgroundTaskProgressTab"
                            text: qsTr("实时进度")
                            highlighted: dialog.detailTab === 0
                            onClicked: dialog.detailTab = 0
                            Accessible.name: qsTr("查看实时进度")
                        }
                        AppButton {
                            objectName: "backgroundTaskReportTab"
                            text: qsTr("完整报告")
                            highlighted: dialog.detailTab === 1
                            enabled: dialog.selectedReport !== ""
                            onClicked: dialog.detailTab = 1
                            Accessible.name: qsTr("查看完整报告")
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: dialog.detailTab === 0
                            text: qsTr("最新在上")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                    }

                    StackLayout {
                        objectName: "backgroundTaskDetailStack"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: dialog.detailTab

                        Item {
                            objectName: "backgroundTaskProgressPage"
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: progressList
                                objectName: "backgroundTaskDetailProgress"
                                anchors.fill: parent
                                clip: true
                                spacing: 2
                                model: dialog.selectedProgressNewestFirst()
                                boundsBehavior: Flickable.StopAtBounds

                                delegate: RowLayout {
                                    required property var modelData
                                    width: progressList.width
                                    spacing: 8
                                    Text {
                                        Layout.preferredWidth: 54
                                        text: dialog.phaseLabel(modelData.phase)
                                        color: Theme.textTertiary
                                        font.pixelSize: Theme.fontSizeMicro
                                        font.family: Theme.fontMono
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(modelData.text || "")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeMicro
                                        font.family: Theme.fontMono
                                        wrapMode: Text.Wrap
                                    }
                                }

                                ScrollBar.vertical: AppScrollBar {}
                            }

                            Text {
                                objectName: "backgroundTaskDetailProgressEmpty"
                                anchors.centerIn: parent
                                width: parent.width - 32
                                horizontalAlignment: Text.AlignHCenter
                                visible: progressList.count === 0
                                text: dialog.selectedTask
                                    && dialog.isActiveState(
                                           dialog.displayState(dialog.selectedTask))
                                    ? qsTr("等待第一条进度…")
                                    : qsTr("没有进度记录。")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                            }
                        }

                        Item {
                            objectName: "backgroundTaskReportPage"
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Flickable {
                                id: reportFlick
                                anchors.fill: parent
                                clip: true
                                contentWidth: width
                                contentHeight: reportText.implicitHeight + 8
                                boundsBehavior: Flickable.StopAtBounds

                                Text {
                                    id: reportText
                                    objectName: "backgroundTaskDetailReport"
                                    width: reportFlick.width - 12
                                    text: dialog.selectedReport
                                    textFormat: TextEdit.MarkdownText
                                    wrapMode: Text.Wrap
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                }

                                ScrollBar.vertical: AppScrollBar {}
                            }

                            Text {
                                anchors.centerIn: parent
                                width: parent.width - 32
                                horizontalAlignment: Text.AlignHCenter
                                visible: dialog.selectedReport === ""
                                text: qsTr("任务完成后可查看完整报告。")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }
}