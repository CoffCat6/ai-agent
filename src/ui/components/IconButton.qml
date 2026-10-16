import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Square icon button with hover tint and optional tooltip. Defaults fit the
// 32x32 toolbar pattern; override width/height/radius for compact variants.
// `enabled` is the native Item property: disabling it dims externally (via
// an opacity binding) and the mouse area stops reacting on its own.
Rectangle {
    id: root

    property string icon: ""
    property int iconSize: 16
    property string tooltip: ""
    property color restIconColor: Theme.textSecondary
    property color hoverIconColor: Theme.brandPrimary
    property color restBgColor: "transparent"
    property color hoverBgColor: Theme.surfaceHover

    signal clicked()

    width: 32
    height: 32
    radius: 6
    color: controlArea.containsMouse ? root.hoverBgColor : root.restBgColor
    // docs/25 D1: 键盘焦点环
    border.width: root.activeFocus ? 2 : 0
    border.color: Theme.brandPrimary

    AppIcon {
        anchors.centerIn: parent
        icon: root.icon
        size: root.iconSize
        iconColor: controlArea.containsMouse ? root.hoverIconColor : root.restIconColor
    }

    MouseArea {
        id: controlArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // docs/25 D1: 键盘可达（Tab 聚焦 + 回车/空格激活）
    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    ToolTip.visible: root.tooltip.length > 0 && controlArea.containsMouse
    ToolTip.delay: 600
    ToolTip.text: root.tooltip

    Accessible.role: Accessible.Button
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : root.icon
    Accessible.description: root.tooltip
    Accessible.onPressAction: root.clicked()
}
