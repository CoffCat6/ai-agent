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

    // docs/32 P2 / docs/34 P3：子代理（spawn_agent）——只读派生与写类提案。
    SettingCard {
        Layout.fillWidth: true
        title: qsTr("子代理")
        subtitle: qsTr("允许模型派生子代理并行检索与分析；写类角色（coder）只起草改动提案，落盘仍由主任务在用户确认后执行；长耗时任务可用后台模式，从顶栏「后台子代理」面板查看。")

        SettingRow {
            objectName: "agent/subagentEnabled"
            label: qsTr("启用子代理")
            description: qsTr("默认开启。模型可调用 spawn_agent 派生子代理：默认只读（读文件、搜索、git 查看）；coder 角色可用 propose_edit 起草提案，但不会写盘；主代理用 apply_proposal 按提案 id 应用改动，逐次确认。")

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
        // 2026-11-03：内置角色的默认后台开关。预设不单独存档案，统一由此控制；
// 自定义角色（含与预设同名）在下方角色档案里单独设置且优先。
        SettingRow {
            objectName: "agent/subagentPresetBackground"
            label: qsTr("内置子代理默认后台运行")
            description: qsTr("开启后 explore / research / review / coder 在模型省略 background 时默认后台执行；自定义角色（含同名覆盖）的设置优先。")

            Switch {
                checked: Settings.subagentPresetBackground
                onToggled: Settings.subagentPresetBackground = checked
                Accessible.name: qsTr("内置子代理默认后台运行")
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
        subtitle: qsTr("spawn_agent 可按角色派生子代理：预设不可改（默认后台见上方开关），自定义档案可指定专属提示词、工具白名单与默认运行方式。")

        property var roleEditing: null   // null 或 {index: -1(新增) / >=0(编辑)}

        // docs/35 P4.6: 角色级模型配置。空 id = 沿用当前活动 profile；指向
        // 已删除的 profile 时装配层回退（这里也把状态标出来）。
        readonly property var profileChoices:
            [{ id: "", displayName: qsTr("沿用当前模型配置") }]
                .concat(Settings.profiles)
        function profileIndex(profileId) {
            for (let i = 0; i < profileChoices.length; ++i)
                if (profileChoices[i].id === (profileId || "")) return i
            return 0
        }
        function profileLabel(profileId) {
            if (!profileId) return qsTr("沿用当前模型配置")
            const list = Settings.profiles
            for (let i = 0; i < list.length; ++i)
                if (list[i].id === profileId)
                    return list[i].displayName || profileId
            return qsTr("%1（已删除，回退当前配置）").arg(profileId)
        }

        function saveRole() {
            const name = roleNameField.text.trim()
            const prompt = rolePromptArea.text.trim()
            if (!name || !prompt) return
            const tools = roleToolsField.text.split(",")
                .map(s => s.trim()).filter(s => s.length > 0)
            const list = Settings.subagentRoles.slice()
            const entry = { name: name, prompt: prompt, tools: tools,
                            profileId: roleProfileCombo.currentValue || "",
                            effort: roleEffortField.text.trim(),
                            background: roleBackgroundSwitch.checked }
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
            text: qsTr("内置角色：explore（广度优先勘察）、research（深度查证）、review（代码评审）、coder（起草改动提案，只提不改）。调用时省略 role 参数默认使用 explore；自定义档案与预设同名时覆盖预设。coder 经 propose_edit 产出锚点式提案，由主任务用 apply_proposal / edit_file 逐条应用并逐次确认；其他预设不授予提案权。内置角色是否默认后台由上方「内置子代理默认后台运行」统一控制。自定义角色可选择独立的模型配置与思考强度（留空沿用当前配置），并可设为默认后台运行（模型省略 background 时生效）。")
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
                                roleProfileCombo.currentIndex =
                                    page_roleCard.profileIndex(
                                        String(modelData.profileId || ""))
                                roleEffortField.text =
                                    String(modelData.effort || "")
                                roleBackgroundSwitch.checked =
                                    modelData.background === true
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
                    Text {
                        Layout.fillWidth: true
                        visible: modelData.background === true
                        text: qsTr("默认后台运行（调用时省略 background）")
                        color: Theme.brandPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: String(modelData.profileId || "") !== ""
                                 || String(modelData.effort || "") !== ""
                        text: qsTr("模型配置：%1%2")
                                  .arg(page_roleCard.profileLabel(
                                           String(modelData.profileId || "")))
                                  .arg(String(modelData.effort || "") !== ""
                                       ? qsTr(" · 思考强度 %1")
                                             .arg(modelData.effort) : "")
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
                placeholderText: qsTr("工具白名单（逗号分隔，留空 = 全部只读工具；写提案需显式加入 propose_edit）")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
            }
            // docs/35 P4.6: 让角色用不同模型配置（coder 强模型 / explore 快模型）。
            ComboBox {
                id: roleProfileCombo
                objectName: "agent/subagentRoleProfile"
                Layout.fillWidth: true
                model: page_roleCard.profileChoices
                textRole: "displayName"
                valueRole: "id"
                Accessible.name: qsTr("角色使用的模型配置")
            }
            TextField {
                id: roleEffortField
                objectName: "agent/subagentRoleEffort"
                Layout.fillWidth: true
                placeholderText: qsTr("思考强度覆盖（low / medium / high；留空沿用配置）")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
            }
            // 角色级默认后台：模型省略 spawn_agent 的 background 参数时生效。
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("默认后台运行")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: roleBackgroundSwitch
                    objectName: "agent/subagentRoleBackground"
                    Accessible.name: qsTr("该角色默认在后台运行")
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("spawn_agent 省略 background 参数时按此设置执行；模型显式传 false 仍可强制同步。后台任务从顶栏「后台子代理」面板查看。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                wrapMode: Text.Wrap
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
                roleProfileCombo.currentIndex = 0
                roleEffortField.text = ""
                roleBackgroundSwitch.checked = false
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
