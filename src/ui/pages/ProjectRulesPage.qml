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
// Two invariants keep the editor honest:
// 1. Every write targets `loadedRoot`, never `selectedProject` - the change
//    handler runs AFTER the selection has moved on, so flushing through the
//    selection would write the previous project's text into the new
//    project's file (that exact data-loss bug shipped with iteration one).
// 2. projectRulesState() is a Q_INVOKABLE with no change notification, so
//    the pill / dots / placeholder re-evaluate off `stateRevision`, bumped
//    after every load and every write.
//
// External edits ("open in system editor") are coordinated through
// AgentVM.watchProjectRules: a disk change reloads a clean editor and only
// warns (banner) when unsaved local edits would be lost by reloading.
Rectangle {
    id: root

    // Populated by the settings dialog shell (same context as other pages).
    color: "transparent"

    // Which project is selected in the list (mirrors the editor below).
    property var selectedProject: null
    // Root path whose content is CURRENTLY in the editor. See header note 1.
    property string loadedRoot: ""
    // True while the editor text is being replaced programmatically. Without
    // it, loading project B would see B's text as an "edit" and write it into
    // B's file (and, worse, flush A's pending edit into B on the way).
    property bool loading: false
    // True when the editor holds user edits not yet on disk (drives the
    // external-change banner: a clean editor reloads silently, a dirty one
    // asks before losing anything).
    property bool dirty: false
    // Set when the watched rules file changed on disk while edits were
    // pending; cleared by either resolution button or by our own overwrite.
    property bool externalConflict: false
    // See header note 2.
    property int stateRevision: 0

    // Debounced write: a keystroke storm must not hit the disk per key.
    Timer {
        id: writeDebounce
        interval: 500
        onTriggered: root.writeRules()
    }

    function writeRules() {
        if (root.loadedRoot === "") return
        // Write failures surface via AgentVM.noticeOccurred (global toast);
        // still bump the revision so the pill reflects the unchanged file.
        if (AgentVM.writeProjectRules(root.loadedRoot, rulesArea.text)) {
            root.dirty = false
            root.externalConflict = false
        }
        root.stateRevision++
    }

    // Display metadata for a raw projectRulesState() result. Colors key off
    // the raw state string - never the translated label, whose wording can
    // change between releases (the first iteration colored by indexOf()).
    function stateMeta(st) {
        switch (st) {
        case "active": return { label: qsTr("生效中"), color: Theme.statusSuccess }
        case "truncated": return { label: qsTr("超过注入上限，截断注入"), color: Theme.statusWarning }
        case "seed": return { label: qsTr("模板未编辑，未注入"), color: Theme.statusWarning }
        case "empty": return { label: qsTr("规则为空，未注入"), color: Theme.textSecondary }
        case "missing": return { label: qsTr("未创建"), color: Theme.textSecondary }
        }
        return { label: st, color: Theme.textSecondary }
    }

    function currentState() {
        if (!root.selectedProject) return ""
        return AgentVM.projectRulesState(root.selectedProject.rootPath)
    }

    // The injection cap (AgentVM.projectRulesMaxBytes) counts bytes, not
    // chars; pulled once at page creation - the constant never changes at
    // runtime.
    readonly property int rulesMaxBytes: AgentVM.projectRulesMaxBytes()

    function utf8Bytes(s) {
        try { return unescape(encodeURIComponent(s)).length }
        catch (e) { return s.length }
    }

    // External save landed on the watched file. The page's OWN debounced
    // write fires this too - identical content means "that was us" and only
    // refreshes the status; different content reloads a clean editor, or
    // raises the banner over a dirty one instead of silently clobbering.
    function onRulesFileChanged(rootPath) {
        if (rootPath !== root.loadedRoot) return
        const disk = AgentVM.loadProjectRules(rootPath)
        if (disk === rulesArea.text) {
            root.stateRevision++
            return
        }
        if (root.dirty || writeDebounce.running) {
            root.externalConflict = true
            return
        }
        root.loading = true
        rulesArea.text = disk
        root.loading = false
        root.stateRevision++
    }

    Connections {
        target: AgentVM
        function onProjectRulesFileChanged(rootPath) {
            root.onRulesFileChanged(rootPath)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ── Header (H2 title + caption, same shape as MemoryPage) ──
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            Layout.leftMargin: 24
            Layout.rightMargin: 24

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                Text {
                    text: qsTr("项目规则")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                    Accessible.name: text
                }

                Text {
                    text: qsTr("每个项目一份 AGENTS.md，注入该项目会话的系统提示；每次任务前自动重读，保存后下一条消息即生效。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        // ── Project list (card, per-project status dot) ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 148
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            radius: Theme.radiusMd
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.borderSubtle
            clip: true
            visible: AgentVM.projects.length > 0

            ListView {
                id: projectList
                objectName: "projectRulesProjectList"
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: AgentVM.projects
                spacing: 2
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: projectDelegate
                    required property var modelData
                    width: projectList.width - 8
                    height: 34
                    radius: 6
                    readonly property bool selected: root.selectedProject !== null
                        && root.selectedProject.id === modelData.id
                    color: selected || projectMouse.hovered
                        ? Theme.surfaceHover : "transparent"
                    border.width: selected ? 1 : 0
                    border.color: Theme.brandPrimary
                    Accessible.name: modelData.name

                    HoverHandler { id: projectMouse }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectedProject = projectDelegate.modelData
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        AppIcon {
                            Layout.alignment: Qt.AlignVCenter
                            icon: "folder"
                            size: 13
                            iconColor: selected || projectMouse.hovered
                                ? Theme.brandPrimary : Theme.textSecondary
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: projectDelegate.modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            elide: Text.ElideMiddle
                            Accessible.name: text
                        }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.maximumWidth: 220
                            text: projectDelegate.modelData.rootPath
                            color: Theme.textTertiary
                            font.pixelSize: 9
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                        }

                        // Per-project rule status (green = injecting).
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                            radius: 4
                            color: {
                                root.stateRevision
                                return root.stateMeta(AgentVM.projectRulesState(
                                    projectDelegate.modelData.rootPath)).color
                            }
                        }
                    }
                }
            }
        }

        // ── No-projects empty state ────────────────────
        Rectangle {
            objectName: "projectRulesNoProjectsCard"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            radius: Theme.radiusMd
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.borderSubtle
            visible: AgentVM.projects.length === 0

            Text {
                objectName: "projectRulesNoProjects"
                anchors.centerIn: parent
                width: parent.width - 48
                text: qsTr("尚未导入任何项目。先在侧栏导入项目文件夹，即可为它配置规则。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                opacity: 0.7
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }

        // ── Rules editor ───────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 6
            spacing: 8
            visible: root.selectedProject !== null

            // Toolbar: file identity + byte budget + status pill + external.
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
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("%1 / %2 字节")
                        .arg(root.utf8Bytes(rulesArea.text))
                        .arg(root.rulesMaxBytes)
                    color: root.utf8Bytes(rulesArea.text) > root.rulesMaxBytes
                        ? Theme.statusWarning : Theme.textTertiary
                    font.pixelSize: 9
                    font.family: Theme.fontMono
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: 18
                    Layout.preferredWidth: statePillText.implicitWidth + 16
                    radius: 9
                    border.width: 1
                    border.color: {
                        root.stateRevision
                        return root.stateMeta(root.currentState()).color
                    }

                    Text {
                        id: statePillText
                        objectName: "projectRulesState"
                        anchors.centerIn: parent
                        text: {
                            root.stateRevision
                            return root.stateMeta(root.currentState()).label
                        }
                        color: {
                            root.stateRevision
                            return root.stateMeta(root.currentState()).color
                        }
                        font.pixelSize: 9
                        font.family: Theme.fontSans
                    }
                }

                // Meaningful only once AGENTS.md exists; for a .nexus-only
                // project the page's own writes create AGENTS.md first.
                IconButton {
                    objectName: "projectRulesOpenExternal"
                    visible: {
                        root.stateRevision
                        return root.currentState() !== "missing"
                    }
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

            // External-change banner: disk moved while edits were pending.
            // Reload loses the local edits; "keep mine" writes the editor
            // text over the external save - both are explicit choices, the
            // third path (ignore + keep typing) also overwrites on the next
            // debounced write, so the banner hides with any resolution.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: Theme.radiusSm
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.statusWarning
                visible: root.externalConflict

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("AGENTS.md 已被外部编辑器修改，重新加载将丢弃本地未保存的修改。")
                        color: Theme.textPrimary
                        font.pixelSize: 9
                        font.family: Theme.fontSans
                        elide: Text.ElideRight
                    }

                    AppButton {
                        text: qsTr("重新加载")
                        // Force-discard: dirty is cleared FIRST (a plain
                        // onRulesFileChanged would see the dirty editor and
                        // just re-raise the banner instead of reloading).
                        onClicked: {
                            writeDebounce.stop()
                            root.dirty = false
                            root.externalConflict = false
                            root.loading = true
                            rulesArea.text = AgentVM.loadProjectRules(
                                root.loadedRoot)
                            root.loading = false
                            root.stateRevision++
                        }
                    }

                    AppButton {
                        text: qsTr("保留我的修改")
                        onClicked: {
                            writeDebounce.stop()
                            root.writeRules()
                        }
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
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary
                        selectedTextColor: Theme.selectedTextColor
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        background: null
                        padding: 10
                        placeholderTextColor: Theme.textTertiary
                        placeholderText: {
                            root.stateRevision
                            return root.currentState() === "missing"
                                ? qsTr("AGENTS.md 尚未创建，输入内容保存后将自动创建并注入会话")
                                : qsTr("写下希望模型始终遵守的项目约定（Markdown）")
                        }
                        Accessible.name: qsTr("项目规则内容")

                        onTextChanged: {
                            if (root.loading) return
                            if (root.selectedProject) {
                                root.dirty = true
                                writeDebounce.restart()
                            }
                        }
                    }

                    // Declared AFTER the TextArea so the overlay is topmost
                    // and sees wheel events before the editor consumes them.
                    // Rules files are written unwrapped: Shift+wheel pans
                    // horizontally, plain wheel still scrolls lines.
                    HWheelScroll {}
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("规则计入上下文预算，注入上限 %1 KB，超出部分自动截断；未编辑的模板不会被注入。")
                    .arg(Math.round(root.rulesMaxBytes / 1024))
                color: Theme.textTertiary
                font.pixelSize: 9
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }
        }

        // ── No-selection placeholder ───────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 6
            radius: Theme.radiusMd
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.borderSubtle
            visible: root.selectedProject === null && AgentVM.projects.length > 0

            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                text: qsTr("从上方选择一个项目，查看并编辑它的 AGENTS.md")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                opacity: 0.7
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }

    // Load the selected project's rules into the editor. Flush any pending
    // edit of the PREVIOUS project first: writeRules targets loadedRoot,
    // which at this point still names the previous project (selectedProject
    // has already changed - writing through it was the iteration-one bug).
    onSelectedProjectChanged: {
        if (writeDebounce.running) {
            writeDebounce.stop()
            root.writeRules()
        }
        // Per-project state resets with the selection (the banner belongs
        // to the previous project's conflict, not the new one).
        root.externalConflict = false
        root.dirty = false
        if (!root.selectedProject) {
            root.loading = true
            rulesArea.text = ""
            root.loading = false
            root.loadedRoot = ""
            root.stateRevision++
            AgentVM.unwatchProjectRules()
            return
        }
        root.loading = true
        rulesArea.text = AgentVM.loadProjectRules(
            root.selectedProject.rootPath)
        root.loading = false
        root.loadedRoot = root.selectedProject.rootPath
        root.stateRevision++
        AgentVM.watchProjectRules(root.loadedRoot)
    }

    // A pending debounced edit must not be dropped when the dialog closes
    // (the shell Loader destroys this page on close).
    Component.onDestruction: {
        if (writeDebounce.running) {
            writeDebounce.stop()
            root.writeRules()
        }
        AgentVM.unwatchProjectRules()
    }

    // Land on the project the user is chatting with; any project beats a
    // blank editor when there is no active one.
    Component.onCompleted: {
        const activeId = AgentVM.activeProjectId
        for (let i = 0; i < AgentVM.projects.length; ++i) {
            if (AgentVM.projects[i].id === activeId
                    || (activeId === "" && i === 0)) {
                root.selectedProject = AgentVM.projects[i]
                return
            }
        }
    }
}
