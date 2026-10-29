import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 提示词与指令（prompts 分类）：三组用户自建文本资产的集中管理页。
//
// 1. 系统提示预设（promptPresets）——输入框下方 PromptPresetSelector 的
//    胶囊一键套用到当前会话（会话级 override，清除后回落全局提示词）；
// 2. 斜杠指令（slashCommands）——输入框以 /指令 触发，发送前由
//    AgentVM.expandSlashCommand 展开为模板（{text} 占位）；
// 3. 会话模板（sessionTemplates）——SessionTemplateMenu 在三个宿主提供
//    入口（侧栏窄条 / 标签栏「+」右键 / 空态），新建会话时预置模型配置 /
//    权限模式 / 首条消息；应用逻辑统一在 AgentVM::startFromSessionTemplate。
//
// 三组都是 SettingsService 上的 JSON 列表属性，改动即时持久化；本页只
// 做编辑，不碰套用/展开/应用逻辑（那些在 TopBar / InputConsole / Sidebar）。
Item {
    id: page

    Accessible.name: qsTr("提示词与指令页")

    // 当前编辑目标：{section: "preset"|"slash"|"template", id: string}。
    // id 为空串 = 新增；null = 无编辑器打开。一次只开一个编辑器。
    property var editing: null

    function startAdd(section) { page.editing = {section: section, id: ""} }
    function startEdit(section, id) { page.editing = {section: section, id: id} }
    function closeEditor() { page.editing = null }

    function entryById(list, id) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i]
        return null
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ── 页头 ────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: header.implicitHeight + Theme.spaceLg
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl

            SettingsPageHeader {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Theme.spaceXs
                title: qsTr("提示词与指令")
                description: qsTr("系统提示预设、斜杠指令与会话模板。改动即时保存。")
            }
        }

        ScrollView {
            id: assetsScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceMd
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: AppScrollBar {}

            ColumnLayout {
                // 限定引用：子项作用域里裸的 availableWidth 解析不到
                // ScrollView 的属性，绑定死亡后宽度回退到隐式宽度（未折行
                // 文本 ≈860px），整页内容被推出对话框右侧、按钮全部不可见。
                width: assetsScroll.availableWidth
                spacing: Theme.spaceXl

                // ══ 1. 系统提示预设 ══════════════════════════
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    RowLayout {
                        objectName: "prompts/presets"
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("系统提示预设")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.bold: true
                        }
                        AppButton {
                            text: qsTr("新建预设")
                            objectName: "promptPresetAddButton"
                            onClicked: page.startAdd("preset")
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("整段角色设定（如「严谨的技术评审」）。在输入框下方的预设选择器里套用到当前会话；覆盖全局系统提示词，删除预设不影响已套用的会话。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: Settings.promptPresets
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: presetCol.implicitHeight + Theme.spaceLg
                            radius: Theme.radiusField
                            color: Theme.bgPrimary
                            border.width: 1
                            border.color: Theme.borderSubtle

                            ColumnLayout {
                                id: presetCol
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
                                        onClicked: page.startEdit("preset", modelData.id)
                                    }
                                    AppButton {
                                        text: qsTr("删除")
                                        onClicked: Settings.removePromptPreset(modelData.id)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.prompt
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    Layout.minimumWidth: 0
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        visible: Settings.promptPresets.length === 0
                        text: qsTr("还没有预设。点「新建预设」写下第一段角色设定。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }

                    // 编辑器（新增/编辑共用）
                    ColumnLayout {
                        visible: page.editing !== null && page.editing.section === "preset"
                        Layout.fillWidth: true
                        spacing: Theme.spaceXs

                        TextField {
                            id: presetName
                            Layout.fillWidth: true
                            placeholderText: qsTr("预设名称（如：技术评审）")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                        }
                        TextArea {
                            id: presetPrompt
                            Layout.fillWidth: true
                            Layout.preferredHeight: 96
                            wrapMode: TextEdit.Wrap
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            placeholderText: qsTr("完整的系统提示词内容…")
                            background: Rectangle {
                                radius: Theme.radiusField
                                color: Theme.codeBackground
                                border.width: 1
                                border.color: Theme.borderSubtle
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("取消")
                                onClicked: page.closeEditor()
                            }
                            AppButton {
                                text: page.editing && page.editing.id !== "" ? qsTr("保存") : qsTr("创建")
                                highlighted: true
                                onClicked: {
                                    if (!presetName.text.trim()) return
                                    if (page.editing.id !== "")
                                        Settings.updatePromptPreset(
                                            page.editing.id, presetName.text,
                                            presetPrompt.text)
                                    else
                                        Settings.addPromptPreset(
                                            presetName.text, presetPrompt.text)
                                    page.closeEditor()
                                }
                            }
                        }
                    }
                }

                // ══ 2. 斜杠指令 ══════════════════════════════
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    RowLayout {
                        objectName: "prompts/slash"
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("斜杠指令")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.bold: true
                        }
                        AppButton {
                            text: qsTr("新建指令")
                            objectName: "slashAddButton"
                            onClicked: page.startAdd("slash")
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("输入框以「/指令」开头发送即展开为模板。模板中的 {text} 会被替换为指令后输入的内容；没有占位符时内容附在模板之后。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: Settings.slashCommands
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: slashCol.implicitHeight + Theme.spaceLg
                            radius: Theme.radiusField
                            color: Theme.bgPrimary
                            border.width: 1
                            border.color: Theme.borderSubtle

                            ColumnLayout {
                                id: slashCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: Theme.spaceMd
                                spacing: Theme.spaceXs

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "/" + modelData.name
                                        color: Theme.brandPrimary
                                        font.pixelSize: Theme.fontSizeBody
                                        font.bold: true
                                        font.family: Theme.fontMono
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: modelData.note
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        elide: Text.ElideRight
                                    }
                                    AppButton {
                                        text: qsTr("编辑")
                                        onClicked: page.startEdit("slash", modelData.id)
                                    }
                                    AppButton {
                                        text: qsTr("删除")
                                        onClicked: Settings.removeSlashCommand(modelData.id)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.templ
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    Layout.minimumWidth: 0
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: Settings.slashCommands.length === 0
                        text: qsTr("还没有指令。例：/review + 模板「请审查以下代码，指出问题：{text}」。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }

                    ColumnLayout {
                        visible: page.editing !== null && page.editing.section === "slash"
                        Layout.fillWidth: true
                        spacing: Theme.spaceXs

                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("/"); color: Theme.textSecondary; font.bold: true }
                            TextField {
                                id: slashName
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                placeholderText: qsTr("指令名（不含斜杠，如 review）")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeBody
                                background: Rectangle {
                                    radius: Theme.radiusField
                                    color: Theme.surfaceHover
                                    border.width: slashName.activeFocus ? 2 : 1
                                    border.color: slashName.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                                }
                            }
                        }
                        TextField {
                            id: slashNote
                            Layout.fillWidth: true
                            placeholderText: qsTr("说明（可选，显示在指令列表）")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            background: Rectangle {
                                radius: Theme.radiusField
                                color: Theme.surfaceHover
                                border.width: slashNote.activeFocus ? 2 : 1
                                border.color: slashNote.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                            }
                        }
                        TextArea {
                            id: slashTemplate
                            Layout.fillWidth: true
                            Layout.preferredHeight: 96
                            wrapMode: TextEdit.Wrap
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            placeholderText: qsTr("展开模板…含 {text} 占位符")
                            background: Rectangle {
                                radius: Theme.radiusField
                                color: Theme.codeBackground
                                border.width: 1
                                border.color: Theme.borderSubtle
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("取消")
                                onClicked: page.closeEditor()
                            }
                            AppButton {
                                text: page.editing && page.editing.id !== "" ? qsTr("保存") : qsTr("创建")
                                highlighted: true
                                onClicked: {
                                    if (!slashName.text.trim() || !slashTemplate.text.trim()) return
                                    if (page.editing.id !== "")
                                        Settings.updateSlashCommand(
                                            page.editing.id, slashName.text,
                                            slashTemplate.text, slashNote.text)
                                    else
                                        Settings.addSlashCommand(
                                            slashName.text, slashTemplate.text,
                                            slashNote.text)
                                    page.closeEditor()
                                }
                            }
                        }
                    }
                }

                // ══ 3. 会话模板 ══════════════════════════════
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    RowLayout {
                        objectName: "prompts/templates"
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("会话模板")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.bold: true
                        }
                        AppButton {
                            text: qsTr("新建模板")
                            objectName: "templateAddButton"
                            onClicked: page.startAdd("template")
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("三个入口都能用：侧栏「新对话」右缘的箭头、会话标签栏「+」的右键/长按、以及空会话页的「从模板开始」。一键按预置的模型配置、权限模式与首条消息开新会话；首条消息进入输入框待确认，不会自动发送。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: Settings.sessionTemplates
                        delegate: Rectangle {
                            required property var modelData
                            readonly property string permText: {
                                const m = modelData.permissionMode
                                if (m < 0) return qsTr("权限不变")
                                if (m === AgentVM.permissionModePlanOnly) return qsTr("只读计划")
                                if (m === AgentVM.permissionModeConfirmChanges) return qsTr("变更确认")
                                if (m === AgentVM.permissionModeAutoEdit) return qsTr("自动编辑")
                                return qsTr("完全访问")
                            }
                            readonly property string profileText: {
                                if (modelData.profileId === "") return qsTr("配置不变")
                                const list = Settings.profiles
                                for (let i = 0; i < list.length; ++i)
                                    if (list[i].id === modelData.profileId)
                                        return list[i].displayName
                                return qsTr("配置不变")
                            }
                            Layout.fillWidth: true
                            implicitHeight: tmplCol.implicitHeight + Theme.spaceLg
                            radius: Theme.radiusField
                            color: Theme.bgPrimary
                            border.width: 1
                            border.color: Theme.borderSubtle

                            ColumnLayout {
                                id: tmplCol
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
                                        onClicked: page.startEdit("template", modelData.id)
                                    }
                                    AppButton {
                                        text: qsTr("删除")
                                        onClicked: Settings.removeSessionTemplate(modelData.id)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: profileText + " · " + permText
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: modelData.firstMessage.length > 0
                                    text: modelData.firstMessage
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    Layout.minimumWidth: 0
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        visible: Settings.sessionTemplates.length === 0
                        text: qsTr("还没有模板。例：「代码审查」= 只读计划 + 首条消息「请审查这个项目的近期改动」。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }

                    ColumnLayout {
                        visible: page.editing !== null && page.editing.section === "template"
                        Layout.fillWidth: true
                        spacing: Theme.spaceXs

                        TextField {
                            id: tmplName
                            Layout.fillWidth: true
                            placeholderText: qsTr("模板名称（如：代码审查）")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            background: Rectangle {
                                radius: Theme.radiusField
                                color: Theme.surfaceHover
                                border.width: tmplName.activeFocus ? 2 : 1
                                border.color: tmplName.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: tmplProfile
                                Layout.fillWidth: true
                                textRole: "displayName"
                                // 首项「配置不变」= 空 profileId。
                                model: [{displayName: qsTr("配置不变"), id: ""}].concat(Settings.profiles)
                                valueRole: "id"
                            }
                            ComboBox {
                                id: tmplPermission
                                Layout.fillWidth: true
                                textRole: "label"
                                valueRole: "value"
                                model: [
                                    {label: qsTr("权限不变"), value: -1},
                                    {label: qsTr("只读计划"), value: AgentVM.permissionModePlanOnly},
                                    {label: qsTr("变更确认"), value: AgentVM.permissionModeConfirmChanges},
                                    {label: qsTr("自动编辑"), value: AgentVM.permissionModeAutoEdit},
                                    {label: qsTr("完全访问"), value: AgentVM.permissionModeFullAccess}
                                ]
                            }
                        }
                        TextArea {
                            id: tmplFirst
                            Layout.fillWidth: true
                            Layout.preferredHeight: 80
                            wrapMode: TextEdit.Wrap
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            placeholderText: qsTr("首条消息（可选，进入输入框待确认）")
                            background: Rectangle {
                                radius: Theme.radiusField
                                color: Theme.codeBackground
                                border.width: 1
                                border.color: Theme.borderSubtle
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("取消")
                                onClicked: page.closeEditor()
                            }
                            AppButton {
                                text: page.editing && page.editing.id !== "" ? qsTr("保存") : qsTr("创建")
                                highlighted: true
                                onClicked: {
                                    if (!tmplName.text.trim()) return
                                    const fields = {
                                        name: tmplName.text.trim(),
                                        profileId: tmplProfile.currentValue || "",
                                        permissionMode: tmplPermission.currentValue,
                                        firstMessage: tmplFirst.text
                                    }
                                    if (page.editing.id !== "")
                                        Settings.updateSessionTemplate(page.editing.id, fields)
                                    else
                                        Settings.addSessionTemplate(
                                            fields.name, fields.profileId,
                                            fields.permissionMode, fields.firstMessage)
                                    page.closeEditor()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 打开编辑器时把条目内容填进表单（新增则留空）。
    onEditingChanged: {
        if (!page.editing) return
        const e = page.editing
        if (e.section === "preset") {
            const p = page.entryById(Settings.promptPresets, e.id)
            presetName.text = p ? p.name : ""
            presetPrompt.text = p ? p.prompt : ""
        } else if (e.section === "slash") {
            const c = page.entryById(Settings.slashCommands, e.id)
            slashName.text = c ? c.name : ""
            slashNote.text = c ? c.note : ""
            slashTemplate.text = c ? c.templ : ""
        } else {
            const t = page.entryById(Settings.sessionTemplates, e.id)
            tmplName.text = t ? t.name : ""
            tmplProfile.currentIndex = 0
            if (t) {
                const list = Settings.profiles
                for (let i = 0; i < list.length; ++i)
                    if (list[i].id === t.profileId) tmplProfile.currentIndex = i + 1
                tmplPermission.currentIndex = t.permissionMode + 1
                tmplFirst.text = t.firstMessage
            } else {
                tmplPermission.currentIndex = 0
                tmplFirst.text = ""
            }
        }
    }
}
