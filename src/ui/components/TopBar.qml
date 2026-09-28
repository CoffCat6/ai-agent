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

        // ── Token Usage (Batch 15 / F5 + docs/16 §8.2) ───
        // Bound to the pre-existing AgentViewModel counters; live updates
        // arrive via tokenUsageChanged while a task is running. A mini
        // progress bar visualizes usedTokens/maxTokens and the per-request
        // cost (lastCostCents) is shown alongside.
        RowLayout {
            spacing: 6
            Layout.rightMargin: 8

            // Mini progress bar: usedTokens / maxTokens
            Rectangle {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 4
                radius: 2
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle
                Rectangle {
                    width: Math.max(0, Math.min(parent.width,
                        AgentVM.maxTokens > 0
                            ? parent.width * Math.min(1.0, AgentVM.usedTokens / AgentVM.maxTokens)
                            : 0))
                    height: parent.height
                    radius: parent.radius
                    color: AgentVM.tokenUsageRate >= 0.9
                        ? Theme.statusDanger : Theme.brandPrimary
                }
            }

            Text {
                text: qsTr("tokens: %1 / %2").arg(AgentVM.usedTokens).arg(AgentVM.maxTokens)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }

            // docs/16 §8.2: per-request cost (only shown when non-zero)
            Text {
                visible: AgentVM.lastCostCents > 0
                text: qsTr("¥%1/轮").arg((AgentVM.lastCostCents / 100.0).toFixed(2))
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }

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
