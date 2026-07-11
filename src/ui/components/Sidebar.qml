import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Left sidebar - session history and navigation
Rectangle {
    id: root

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
                onClicked: AgentVM.cancel()
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
                model: ListModel {
                    ListElement { title: "Code Review Session"; time: "2 min ago" }
                    ListElement { title: "Document Analysis"; time: "1 hour ago" }
                    ListElement { title: "Bug Investigation"; time: "3 hours ago" }
                    ListElement { title: "Data Processing"; time: "Yesterday" }
                    ListElement { title: "API Integration"; time: "2 days ago" }
                }

                delegate: ItemDelegate {
                    width: sessionList.width
                    height: 48

                    background: Rectangle {
                        color: hovered ? Theme.surfaceHover : "transparent"
                        Rectangle {
                            width: 3
                            height: parent.height
                            color: Theme.brandPrimary
                            visible: index === 0
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 12
                        spacing: 2

                        Text {
                            text: model.title
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: model.time
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                        }
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

                // Settings icon
                Text {
                    text: "⚙"
                    color: Theme.textSecondary
                    font.pixelSize: 18
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
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
