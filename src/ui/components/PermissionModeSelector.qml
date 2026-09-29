import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Session permission mode selector (变更确认 / 自动编辑 / 完全访问).
// A pill trigger beside the ModelSelector opening an UPWARD popup with one
// entry per mode; switching applies to the ACTIVE tab immediately (even
// mid-task — the next tool call is judged by the new mode). FullAccess
// asks for a one-time confirmation because it auto-runs every tool,
// including shell commands, without per-call approval.
Item {
    id: root

    implicitWidth: modeRow.implicitWidth + 20
    implicitHeight: 28

    readonly property int mode: AgentVM.permissionMode
    readonly property bool isFullAccess: mode === AgentVM.permissionModeFullAccess
    readonly property bool isAutoEdit: mode === AgentVM.permissionModeAutoEdit

    // Pill accent + label follow the risk level: neutral → brand → danger.
    readonly property color accentColor: isFullAccess ? Theme.statusDanger
                                    : isAutoEdit ? Theme.brandPrimary
                                    : Theme.textSecondary
    readonly property string modeLabel:
        mode === AgentVM.permissionModeFullAccess ? qsTr("完全访问")
        : mode === AgentVM.permissionModeAutoEdit ? qsTr("自动编辑")
        : qsTr("变更确认")

    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("会话权限模式") + " · " + modeLabel

    // Trigger pill (ModelSelector styling)
    Rectangle {
        id: triggerBox
        anchors.fill: parent
        radius: 6
        color: triggerMA.containsMouse || modePopup.opened
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: modePopup.opened || root.isFullAccess
            ? root.accentColor : "transparent"

        Behavior on color { ColorAnimation { duration: 150 } }

        Row {
            id: modeRow
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: root.accentColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.modeLabel
                color: triggerMA.containsMouse || modePopup.opened
                    ? root.accentColor : Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontMono
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: modePopup.opened ? "chevron-up" : "chevron-down"
                size: 11
                iconColor: triggerMA.containsMouse || modePopup.opened
                    ? root.accentColor : Theme.textSecondary
            }
        }

        MouseArea {
            id: triggerMA
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: modePopup.opened ? modePopup.close() : modePopup.open()
        }
    }

    Popup {
        id: modePopup
        x: 0
        y: -height - 6          // opens above the trigger
        width: 300
        height: modeContent.implicitHeight + 12
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: modeContent
            spacing: 2

            Repeater {
                model: [
                    {
                        mode: AgentVM.permissionModeConfirmChanges,
                        label: qsTr("变更确认"),
                        desc: qsTr("敏感工具执行前逐次询问（默认）")
                    },
                    {
                        mode: AgentVM.permissionModeAutoEdit,
                        label: qsTr("自动编辑"),
                        desc: qsTr("自动执行文件编辑与搜索抓取；命令与 MCP 工具仍询问")
                    },
                    {
                        mode: AgentVM.permissionModeFullAccess,
                        label: qsTr("完全访问"),
                        desc: qsTr("所有工具自动执行，不再询问")
                    }
                ]

                delegate: Rectangle {
                    id: modeItem
                    required property var modelData
                    readonly property bool isSelected:
                        modelData.mode === AgentVM.permissionMode
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    Layout.preferredHeight: 34
                    radius: 5
                    color: modeMA.containsMouse ? Theme.surfaceHover : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6

                        AppIcon {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            icon: "check"
                            size: 12
                            iconColor: Theme.brandPrimary
                            visible: modeItem.isSelected
                        }

                        Item {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            visible: !modeItem.isSelected
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                text: modelData.label
                                color: modeItem.isSelected
                                    ? Theme.brandPrimary
                                    : (modelData.mode === AgentVM.permissionModeFullAccess
                                       ? Theme.statusDanger : Theme.textPrimary)
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                font.bold: modeItem.isSelected
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.desc
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: modeMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            modePopup.close()
                            if (modelData.mode === AgentVM.permissionModeFullAccess
                                    && AgentVM.permissionMode !== AgentVM.permissionModeFullAccess)
                                root.confirmFullAccess()
                            else
                                AgentVM.permissionMode = modelData.mode
                        }
                    }
                }
            }
        }
    }

    // One-time confirmation before enabling FullAccess on this tab: every
    // tool (shell commands included) will run without per-call approval.
    function confirmFullAccess() {
        fullAccessDialog.open()
    }

    Dialog {
        id: fullAccessDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        modal: true
        closePolicy: Popup.CloseOnEscape

        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("启用完全访问？")
                color: Theme.statusDanger
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("本会话中所有工具（包括 shell 命令、文件写入、MCP 工具）")
                      + qsTr("都将不经逐次确认自动执行，仅在会话内生效，重启后回落到默认模式。确定继续吗？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                AppButton {
                    text: qsTr("取消")
                    onClicked: fullAccessDialog.close()
                }
                AppButton {
                    text: qsTr("启用完全访问")
                    textColor: Theme.statusDanger
                    highlighted: true
                    onClicked: {
                        fullAccessDialog.close()
                        AgentVM.permissionMode = AgentVM.permissionModeFullAccess
                    }
                }
            }
        }
    }
}
