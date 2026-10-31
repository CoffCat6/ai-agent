import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/31 N2：快捷键自定义页。
//
// 动作表、默认值、冲突校验全部在 ShortcutMap；页面只做“录制 → 交给
// ShortcutMap.assign → 显示结果”。录制期间 ShortcutMap.recording 为 true，
// main.qml 的全局 Shortcut 集体让位，否则按 Ctrl+B 会先触发旧动作。
SettingsPage {
    id: page

    Accessible.name: qsTr("快捷键设置页")

    title: qsTr("快捷键")
    description: qsTr("点击「录制」后按下新的组合键。全局快捷键必须包含 Ctrl、Alt 或 Meta，或使用 F1–F12 功能键；Esc 取消录制。")

    property string recordingId: ""
    property string message: ""
    property bool messageIsError: false

    function beginRecording(actionId) {
        page.recordingId = actionId
        page.message = ""
        ShortcutMap.recording = true
        recorder.forceActiveFocus()
    }

    function cancelRecording() {
        page.recordingId = ""
        ShortcutMap.recording = false
    }

    // 关闭设置中心 / 切到别的分类时，由宿主把 recording 置回 false；
    // 这里同步清掉录制行，避免下次打开页面还停在“按下组合键…”。
    Connections {
        target: ShortcutMap
        function onRecordingChanged() {
            if (!ShortcutMap.recording && page.recordingId.length > 0)
                page.recordingId = ""
        }
    }

    function applyRecording(sequence) {
        if (page.recordingId.length === 0) return
        const result = ShortcutMap.assign(page.recordingId, sequence)
        page.recordingId = ""
        ShortcutMap.recording = false
        if (result.ok) {
            page.messageIsError = false
            page.message = qsTr("快捷键已更新")
        } else {
            page.messageIsError = true
            page.message = result.error || qsTr("快捷键未能更新")
        }
    }

    // 返回 true 表示事件已被录制流程消费（调用方负责 accepted）。
    function handleKey(event) {
        if (page.recordingId.length === 0) return false
        if (event.key === Qt.Key_Escape) {
            page.cancelRecording()
            return true
        }
        const sequence = ShortcutMap.sequenceFromKey(event.key, event.modifiers)
        // 纯修饰键（Ctrl/Shift/…）不算一个组合，继续等主键。
        if (sequence.length === 0) return true
        page.applyRecording(sequence)
        return true
    }

    SettingsSection {
        title: qsTr("全局快捷键")

        // 目录锚点与行级复位：SettingsCatalog 的 ui.shortcuts 条目跳到这里。
        SettingRow {
            objectName: "ui.shortcuts"
            settingKey: "ui.shortcuts"
            label: qsTr("自定义快捷键")
            description: qsTr("修改立即保存；恢复默认只影响快捷键，不动其他设置。")

            Text {
                objectName: "shortcut/summary"
                Layout.preferredWidth: Theme.controlWidthSm
                horizontalAlignment: Text.AlignRight
                text: ShortcutMap.modifiedCount > 0
                      ? qsTr("%1 项已自定义").arg(ShortcutMap.modifiedCount)
                      : qsTr("全部为默认")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                elide: Text.ElideRight
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        Repeater {
            model: ShortcutMap.actions

            delegate: SettingRow {
                required property var modelData

                objectName: "shortcut/" + modelData.id
                label: modelData.title
                description: modelData.description

                RowLayout {
                    spacing: Theme.spaceSm

                    Rectangle {
                        Layout.preferredWidth: sequenceText.implicitWidth + 20
                        Layout.preferredHeight: 28
                        radius: Theme.radiusField
                        color: page.recordingId === modelData.id
                               ? Theme.brandPrimary : Theme.surfaceHover
                        border.width: 1
                        border.color: page.recordingId === modelData.id
                                      ? Theme.brandPrimary : Theme.borderSubtle

                        Text {
                            id: sequenceText
                            objectName: "shortcut/value/" + modelData.id
                            anchors.centerIn: parent
                            text: page.recordingId === modelData.id
                                  ? qsTr("按下组合键…") : modelData.sequence
                            color: page.recordingId === modelData.id
                                   ? Theme.bgPrimary : Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.bold: true
                            font.family: Theme.fontMono
                        }
                    }

                    AppButton {
                        objectName: "shortcut/record/" + modelData.id
                        text: page.recordingId === modelData.id ? qsTr("取消")
                                                                : qsTr("录制")
                        highlighted: page.recordingId === modelData.id
                        onClicked: {
                            if (page.recordingId === modelData.id)
                                page.cancelRecording()
                            else
                                page.beginRecording(modelData.id)
                        }
                    }

                    AppButton {
                        objectName: "shortcut/reset/" + modelData.id
                        visible: modelData.modified
                        text: qsTr("恢复")
                        onClicked: {
                            // resetAction 会发 changed → Repeater 重建 delegate，
                            // 本按钮随即销毁。先在销毁前把提示写好并取走标题，
                            // 否则后续的 page/modelData 查找会落在失效上下文里。
                            const title = modelData.title
                            page.messageIsError = false
                            page.message = qsTr("已恢复「%1」的默认快捷键")
                                .arg(title)
                            ShortcutMap.resetAction(modelData.id)
                        }
                    }
                }
            }
        }

        Text {
            objectName: "shortcut/message"
            visible: text.length > 0
            Layout.fillWidth: true
            text: page.message
            color: page.messageIsError ? Theme.statusDanger : Theme.statusSuccess
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.WrapAnywhere
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    // 录制期间的键盘焦点宿主。平时 focus=false，不干扰 Tab 导航；
    // beginRecording() 通过 forceActiveFocus() 把按键流接管过来。
    // 它属于页面的内容布局，不能 anchors.fill（布局管理的项上有锚点会报
    // 未定义行为）；只有键盘焦点用途，尺寸为零即可。
    Item {
        id: recorder
        objectName: "shortcut/recorder"
        Layout.preferredWidth: 0
        Layout.preferredHeight: 0
        focus: page.recordingId.length > 0

        Keys.onPressed: (event) => {
            if (page.handleKey(event))
                event.accepted = true
        }

        // 页面被切走/关闭时不能把全局快捷键留在“让位”状态。
        Component.onDestruction: {
            if (page.recordingId.length > 0) ShortcutMap.recording = false
        }
    }
}
