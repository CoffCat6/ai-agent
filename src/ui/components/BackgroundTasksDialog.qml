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
// 列表只携带轻量字段（goal/state/耗时/步数/提案数/最近 3 条进度）；报告
// 全文按需取（展开时才调 AgentVM.backgroundSubAgentReport(id)），避免每
// 次状态变化都搬运大字符串。
Dialog {
    id: dialog

    property var tasks: []
    // 行内实时耗时：终态读后端 elapsedMs，运行中按本地时钟推进，不依赖
    // 后端刷新频率。
    property double clockMs: Date.now()

    readonly property int taskCount: tasks ? tasks.length : 0
    readonly property int runningCount: {
        let n = 0
        for (let i = 0; i < taskCount; ++i) {
            const s = String(tasks[i].state || "")
            if (s === "running") ++n
        }
        return n
    }
    readonly property int queuedCount: {
        let n = 0
        for (let i = 0; i < taskCount; ++i)
            if (String(tasks[i].state || "") === "queued") ++n
        return n
    }
    readonly property int finishedCount: {
        let n = 0
        for (let i = 0; i < taskCount; ++i) {
            const s = String(tasks[i].state || "")
            if (s === "completed" || s === "failed" || s === "cancelled") ++n
        }
        return n
    }
    readonly property int activeCount: runningCount + queuedCount
    readonly property bool hasTasks: taskCount > 0

    function stateLabel(state) {
        switch (state) {
        case "queued": return qsTr("排队中")
        case "running": return qsTr("运行中")
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
        case "completed": return Theme.statusSuccess
        case "failed": return Theme.statusDanger
        case "cancelled": return Theme.textSecondary
        default: return Theme.textSecondary
        }
    }
    function isActive(state) {
        return state === "queued" || state === "running"
    }
    function elapsedText(row) {
        const active = dialog.isActive(String(row.state || ""))
        const ms = active
            ? Math.max(0, dialog.clockMs - Number(row.startedAt || 0))
            : Number(row.elapsedMs || 0)
        const secs = Math.floor(ms / 1000)
        if (secs < 60) return qsTr("%1 秒").arg(secs)
        const mins = Math.floor(secs / 60)
        if (mins < 60) return qsTr("%1 分 %2 秒").arg(mins).arg(secs % 60)
        return qsTr("%1 时 %2 分").arg(Math.floor(mins / 60)).arg(mins % 60)
    }

    Timer {
        interval: 1000
        repeat: true
        running: dialog.visible && dialog.activeCount > 0
        onTriggered: dialog.clockMs = Date.now()
    }

    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 48 : 560)
    height: Math.min(parent ? parent.height - 80 : 520,
                     dialog.hasTasks
                         ? 132 + Math.min(420, taskList.contentHeight)
                         : 260)
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
                    ? qsTr("后台子代理 · %1 个运行中 · %2 个排队")
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

        // ── Empty state ───────────────────────────────
        Item {
            objectName: "backgroundTasksEmpty"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !dialog.hasTasks

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(380, parent.width - 64)
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

        // ── Task rows ─────────────────────────────────
        ListView {
            id: taskList
            objectName: "backgroundTasksList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            visible: dialog.hasTasks
            clip: true
            spacing: 8
            model: dialog.tasks
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("后台子代理任务列表")

            delegate: Rectangle {
                id: rowCard
                required property var modelData
                required property int index

                readonly property string state: String(modelData.state || "")
                readonly property bool active: dialog.isActive(state)
                readonly property int proposalCount: Number(modelData.proposalCount || 0)
                property bool expanded: false
                property string reportText: ""

                width: taskList.width
                implicitHeight: rowCol.implicitHeight + 20
                radius: Theme.radiusField
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: rowCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        // 状态点
                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 5
                            width: 7
                            height: 7
                            radius: 4
                            color: dialog.stateColor(rowCard.state)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 · %2")
                                              .arg(String(rowCard.modelData.role || "explore"))
                                              .arg(dialog.stateLabel(rowCard.state))
                                    color: dialog.stateColor(rowCard.state)
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: dialog.elapsedText(rowCard.modelData)
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontMono
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: String(rowCard.modelData.goal || "")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: qsTr("步数 %1").arg(Number(rowCard.modelData.steps || 0))
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontMono
                                }
                                Text {
                                    visible: rowCard.proposalCount > 0
                                    text: qsTr("%1 个提案").arg(rowCard.proposalCount)
                                    color: Theme.brandPrimary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontMono
                                }
                                Item { Layout.fillWidth: true }
                                AppButton {
                                    visible: rowCard.active
                                    text: qsTr("停止")
                                    onClicked: AgentVM.cancelBackgroundSubAgent(
                                        String(rowCard.modelData.id))
                                    Accessible.name: qsTr("停止这个后台子代理")
                                }
                                AppButton {
                                    text: rowCard.expanded ? qsTr("收起") : qsTr("查看")
                                    onClicked: {
                                        rowCard.expanded = !rowCard.expanded
                                        if (rowCard.expanded)
                                            rowCard.reportText =
                                                AgentVM.backgroundSubAgentReport(
                                                    String(rowCard.modelData.id))
                                    }
                                    Accessible.name: rowCard.expanded
                                        ? qsTr("收起任务详情")
                                        : qsTr("查看任务详情")
                                }
                            }
                        }
                    }

                    // ── Expanded details ──────────────
                    ColumnLayout {
                        visible: rowCard.expanded
                        Layout.fillWidth: true
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("最近进度")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                            Text {
                                visible: String(rowCard.modelData.error || "") !== ""
                                Layout.fillWidth: true
                                text: String(rowCard.modelData.error || "")
                                color: Theme.statusDanger
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                            }
                        }

                        Repeater {
                            model: rowCard.modelData.progressTail || []
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: "%1: %2".arg(String(modelData.phase || ""))
                                                .arg(String(modelData.text || ""))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontMono
                                wrapMode: Text.Wrap
                            }
                        }

                        Text {
                            visible: (rowCard.modelData.progressTail || []).length === 0
                            Layout.fillWidth: true
                            text: rowCard.active ? qsTr("等待第一条进度…") : qsTr("没有进度记录。")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }

                        CodeBlock {
                            visible: rowCard.reportText !== ""
                            Layout.fillWidth: true
                            Layout.preferredHeight: 180
                            text: rowCard.reportText
                            textColor: Theme.textPrimary
                            bgColor: Theme.codeBackground
                        }
                    }
                }
            }

            ScrollBar.vertical: AppScrollBar {}
        }

        // ── Footer ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: dialog.hasTasks
                    ? qsTr("共 %1 个任务，%2 个已结束")
                          .arg(dialog.taskCount).arg(dialog.finishedCount)
                    : ""
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
                elide: Text.ElideRight
            }

            AppButton {
                objectName: "backgroundTasksClearFinished"
                visible: dialog.finishedCount > 0
                text: qsTr("清除已结束")
                onClicked: AgentVM.dismissFinishedBackgroundSubAgents()
                Accessible.name: qsTr("清除已结束的后台任务")
            }
            AppButton {
                visible: dialog.activeCount > 0
                text: qsTr("全部停止")
                onClicked: AgentVM.cancelAllBackgroundSubAgents()
                Accessible.name: qsTr("停止全部后台子代理")
            }
        }
    }
}