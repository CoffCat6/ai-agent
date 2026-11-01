import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 智能体行为：原「通用设置」的模型行为区块。
// 与「模型配置」分开——那边决定"用哪个模型、怎么连"，这里决定"智能体
// 怎么跑"（预算、超时、系统提示词）。所有键由 SettingsService 即时保存。
SettingsPage {
    id: page

    Accessible.name: qsTr("智能体行为页")

    title: qsTr("智能体行为")
    description: qsTr("控制单次任务如何运行；修改即时保存。")

    function requestResetDefaults() {
        Settings.resetAgentToDefaults()
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("任务预算")
        subtitle: qsTr("限制一次任务里模型能走多远、跑多久；复杂任务可调高，但通常会增加耗时和 token 消耗。")

        SettingsField {
            id: maxStepsField
            objectName: "agent/maxSteps"
            label: qsTr("单次任务步数预算")
            labelWidth: 160
            text: Settings.maxSteps
            // 基线跟着已保存值走，避免页面重建时把原值再写一遍。
            lastKnownValue: String(Settings.maxSteps)
            placeholderText: "30"
            validator: IntValidator { bottom: 3; top: 1000 }
            // 未完成编辑期间不持久化（例如清空后输入 1000 的中间态）；
            // 失焦或 Loader 销毁时只提交范围内的有效整数。
            ignoreEmptySubmit: true
            commitOnDestroy: true
            onSubmitted: (value) => {
                const steps = Number(value)
                if (Number.isInteger(steps) && steps >= 3 && steps <= 1000)
                    Settings.maxSteps = steps
            }
        }

        Text {
            text: qsTr("一次任务里模型最多进行多少轮「思考 → 工具调用」（3–1000）。用完会强制基于已有信息作答。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        SettingRow {
            objectName: "agent/taskTimeoutMinutes"
            label: qsTr("单次任务超时（分钟）")
            description: qsTr("任务运行超过此时长会停止并基于已收集的信息作答。需要多次网络调用的任务可以适当调高。")

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

    // docs/32 P2: 子代理（spawn_agent）——只读子代理的派生与预算。
    SettingCard {
        Layout.fillWidth: true
        title: qsTr("子代理")
        subtitle: qsTr("允许模型派生只读子代理并行检索与分析；所有写操作仍由主任务执行。")

        SettingRow {
            objectName: "agent/subagentEnabled"
            label: qsTr("启用子代理")
            description: qsTr("开启后模型可以调用 spawn_agent 派生只读子代理（仅读文件、搜索、git 查看）。")

            Switch {
                checked: Settings.subagentEnabled
                onToggled: Settings.subagentEnabled = checked
                Accessible.name: qsTr("启用子代理")
            }
        }

        SettingRow {
            objectName: "agent/subagentMaxParallel"
            label: qsTr("并行上限")
            description: qsTr("同时运行的子代理数量上限（1–8），超出的派生请求排队等待。")

            SpinBox {
                from: 1
                to: 8
                value: Settings.subagentMaxParallel
                editable: true
                onValueModified: Settings.subagentMaxParallel = value
                Accessible.name: qsTr("子代理并行上限")
            }
        }

        SettingRow {
            objectName: "agent/subagentMaxSteps"
            label: qsTr("单个子代理步数预算")
            description: qsTr("每个子代理自己的「思考 → 工具调用」轮数上限（2–40），与主任务预算相互独立。")

            SpinBox {
                from: 2
                to: 40
                value: Settings.subagentMaxSteps
                editable: true
                onValueModified: Settings.subagentMaxSteps = value
                Accessible.name: qsTr("子代理步数预算")
            }
        }

        SettingRow {
            objectName: "agent/subagentTimeoutSec"
            label: qsTr("单个子代理超时（秒）")
            description: qsTr("子代理运行超过此时长会被中止并报告部分结果（30–1800）。")

            SpinBox {
                from: 30
                to: 1800
                value: Settings.subagentTimeoutSec
                editable: true
                stepSize: 30
                onValueModified: Settings.subagentTimeoutSec = value
                Accessible.name: qsTr("子代理超时，秒")
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("系统提示词")
        subtitle: qsTr("留空使用内置提示词；修改即时生效，内容随请求发送给当前模型服务。")

        TextArea {
            id: promptField
            objectName: "agent/systemPromptOverride"
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
                radius: Theme.radiusField
                color: Theme.surfaceHover
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
            font.pixelSize: Theme.fontSizeMicro
            font.family: Theme.fontMono
        }
    }
}
