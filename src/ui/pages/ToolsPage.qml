import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    Flickable {
        anchors.fill: parent
        clip: true
        contentHeight: contentCol.implicitHeight + 24
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: parent.width
            spacing: 12

            Item { Layout.preferredHeight: 4 }

            Text {
                text: qsTr("工具管理")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Text {
                text: qsTr("控制哪些工具可被 AI 调用。禁用的工具不会暴露给模型。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Repeater {
                model: AgentVM.toolRegistryModel

                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    implicitHeight: row.implicitHeight + 20
                    color: Theme.bgSecondary
                    radius: 8

                    RowLayout {
                        id: row
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                spacing: 6
                                Text {
                                    text: modelData.name
                                    color: Theme.textPrimary
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontSans
                                }
                                Text {
                                    text: modelData.source === "MCP" ? "· MCP" : "· " + qsTr("内置")
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                    font.family: Theme.fontSans
                                }
                                Text {
                                    text: "· " + (modelData.level === "safe" ? qsTr("自动")
                                                : modelData.level === "requires_confirm" ? qsTr("需确认")
                                                : qsTr("危险"))
                                    color: modelData.level === "dangerous" ? "#e85d5d" : Theme.textSecondary
                                    font.pixelSize: 10
                                    font.family: Theme.fontSans
                                }
                            }

                            Text {
                                text: modelData.description
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                opacity: 0.85
                            }
                        }

                        Switch {
                            checked: modelData.enabled
                            onToggled: AgentVM.setToolEnabled(modelData.name, checked)
                        }
                    }
                }
            }
        }
    }
}
