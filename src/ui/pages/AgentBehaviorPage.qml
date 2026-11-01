import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 智能体行为：原「通用设置」的模型行为区块。
// 与「模型配置」分开——那边决定"用哪个模型、怎么连"，这里决定"智能体
// 怎么跑"（子代理、系统提示词；任务步数/超时限制已取消）。所有键由
// SettingsService 即时保存。
SettingsPage {
    id: page

    Accessible.name: qsTr("智能体行为页")

    title: qsTr("智能体行为")
    description: qsTr("控制单次任务如何运行；修改即时保存。")

    function requestResetDefaults() {
        Settings.resetAgentToDefaults()
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
        // 2026-11-01：子代理步数预算/超时设置项已随「取消子代理限制」移除，
        // 会话窗口内可随时手动停止运行中的子代理。
    }

    // docs/33 E2: 子代理角色档案——预设只读展示，用户档案增删改后整表提交。
    SettingCard {
        id: page_roleCard
        objectName: "agent/subagentRoles"
        Layout.fillWidth: true
        title: qsTr("子代理角色档案")
        subtitle: qsTr("spawn_agent 可按角色派生子代理：预设不可改，自定义档案指定专属提示词与工具白名单。")

        property var roleEditing: null   // null 或 {index: -1(新增) / >=0(编辑)}

        function saveRole() {
            const name = roleNameField.text.trim()
            const prompt = rolePromptArea.text.trim()
            if (!name || !prompt) return
            const tools = roleToolsField.text.split(",")
                .map(s => s.trim()).filter(s => s.length > 0)
            const list = Settings.subagentRoles.slice()
            const entry = { name: name, prompt: prompt, tools: tools }
            if (page_roleCard.roleEditing.index >= 0)
                list[page_roleCard.roleEditing.index] = entry
            else
                list.push(entry)
            Settings.subagentRoles = list
            page_roleCard.roleEditing = null
        }
        function removeRole(index) {
            const list = Settings.subagentRoles.slice()
            list.splice(index, 1)
            Settings.subagentRoles = list
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("内置角色：explore（广度优先勘察）、research（深度查证）、review（代码评审）。调用时省略 role 参数默认使用 explore；自定义档案与预设同名时覆盖预设。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.Wrap
        }

        Repeater {
            model: Settings.subagentRoles
            delegate: Rectangle {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                implicitHeight: roleCol.implicitHeight + Theme.spaceLg
                radius: Theme.radiusField
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: roleCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        AppButton {
                            text: qsTr("编辑")
                            onClicked: {
                                roleNameField.text = modelData.name
                                rolePromptArea.text = modelData.prompt
                                roleToolsField.text =
                                    (modelData.tools || []).join(", ")
                                page_roleCard.roleEditing = { index: index }
                            }
                        }
                        AppButton {
                            text: qsTr("删除")
                            onClicked: page_roleCard.removeRole(index)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: modelData.prompt
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (modelData.tools || []).length > 0
                        text: qsTr("工具白名单：%1").arg((modelData.tools || []).join(", "))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        elide: Text.ElideRight
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: Settings.subagentRoles.length === 0
            text: qsTr("还没有自定义角色。点「新建角色」定义第一支专属子代理。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.Wrap
        }

        // 编辑器（新增/编辑共用）
        ColumnLayout {
            visible: page_roleCard.roleEditing !== null
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            TextField {
                id: roleNameField
                Layout.fillWidth: true
                placeholderText: qsTr("角色名（模型以 role 参数引用，如：doc-auditor）")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
            }
            TextArea {
                id: rolePromptArea
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                wrapMode: TextEdit.Wrap
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                placeholderText: qsTr("该角色的专注指令（追加到子代理系统提示词）…")
                background: Rectangle {
                    radius: Theme.radiusField
                    color: Theme.codeBackground
                    border.width: 1
                    border.color: Theme.borderSubtle
                }
            }
            TextField {
                id: roleToolsField
                Layout.fillWidth: true
                placeholderText: qsTr("工具白名单（逗号分隔，留空 = 全部只读工具）")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: page_roleCard.roleEditing = null
                }
                AppButton {
                    text: page_roleCard.roleEditing !== null
                          && page_roleCard.roleEditing.index >= 0
                          ? qsTr("保存") : qsTr("创建")
                    highlighted: true
                    onClicked: page_roleCard.saveRole()
                }
            }
        }

        AppButton {
            text: qsTr("新建角色")
            objectName: "agent/subagentRoleAddButton"
            onClicked: {
                roleNameField.text = ""
                rolePromptArea.text = ""
                roleToolsField.text = ""
                page_roleCard.roleEditing = { index: -1 }
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
