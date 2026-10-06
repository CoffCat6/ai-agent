import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Agent chat page - main content area with trace view and input
Item {
    id: root

    signal settingsRequested()

    // Sending can synchronously insert the user row before requestFollow().
    // Forward the intent immediately instead of waiting for another count change.
    QtObject {
        id: traceJumpImpl
        signal followRequested()
        function requestFollow() {
            followRequested()
        }
    }
    readonly property alias traceJump: traceJumpImpl

    Accessible.name: qsTr("AI 对话页面")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
        }

        SessionTabs {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TranscriptViewport {
                id: transcript
                anchors.fill: parent
                model: AgentVM.traceModel
                scrollBarColor: Theme.textSecondary

                // Keep detail state outside delegates so resets/session changes
                // do not discard the user's expand/collapse choices.
                property var detailState: ({})
                property bool showProcess: true
                readonly property int intermediateCount: AgentVM.intermediateCount

                function detailExpanded(key, defaultExpanded) {
                    return key in detailState ? detailState[key] : defaultExpanded
                }

                function toggleDetail(key, defaultExpanded) {
                    const next = Object.assign({}, detailState)
                    next[key] = !(key in next ? next[key] : defaultExpanded)
                    detailState = next
                }

                function toggleProcess() {
                    showProcess = !showProcess
                    requestScrollToBottom()
                }

                delegate: TraceNodeRouter {
                    width: transcript.availableWidth
                    showProcess: transcript.showProcess
                    detailExpandedFn: function(key, def) {
                        return transcript.detailExpanded(key, def)
                    }
                    toggleDetailFn: function(key, def) {
                        transcript.toggleDetail(key, def)
                    }
                }
            }

            Column {
                id: emptyState
                anchors.centerIn: parent
                width: 260
                spacing: Theme.spaceMd
                visible: transcript.count === 0

                Rectangle {
                    x: (emptyState.width - width) / 2
                    width: 48
                    height: 48
                    radius: Theme.radiusMd
                    color: Theme.brandPrimary
                    opacity: 0.92

                    Text {
                        anchors.centerIn: parent
                        text: "N"
                        color: Theme.bgPrimary
                        font.pixelSize: 26
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                }

                Text {
                    id: emptyText
                    width: parent.width
                    text: qsTr("发送一条消息，开始与 NexusAI 对话")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                visible: transcript.intermediateCount > 0
                anchors.top: parent.top
                anchors.topMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                z: 5
                width: processRow.implicitWidth + 20
                height: 26
                radius: height / 2
                color: processMA.containsMouse ? Theme.surfaceHover : Theme.cardBackground
                border.width: 1
                border.color: Theme.cardBorder
                opacity: 0.94

                Row {
                    id: processRow
                    anchors.centerIn: parent
                    spacing: 6

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        icon: transcript.showProcess ? "chevron-up" : "chevron-down"
                        size: 11
                        iconColor: processMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: transcript.showProcess
                            ? qsTr("折叠过程")
                            : qsTr("思考过程 · %1").arg(transcript.intermediateCount)
                        color: processMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontSans
                    }
                }

                MouseArea {
                    id: processMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: transcript.toggleProcess()
                }

                ToolTip.visible: processMA.containsMouse
                ToolTip.delay: 600
                ToolTip.text: transcript.showProcess
                    ? qsTr("隐藏思考与工具调用过程") : qsTr("显示思考与工具调用过程")
            }

            Rectangle {
                visible: !transcript.pinnedToBottom && transcript.maximumOffset > 0
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 16
                z: 5
                width: 32
                height: 32
                radius: width / 2
                color: Theme.cardBackground
                border.width: 1
                border.color: Theme.cardBorder
                opacity: 0.94

                AppIcon {
                    anchors.centerIn: parent
                    icon: "chevron-down"
                    size: 15
                    iconColor: jumpMA.containsMouse ? Theme.brandPrimary : Theme.textPrimary
                }

                MouseArea {
                    id: jumpMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: transcript.jumpToBottom()
                }

                ToolTip.visible: jumpMA.containsMouse
                ToolTip.delay: 600
                ToolTip.text: qsTr("回到底部")
            }
        }

        Connections {
            target: root.traceJump
            function onFollowRequested() {
                transcript.jumpToBottom()
            }
        }

        Connections {
            target: AgentVM
            function onCurrentSessionIdChanged() {
                transcript.jumpToBottom()
            }
        }

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
                traceJump: root.traceJump
            }

            ModelSelector {
                id: modelBadge
                anchors.top: inputConsole.bottom
                anchors.topMargin: 4
                anchors.left: inputConsole.left
                anchors.leftMargin: 4
                onSettingsRequested: root.settingsRequested()
            }

            ThinkingEffortSelector {
                id: effortBadge
                anchors.top: inputConsole.bottom
                anchors.topMargin: 4
                anchors.left: modelBadge.right
                anchors.leftMargin: 8
            }

            PermissionModeSelector {
                id: permissionBadge
                anchors.top: inputConsole.bottom
                anchors.topMargin: 4
                anchors.left: effortBadge.right
                anchors.leftMargin: 8
            }
        }
    }
}
