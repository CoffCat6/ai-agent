pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 技能（skills 分类，docs/37 §3.2/§4）。
//
// 只读展示 AgentVM.listSkills 的结果：名称、说明、来源（项目/用户）、
// 文件大小与截断标记。发现/解析/去重/上限全部在 SkillRegistry 完成，
// 页面不重复判定。
//
// 项目技能随活动项目根变化，切换项目后需要重新扫描：刷新按钮显式调用
// AgentVM.reloadSkills()，页面重新拉取列表。目录说明写死在页面上（不暴露
// 用户绝对路径），避免把本机路径带进截图/日志；如需打开目录，后续可在
// AgentViewModel 增加受控的 Q_INVOKABLE，而不是让 QML 直接调 QDesktopServices。
SettingsPage {
    id: page

    objectName: "skillsPage"
    Accessible.name: qsTr("技能页")

    title: qsTr("技能")
    description: qsTr("技能是通过 $name 在消息中引用的行为指令。"
                      + "项目技能随项目提供，用户技能对所有项目生效；"
                      + "同名时项目技能覆盖用户技能。")

    // docs/37 W5-1：设置项没有 Q_PROPERTY 通知，页面在 refresh() 时回读；
    // 用户操作后先调 setter，失败则回滚到 getter 的真实值。
    property bool skillsEnabled: true
    property bool projectSkillsEnabled: true
    property int maxPerMessage: 3
    readonly property bool hasActiveProject: AgentVM.activeProjectRoot.length > 0
    // 新建技能的默认来源：有活动项目时优先项目，否则用户。
    readonly property string currentSource:
        page.hasActiveProject ? "project" : "user"

    // 列表快照：刷新与项目切换后整体重新赋值，绑定才会重新求值。
    property var skills: []
    readonly property var projectSkills:
        page.skills.filter(s => s.source === "project")
    readonly property var userSkills:
        page.skills.filter(s => s.source !== "project")

    function refresh() {
        page.skillsEnabled = AgentVM.skillFeatureEnabled()
        page.projectSkillsEnabled = AgentVM.skillProjectEnabled()
        page.maxPerMessage = AgentVM.skillMaxPerMessage()
        AgentVM.reloadSkills()
        page.skills = AgentVM.listSkills("", 200)
    }

    Component.onCompleted: page.refresh()

    // 内联组件：一行技能。字节数由 SkillRegistry 记录（截断时也保留原始
    // 大小），这里只做 KB 展示。
    component SkillRow: Rectangle {
        id: row
        required property var modelData

        objectName: "skillRow"
        Layout.fillWidth: true
        implicitHeight: 58
        radius: 8
        color: Theme.bgSecondary
        border.width: 1
        border.color: Theme.borderSubtle

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                icon: "preset"
                size: 15
                iconColor: row.modelData.source === "project"
                    ? Theme.brandPrimary : Theme.textSecondary
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                RowLayout {
                    spacing: 8

                    Text {
                        text: "$" + row.modelData.name
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontMono
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: sourceText.implicitWidth + 10
                        height: 18
                        radius: 9
                        color: row.modelData.source === "project"
                            ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                      Theme.brandPrimary.b, 0.14)
                            : Theme.surfaceHover

                        Text {
                            id: sourceText
                            objectName: "skillSourceBadge"
                            anchors.centerIn: parent
                            text: row.modelData.source === "project"
                                ? qsTr("项目") : qsTr("用户")
                            color: row.modelData.source === "project"
                                ? Theme.brandPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                    }

                    Text {
                        visible: row.modelData.truncated
                        text: qsTr("已截断")
                        color: Theme.statusWarning
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontSans
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: row.modelData.description
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    elide: Text.ElideRight
                }
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: (Number(row.modelData.bytes) / 1024).toFixed(1) + " KB"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontMono
            }

            // docs/37 W5-2b：行内编辑/删除入口。删除永远先经过确认弹窗。
            IconButton {
                objectName: "skillEditButton"
                Layout.alignment: Qt.AlignVCenter
                icon: "edit"
                tooltip: qsTr("编辑技能")
                Accessible.name: qsTr("编辑技能 %1").arg(row.modelData.name)
                onClicked: editor.openEdit(String(row.modelData.id),
                                           String(row.modelData.sourceId))
            }

            IconButton {
                objectName: "skillDeleteButton"
                Layout.alignment: Qt.AlignVCenter
                icon: "trash"
                tooltip: qsTr("删除技能")
                Accessible.name: qsTr("删除技能 %1").arg(row.modelData.name)
                onClicked: deleteConfirm.openFor(String(row.modelData.id),
                                                 String(row.modelData.sourceId))
            }
        }
    }

    // ── 工具条 ──────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spaceSm

        AppButton {
            objectName: "skillsNewButton"
            text: qsTr("新建技能")
            Accessible.name: qsTr("新建技能")
            onClicked: editor.openCreate(page.currentSource)
        }

        AppButton {
            objectName: "skillsRefreshButton"
            text: qsTr("刷新")
            Accessible.name: qsTr("重新扫描技能目录")
            onClicked: page.refresh()
        }

        AppButton {
            objectName: "skillsOpenUserDirButton"
            text: qsTr("打开用户目录")
            Accessible.name: qsTr("打开用户技能目录")
            onClicked: AgentVM.openSkillsDirectory("user")
        }

        AppButton {
            objectName: "skillsOpenProjectDirButton"
            text: qsTr("打开项目目录")
            Accessible.name: qsTr("打开项目技能目录")
            enabled: page.hasActiveProject
            onClicked: AgentVM.openSkillsDirectory("project")
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("共 %1 个 · 项目 %2 · 用户 %3")
                .arg(page.skills.length)
                .arg(page.projectSkills.length)
                .arg(page.userSkills.length)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }

        Text {
            objectName: "skillsEmptyHint"
            visible: page.skills.length === 0
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("尚未发现技能")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }
    }

    // ── 技能设置（docs/37 W5-1） ─────────────────────────
    SettingsSection {
        objectName: "skillsSettingsSection"
        title: qsTr("技能设置")
        description: qsTr("关闭后技能仍保留在目录中，但不再参与 $ 补全或注入；修改即时保存。")

        SettingRow {
            objectName: "skills/enabled"
            label: qsTr("启用技能")
            description: qsTr("允许通过 $name 引用技能并把正文注入系统提示。")

            Switch {
                id: enabledSwitch
                objectName: "skillsEnabledSwitch"
                checked: page.skillsEnabled
                Accessible.name: qsTr("启用技能")
                onToggled: {
                    // Switch.checked 是用户赋值，绑定已断开，所以失败时必须
                    // 显式回写；成功路径由 refresh() 同步页面属性。
                    const ok = AgentVM.setSkillFeatureEnabled(checked)
                    page.refresh()
                    if (!ok) enabledSwitch.checked = page.skillsEnabled
                }
            }
        }

        SettingRow {
            objectName: "skills/projectEnabled"
            label: qsTr("启用项目技能")
            description: qsTr("扫描当前项目的 .nexusai/skills；无活动项目或技能总开关关闭时不可用。")

            Switch {
                id: projectSwitch
                objectName: "skillsProjectSwitch"
                enabled: page.skillsEnabled && page.hasActiveProject
                checked: page.projectSkillsEnabled
                Accessible.name: qsTr("启用项目技能")
                onToggled: {
                    const ok = AgentVM.setSkillProjectEnabled(checked)
                    page.refresh()
                    if (!ok) projectSwitch.checked = page.projectSkillsEnabled
                }
            }
        }

        SettingRow {
            objectName: "skills/maxPerMessage"
            label: qsTr("每轮引用上限")
            description: qsTr("一条消息最多注入几个技能正文（1–5）。")

            SpinBox {
                id: maxPerMessageSpin
                objectName: "skillsMaxPerMessageSpin"
                from: 1
                to: 5
                editable: true
                enabled: page.skillsEnabled
                value: page.maxPerMessage
                Accessible.name: qsTr("每轮引用上限")
                onValueModified: {
                    if (!AgentVM.setSkillMaxPerMessage(value))
                        maxPerMessageSpin.value = page.maxPerMessage
                    page.refresh()
                }
            }
        }
    }

    // ── 已发现技能 ──────────────────────────────────────
    SettingsSection {
        objectName: "skillsProjectSection"
        title: qsTr("项目技能")
        description: qsTr("来自当前项目根目录，优先于同名用户技能。切换项目后点「刷新」重新扫描。")
        badge: qsTr("%1 个").arg(page.projectSkills.length)

        Repeater {
            model: page.projectSkills
            delegate: SkillRow {}
        }

        Text {
            visible: page.projectSkills.length === 0
            Layout.fillWidth: true
            text: qsTr("当前项目没有技能。在 <项目根>/.nexusai/skills/<id>/SKILL.md 中新建。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }
    }

    SettingsSection {
        objectName: "skillsUserSection"
        title: qsTr("用户技能")
        description: qsTr("来自用户配置目录，对所有项目生效。")
        badge: qsTr("%1 个").arg(page.userSkills.length)

        Repeater {
            model: page.userSkills
            delegate: SkillRow {}
        }

        Text {
            visible: page.userSkills.length === 0
            Layout.fillWidth: true
            text: qsTr("还没有用户技能。在应用配置目录的 skills/<id>/SKILL.md 中新建。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }
    }

    // ── 目录说明 ────────────────────────────────────────
    SettingsSection {
        title: qsTr("技能目录")
        description: qsTr("每个技能是一个目录下的 SKILL.md，frontmatter 可写 name / description / max-bytes；"
                          + "正文会注入系统提示。本页不执行技能内的脚本或资源。")

        Text {
            objectName: "skillsProjectRootHint"
            Layout.fillWidth: true
            text: qsTr("项目：<项目根>/.nexusai/skills/<id>/SKILL.md")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
            wrapMode: Text.Wrap
        }

        Text {
            objectName: "skillsUserRootHint"
            Layout.fillWidth: true
            text: qsTr("用户：应用配置目录/skills/<id>/SKILL.md")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
            wrapMode: Text.Wrap
        }
    }

    // ── 编辑器 / 删除确认（docs/37 W5-2b） ─────────────
    SkillEditorDialog {
        id: editor
        projectAvailable: page.hasActiveProject
        onSaved: page.refresh()
    }

    Dialog {
        id: deleteConfirm
        objectName: "skillsDeleteConfirm"

        property string pendingId: ""
        property string pendingSource: "user"
        property string errorText: ""

        function openFor(id, source) {
            deleteConfirm.pendingId = String(id || "")
            deleteConfirm.pendingSource =
                source === "project" ? "project" : "user"
            deleteConfirm.errorText = ""
            open()
        }

        // 删除不可撤销，必须由用户在确认弹窗里再点一次。
        function confirm() {
            const result = AgentVM.removeSkillById(deleteConfirm.pendingId,
                                                   deleteConfirm.pendingSource)
            if (result.ok === true) {
                close()
                page.refresh()
                return true
            }
            deleteConfirm.errorText = String(result.error || "")
            return false
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 32 : 440)
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 10
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("删除技能确认")

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: qsTr("删除技能「$%1」？").arg(deleteConfirm.pendingId)
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: qsTr("来源：%1。只删除该技能的 SKILL.md，目录内其它文件会保留。")
                    .arg(deleteConfirm.pendingSource === "project"
                         ? qsTr("项目") : qsTr("用户"))
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeCaption
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                visible: deleteConfirm.errorText.length > 0
                text: deleteConfirm.errorText
                color: Theme.statusDanger
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeCaption
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    objectName: "skillsDeleteCancelButton"
                    text: qsTr("取消")
                    Accessible.name: qsTr("取消删除技能")
                    onClicked: deleteConfirm.close()
                }
                AppButton {
                    objectName: "skillsDeleteConfirmButton"
                    text: qsTr("删除")
                    highlighted: true
                    Accessible.name: qsTr("确认删除技能")
                    onClicked: deleteConfirm.confirm()
                }
            }
        }
    }
}