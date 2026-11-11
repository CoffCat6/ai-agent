import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// docs/50 §7.3: 定时任务编辑器——新建/编辑一个调度任务。由
// ScheduledTasksDialog 以 openFor(task) 驱动（task=null 即新建）。
//
// 字段全部本地暂存，保存时一次 upsertTask（manager 会即时 tick 并广播
// tasksChanged）；调度规格在 QML 侧拼 JSON 串（ScheduleSpec 的语义校验在
// C++，编辑器只做行级必填/格式提示）。权限选择器不复用
// PermissionModeSelector——那个绑定 AgentVM.permissionMode（活动会话的
// 实时权限档），编辑器需要的是任务定义上的一个普通字段。
Dialog {
    id: editor

    // 正在编辑的任务行（null = 新建）。openFor 深拷贝，避免编辑中途
    // tasksChanged 刷新把字段绑定冲掉。
    property var editTask: null
    // Settings.profiles 的轻投影（label + id），ComboBox 的 textRole 需要
    // 对象数组而非 QVariantMap 列表。
    property var profileOptions: {
        const out = []
        const list = Settings.profiles || []
        for (let i = 0; i < list.length; ++i)
            out.push({label: String(list[i].name || list[i].id), profileId: String(list[i].id)})
        return out
    }
    property string errorText: ""
    // 自定义标题（不用 Dialog.title——非空 title 会再渲染内建标题栏）。
    property string dialogTitle: qsTr("新建定时任务")

    signal saved()

    function openFor(task) {
        editTask = task ? JSON.parse(JSON.stringify(task)) : null
        const spec = parseSchedule(editTask ? editTask.scheduleValue : "")
        nameField.text = editTask ? String(editTask.name || "") : ""
        promptArea.text = editTask ? String(editTask.prompt || "") : ""
        kindCombo.currentIndex = spec.kind === "weekly" ? 1
                               : spec.kind === "interval" ? 2 : 0
        timeField.text = String(spec.time || "09:00")
        weekdayCombo.currentIndex = (spec.weekday >= 1 && spec.weekday <= 7)
                                    ? spec.weekday - 1 : 0
        hoursSpin.value = Math.min(168, Math.max(1, Number(spec.hours) || 1))
        permCombo.currentIndex = permIndexFor(String(
            editTask ? editTask.permissionMode || "plan_only" : "plan_only"))
        notifySwitch.checked = editTask ? editTask.notify !== false : true
        errorText = ""
        dialogTitle = editTask ? qsTr("编辑定时任务") : qsTr("新建定时任务")
        open()
    }

    function parseSchedule(value) {
        try { return JSON.parse(String(value || "{}")) } catch (e) { return {} }
    }

    function permIndexFor(mode) {
        for (let i = 0; i < permOptions.length; ++i)
            if (permOptions[i].value === mode) return i
        return 3   // plan_only
    }

    // 与 PermissionMode 的四档文案同源（docs/26 G1 措辞）。
    readonly property var permOptions: [
        {label: qsTr("变更确认"), value: "confirm"},
        {label: qsTr("自动编辑"), value: "auto_edit"},
        {label: qsTr("完全访问"), value: "full_access"},
        {label: qsTr("只读（计划）"), value: "plan_only"}
    ]

    function save() {
        const name = nameField.text.trim()
        if (!name) { errorText = qsTr("请填写任务名称"); return }
        const prompt = promptArea.text.trim()
        if (!prompt) { errorText = qsTr("请填写提示词"); return }

        const kind = kindCombo.currentIndex === 1 ? "weekly"
                   : kindCombo.currentIndex === 2 ? "interval" : "daily"
        let scheduleValue = {}
        if (kind === "interval") {
            scheduleValue = {kind: "interval", hours: hoursSpin.value}
        } else {
            const m = /^(\d{1,2}):(\d{2})$/.exec(timeField.text.trim())
            const hh = m ? Number(m[1]) : -1
            const mm = m ? Number(m[2]) : -1
            if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
                errorText = qsTr("请填写有效的触发时间（HH:mm）")
                return
            }
            scheduleValue = {kind: kind, time: timeField.text.trim()}
            if (kind === "weekly")
                scheduleValue.weekday = weekdayCombo.currentIndex + 1
        }

        const profile = profileOptions[profileCombo.currentIndex] || {profileId: "", label: ""}
        if (!profile.profileId) {
            errorText = qsTr("请先在设置中配置模型")
            return
        }

        const map = {
            name: name,
            prompt: prompt,
            profileId: profile.profileId,
            profileName: profile.label,
            scheduleKind: kind,
            scheduleValue: JSON.stringify(scheduleValue),
            permissionMode: permOptions[permCombo.currentIndex].value,
            notify: notifySwitch.checked
        }
        if (editTask) map.id = editTask.id
        ScheduleManager.upsertTask(map)
        saved()
        close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 64 : 560)
    height: Math.min(620, parent ? parent.height - 80 : 620)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 0

        // ── Header ─────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.bgPrimary

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                spacing: 8

                Text {
                    text: editor.dialogTitle
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                IconButton {
                    icon: "x"
                    iconSize: 12
                    width: 24
                    height: 24
                    tooltip: qsTr("关闭")
                    onClicked: editor.close()
                    Accessible.name: qsTr("关闭")
                }
            }
        }

        // ── Form ───────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("名称")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                TextField {
                    id: nameField
                    objectName: "scheduleTaskNameField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("如：每日要闻总结")
                    Accessible.name: qsTr("任务名称")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("提示词（到点以此新建会话执行）")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    TextArea {
                        id: promptArea
                        objectName: "scheduleTaskPromptArea"
                        wrapMode: TextArea.Wrap
                        placeholderText: qsTr("模型将收到的完整指令")
                        Accessible.name: qsTr("提示词")
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("模型配置")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                ComboBox {
                    id: profileCombo
                    objectName: "scheduleTaskProfileCombo"
                    Layout.fillWidth: true
                    model: editor.profileOptions
                    textRole: "label"
                    Accessible.name: qsTr("模型配置")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("调度")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ComboBox {
                        id: kindCombo
                        objectName: "scheduleTaskKindCombo"
                        Layout.preferredWidth: 140
                        model: [qsTr("每天"), qsTr("每周"), qsTr("每 N 小时")]
                        Accessible.name: qsTr("调度形态")
                    }
                    TextField {
                        id: timeField
                        objectName: "scheduleTaskTimeField"
                        Layout.preferredWidth: 90
                        placeholderText: "09:00"
                        inputMask: "99:99"
                        visible: kindCombo.currentIndex < 2
                        Accessible.name: qsTr("触发时间")
                    }
                    ComboBox {
                        id: weekdayCombo
                        Layout.preferredWidth: 100
                        model: [qsTr("周一"), qsTr("周二"), qsTr("周三"), qsTr("周四"),
                                qsTr("周五"), qsTr("周六"), qsTr("周日")]
                        visible: kindCombo.currentIndex === 1
                        Accessible.name: qsTr("星期")
                    }
                    SpinBox {
                        id: hoursSpin
                        from: 1
                        to: 168
                        visible: kindCombo.currentIndex === 2
                        Accessible.name: qsTr("间隔小时数")
                    }
                    Text {
                        text: qsTr("小时")
                        visible: kindCombo.currentIndex === 2
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontSans
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("权限档")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ComboBox {
                        id: permCombo
                        objectName: "scheduleTaskPermCombo"
                        Layout.preferredWidth: 160
                        model: editor.permOptions
                        textRole: "label"
                        Accessible.name: qsTr("权限档")
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: permCombo.currentIndex !== 3
                        wrapMode: Text.WordWrap
                        text: qsTr("无人值守运行：需要确认的工具会被自动拒绝，"
                                   + "不会挂起等待；建议配合较小墙钟上限。")
                        color: Theme.statusWarning
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontSans
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("完成时系统通知")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                    Layout.fillWidth: true
                }
                Switch {
                    id: notifySwitch
                    objectName: "scheduleTaskNotifySwitch"
                    Accessible.name: qsTr("完成时系统通知")
                }
            }

            Text {
                objectName: "scheduleTaskErrorText"
                Layout.fillWidth: true
                visible: editor.errorText !== ""
                text: editor.errorText
                color: Theme.statusDanger
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }
        }

        // ── Footer ─────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.bgPrimary

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    implicitHeight: 30
                    onClicked: editor.close()
                    Accessible.name: qsTr("取消")
                }
                AppButton {
                    objectName: "scheduleTaskSaveButton"
                    text: qsTr("保存")
                    implicitHeight: 30
                    highlighted: true
                    onClicked: editor.save()
                    Accessible.name: qsTr("保存定时任务")
                }
            }
        }
    }
}
