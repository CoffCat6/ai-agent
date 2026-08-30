import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Left sidebar - session history and navigation
Rectangle {
    id: root

    signal settingsRequested()
    signal auditRequested()

    color: Theme.bgSecondary
    border.width: 0

    // Right border separator
    Rectangle {
        width: 1
        height: parent.height
        anchors.right: parent.right
        color: Theme.borderSubtle
    }

    // ── Collapse Handle ────────────────────────────
    Rectangle {
        id: collapseHandle
        width: 24
        height: 56
        radius: height / 2
        anchors.right: parent.right
        anchors.rightMargin: -12
        anchors.verticalCenter: parent.verticalCenter
        z: 10

        color: {
            if (collapseMA.containsMouse && AgentVM.isSidebarExpanded) return Theme.brandPrimary
            if (collapseMA.containsMouse) return Theme.surfaceHover
            return Theme.bgSecondary
        }
        border.width: 1
        border.color: collapseMA.containsMouse && AgentVM.isSidebarExpanded
            ? Theme.brandPrimary : Theme.borderSubtle

        Behavior on color { ColorAnimation { duration: 150 } }

        // Chevron arrow drawn via Canvas for crisp rendering at any scale
        Canvas {
            id: arrowIcon
            anchors.centerIn: parent
            width: 8
            height: 14

            onPaint: {
                var ctx = getContext("2d")
                ctx.strokeStyle = collapseMA.containsMouse && AgentVM.isSidebarExpanded
                    ? Theme.bgPrimary : Theme.textSecondary
                ctx.lineWidth = 2.2
                ctx.lineCap = "round"
                ctx.lineJoin = "round"

                ctx.beginPath()
                ctx.moveTo(width - 1.5, 1.5)
                ctx.lineTo(1.5, height / 2)
                ctx.lineTo(width - 1.5, height - 1.5)
                ctx.stroke()
            }

            // Arrow rotates 180° when sidebar toggles
            transform: Rotation {
                origin.x: width / 2
                origin.y: height / 2
                angle: AgentVM.isSidebarExpanded ? 0 : 180
                Behavior on angle { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
            }
        }

        MouseArea {
            id: collapseMA
            anchors.fill: parent
            anchors.margins: -4   // Slightly larger hit area
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: AgentVM.toggleSidebar()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // ── Logo / Title ───────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8

                // Logo mark
                Rectangle {
                    width: 24
                    height: 24
                    radius: 4
                    color: Theme.brandPrimary

                    Text {
                        anchors.centerIn: parent
                        text: "N"
                        color: Theme.bgPrimary
                        font.pixelSize: 14
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                }

                Text {
                    text: "NexusAI"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.weight: Font.Bold
                    font.family: Theme.fontSans
                }
            }
        }

        // ── New Chat Button ───────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.preferredHeight: 36
            radius: 6
            color: newChatMA.containsMouse ? Theme.surfaceHover : "transparent"
            border.width: 1
            border.color: Theme.borderSubtle

            Text {
                anchors.centerIn: parent
                text: "+ New Chat"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
            }

            MouseArea {
                id: newChatMA
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: AgentVM.newChat()
            }
        }

        // ── Session History ──────────────────────────
        Text {
            Layout.topMargin: 16
            Layout.leftMargin: 20
            text: "SESSIONS"
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontMono
            font.weight: Font.DemiBold
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true

            ListView {
                id: sessionList
                model: AgentVM.sessionList
                spacing: 2
                clip: true

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: sessionList.count === 0
                    text: qsTr("暂无历史会话")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }

                delegate: ItemDelegate {
                    id: sessionDelegate
                    width: sessionList.width
                    height: 48

                    readonly property bool isActive: AgentVM.currentSessionId === modelData.id
                    property bool renaming: false
                    property bool confirmDelete: false

                    background: Rectangle {
                        color: hovered ? Theme.surfaceHover : "transparent"
                        Rectangle {
                            width: 3
                            height: parent.height
                            color: Theme.brandPrimary
                            visible: isActive
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 12
                        spacing: 2

                        // Title row: inline editor / label / hover actions
                        Item {
                            Layout.fillWidth: true
                            height: 20

                            TextInput {
                                id: renameInput
                                visible: sessionDelegate.renaming
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 8
                                text: modelData.title
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                selectByMouse: true
                                clip: true
                                onAccepted: {
                                    if (text.trimmed().length > 0)
                                        AgentVM.renameSession(modelData.id, text)
                                    sessionDelegate.renaming = false
                                }
                                onFocusChanged: if (!focus && sessionDelegate.renaming)
                                    sessionDelegate.renaming = false

                                Component.onCompleted: if (sessionDelegate.renaming) forceActiveFocus()
                            }

                            Text {
                                visible: !sessionDelegate.renaming
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.title && modelData.title.length > 0
                                    ? modelData.title : qsTr("新会话")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                                width: parent.width - (sessionDelegate.hovered ? 40 : 0)
                            }

                            Row {
                                visible: sessionDelegate.hovered && !sessionDelegate.renaming
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                spacing: 4

                                Text {
                                    text: "✎"
                                    color: renameMA.containsMouse ? Theme.brandPrimary : Theme.textSecondary
                                    font.pixelSize: 11

                                    MouseArea {
                                        id: renameMA
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            sessionDelegate.confirmDelete = false
                                            sessionDelegate.renaming = true
                                        }
                                    }
                                }

                                Text {
                                    text: sessionDelegate.confirmDelete ? qsTr("确认?") : "×"
                                    color: deleteMA.containsMouse || sessionDelegate.confirmDelete
                                        ? Theme.statusDanger : Theme.textSecondary
                                    font.pixelSize: sessionDelegate.confirmDelete ? 9 : 13
                                    font.bold: true

                                    MouseArea {
                                        id: deleteMA
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (!sessionDelegate.confirmDelete) {
                                                sessionDelegate.confirmDelete = true
                                                confirmResetTimer.restart()
                                            } else {
                                                confirmResetTimer.stop()
                                                sessionDelegate.confirmDelete = false
                                                AgentVM.deleteSession(modelData.id)
                                            }
                                        }
                                    }
                                }
                            }

                            Timer {
                                id: confirmResetTimer
                                interval: 2500
                                onTriggered: sessionDelegate.confirmDelete = false
                            }
                        }

                        Text {
                            text: modelData.timeText ? modelData.timeText : ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                        }
                    }

                    onClicked: {
                        if (sessionDelegate.renaming) return
                        sessionDelegate.confirmDelete = false
                        AgentVM.loadSession(modelData.id)
                    }
                }
            }
        }

        // ── Bottom Bar: Settings + Theme Toggle ──────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            Rectangle {
                width: parent.width
                height: 1
                anchors.top: parent.top
                color: Theme.borderSubtle
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                // Audit log icon
                Text {
                    text: "🛡"
                    color: Theme.textSecondary
                    font.pixelSize: 15
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.auditRequested()
                    }
                }

                // Settings icon
                Text {
                    text: "⚙"
                    color: Theme.textSecondary
                    font.pixelSize: 18
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.settingsRequested()
                    }
                }

                Item { Layout.fillWidth: true }

                // Theme toggle
                Rectangle {
                    width: 48
                    height: 24
                    radius: 12
                    color: ThemeManager.isDark ? Theme.brandPrimary : Theme.surfaceHover
                    border.width: 1
                    border.color: Theme.borderSubtle

                    Text {
                        text: ThemeManager.isDark ? "🌙" : "☀"
                        font.pixelSize: 12
                        anchors.centerIn: parent
                    }

                    // Toggle knob
                    Rectangle {
                        width: 18
                        height: 18
                        radius: 9
                        anchors.verticalCenter: parent.verticalCenter
                        x: ThemeManager.isDark ? 27 : 3
                        color: Theme.bgSecondary
                        border.width: 1
                        border.color: Theme.borderSubtle

                        Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ThemeManager.isDark = !ThemeManager.isDark
                    }
                }
            }
        }
    }
}
