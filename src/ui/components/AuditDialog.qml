import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Tool-activity audit trail viewer (SQLite-persisted via SqliteMemory).
// Entries are loaded fresh on every open.
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0

    property var entries: []

    onAboutToShow: entries = AgentVM.auditLog(200)

    background: Rectangle {
        color: Theme.bgSecondary
        radius: 8
        border.width: 1
        border.color: Theme.borderSubtle
    }

    contentItem: ColumnLayout {
        spacing: 12
        width: 620
        height: 460

        Text {
            Layout.leftMargin: 24
            Layout.topMargin: 24
            text: qsTr("审计日志")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
        }

        Text {
            Layout.leftMargin: 24
            text: qsTr("最近 %1 条工具操作记录").arg(entries.length)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }

        ListView {
            id: auditList
            model: dialog.entries
            clip: true
            spacing: 4
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24

            ScrollBar.vertical: ScrollBar {}

            Text {
                anchors.centerIn: parent
                visible: auditList.count === 0
                text: qsTr("暂无记录")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
            }

            delegate: Rectangle {
                width: auditList.width
                height: auditRow.implicitHeight + 12
                radius: 6
                color: Theme.surfaceHover

                RowLayout {
                    id: auditRow
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 10

                    // Result / event badge
                    Rectangle {
                        width: badge.implicitWidth + 12
                        height: 16
                        radius: 8
                        color: {
                            var ev = modelData.event
                            if (ev === "tool_execute")
                                return modelData.success ? Qt.rgba(0, 1, 0.58, 0.15) : Qt.rgba(1, 0.23, 0.19, 0.15)
                            if (ev === "tool_declined" || ev === "tool_blocked" || ev === "unknown_tool")
                                return Qt.rgba(1, 0.72, 0, 0.15)
                            return Qt.rgba(0.4, 0.6, 1, 0.15)
                        }

                        Text {
                            id: badge
                            anchors.centerIn: parent
                            text: {
                                var ev = modelData.event
                                if (ev === "tool_execute") return modelData.success ? "OK" : "FAIL"
                                if (ev === "tool_approved") return "ALLOW"
                                if (ev === "tool_declined") return "DENIED"
                                if (ev === "tool_blocked") return "BLOCKED"
                                if (ev === "unknown_tool") return "UNKNOWN"
                                return ev.toUpperCase()
                            }
                            font.pixelSize: 9
                            font.family: Theme.fontMono
                            font.bold: true
                            color: {
                                var ev = modelData.event
                                if (ev === "tool_execute")
                                    return modelData.success ? Theme.statusSuccess : Theme.statusDanger
                                if (ev === "tool_declined" || ev === "tool_blocked" || ev === "unknown_tool")
                                    return Theme.statusWarning
                                return Theme.brandPrimary
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: modelData.toolName + "  ·  " + modelData.event
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: (modelData.detail || "") !== ""
                            text: modelData.detail || ""
                            color: Theme.textSecondary
                            font.pixelSize: 9
                            font.family: Theme.fontSans
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: (modelData.arguments || "") !== ""
                            text: modelData.arguments || ""
                            color: Theme.textSecondary
                            opacity: 0.7
                            font.pixelSize: 9
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }

                    Text {
                        text: modelData.timestamp
                            ? new Date(modelData.timestamp).toLocaleString(Qt.locale(), "MM-dd HH:mm:ss")
                            : ""
                        color: Theme.textSecondary
                        font.pixelSize: 9
                        font.family: Theme.fontMono
                    }
                }
            }
        }

        // ── Footer ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 24
            spacing: 8

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("关闭")
                highlighted: true
                onClicked: dialog.close()
            }
        }
    }
}
