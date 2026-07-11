import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1200
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    title: "NexusAI"
    color: Theme.bgPrimary

    // ── Main Layout ────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Left: Sidebar ──────────────────────────────
        Sidebar {
            visible: AgentVM.isSidebarExpanded
            Layout.preferredWidth: 260
            Layout.fillHeight: true

            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
            }
        }

        // ── Center: Chat Area ──────────────────────────
        AgentChatPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // ── Right: Context Panel ──────────────────────
        ContextPanel {
            Layout.preferredWidth: 300
            Layout.minimumWidth: 250
            Layout.fillHeight: true
            visible: AgentVM.isPanelVisible

            // Smooth show/hide animation
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
            }
        }
    }

    // ── Tool Confirmation Dialog ──────────────────────
    ToolConfirmDialog {
        id: toolConfirmDialog
        parent: Overlay.overlay
    }

    // ── Connect tool confirmation signal ───────────────
    Connections {
        target: AgentVM
        function onToolConfirmRequired(id, name, args) {
            toolConfirmDialog.toolName = name
            toolConfirmDialog.arguments = JSON.stringify(args, null, 2)
            toolConfirmDialog.open()
        }
    }

    // ── Keyboard Shortcuts ─────────────────────────────
    Shortcut {
        sequence: "Ctrl+B"
        onActivated: AgentVM.togglePanel()
    }

    Shortcut {
        sequence: "Ctrl+["
        onActivated: AgentVM.toggleSidebar()
    }

    Shortcut {
        sequence: "Ctrl+D"
        onActivated: ThemeManager.isDark = !ThemeManager.isDark
    }
}
