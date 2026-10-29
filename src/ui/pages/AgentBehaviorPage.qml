import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 智能体行为：原「通用设置」的模型行为区块。
// 与「模型配置」分开——那边决定"用哪个模型、怎么连"，这里决定"智能体
// 怎么跑"（预算、超时、系统提示词）。所有键由 SettingsService 即时保存。
Item {
    id: page

    Accessible.name: qsTr("智能体行为页")

    function requestResetDefaults() {
        Settings.resetAgentToDefaults()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 32

        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: flick.width
            spacing: 16

            Item { Layout.preferredHeight: 4 }

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("智能体行为")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("控制单次任务如何运行；修改即时保存。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            SettingCard {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                title: qsTr("任务预算")
                subtitle: qsTr("限制一次任务里模型能走多远、跑多久；复杂任务可调高，但通常会增加耗时和 token 消耗。")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: qsTr("单次任务步数预算")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                    TextField {
                        id: maxStepsField
                        Layout.fillWidth: true
                        text: Settings.maxSteps
                        placeholderText: "30"
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary
                        selectedTextColor: Theme.selectedTextColor
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        validator: IntValidator { bottom: 3; top: 1000 }
                        background: Rectangle {
                            radius: 6
                            color: Theme.bgPrimary
                            border.width: maxStepsField.activeFocus ? 2 : 1
                            border.color: maxStepsField.activeFocus
                                ? Theme.brandPrimary : Theme.borderSubtle
                        }
                        // 未完成编辑期间不持久化（例如清空后输入 1000 的中间态）；
                        // 失焦或 Loader 销毁时只提交有效整数。
                        function commit() {
                            if (!acceptableInput) return
                            Settings.maxSteps = parseInt(text)
                        }
                        onEditingFinished: { commit(); text = Settings.maxSteps }
                        Component.onDestruction: commit()
                        Accessible.name: qsTr("单次任务步数预算")
                    }
                    Text {
                        text: qsTr("一次任务里模型最多进行多少轮「思考 → 工具调用」（3–1000）。用完会强制基于已有信息作答。")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("单次任务超时（分钟）")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.bold: true
                            font.family: Theme.fontMono
                        }
                        Text {
                            text: qsTr("任务运行超过此时长会停止并基于已收集的信息作答。需要多次网络调用的任务可以适当调高。")
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                    SpinBox {
                        from: 1
                        to: 60
                        value: Settings.taskTimeoutMinutes
                        editable: true
                        onValueModified: Settings.taskTimeoutMinutes = value
                        Accessible.name: qsTr("单次任务超时，分钟")
                    }
                }
            }

            SettingCard {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.bottomMargin: 16
                title: qsTr("系统提示词")
                subtitle: qsTr("留空使用内置提示词；修改即时生效，内容随请求发送给当前模型服务。")

                TextArea {
                    id: promptField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 160
                    text: Settings.systemPromptOverride
                    placeholderText: qsTr("留空 = 内置提示词")
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: Theme.selectedTextColor
                    wrapMode: TextArea.Wrap
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontMono
                    background: Rectangle {
                        radius: 6
                        color: Theme.bgPrimary
                        border.width: promptField.activeFocus ? 2 : 1
                        border.color: promptField.activeFocus
                            ? Theme.brandPrimary : Theme.borderSubtle
                    }
                    // 长文本按 400ms 防抖提交（避免每次按键都记一条审计并写盘），
                    // 切换分类导致的销毁由 onDestruction 兜底。
                    function commit() { Settings.systemPromptOverride = text }
                    onTextEdited: promptCommitTimer.restart()
                    onEditingFinished: commit()
                    Component.onDestruction: commit()
                    Timer {
                        id: promptCommitTimer
                        interval: 400
                        onTriggered: promptField.commit()
                    }
                    Accessible.name: qsTr("自定义系统提示词")
                }

                Text {
                    text: qsTr("%1 字符").arg(promptField.text.length)
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }
            }
        }
    }
}
