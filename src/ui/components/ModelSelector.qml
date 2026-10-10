import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Dropdown model & profile selector used below the chat input. The popup
// opens UPWARD because the trigger sits near the window bottom edge.
Item {
    id: root
    enabled: !AgentVM.stateBusy
    opacity: enabled ? 1.0 : 0.6

    signal settingsRequested()

    readonly property var activeProfile: AgentVM.chatModelProfile
    readonly property bool hasProfile: activeProfile
        && activeProfile.provider !== undefined && activeProfile.provider !== ""
    // 自定义网关可以在填模型名之前就被选中，列表里的空串会让选择器出现一行空白项。
    readonly property var currentModels: root.hasProfile
        ? (root.activeProfile.models || []).filter(function (m) { return String(m) !== "" })
        : []

    // docs/15 §4.3.2: brief hint of the new context window after switching
    // models so the user perceives the budget change (optional enhancement).
    ToolTip {
        id: windowHint
        parent: selectorBox
        font.pixelSize: 10
        font.family: Theme.fontMono
    }
    function showWindowHint(apiProtocol, model) {
        const cw = Settings.modelContextWindow(apiProtocol, model)
        const hint = cw > 0
            ? qsTr("上下文窗口：%1K").arg(Math.round(cw / 1000))
            : qsTr("上下文窗口：未知（使用全局默认）")
        windowHint.show(hint, 2500)
    }

    implicitWidth: selectorRow.implicitWidth + 20
    implicitHeight: 28

    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("模型选择器")
    Accessible.description: hasProfile
        ? (activeProfile.displayName || activeProfile.provider) + " · " + (activeProfile.model || "")
        : qsTr("未配置模型")

    // Trigger pill button
    Rectangle {
        id: selectorBox
        anchors.fill: parent
        radius: 6
        color: selectorMA.containsMouse || menuPopup.opened
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: menuPopup.opened || selectorMA.containsMouse
            ? Theme.brandPrimary : "transparent"

        Behavior on color { ColorAnimation { duration: 150 } }

        Row {
            id: selectorRow
            anchors.centerIn: parent
            spacing: 6

            // Provider indicator dot
            Rectangle {
                width: 6
                height: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                visible: root.hasProfile
                color: root.activeProfile.provider === "ollama"
                    ? Theme.statusSuccess : Theme.brandPrimary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (!root.hasProfile) return qsTr("未配置模型 · 点击设置")
                    const p = root.activeProfile
                    return (p.displayName ? p.displayName : p.provider)
                        + " · " + (p.model ? p.model : qsTr("未指定模型"))
                }
                color: selectorMA.containsMouse || menuPopup.opened
                    ? Theme.brandPrimary : Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontMono
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: menuPopup.opened ? "chevron-up" : "chevron-down"
                size: 11
                iconColor: selectorMA.containsMouse || menuPopup.opened
                    ? Theme.brandPrimary : Theme.textSecondary
            }
        }

        MouseArea {
            id: selectorMA
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: menuPopup.opened ? menuPopup.close() : menuPopup.open()
        }
    }

    // Dropdown popup (opens upward, scrolls when tall)
    Popup {
        id: menuPopup
        x: 0
        y: -height - 6          // opens above the trigger
        width: Math.max(280, root.width)
        height: Math.min(menuContent.implicitHeight + 12,
                         Math.max(200, root.mapToItem(null, 0, 0).y - 12))
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: DialogBackground {}

        contentItem: ScrollView {
            id: menuScroll
            clip: true

            ColumnLayout {
                id: menuContent
                width: menuScroll.width
                spacing: 2

                // ── Section 1: models of the active profile ──
                Text {
                    visible: root.hasProfile && root.currentModels.length > 0
                    text: root.hasProfile
                        ? qsTr("模型 · %1").arg(root.activeProfile.displayName
                                               || root.activeProfile.provider)
                        : ""
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                    font.family: Theme.fontMono
                    Layout.leftMargin: 8
                    Layout.topMargin: 2
                }

                Repeater {
                    model: root.currentModels

                    delegate: Rectangle {
                        id: modelItem
                        readonly property bool isSelected: modelData === root.activeProfile.model
                        Layout.fillWidth: true
                        Layout.leftMargin: 2
                        Layout.rightMargin: 2
                        Layout.preferredHeight: 28
                        radius: 5
                        color: modelMA.containsMouse ? Theme.surfaceHover : "transparent"

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
                                visible: modelItem.isSelected
                            }

                            Item {
                                Layout.preferredWidth: 12
                                Layout.preferredHeight: 12
                                visible: !modelItem.isSelected
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: modelItem.isSelected
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                font.bold: modelItem.isSelected
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: modelMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                AgentVM.selectChatModel(
                                    root.activeProfile.id, modelData)
                                menuPopup.close()
                                root.showWindowHint(root.activeProfile.apiProtocol, modelData)
                            }
                        }
                    }
                }

                Text {
                    visible: root.hasProfile && root.currentModels.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    text: qsTr("这条配置还没有可选模型：先在「管理模型配置…」里填写模型名。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    Layout.preferredHeight: 1
                    color: Theme.borderSubtle
                }

                // ── Section 2: profile switcher ──
                Text {
                    text: qsTr("配置 · 共 %1 个").arg(Settings.profiles.length)
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                    font.family: Theme.fontMono
                    Layout.leftMargin: 8
                }

                Repeater {
                    model: Settings.profiles

                    delegate: Rectangle {
                        id: profileItem
                        readonly property bool isCurrent: modelData.id === root.activeProfile.id
                        Layout.fillWidth: true
                        Layout.leftMargin: 2
                        Layout.rightMargin: 2
                        Layout.preferredHeight: 28
                        radius: 5
                        color: profileMA.containsMouse ? Theme.surfaceHover : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 6

                            Rectangle {
                                Layout.preferredWidth: 6
                                Layout.preferredHeight: 6
                                radius: 3
                                color: modelData.provider === "ollama"
                                    ? Theme.statusSuccess : Theme.brandPrimary
                                visible: profileItem.isCurrent
                            }

                            Item {
                                Layout.preferredWidth: 6
                                Layout.preferredHeight: 6
                                visible: !profileItem.isCurrent
                            }

                            Text {
                                Layout.fillWidth: true
                                text: (modelData.displayName || modelData.provider)
                                    + " · " + (modelData.model
                                               ? modelData.model : qsTr("未指定模型"))
                                color: profileItem.isCurrent
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: profileMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                AgentVM.selectChatModel(modelData.id)
                                menuPopup.close()
                                root.showWindowHint(modelData.apiProtocol, modelData.model)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    Layout.preferredHeight: 1
                    color: Theme.borderSubtle
                }

                // ── Section 3: open full settings ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    Layout.bottomMargin: 2
                    Layout.preferredHeight: 30
                    radius: 5
                    color: settingsMA.containsMouse ? Theme.surfaceHover : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6

                        AppIcon {
                            Layout.preferredWidth: 13
                            Layout.preferredHeight: 13
                            icon: "settings"
                            size: 13
                            iconColor: Theme.textSecondary
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("管理模型配置…")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                        }
                    }

                    MouseArea {
                        id: settingsMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            menuPopup.close()
                            root.settingsRequested()
                        }
                    }
                }
            }
        }
    }
}
