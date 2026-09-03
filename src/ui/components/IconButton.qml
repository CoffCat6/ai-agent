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

    ToolTip.visible: root.tooltip.length > 0 && controlArea.containsMouse
    ToolTip.delay: 600
    ToolTip.text: root.tooltip
}
