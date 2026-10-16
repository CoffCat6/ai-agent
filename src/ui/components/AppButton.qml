import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Reusable button component with tech-minimalist styling
Rectangle {
    id: root

    // ── Public Properties ───────────────────────────────
    property string text: ""
    property color buttonColor: Theme.borderSubtle
    property color textColor: Theme.textPrimary
    property bool highlighted: false
    // `enabled` is the native Item property - do not redeclare it. Setting
    // it (or disabling an ancestor) propagates to the MouseArea below,
    // which then stops receiving events on its own.

    // ── Signals ────────────────────────────────────────
    signal clicked()

    // ── Styling ────────────────────────────────────────
    implicitHeight: 32
    implicitWidth: buttonText.implicitWidth + 24
    radius: 6
    color: highlighted ? Theme.brandPrimary
                       : (controlArea.containsMouse ? Theme.surfaceHover : buttonColor)
    opacity: enabled ? 1.0 : 0.5
    // docs/25 D1: 键盘焦点环——Tab/回车可达时给出可见提示
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.brandPrimary
                              : (highlighted ? Theme.brandPrimary : Theme.borderSubtle)

    // Press animation
    scale: controlArea.containsPress ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: 100 } }

    // ── Content ────────────────────────────────────────
    Text {
        id: buttonText
        anchors.centerIn: parent
        text: root.text
        color: highlighted ? Theme.bgPrimary : textColor
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        font.weight: highlighted ? Font.DemiBold : Font.Normal
    }

    // ── Interaction ───────────────────────────────────
    MouseArea {
        id: controlArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // docs/25 D1: 纯 MouseArea 按钮默认不可 Tab 聚焦——基类统一补键盘
    // 激活路径，所有实例（含禁用态）一次性受益。
    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    Accessible.role: Accessible.Button
    Accessible.name: root.text
    Accessible.onPressAction: root.clicked()
}
