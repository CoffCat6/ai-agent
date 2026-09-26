import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Tool-activity audit trail viewer (SQLite-persisted via SqliteMemory).
// Supports event-type filtering, manual/auto refresh and clipboard export.
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    // Fixed content size: a ColumnLayout contentItem cannot derive its own
    // implicit height from a fillHeight ListView, so size the popup here.
    width: 680
    height: 520

    property var entries: []
    property string filter: "all"   // all | execute | approved | denied

    Accessible.name: qsTr("审计日志对话框")
    Accessible.description: qsTr("查看工具执行审计记录，支持筛选与复制")

    readonly property var filtered: entries.filter(function(e) {
        if (filter === "all") return true
        if (filter === "execute") return e.event === "tool_execute"
        if (filter === "approved") return e.event === "tool_approved"
        return e.event === "tool_declined" || e.event === "tool_blocked"
            || e.event === "unknown_tool"
    })

    function reload() {
        entries = AgentVM.auditLog(200)
        logPathCaption.text = AgentVM.logFilePath()
    }

    function toClipboardText() {
        var lines = []
        for (var i = 0; i < filtered.length; ++i) {
            var e = filtered[i]
            var t = e.timestamp ? Qt.formatDateTime(new Date(e.timestamp), "yyyy-MM-dd hh:mm:ss") : "-"
            lines.push(t + "  [" + e.event + "]  " + e.toolName
                       + (e.arguments ? "  " + e.arguments : "")
                       + (e.detail ? "  // " + e.detail : ""))
        }
        return lines.join("\n")
    }

    onAboutToShow: {
        filter = "all"
        reload()
    }

    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.leftMargin: 24
            Layout.topMargin: 24
            text: qsTr("审计日志")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
        }

        // ── Filter chips + summary ────────────────────
        RowLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 8

            Repeater {
                model: [
                    { key: "all", label: qsTr("全部") },
                    { key: "execute", label: qsTr("执行") },
                    { key: "approved", label: qsTr("允许") },
                    { key: "denied", label: qsTr("拒绝/拦截") }
                ]

                delegate: Rectangle {
                    readonly property bool active: dialog.filter === modelData.key
                    width: chipLabel.implicitWidth + 20
                    height: 24
                    radius: 12
                    color: active ? Theme.brandPrimary
                        : (chipMA.containsMouse ? Theme.surfaceHover : "transparent")
                    border.width: 1
                    border.color: active ? Theme.brandPrimary : Theme.borderSubtle

                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        color: active ? Theme.bgPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    MouseArea {
                        id: chipMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dialog.filter = modelData.key
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("显示 %1 / %2 条").arg(dialog.filtered.length).arg(dialog.entries.length)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }

        // ── Entry list ────────────────────────────────
        ListView {
            id: auditList
            model: dialog.filtered
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
                text: dialog.entries.length === 0 ? qsTr("暂无记录") : qsTr("当前筛选条件下暂无记录")
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
        Text {
            id: logPathCaption
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: ""
            color: Theme.textSecondary
            opacity: 0.8
            font.pixelSize: 9
            font.family: Theme.fontMono
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 24
            spacing: 8

            // Refresh
            IconButton {
                width: 28
                height: 28
                icon: "refresh"
                iconSize: 15
                tooltip: qsTr("刷新")
                onClicked: dialog.reload()
            }

            // Copy filtered entries (CopyButton carries the "已复制" feedback)
            CopyButton {
                text: dialog.toClipboardText()
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("关闭")
                highlighted: true
                onClicked: dialog.close()
            }
        }
    }

    // Auto-refresh while the dialog is open (5s) for live monitoring
    Timer {
        running: dialog.visible
        interval: 5000
        repeat: true
        onTriggered: dialog.reload()
    }
}
