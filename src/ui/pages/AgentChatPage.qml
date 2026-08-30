import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Agent chat page - main content area with trace view and input
Item {
    id: root

    signal settingsRequested()

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
                    // No explicit modelData binding here: AgentTraceNode's
                    // required property is auto-filled by the view from the
                    // list model context. Binding "modelData: modelData"
                    // would self-reference and evaluate to undefined,
                    // blanking every trace node.
                }
            }
        }

        // Auto-scroll the trace view as content updates
        // (streaming responses, new nodes, session loading)
        Connections {
            target: AgentVM
            function onTraceChanged() {
                Qt.callLater(function() {
                    traceListView.positionViewAtEnd()
                })
            }
        }

        // ── Input Console ──────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: inputConsole.height + 52

            InputConsole {
                id: inputConsole
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 14
                anchors.leftMargin: 20
                anchors.rightMargin: 20
            }

            // Active model badge (click opens the settings dialog)
            Item {
                id: modelBadge
                anchors.top: inputConsole.bottom
                anchors.topMargin: 6
                anchors.left: inputConsole.left
                anchors.leftMargin: 8
                width: badgeRow.implicitWidth
                height: 16

                readonly property var profile: Settings.activeProfile
                readonly property bool hasProfile: profile && profile.provider !== undefined
                    && profile.provider !== ""

                Row {
                    id: badgeRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        visible: modelBadge.hasProfile
                        color: modelBadge.profile.provider === "ollama"
                            ? Theme.statusSuccess : Theme.brandPrimary
                    }

                    Text {
                        text: {
                            const p = modelBadge.profile
                            if (!modelBadge.hasProfile) return qsTr("未配置模型 · 点击设置")
                            return (p.displayName ? p.displayName : p.provider) + " · " + p.model
                        }
                        color: badgeMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontMono
                    }
                }

                MouseArea {
                    id: badgeMA
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.settingsRequested()
                }
            }
        }
    }
}
