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

    // Width always kept free of the two side panels when resizing them
    readonly property int chatMinWidth: 320

    // ── Main Layout ────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Left: Sidebar ──────────────────────────────
        // Collapse animates the width to 0 (content fades inside the
        // Sidebar); the floating handle below stays reachable while hidden.
        // Expanded width comes from AgentVM.sidebarWidth, dragged via
        // leftSplitter and persisted across runs.
        Sidebar {
            id: sidebarPanel
            Layout.preferredWidth: AgentVM.isSidebarExpanded ? AgentVM.sidebarWidth : 0
            Layout.fillHeight: true
            // Stay visible through the whole collapse animation; RowLayout
            // skips invisible items entirely, which would freeze the width.
            visible: AgentVM.isSidebarExpanded || width > 0.5

            Behavior on Layout.preferredWidth {
                // While the splitter is dragged the width must track the
                // cursor exactly instead of easing behind it
                enabled: !leftSplitter.dragActive
                NumberAnimation { duration: 220; easing.type: Easing.OutQuad }
            }

            onSettingsRequested: settingsDialog.open()
            onAuditRequested: auditDialog.open()
        }

        // ── Sidebar / Chat Splitter ────────────────────
        SplitBar {
            id: leftSplitter
            // Sits before AgentChatPage in the RowLayout, so lift it above
            // the chat's ScrollView to keep the full hit area usable
            z: 5
            Layout.fillHeight: true
            visible: sidebarPanel.width > 0.5
            Layout.preferredWidth: visible ? implicitWidth : 0

            onDragged: (delta) => {
                const reserved = (AgentVM.isPanelVisible ? AgentVM.panelWidth : 0)
                    + chatMinWidth + rightSplitter.width
                AgentVM.sidebarWidth = Math.max(180, Math.min(
                    AgentVM.sidebarWidth + delta,
                    Math.min(560, mainWindow.width - reserved)))
            }
            onResetRequested: AgentVM.sidebarWidth = 260
        }

        // ── Center: Chat Area ──────────────────────────
        AgentChatPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            onSettingsRequested: settingsDialog.open()
        }

        // ── Chat / Context Panel Splitter ──────────────
        SplitBar {
            id: rightSplitter
            z: 5
            Layout.fillHeight: true
            visible: contextPanel.width > 0.5
            Layout.preferredWidth: visible ? implicitWidth : 0

            // Dragging left (negative delta) widens the panel
            onDragged: (delta) => {
                const reserved = (AgentVM.isSidebarExpanded ? AgentVM.sidebarWidth : 0)
                    + chatMinWidth + leftSplitter.width
                AgentVM.panelWidth = Math.max(220, Math.min(
                    AgentVM.panelWidth - delta,
                    Math.min(640, mainWindow.width - reserved)))
            }
            onResetRequested: AgentVM.panelWidth = 300
        }

        // ── Right: Context Panel ──────────────────────
        ContextPanel {
            id: contextPanel
            Layout.preferredWidth: AgentVM.isPanelVisible ? AgentVM.panelWidth : 0
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            visible: AgentVM.isPanelVisible || width > 0.5

            Behavior on Layout.preferredWidth {
                enabled: !rightSplitter.dragActive
                NumberAnimation { duration: 220; easing.type: Easing.OutQuad }
            }

            onDocumentDetailsRequested: knowledgeDocDialog.openFor(name)
        }
    }

    // ── Sidebar Toggle Handle ──────────────────────────
    // Floats on the sidebar's right edge; docks to the window edge while
    // collapsed so the sidebar can always be re-opened from here.
    Item {
        id: sidebarHandle
        width: 24
        height: 56
        z: 20
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(sidebarPanel.width - 12, 6)

        Rectangle {
            anchors.fill: parent
            radius: height / 2

            color: {
                if (sidebarHandleMA.containsMouse && AgentVM.isSidebarExpanded) return Theme.brandPrimary
                if (sidebarHandleMA.containsMouse) return Theme.surfaceHover
                return Theme.bgSecondary
            }
            border.width: 1
            border.color: sidebarHandleMA.containsMouse && AgentVM.isSidebarExpanded
                ? Theme.brandPrimary : Theme.borderSubtle

            Behavior on color { ColorAnimation { duration: 150 } }

            AppIcon {
                anchors.centerIn: parent
                icon: AgentVM.isSidebarExpanded ? "chevron-left" : "chevron-right"
                size: 14
                iconColor: sidebarHandleMA.containsMouse && AgentVM.isSidebarExpanded
                    ? Theme.bgPrimary : Theme.textSecondary
            }

            MouseArea {
                id: sidebarHandleMA
                anchors.fill: parent
                anchors.margins: -4   // Slightly larger hit area
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: AgentVM.toggleSidebar()
            }

            ToolTip.visible: sidebarHandleMA.containsMouse
            ToolTip.delay: 600
            ToolTip.text: AgentVM.isSidebarExpanded
                ? qsTr("收起侧边栏 (Ctrl+[)") : qsTr("展开侧边栏 (Ctrl+[)")
        }
    }

    // ── Context Panel Toggle Handle ────────────────────
    // Mirrors the sidebar handle on the right edge.
    Item {
        id: panelHandle
        width: 24
        height: 56
        z: 20
        anchors.verticalCenter: parent.verticalCenter
        x: parent.width - width - Math.max(contextPanel.width - 12, 6)

        Rectangle {
            anchors.fill: parent
            radius: height / 2

            color: {
                if (panelHandleMA.containsMouse && AgentVM.isPanelVisible) return Theme.brandPrimary
                if (panelHandleMA.containsMouse) return Theme.surfaceHover
                return Theme.bgSecondary
            }
            border.width: 1
            border.color: panelHandleMA.containsMouse && AgentVM.isPanelVisible
                ? Theme.brandPrimary : Theme.borderSubtle

            Behavior on color { ColorAnimation { duration: 150 } }

            AppIcon {
                anchors.centerIn: parent
                icon: AgentVM.isPanelVisible ? "chevron-right" : "chevron-left"
                size: 14
                iconColor: panelHandleMA.containsMouse && AgentVM.isPanelVisible
                    ? Theme.bgPrimary : Theme.textSecondary
            }

            MouseArea {
                id: panelHandleMA
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: AgentVM.togglePanel()
            }

            ToolTip.visible: panelHandleMA.containsMouse
            ToolTip.delay: 600
            ToolTip.text: AgentVM.isPanelVisible
                ? qsTr("收起上下文面板 (Ctrl+B)") : qsTr("展开上下文面板 (Ctrl+B)")
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

    // ── Knowledge Document Detail Dialog ──────────────
    KnowledgeDocDialog {
        id: knowledgeDocDialog
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

    // ── Notice Toast (success / info) ────────────────────
    Rectangle {
        id: noticeToast
        parent: Overlay.overlay
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: errorToast.visible ? 64 : 24
        z: 100

        property string message: ""

        width: Math.min(noticeText.implicitWidth, 460) + 32
        height: noticeText.implicitHeight + 20
        radius: 8
        color: Theme.bgSecondary
        border.width: 1
        border.color: Theme.statusSuccess
        opacity: 0
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: 200 } }

        Text {
            id: noticeText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 460)
            text: noticeToast.message
            color: Theme.statusSuccess
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        function show(msg) {
            message = msg
            opacity = 1
            noticeHideTimer.restart()
        }

        Timer {
            id: noticeHideTimer
            interval: 3500
            onTriggered: noticeToast.opacity = 0
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: noticeToast.opacity = 0
        }
    }

    Connections {
        target: AgentVM
        function onNoticeOccurred(message) {
            noticeToast.show(message)
        }
    }

    // ── Keyboard Shortcuts ─────────────────────────────
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+L"
        onActivated: auditDialog.open()
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
