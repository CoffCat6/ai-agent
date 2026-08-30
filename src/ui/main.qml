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

            onSettingsRequested: settingsDialog.open()
            onAuditRequested: auditDialog.open()
        }

        // ── Center: Chat Area ──────────────────────────
        AgentChatPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            onSettingsRequested: settingsDialog.open()
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

    // ── Settings Dialog ───────────────────────────────
    SettingsDialog {
        id: settingsDialog
        parent: Overlay.overlay
    }

    // ── Audit Log Dialog ──────────────────────────────
    AuditDialog {
        id: auditDialog
        parent: Overlay.overlay
    }

    // ── Connect tool confirmation signal ───────────────
    Connections {
        target: AgentVM
        function onToolConfirmRequired(id, name, args) {
            toolConfirmDialog.pendingToolCallId = id
            toolConfirmDialog.toolName = name
            toolConfirmDialog.arguments = JSON.stringify(args, null, 2)
            toolConfirmDialog.open()
        }
    }

    // ── Error Toast ─────────────────────────────────────
    Rectangle {
        id: errorToast
        parent: Overlay.overlay
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 24
        z: 100

        property string message: ""

        width: Math.min(toastText.implicitWidth, 460) + 32
        height: toastText.implicitHeight + 20
        radius: 8
        color: Theme.bgSecondary
        border.width: 1
        border.color: Theme.statusDanger
        opacity: 0
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: 200 } }

        Text {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 460)
            text: errorToast.message
            color: Theme.statusDanger
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        function show(msg) {
            message = msg
            opacity = 1
            hideTimer.restart()
        }

        Timer {
            id: hideTimer
            interval: 5000
            onTriggered: errorToast.opacity = 0
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: errorToast.opacity = 0
        }
    }

    Connections {
        target: AgentVM
        function onErrorOccurred(message) {
            errorToast.show(message)
        }
    }

    // ── Keyboard Shortcuts ─────────────────────────────
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.open()
    }

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
