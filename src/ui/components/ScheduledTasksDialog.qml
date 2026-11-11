import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// docs/50 §7.2: 定时任务管理——侧栏工具条时钟入口的独立对话框（知识库同款
// 模式）。列表 + 行内启停/编辑/立即运行/删除（二次确认）+ 历史展开 +
// 「打开会话」。数据面全部走 ScheduleManager 上下文属性（main.cpp 注入），
// tasksChanged 驱动重载。
Dialog {
    id: dialog

    property var tasks: []
    // 历史展开的任务 id（同时只展开一个）。
    property string expandedTaskId: ""
    // 删除确认目标（行内二次确认的暂存）。
    property var deleteTarget: null
    // 底部瞬态提示（立即运行撞闸 / runFinished 回执）。
    property string transientNotice: ""

    function reload() {
        tasks = ScheduleManager.tasks()
    }

    function notify(text) {
        transientNotice = text
        noticeTimer.restart()
    }

    // run 状态 → 展示文案 / 颜色（BackgroundTasksDialog.stateColor 同款映射）。
    function statusLabel(status) {
        switch (String(status)) {
        case "running": return qsTr("运行中")
        case "success": return qsTr("已完成")
        case "failed": return qsTr("失败")
        case "timeout": return qsTr("超时")
        case "stopped": return qsTr("已停止")
        case "missed": return qsTr("已错过")
        case "skipped_busy": return qsTr("忙碌跳过")
        default: return String(status || "")
        }
    }

    function statusColor(status) {
        switch (String(status)) {
        case "running": return Theme.brandPrimary
        case "success": return Theme.statusSuccess
        case "failed":
        case "timeout": return Theme.statusDanger
        default: return Theme.textSecondary   // stopped/missed/skipped_busy
        }
    }

    function formatMs(ms) {
        if (!ms) return "—"
        return Qt.formatDateTime(new Date(Number(ms)), "MM-dd HH:mm")
    }

    function formatDuration(run) {
        const start = Number(run.startedAt || 0)
        const end = Number(run.finishedAt || 0)
        if (!start || !end || end < start) return ""
        const secs = Math.round((end - start) / 1000)
        if (secs < 60) return qsTr("%1 秒").arg(secs)
        return qsTr("%1 分钟").arg(Math.round(secs / 60))
    }

    onOpened: reload()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, parent ? parent.width - 64 : 760)
    height: Math.min(620, parent ? parent.height - 80 : 620)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    background: DialogBackground {}

    Timer {
        id: noticeTimer
        interval: 4000
        onTriggered: dialog.transientNotice = ""
    }

    Connections {
        // ScheduleManager 是 main.cpp 注入的上下文属性，不在 ViewModels 的
        // QML 单例体系里——test_chat_workflows 等直接加载 main.qml 的夹具
        // 没有它，typeof 守卫让连接安静地落空（对话框交互路径同样不触达）。
        target: typeof ScheduleManager !== "undefined" ? ScheduleManager : null
        function onTasksChanged() { dialog.reload() }
        function onRunFinished(taskId, taskName, success, sessionId, summary) {
            dialog.notify(success
                ? qsTr("定时任务「%1」完成").arg(taskName)
                : qsTr("定时任务「%1」失败").arg(taskName))
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // ── Header ─────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 12
            Layout.topMargin: 16
            Layout.bottomMargin: 10
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: "clock"
                    size: 16
                    iconColor: Theme.textSecondary
                }
                Text {
                    text: qsTr("定时任务")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    Layout.fillWidth: true
                }
                AppButton {
                    objectName: "scheduledTaskNewButton"
                    text: qsTr("新建任务")
                    implicitHeight: 26
                    highlighted: true
                    onClicked: taskEditorDialog.openFor(null)
                    Accessible.name: qsTr("新建定时任务")
                }
                IconButton {
                    icon: "x"
                    iconSize: 12
                    width: 24
                    height: 24
                    tooltip: qsTr("关闭")
                    onClicked: dialog.close()
                    Accessible.name: qsTr("关闭")
                }
            }

            Text {
                // D5 的显式声明：不补跑（docs/50 §2）。
                text: qsTr("仅在应用运行期间触发；错过的周期不补跑，会记录在执行历史里。")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

        // ── Task list ──────────────────────────────────
        ListView {
            objectName: "scheduledTaskList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: dialog.tasks
            visible: dialog.tasks.length > 0
            spacing: 0

            delegate: ColumnLayout {
                id: taskDelegate
                width: ListView.view.width
                spacing: 0
                required property var modelData
                readonly property bool expanded:
                    dialog.expandedTaskId === modelData.id

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: taskRowMouse.containsMouse
                           ? Theme.surfaceHover : Theme.bgPrimary

                    MouseArea {
                        id: taskRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: dialog.expandedTaskId =
                            taskDelegate.expanded ? "" : modelData.id
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 12
                        spacing: 10

                        Switch {
                            objectName: "scheduledTaskSwitch"
                            checked: modelData.enabled
                            onToggled: (checked) =>
                                ScheduleManager.setTaskEnabled(modelData.id, checked)
                            Accessible.name: modelData.enabled
                                ? qsTr("停用任务 %1").arg(modelData.name)
                                : qsTr("启用任务 %1").arg(modelData.name)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: String(modelData.name || "")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: ScheduleManager.describeTask(modelData)
                                      + " · " + String(modelData.profileName || "")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            text: modelData.enabled
                                ? qsTr("下次 %1").arg(
                                      dialog.formatMs(ScheduleManager.nextDueMs(modelData)))
                                : qsTr("已停用")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }

                        IconButton {
                            icon: "send"
                            iconSize: 14
                            width: 26
                            height: 26
                            tooltip: qsTr("立即运行")
                            onClicked: {
                                if (!ScheduleManager.runNow(modelData.id))
                                    dialog.notify(qsTr("已有任务在运行，请稍后再试"))
                            }
                            Accessible.name: qsTr("立即运行 %1").arg(modelData.name)
                        }
                        IconButton {
                            icon: "edit"
                            iconSize: 14
                            width: 26
                            height: 26
                            tooltip: qsTr("编辑")
                            onClicked: taskEditorDialog.openFor(modelData)
                            Accessible.name: qsTr("编辑 %1").arg(modelData.name)
                        }
                        IconButton {
                            objectName: "scheduledTaskDeleteButton"
                            icon: "trash"
                            iconSize: 14
                            width: 26
                            height: 26
                            tooltip: qsTr("删除")
                            hoverBgColor: Qt.rgba(Theme.statusDanger.r,
                                                  Theme.statusDanger.g,
                                                  Theme.statusDanger.b, 0.15)
                            hoverIconColor: Theme.statusDanger
                            onClicked: {
                                dialog.deleteTarget = modelData
                                deleteConfirmDialog.open()
                            }
                            Accessible.name: qsTr("删除 %1").arg(modelData.name)
                        }
                        AppIcon {
                            icon: taskDelegate.expanded
                                  ? "chevron-up" : "chevron-down"
                            size: 12
                            iconColor: Theme.textTertiary
                        }
                    }
                }

                // ── Run history（行内展开）─────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: expanded ? Math.min(180, historyList.contentHeight + 8) : 0
                    visible: expanded
                    clip: true
                    color: Theme.bgPrimary

                    ListView {
                        id: historyList
                        anchors.fill: parent
                        clip: true
                        interactive: contentHeight > height
                        model: expanded
                            ? ScheduleManager.runsForTask(modelData.id) : []

                        delegate: Rectangle {
                            width: historyList.width
                            height: runRow.height
                            color: "transparent"
                            required property var modelData

                            RowLayout {
                                id: runRow
                                width: parent.width
                                height: Math.max(30, summaryLine.implicitHeight + 18)
                                spacing: 8

                                Item { Layout.preferredWidth: 18 }
                                Rectangle {
                                    Layout.preferredWidth: 7
                                    Layout.preferredHeight: 7
                                    radius: 4
                                    color: dialog.statusColor(modelData.status)
                                }
                                Text {
                                    text: dialog.statusLabel(modelData.status)
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontSans
                                    Layout.preferredWidth: 60
                                }
                                Text {
                                    text: dialog.formatMs(modelData.startedAt)
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontSans
                                    Layout.preferredWidth: 84
                                }
                                Text {
                                    text: modelData.triggerSource === "manual"
                                          ? qsTr("手动") : qsTr("计划")
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontSans
                                    Layout.preferredWidth: 40
                                }
                                Text {
                                    text: dialog.formatDuration(modelData)
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontSans
                                    Layout.preferredWidth: 52
                                }
                                Text {
                                    id: summaryLine
                                    text: String(modelData.summary
                                                 || modelData.error || "")
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSizeMicro
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                AppButton {
                                    visible: String(modelData.sessionId || "") !== ""
                                    text: qsTr("打开会话")
                                    implicitHeight: 22
                                    onClicked: {
                                        AgentVM.openConversation(
                                            String(modelData.sessionId))
                                        dialog.close()
                                    }
                                    Accessible.name: qsTr("打开会话")
                                }
                                Item { Layout.preferredWidth: 12 }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }
            }
        }

        // ── Empty state ────────────────────────────────
        Item {
            objectName: "scheduledTaskEmptyState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: dialog.tasks.length === 0

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                AppIcon {
                    Layout.alignment: Qt.AlignHCenter
                    icon: "clock"
                    size: 28
                    iconColor: Theme.textTertiary
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("还没有定时任务")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("创建一个任务，让模型每天、每周或每隔几小时"
                               + "自动跑一次提示词。")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("新建第一个定时任务")
                    implicitHeight: 28
                    highlighted: true
                    onClicked: taskEditorDialog.openFor(null)
                    Accessible.name: qsTr("新建定时任务")
                }
            }
        }

        // ── Footer：瞬态提示 ────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            visible: dialog.transientNotice !== ""
            color: Theme.bgPrimary

            Text {
                anchors.centerIn: parent
                text: dialog.transientNotice
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
                elide: Text.ElideRight
                width: parent.width - 32
            }
        }
    }

    // ── Delete confirm（docs/50 §7.2：删除需二次确认）────────
    Dialog {
        id: deleteConfirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(380, parent ? parent.width - 64 : 380)
        modal: true
        closePolicy: Popup.CloseOnEscape
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: dialog.deleteTarget
                    ? qsTr("删除定时任务「%1」？").arg(dialog.deleteTarget.name)
                    : ""
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                text: qsTr("该任务的执行历史会一并删除，已产生的会话保留。")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    implicitHeight: 28
                    onClicked: deleteConfirmDialog.close()
                }
                AppButton {
                    objectName: "scheduledTaskDeleteConfirmButton"
                    text: qsTr("删除")
                    implicitHeight: 28
                    onClicked: {
                        if (dialog.deleteTarget)
                            ScheduleManager.deleteTask(
                                String(dialog.deleteTarget.id))
                        dialog.deleteTarget = null
                        deleteConfirmDialog.close()
                    }
                }
            }
        }
    }

    ScheduledTaskEditorDialog {
        id: taskEditorDialog
        onSaved: dialog.reload()
    }
}
