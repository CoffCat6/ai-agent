pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// docs/37 W5-2b：Skill 新建 / 编辑弹窗。
//
// 字段与 core 的 SkillEditorService::Draft 一一对应：id（目录名）、
// name/description/allowed-tools（frontmatter）、body（正文）。保存全部
// 交给 AgentVM.saveSkill，QML 只做表单校验与“已存在 → 覆盖确认”的二次
// 交互；磁盘写入、canonical containment、64 KiB 上限与 .bak 备份都在
// core 层完成，避免 UI 与安全规则分叉。
Dialog {
    id: dialog

    objectName: "skillEditorDialog"


    // 非空表示编辑已有技能；此时 id / 来源都不可改。
    property string editId: ""
    readonly property bool editing: editId.length > 0
    // 首次保存命中“技能已存在”后置为 true；第二次保存才允许覆盖。
    property bool overwritePending: false
    property string errorText: ""

    // 由宿主页面绑定（page.hasActiveProject）。不能用
// AgentVM.skillRootForSource("project") 的返回值做只读绑定：那是 C++
// 内部方法，QML 无法捕获 activeProjectRoot 依赖，项目切换后不会刷新。
    property bool projectAvailable: false
    readonly property string sourceValue:
        sourceCombo.currentIndex === 1 ? "project" : "user"
    // 新建时的实时 id 校验：空串合法（尚未输入），非法时才显示文案。
    readonly property string idError:
        dialog.editing ? "" : AgentVM.validateSkillId(idField.text.trim())

    // 与 core 序列化保持同一形状，仅用于预览，不参与保存。
    readonly property string previewText: {
        const lines = ["---"]
        const name = nameField.text.trim()
        if (name.length > 0) lines.push("name: " + name)
        const description = descField.text.trim()
        if (description.length > 0) lines.push("description: " + description)
        const tools = dialog.parseTools(toolsField.text)
        if (tools.length > 0) lines.push("allowed-tools: [" + tools.join(", ") + "]")
        lines.push("---")
        return lines.join("\n") + "\n" + bodyArea.text
    }

    signal saved()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(680, parent ? parent.width - 48 : 680)
    height: Math.min(700, parent ? parent.height - 60 : 700)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    background: DialogBackground {}

    function parseTools(text) {
        const parts = String(text || "").split(",")
        const tools = []
        for (let i = 0; i < parts.length; ++i) {
            const tool = parts[i].trim()
            if (tool.length > 0) tools.push(tool)
        }
        return tools
    }

    function resetFields() {
        errorText = ""
        overwritePending = false
        idField.text = ""
        nameField.text = ""
        descField.text = ""
        toolsField.text = ""
        bodyArea.text = ""
    }

    // source = "user" | "project"；无项目根时强制回落到用户来源。
    function openCreate(source) {
        editId = ""
        resetFields()
        sourceCombo.currentIndex =
            (source === "project" && dialog.projectAvailable) ? 1 : 0
        open()
        idField.forceActiveFocus()
    }

    function openEdit(id, source) {
        resetFields()
        if (source === "project" && !dialog.projectAvailable) {
            // 不能静默回落到用户来源：否则可能把同名用户技能当成项目技能
            // 打开/覆盖。只展示错误，保持 editId 为空以免误保存。
            editId = ""
            errorText = qsTr("当前没有活动项目，无法编辑项目技能。")
            sourceCombo.currentIndex = 0
            open()
            return
        }
        editId = String(id || "")
        sourceCombo.currentIndex =
            (source === "project" && dialog.projectAvailable) ? 1 : 0

        const draft = AgentVM.skillDraft(editId, sourceValue)
        if (draft.ok !== true) {
            // 打开失败（文件被删/越界）仍展示错误，用户可取消关闭。
            errorText = String(draft.error || "")
        } else {
            idField.text = String(draft.id || "")
            nameField.text = String(draft.name || "")
            descField.text = String(draft.description || "")
            const tools = draft.allowedTools || []
            toolsField.text = tools.join(", ")
            bodyArea.text = String(draft.body || "")
        }
        open()
        idField.forceActiveFocus()
    }

    function submit() {
        const id = idField.text.trim()
        if (id.length === 0) {
            errorText = qsTr("请填写技能 id。")
            idField.forceActiveFocus()
            return
        }
        const idProblem = AgentVM.validateSkillId(id)
        if (idProblem.length > 0) {
            errorText = idProblem
            return
        }
        if (dialog.sourceValue === "project" && !dialog.projectAvailable) {
            errorText = qsTr("当前没有活动项目，无法保存项目技能。")
            return
        }

        const draft = {
            id: id,
            name: nameField.text.trim(),
            description: descField.text.trim(),
            allowedTools: dialog.parseTools(toolsField.text),
            body: bodyArea.text
        }
        // 编辑已有技能的目的就是覆盖，不需要再走一次确认；新建时则只有
        // “已存在”响应过一次覆盖提示、用户再次点击才传 overwrite=true。
        const overwrite = dialog.editing || dialog.overwritePending
        const result = AgentVM.saveSkill(draft, dialog.sourceValue, overwrite)
        if (result.ok === true) {
            overwritePending = false
            errorText = ""
            close()
            dialog.saved()
            return
        }

        const message = String(result.error || "")
        if (!dialog.editing && message.indexOf(qsTr("已存在")) >= 0) {
            overwritePending = true
            errorText = qsTr("技能「%1」已存在。再次保存将覆盖并生成 .bak 备份。")
                .arg(id)
            return
        }
        overwritePending = false
        errorText = message
    }

    onClosed: {
        overwritePending = false
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceSm
        Accessible.role: Accessible.Dialog
        Accessible.name: dialog.editing ? qsTr("编辑技能") : qsTr("新建技能")

        Text {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceLg
            Layout.fillWidth: true
            text: dialog.editing ? qsTr("编辑技能") : qsTr("新建技能")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
        }

        // ── 来源 + id ──────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            spacing: Theme.spaceMd

            ColumnLayout {
                spacing: Theme.spaceXs
                Text {
                    text: qsTr("来源")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
                ComboBox {
                    id: sourceCombo
                    objectName: "skillSourceCombo"
                    Accessible.name: qsTr("技能来源")
                    Layout.preferredWidth: Theme.controlWidthXs
                    enabled: !dialog.editing
                    model: [qsTr("用户"), qsTr("项目")]
                    delegate: ItemDelegate {
                        required property int index
                        required property string modelData
                        width: sourceCombo.width
                        text: modelData
                        // 没有活动项目时不能新建项目技能；用户来源始终可用。
                        enabled: index === 0 || dialog.projectAvailable
                        highlighted: sourceCombo.highlightedIndex === index
                        onClicked: sourceCombo.currentIndex = index
                    }
                    onActivated: dialog.resetOverwriteState()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXs
                Text {
                    text: qsTr("id（目录名）")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
                TextField {
                    id: idField
                    objectName: "skillIdField"
                    Layout.fillWidth: true
                    readOnly: dialog.editing
                    Accessible.name: qsTr("技能 id")
                    placeholderText: qsTr("例如 code-review")
                    placeholderTextColor: Theme.textSecondary
                    color: dialog.editing ? Theme.textSecondary : Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: Theme.selectedTextColor
                    selectByMouse: true
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontMono
                    background: Rectangle {
                        radius: Theme.radiusField
                        color: Theme.surfaceHover
                        border.width: idField.activeFocus ? 2 : 1
                        border.color: idField.activeFocus
                            ? Theme.brandPrimary : Theme.borderSubtle
                    }
                    onTextEdited: dialog.resetOverwriteState()
                }
            }
        }

        Text {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.fillWidth: true
            visible: dialog.idError.length > 0
            text: dialog.idError
            color: Theme.statusDanger
            font.pixelSize: Theme.fontSizeMicro
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        // ── name + description ─────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            spacing: Theme.spaceXs

            Text {
                text: qsTr("名称（frontmatter name，可留空）")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }
            TextField {
                id: nameField
                objectName: "skillNameField"
                Layout.fillWidth: true
                Accessible.name: qsTr("技能名称")
                placeholderText: qsTr("显示在 $ 补全里的名称")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                font.pixelSize: Theme.fontSizeBody
                background: Rectangle {
                    radius: Theme.radiusField
                    color: Theme.surfaceHover
                    border.width: nameField.activeFocus ? 2 : 1
                    border.color: nameField.activeFocus
                        ? Theme.brandPrimary : Theme.borderSubtle
                }
            }

            Text {
                text: qsTr("说明（frontmatter description，可留空）")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }
            TextField {
                id: descField
                objectName: "skillDescriptionField"
                Layout.fillWidth: true
                Accessible.name: qsTr("技能说明")
                placeholderText: qsTr("一句话说明这个技能做什么")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                font.pixelSize: Theme.fontSizeBody
                background: Rectangle {
                    radius: Theme.radiusField
                    color: Theme.surfaceHover
                    border.width: descField.activeFocus ? 2 : 1
                    border.color: descField.activeFocus
                        ? Theme.brandPrimary : Theme.borderSubtle
                }
            }

            Text {
                text: qsTr("allowed-tools（逗号分隔，可留空）")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }
            TextField {
                id: toolsField
                objectName: "skillToolsField"
                Layout.fillWidth: true
                Accessible.name: qsTr("允许的工具")
                placeholderText: qsTr("read_file, search_code")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontMono
                background: Rectangle {
                    radius: Theme.radiusField
                    color: Theme.surfaceHover
                    border.width: toolsField.activeFocus ? 2 : 1
                    border.color: toolsField.activeFocus
                        ? Theme.brandPrimary : Theme.borderSubtle
                }
            }
        }

        // ── 正文 ───────────────────────────────────────
        Text {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.fillWidth: true
            text: qsTr("正文（保存时自动写入 frontmatter）")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            clip: true

            TextArea {
                id: bodyArea
                objectName: "skillBodyArea"
                width: parent.width
                Accessible.name: qsTr("技能正文")
                placeholderText: qsTr("技能正文会注入系统提示；这里不执行脚本或资源。")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                background: Rectangle {
                    radius: Theme.radiusField
                    color: Theme.surfaceHover
                    border.width: bodyArea.activeFocus ? 2 : 1
                    border.color: bodyArea.activeFocus
                        ? Theme.brandPrimary : Theme.borderSubtle
                }
            }
        }

        // ── 只读预览 ───────────────────────────────────
        Text {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.fillWidth: true
            text: qsTr("预览（保存后的 SKILL.md）")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }
        TextArea {
            objectName: "skillPreviewArea"
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            readOnly: true
            text: dialog.previewText
            color: Theme.textSecondary
            selectionColor: Theme.brandPrimary
            selectedTextColor: Theme.selectedTextColor
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            font.pixelSize: Theme.fontSizeMicro
            font.family: Theme.fontMono
            background: Rectangle {
                radius: Theme.radiusField
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle
            }
        }

        Text {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.fillWidth: true
            visible: dialog.errorText.length > 0
            text: dialog.errorText
            color: Theme.statusDanger
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        RowLayout {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.bottomMargin: Theme.spaceLg
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Item { Layout.fillWidth: true }

            AppButton {
                objectName: "skillCancelButton"
                text: qsTr("取消")
                Accessible.name: qsTr("取消编辑技能")
                onClicked: dialog.close()
            }
            AppButton {
                objectName: "skillSaveButton"
                text: dialog.overwritePending ? qsTr("覆盖保存") : qsTr("保存")
                highlighted: true
                Accessible.name: dialog.overwritePending
                    ? qsTr("确认覆盖技能") : qsTr("保存技能")
                onClicked: dialog.submit()
            }
        }
    }

    // 用户改 id/来源后，上一次“已存在”确认失效，避免把新草稿误当成
    // 用户确认过的覆盖。
    function resetOverwriteState() {
        overwritePending = false
        if (errorText.indexOf(qsTr("已存在")) >= 0
                || errorText.indexOf(qsTr("覆盖")) >= 0)
            errorText = ""
    }
}