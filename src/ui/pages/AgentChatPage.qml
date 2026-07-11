import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Agent chat page - main content area with trace view and input
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Top Bar ────────────────────────────────────
        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
        }

        // ── Trace View (scrollable) ────────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: traceListView
                model: AgentVM.traceModel
                spacing: 0

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: traceListView.count === 0
                    text: "Send a message to start chatting with NexusAI"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                }

                delegate: AgentTraceNode {
                    width: traceListView.width
                    modelData: modelData
                }
            }
        }

        // ── Input Console ──────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: inputConsole.height + 40

            InputConsole {
                id: inputConsole
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 20
            }
        }
    }
}
