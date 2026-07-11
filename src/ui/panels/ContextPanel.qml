import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Context panel - token budget and tool sandbox status
Rectangle {
    id: root

    color: Theme.bgSecondary
    border.width: 0

    // Left border separator
    Rectangle {
        width: 1
        height: parent.height
        anchors.left: parent.left
        color: Theme.borderSubtle
    }

    // ── Collapse Handle ────────────────────────────
    Rectangle {
        id: collapseHandle
        width: 24
        height: 56
        radius: height / 2
        anchors.left: parent.left
        anchors.leftMargin: -12
        anchors.verticalCenter: parent.verticalCenter
        z: 10

        color: {
            if (collapseMA.containsMouse && AgentVM.isPanelVisible) return Theme.brandPrimary
            if (collapseMA.containsMouse) return Theme.surfaceHover
            return Theme.bgSecondary
        }
        border.width: 1
        border.color: collapseMA.containsMouse && AgentVM.isPanelVisible
            ? Theme.brandPrimary : Theme.borderSubtle

        Behavior on color { ColorAnimation { duration: 150 } }

        // Chevron arrow via Canvas
        Canvas {
            id: arrowIcon
            anchors.centerIn: parent
            width: 8
            height: 14

            onPaint: {
                var ctx = getContext("2d")
                ctx.strokeStyle = collapseMA.containsMouse && AgentVM.isPanelVisible
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

            // Arrow rotates 180° when panel toggles
            transform: Rotation {
                origin.x: width / 2
                origin.y: height / 2
                angle: AgentVM.isPanelVisible ? 0 : 180
                Behavior on angle { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
            }
        }

        MouseArea {
            id: collapseMA
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: AgentVM.togglePanel()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // ── Context Budget Section ───────────────────
        Text {
            text: qsTr("Context Budget")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.bold: true
            font.family: Theme.fontMono
            Layout.fillWidth: true
        }

        // Token usage progress bar (custom, not using ProgressBar to avoid style issues)
        Item {
            Layout.fillWidth: true
            height: 8

            // Background track
            Rectangle {
                anchors.fill: parent
                color: Theme.surfaceHover
                radius: 3
            }

            // Fill bar
            Rectangle {
                height: parent.height
                width: parent.width * AgentVM.tokenUsageRate
                color: AgentVM.tokenUsageRate > 0.8 ? Theme.statusDanger :
                       AgentVM.tokenUsageRate > 0.6 ? Theme.statusWarning :
                       Theme.brandPrimary
                radius: 3

                Behavior on color { ColorAnimation { duration: 200 } }
                Behavior on width { NumberAnimation { duration: 200 } }
            }
        }

        // Token count text
        Text {
            text: AgentVM.usedTokens + " / " + AgentVM.maxTokens + " Tokens"
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
            Layout.fillWidth: true
        }

        // ── Divider ──────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderSubtle
        }

        // ── Tool Sandbox Section ─────────────────────
        Text {
            text: qsTr("Tool Sandbox")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.bold: true
            font.family: Theme.fontMono
            Layout.fillWidth: true
        }

        // Tool list
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: toolListView
                model: AgentVM.toolRegistryModel
                spacing: 4

                delegate: RowLayout {
                    width: toolListView.width
                    height: 28
                    spacing: 8

                    // Status indicator dot
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: {
                            var level = modelData.level
                            if (level === "safe") return Theme.statusSuccess
                            if (level === "requires_confirm") return Theme.statusWarning
                            if (level === "dangerous") return Theme.statusDanger
                            return Theme.textSecondary
                        }
                    }

                    // Tool name
                    Text {
                        text: modelData.name
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        Layout.fillWidth: true
                    }

                    // Level badge
                    Rectangle {
                        width: levelText.implicitWidth + 12
                        height: 16
                        radius: 8
                        color: {
                            var level = modelData.level
                            if (level === "safe") return Qt.rgba(0, 1, 0.58, 0.15)
                            if (level === "requires_confirm") return Qt.rgba(1, 0.72, 0, 0.15)
                            if (level === "dangerous") return Qt.rgba(1, 0.23, 0.19, 0.15)
                            return Theme.surfaceHover
                        }

                        Text {
                            id: levelText
                            anchors.centerIn: parent
                            text: {
                                var level = modelData.level
                                if (level === "safe") return "SAFE"
                                if (level === "requires_confirm") return "CONFIRM"
                                if (level === "dangerous") return "BLOCKED"
                                return level.toUpperCase()
                            }
                            color: {
                                var level = modelData.level
                                if (level === "safe") return Theme.statusSuccess
                                if (level === "requires_confirm") return Theme.statusWarning
                                if (level === "dangerous") return Theme.statusDanger
                                return Theme.textSecondary
                            }
                            font.pixelSize: 9
                            font.family: Theme.fontMono
                            font.bold: true
                        }
                    }
                }
            }
        }

        // ── Divider ──────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderSubtle
        }

        // ── System Info ──────────────────────────────
        Text {
            text: qsTr("System")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.bold: true
            font.family: Theme.fontMono
            Layout.fillWidth: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Engine:")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "NexusAI v0.1"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Theme:")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: ThemeManager.isDark ? "Dark" : "Light"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
            }
        }
    }
}
