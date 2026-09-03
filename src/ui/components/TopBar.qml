import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

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
        IconButton {
            icon: "menu"
            iconSize: 20
            restIconColor: Theme.textPrimary
            tooltip: AgentVM.isSidebarExpanded
                ? qsTr("收起侧边栏 (Ctrl+[)") : qsTr("展开侧边栏 (Ctrl+[)")
            onClicked: AgentVM.toggleSidebar()
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
                    if (s === AgentVM.stateIdle) return Theme.textSecondary
                    if (s === AgentVM.stateThinking) return Theme.brandPrimary
                    if (s === AgentVM.stateAwaitingToolConfirm) return Theme.statusWarning
                    if (s === AgentVM.stateExecutingTool) return Theme.statusWarning
                    if (s === AgentVM.stateSummarizing) return Theme.brandPrimary
                    if (s === AgentVM.stateCompleted) return Theme.statusSuccess
                    if (s === AgentVM.stateError) return Theme.statusDanger
                    return Theme.textSecondary
                }

                // Pulsing animation for active states
                SequentialAnimation on opacity {
                    running: AgentVM.stateBusy
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
        IconButton {
            icon: AgentVM.isPanelVisible ? "chevron-right" : "chevron-left"
            iconSize: 18
            restIconColor: Theme.textPrimary
            border.width: AgentVM.isPanelVisible ? 0 : 1
            border.color: Theme.borderSubtle
            tooltip: AgentVM.isPanelVisible
                ? qsTr("收起上下文面板 (Ctrl+B)") : qsTr("展开上下文面板 (Ctrl+B)")
            onClicked: AgentVM.togglePanel()
        }

    }
}
