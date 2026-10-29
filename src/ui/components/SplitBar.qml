import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Splitter strip placed between two layout siblings. Dragging reports raw
// pointer deltas through dragged(); the host applies them (sign + clamping)
// to its own size. Double-click resets to the host's default. dragActive
// lets the host pause size animations so the panel tracks the cursor
// exactly instead of easing behind it.
//
// orientation:
//   "horizontal" - a vertical seam. The strip itself is 7px wide and one
//                  pixel tall, so it splits the layout horizontally and the
//                  host resizes the column to its right (main.qml's two
//                  panel splitters). This is the default and keeps the
//                  original API untouched.
//   "vertical"   - a horizontal seam. The strip is 7px tall and stretches
//                  across its host, so it splits the layout vertically and
//                  the host resizes the row below it (the review tab's
//                  inline file-tree / diff divider).
Item {
    id: root

    // Seam orientation. "horizontal" = the strip splits the layout
    // horizontally (a vertical line); "vertical" = it splits the layout
    // vertically (a horizontal line).
    property string orientation: "horizontal"
    readonly property bool isVertical: orientation === "vertical"

    signal dragged(real delta)
    signal resetRequested()

    // True while the user is holding the strip; also drives the accent look
    readonly property bool dragActive: dragArea.pressed

    implicitWidth: isVertical ? 1 : 7
    implicitHeight: isVertical ? 7 : 1

    // Resting 1px seam line; thickens into an accent bar on hover/drag.
    // Two variants because the seam is orthogonal to the strip: a vertical
    // seam for the horizontal orientation, a horizontal one for the
    // vertical orientation.
    Rectangle {
        visible: !root.isVertical
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.dragActive || dragArea.containsMouse ? 3 : 1
        radius: 1
        color: root.dragActive || dragArea.containsMouse
            ? Theme.brandPrimary : Theme.borderSubtle

        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    }

    Rectangle {
        visible: root.isVertical
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: root.dragActive || dragArea.containsMouse ? 3 : 1
        radius: 1
        color: root.dragActive || dragArea.containsMouse
            ? Theme.brandPrimary : Theme.borderSubtle

        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        // Wider hit area across the seam. The vertical seam is thinner in
        // the direction it drags, so it needs less bleed there.
        anchors.margins: root.isVertical ? -2 : -3
        hoverEnabled: true
        cursorShape: root.isVertical ? Qt.SizeVerCursor : Qt.SizeHorCursor

        // Deltas must be measured in scene coordinates: the splitter itself
        // moves while dragging, so raw local deltas feed back into the layout
        // (each layout shift re-anchors mouse.x and synthesizes a hover event)
        // and the panel oscillates around the cursor. The scene root is a
        // stationary reference frame, which makes the tracking 1:1 smooth.
        property real lastSceneX: 0
        property real lastSceneY: 0
        onPressed: (mouse) => {
            const scene = mapToItem(null, mouse.x, mouse.y)
            lastSceneX = scene.x
            lastSceneY = scene.y
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            const scene = mapToItem(null, mouse.x, mouse.y)
            if (root.isVertical) {
                root.dragged(scene.y - lastSceneY)
                lastSceneY = scene.y
            } else {
                root.dragged(scene.x - lastSceneX)
                lastSceneX = scene.x
            }
        }
        onDoubleClicked: root.resetRequested()
    }

    ToolTip.visible: dragArea.containsMouse && !root.dragActive
    ToolTip.delay: 600
    ToolTip.text: root.isVertical
        ? qsTr("拖动调节高度 · 双击复位")
        : qsTr("拖动调节宽度 · 双击复位")

    Accessible.role: Accessible.Separator
    Accessible.name: root.isVertical ? qsTr("详情分隔条") : qsTr("面板分隔条")
    Accessible.description: root.isVertical
        ? qsTr("拖动调节详情区域高度，双击复位")
        : qsTr("拖动调节面板宽度，双击复位")
}
