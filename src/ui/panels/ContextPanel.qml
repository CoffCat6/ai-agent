import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// Context panel — right sidebar. A narrow column, so it only hosts what
// suits one: the Git section and the knowledge base, switched by tabs.
// The 「审阅」 review tree and the task list used to live here too, but a
// diff and a task list both need width this panel cannot give — they moved
// to ReviewDialog and TodoDialog, opened from the TopBar.
// Collapse/expand is driven by AgentVM.isPanelVisible: main.qml animates
// this item's width to 0 and hosts the floating toggle handle.
Rectangle {
    id: root

    Accessible.name: qsTr("上下文面板")

    color: Theme.bgSecondary
    border.width: 0
    clip: true

    // Fade the content out while collapsing
    readonly property real contentFade: AgentVM.isPanelVisible ? 1.0 : 0.0
    // 0 = Git, 1 = project files. Both are PROJECT-scoped: this panel has
    // nothing to show in a local chat and says so (see the hint overlay at
    // the bottom).
    //
    // History: review, the task list and the knowledge base all used to live
    // here and all left for the same two reasons - a 220..660px column cannot
    // give a diff/tree/list the width it needs, and the knowledge base is
    // global rather than project-scoped (it now opens from the sidebar tool
    // strip, next to log/audit/settings).
    //
    // A plain property, not a binding: the fallback below assigns to it when
    // a session switches, and an assignment would silently break a binding.
    property int activeSection: 0

    // Left border separator
    Rectangle {
        width: 1
        height: parent.height
        anchors.left: parent.left
        color: Theme.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        opacity: root.contentFade

        Behavior on opacity { NumberAnimation { duration: 150 } }

        // ── Section tabs ─────────────────────────────
        // Equal-width tabs keep the labels readable down to the 220px
        // panel minimum; per-section counts live in the section headers.
        //
        // Both remaining sections are project-scoped, which is the point:
        // this panel answers "what is in my project". Review, the task list
        // and the knowledge base all left for the same reasons - they need
        // width a 220..660px column cannot give, and the knowledge base is
        // not project-scoped at all (it lives in the sidebar tool strip).
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            // Project files: a read-only browser. Same visibility gate as
            // Git - a local chat has no project to browse.
            AppButton {
                objectName: "filesTab"
                Layout.fillWidth: true
                visible: AgentVM.activeProjectRoot.length > 0
                text: qsTr("文件")
                implicitHeight: 26
                highlighted: root.activeSection === 1
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: root.activeSection = 1
                Accessible.name: qsTr("浏览项目文件")
            }

            AppButton {
                objectName: "gitTab"
                Layout.fillWidth: true
                visible: AgentVM.activeProjectRoot.length > 0
                text: qsTr("Git")
                implicitHeight: 26
                highlighted: root.activeSection === 0
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: root.activeSection = 0
                Accessible.name: qsTr("查看 Git 管理与历史")
            }

        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activeSection
            // Both pages are project-scoped. Without this the stack would still
            // render its current page in a local chat, and GitPanel carries its
            // own "no project" hint - which would leak through. A local chat
            // shows an empty panel, on purpose: nothing here applies to it.
            visible: AgentVM.activeProjectRoot.length > 0

            // ── Git Section (read-only) ──────────────────
            // Project sessions only; the tab itself is hidden for local chats.
            GitPanel {
                id: gitPanel
                projectRoot: AgentVM.activeProjectRoot
                active: root.activeSection === 0
            }

            // ── Project Files Section (read-only) ─────────
            // Both sections are project-scoped, so this panel is only
            // meaningful inside a project; a local chat gets the hint overlay
            // instead (see the bottom of this file).
            ProjectFilesPanel {
                id: filesPanel
                projectRoot: AgentVM.activeProjectRoot
                active: root.activeSection === 1
            }
        }
    }


    Connections {
        target: AgentVM
        // Both sections are project-only, so a local chat has nothing to
        // show here; the hint overlay covers the panel. Returning to a
        // project always lands on Git, which is the stable starting point.
        function onActiveProjectIdChanged() {
            root.activeSection = 0
        }
    }
}
