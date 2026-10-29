import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Session permission mode selector (只读 / 变更确认 / 自动编辑 / 完全访问).
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
    readonly property bool isPlanOnly: mode === AgentVM.permissionModePlanOnly

    // Pill accent + label follow the risk level: safe → neutral → brand → danger.
    readonly property color accentColor: isFullAccess ? Theme.statusDanger
                                    : isAutoEdit ? Theme.brandPrimary
                                    : isPlanOnly ? Theme.statusSuccess
                                    : Theme.textSecondary
    readonly property string modeLabel:
        mode === AgentVM.permissionModeFullAccess ? qsTr("完全访问")
        : mode === AgentVM.permissionModeAutoEdit ? qsTr("自动编辑")
        : mode === AgentVM.permissionModePlanOnly ? qsTr("只读")
        : qsTr("变更确认")

    // 回归用例用：弹层是否打开（Popup 不是 Item，测试取不到它）。
    readonly property bool opened: modePopup.opened

    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("会话权限模式") + " · " + modeLabel
    // docs/25 D4: 触发器键盘可达——Tab 聚焦，回车/空格/下方向键开合菜单
    Accessible.onPressAction: root.toggleMenu()
    activeFocusOnTab: true
    function toggleMenu() {
        modePopup.opened ? modePopup.close() : modePopup.open()
    }
    Keys.onReturnPressed: root.toggleMenu()
    Keys.onSpacePressed: root.toggleMenu()
    Keys.onDownPressed: root.toggleMenu()

    // Trigger pill (ModelSelector styling)
    Rectangle {
        id: triggerBox
        objectName: "permissionModeTrigger"
        anchors.fill: parent
        radius: 6
        color: triggerMA.containsMouse || modePopup.opened || root.activeFocus
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: modePopup.opened || root.activeFocus
            ? root.accentColor : "transparent"

        Behavior on color { ColorAnimation { duration: Theme.motionBase } }

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
                font.pixelSize: Theme.fontSizeMicro
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
            // 再点一次关闭（修复）：原先 onClicked + CloseOnPressOutside 的组合
            // 有竞态——弹层在**按下**时就被关掉，抬起时 onClicked 读到 opened
            // 为 false 又打开一次，表现为"点第二次还是开着"。
            // 现在把判断放在 onPressed（此刻弹层状态还没被外部点击策略改写），
            // 并让弹层改用 CloseOnReleaseOutside：鼠标释放时才处理"点外面"，
            // 于是触发器自己的 pressed 处理总是先看到真实状态。
            // 必须在 onPressed 而不是 onReleased/onClicked 上做这个判断：
            // 弹层的 CloseOnReleaseOutside 会在**鼠标释放**时先把弹层关掉，
            // 于是 onReleased/onClicked 读到的 opened 恒为 false，toggle 就
            // 又打开了——用户看到的正是"点第二次还是开着，只能点别处关"。
            // onPressed 发生在弹层的外部点击处理之前，读到的才是点击前状态。
            onPressed: modePopup.opened ? modePopup.close() : modePopup.open()
        }
    }

    Popup {
        id: modePopup
        // docs/25 D4: 打开时接受键盘焦点，菜单项才能进入焦点链
        focus: true
        x: 0
        y: -height - 6          // opens above the trigger
        width: 300
        height: modeContent.implicitHeight + 12
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: modeContent
            spacing: 2

            Repeater {
                model: [
                    {
                        mode: AgentVM.permissionModePlanOnly,
                        label: qsTr("只读（计划模式）"),
                        desc: qsTr("仅分析与检索；写入文件、执行命令、提交与 MCP 工具将被拒绝")
                    },
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
                    color: modeMA.containsMouse || modeItem.activeFocus
                        ? Theme.surfaceHover : "transparent"

                    // docs/25 D4: 菜单项键盘激活 + 读屏单选语义
                    //（完全访问需二次确认，与鼠标路径同一分支）
                    function activate() {
                        modePopup.close()
                        if (modelData.mode === AgentVM.permissionModeFullAccess
                                && AgentVM.permissionMode !== AgentVM.permissionModeFullAccess)
                            root.confirmFullAccess()
                        else
                            AgentVM.permissionMode = modelData.mode
                    }

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
                                       ? Theme.statusDanger
                                       : modelData.mode === AgentVM.permissionModePlanOnly
                                         ? Theme.statusSuccess : Theme.textPrimary)
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                font.bold: modeItem.isSelected
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.desc
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeMicro
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
                        onClicked: modeItem.activate()
                    }
                    activeFocusOnTab: true
                    Keys.onReturnPressed: modeItem.activate()
                    Keys.onSpacePressed: modeItem.activate()
                    Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                    Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.label
                    Accessible.description: modelData.desc
                    Accessible.checked: modeItem.isSelected
                    Accessible.onPressAction: modeItem.activate()
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
