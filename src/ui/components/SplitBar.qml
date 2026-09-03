import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Vertical splitter strip placed between main-layout columns. Dragging
// reports raw horizontal mouse deltas through dragged(); the host applies
// them (sign + clamping) to the panel width. Double-click resets to the
// default width. dragActive lets the host pause width animations so the
// panel tracks the cursor exactly instead of easing behind it.
Item {
    id: root

    signal dragged(real delta)
    signal resetRequested()

    // True while the user is holding the strip; also drives the accent look
    readonly property bool dragActive: dragArea.pressed

    implicitWidth: 7
    implicitHeight: 1

    // Resting 1px seam line; thickens into an accent bar on hover/drag
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.dragActive || dragArea.containsMouse ? 3 : 1
        radius: 1
        color: root.dragActive || dragArea.containsMouse
            ? Theme.brandPrimary : Theme.borderSubtle

        Behavior on color { ColorAnimation { duration: 120 } }
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        anchors.margins: -3   // wider hit area across the seam
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor

        // Deltas must be measured in scene coordinates: the splitter itself
        // moves while dragging, so raw local deltas feed back into the layout
        // (each layout shift re-anchors mouse.x and synthesizes a hover event)
        // and the panel oscillates around the cursor. The scene root is a
        // stationary reference frame, which makes the tracking 1:1 smooth.
        property real lastSceneX: 0
        onPressed: (mouse) => lastSceneX = mapToItem(null, mouse.x, mouse.y).x
        onPositionChanged: (mouse) => {
            if (!pressed) return
            const sceneX = mapToItem(null, mouse.x, mouse.y).x
            root.dragged(sceneX - lastSceneX)
            lastSceneX = sceneX
        }
        onDoubleClicked: root.resetRequested()
    }

    ToolTip.visible: dragArea.containsMouse && !root.dragActive
    ToolTip.delay: 600
    ToolTip.text: qsTr("拖动调节宽度 · 双击复位")
}
