import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Top bar - sidebar toggle, status indicator and panel toggle
Rectangle {
    id: root

    color: Theme.bgSecondary

    // Bottom border
    Rectangle {
        width: parent.width
        height: 1
        anchors.bottom: parent.bottom
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 0

        // ── Sidebar Toggle ───────────────────────────
        Rectangle {
            width: 32
            height: 32
            radius: 6
            color: sidebarMA.containsMouse ? Theme.surfaceHover : "transparent"

            AppIcon {
                anchors.centerIn: parent
                icon: "menu"
                size: 20
                iconColor: sidebarMA.containsMouse ? Theme.brandPrimary : Theme.textPrimary
            }

            MouseArea {
                id: sidebarMA
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: AgentVM.toggleSidebar()
            }

            ToolTip.visible: sidebarMA.containsMouse
            ToolTip.delay: 600
            ToolTip.text: AgentVM.isSidebarExpanded
                ? qsTr("收起侧边栏 (Ctrl+[)") : qsTr("展开侧边栏 (Ctrl+[)")
        }

        // ── Spacer ────────────────────────────────────
        Item { Layout.fillWidth: true }

        // ── State Status Indicator ───────────────────
        RowLayout {
            spacing: 6

            // Status dot
            Rectangle {
                width: 8
                height: 8
                radius: 4

                color: {
                    var s = AgentVM.state
                    if (s === 0) return Theme.textSecondary        // Idle
                    if (s === 1) return Theme.brandPrimary         // Thinking
                    if (s === 2) return Theme.statusWarning        // AwaitingToolConfirm
                    if (s === 3) return Theme.statusWarning        // ExecutingTool
                    if (s === 4) return Theme.brandPrimary         // Summarizing
                    if (s === 5) return Theme.statusSuccess        // Completed
                    if (s === 6) return Theme.statusDanger         // Error
                    return Theme.textSecondary
                }

                // Pulsing animation for active states
                SequentialAnimation on opacity {
                    running: AgentVM.state >= 1 && AgentVM.state <= 4
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.3; duration: 800 }
                    NumberAnimation { from: 0.3; to: 1.0; duration: 800 }
                }
            }

            Text {
                text: AgentVM.stateText
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }

        Item { Layout.fillWidth: true }

        // ── Panel Toggle Button ──────────────────────
        Rectangle {
            width: 32
            height: 32
            radius: 6
            color: panelToggleMA.containsMouse ? Theme.surfaceHover : "transparent"
            border.width: AgentVM.isPanelVisible ? 0 : 1
            border.color: Theme.borderSubtle

            AppIcon {
                anchors.centerIn: parent
                icon: AgentVM.isPanelVisible ? "chevron-right" : "chevron-left"
                size: 18
                iconColor: panelToggleMA.containsMouse ? Theme.brandPrimary : Theme.textPrimary
            }

            MouseArea {
                id: panelToggleMA
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: AgentVM.togglePanel()
            }

            ToolTip.visible: panelToggleMA.containsMouse
            ToolTip.delay: 600
            ToolTip.text: AgentVM.isPanelVisible
                ? qsTr("收起上下文面板 (Ctrl+B)") : qsTr("展开上下文面板 (Ctrl+B)")
        }

    }
}
