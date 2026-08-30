import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI

// Context panel - context budget and knowledge base management
Rectangle {
    id: root

    color: Theme.bgSecondary
    border.width: 0

    // Document import dialog (handled off-GUI-thread by AgentViewModel)
    FileDialog {
        id: kbFileDialog
        nameFilters: [qsTr("文本文件 (*.txt *.md *.json *.csv *.log)"), qsTr("所有文件 (*)")]
        onAccepted: AgentVM.importDocument(selectedFile)
    }

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
        anchors.margins: 20
        spacing: 12

        // ── Knowledge Base Section ───────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: qsTr("知识库")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
                Layout.fillWidth: true
            }

            Text {
                text: "+"
                color: addMouse.containsMouse ? Theme.brandPrimary : Theme.textSecondary
                font.pixelSize: 16
                font.bold: true

                MouseArea {
                    id: addMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: kbFileDialog.open()
                }
            }
        }

        Text {
            visible: AgentVM.knowledgeDocuments.length === 0
            text: qsTr("导入文本文件（.txt / .md …），\n模型即可检索其内容回答问题")
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: AgentVM.knowledgeDocuments.length > 0

            ListView {
                id: kbList
                model: AgentVM.knowledgeDocuments
                spacing: 4

                delegate: Rectangle {
                    width: kbList.width
                    height: kbRow.implicitHeight + 14
                    radius: 6
                    color: kbHover.hovered ? Theme.surfaceHover : "transparent"

                    // Passive hover detection (does not steal the delete click)
                    HoverHandler { id: kbHover }

                    RowLayout {
                        id: kbRow
                        anchors.fill: parent
                        anchors.margins: 7
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: modelData.name
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            Text {
                                text: modelData.chunks + qsTr(" 段 · ") + modelData.chars + qsTr(" 字符")
                                color: Theme.textSecondary
                                font.pixelSize: 9
                                font.family: Theme.fontMono
                            }
                        }

                        Text {
                            text: "×"
                            color: kbDeleteMA.containsMouse ? Theme.statusDanger : Theme.textSecondary
                            font.pixelSize: 14
                            font.bold: true

                            MouseArea {
                                id: kbDeleteMA
                                anchors.fill: parent
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: AgentVM.removeDocument(modelData.name)
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true; visible: AgentVM.knowledgeDocuments.length === 0 }
    }
}
