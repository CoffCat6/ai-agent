import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Project rules (G3): per-project AGENTS.md management.
//
// The injected content lives on disk under each project root and is read by
// AIAgent::projectRulesBlock() before every task (mtime-cached). This page
// owns the AUTHORING side: list imported projects, show whether rules are in
// effect for each, edit in place with a debounced write, or hand the file to
// an external editor for longer sessions.
//
// The textarea writes the file directly - the agent re-reads on mtime change
// at the next task, so no restart is needed. An untouched seed template is
// treated as "no rules" by the agent; this page shows that state as a hint.
Rectangle {
    id: root

    // Populated by the settings dialog shell (same context as other pages).
    color: "transparent"

    // Which project is being edited (mirrors the selection below).
    property var selectedProject: null
    // True while the editor text is being replaced programmatically. Without
    // it, loading project B would see B's text as an "edit" and write it into
    // B's file (and, worse, flush A's pending edit into B on the way).
    property bool loading: false

    // Debounced write: a keystroke storm must not hit the disk per key.
    Timer {
        id: writeDebounce
        interval: 500
        onTriggered: root.writeRules()
    }

    function writeRules() {
        if (!root.selectedProject) return
        AgentVM.writeProjectRules(root.selectedProject.rootPath, rulesArea.text)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        // ── Header ─────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                icon: "layers"
                size: 15
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("项目规则")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                Accessible.name: text
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("每个项目一份 AGENTS.md，自动注入项目会话的系统提示")
                color: Theme.textSecondary
                font.pixelSize: 10
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("规则在每次任务前自动重读（按修改时间），修改后下一条消息即生效。内容计入上下文预算，上限 8 KB。")
            color: Theme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }

        // ── Project list ───────────────────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            clip: true
            visible: AgentVM.projects.length > 0

            ListView {
                id: projectList
                objectName: "projectRulesProjectList"
                model: AgentVM.projects
                spacing: 2

                delegate: Rectangle {
                    required property var modelData
                    width: projectList.width
                    height: 30
                    radius: 5
                    color: projectMouse.hovered ? Theme.surfaceHover : "transparent"
                    border.width: root.selectedProject
                                  && root.selectedProject.id === modelData.id ? 1 : 0
                    border.color: Theme.brandPrimary

                    HoverHandler { id: projectMouse }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectedProject = modelData
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        AppIcon {
                            Layout.alignment: Qt.AlignVCenter
                            icon: "folder"
                            size: 13
                            iconColor: projectMouse.hovered ? Theme.brandPrimary : Theme.textSecondary
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                            Accessible.name: text
                        }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.rootPath
                            color: Theme.textTertiary
                            font.pixelSize: 9
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                            Layout.maximumWidth: 180
                        }
                    }
                }
            }
        }

        Text {
            objectName: "projectRulesNoProjects"
            Layout.fillWidth: true
            visible: AgentVM.projects.length === 0
            text: qsTr("尚未导入任何项目。先在侧栏导入项目文件夹，即可为它配置规则。")
            color: Theme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }

        // ── Rules editor ───────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            visible: root.selectedProject !== null

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.selectedProject
                        ? qsTr("AGENTS.md — %1").arg(root.selectedProject.name) : ""
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                    elide: Text.ElideMiddle
                    Accessible.name: text
                }

                Text {
                    objectName: "projectRulesState"
                    Layout.alignment: Qt.AlignVCenter
                    text: {
                        if (!root.selectedProject) return ""
                        const st = AgentVM.projectRulesState(
                            root.selectedProject.rootPath)
                        if (st === "empty") return qsTr("规则为空，未注入")
                        if (st === "seed") return qsTr("模板未编辑，未注入")
                        if (st === "truncated") return qsTr("超过 8KB，已截断注入")
                        return qsTr("生效中")
                    }
                    color: text.indexOf(qsTr("生效中")) === 0
                        ? Theme.statusSuccess : Theme.statusWarning
                    font.pixelSize: 9
                    font.family: Theme.fontSans
                }

                IconButton {
                    objectName: "projectRulesOpenExternal"
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    radius: 4
                    icon: "export"
                    iconSize: 11
                    tooltip: qsTr("在系统编辑器中打开")
                    Accessible.name: tooltip
                    onClicked: {
                        if (root.selectedProject)
                            ProjectFileVM.openAbsolute(
                                root.selectedProject.rootPath + "/AGENTS.md")
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusSm
                color: Theme.codeBackground
                border.width: 1
                border.color: rulesArea.activeFocus
                    ? Theme.brandPrimary : Theme.borderSubtle
                clip: true

                ScrollView {
                    anchors.fill: parent

                    TextArea {
                        id: rulesArea
                        objectName: "projectRulesEdit"
                        readOnly: false
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        background: null
                        padding: 8
                        Accessible.name: qsTr("项目规则内容")

                        onTextChanged: {
                            if (root.loading) return
                            if (root.selectedProject) writeDebounce.restart()
                        }
                    }
                }
            }
        }
    }

    // Load the selected project's rules into the editor. Flush any pending
    // edit of the PREVIOUS project first so switching away does not lose it.
    onSelectedProjectChanged: {
        if (writeDebounce.running) {
            writeDebounce.stop()
            root.writeRules()
        }
        if (!root.selectedProject) {
            root.loading = true
            rulesArea.text = ""
            root.loading = false
            return
        }
        root.loading = true
        rulesArea.text = AgentVM.loadProjectRules(
            root.selectedProject.rootPath)
        root.loading = false
    }

    // A pending debounced edit must not be dropped when the dialog closes.
    Component.onDestruction: {
        if (writeDebounce.running) {
            writeDebounce.stop()
            root.writeRules()
        }
    }
}
