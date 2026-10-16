import QtQuick

// Transcript rows must have measured heights before scrolling. Unlike a
// virtualized ListView, this column never changes its extent estimates during
// a scrollbar drag. Model deltas still update only the affected delegates.
Item {
    id: root

    property alias model: messages.model
    property alias delegate: messages.delegate
    readonly property int count: messages.count
    readonly property real contentHeight: flick.contentHeight
    readonly property real availableWidth: flick.width
    readonly property real scrollOffset: flick.contentY
    readonly property real maximumOffset: Math.max(0, flick.contentHeight - flick.height)
    readonly property bool userInteracting: flick.moving || scrollMouse.pressed
    property bool pinnedToBottom: true
    // docs/25 C6: 本组件刻意不 import NexusAI（tests/qml 以相对路径直接
    // 加载，保持 dependency-free），故无法引用 Theme token；此中性灰仅作
    // 模块外回退默认值，生产路径由 AgentChatPage 注入 Theme token。
    property color scrollBarColor: "#808080"

    function clampOffset(offset) {
        return Math.max(0, Math.min(maximumOffset, offset))
    }

    function syncPosition() {
        messageColumn.forceLayout()
        if (userInteracting)
            return
        const next = pinnedToBottom ? maximumOffset : clampOffset(flick.contentY)
        if (Math.abs(flick.contentY - next) > 0.01)
            flick.contentY = next
    }

    function requestScrollToBottom() {
        // Passing the same function coalesces model and geometry notifications.
        Qt.callLater(syncPosition)
    }

    function jumpToBottom() {
        flick.cancelFlick()
        pinnedToBottom = true
        syncPosition()
        requestScrollToBottom()
    }

    function beginUserScroll() {
        pinnedToBottom = false
    }

    function endUserScroll() {
        if (userInteracting)
            return
        pinnedToBottom = maximumOffset - clampOffset(flick.contentY) <= 2
        requestScrollToBottom()
    }

    // Used by wheel input as well as keyboard/test callers. User input owns
    // the offset; geometry updates never re-arm follow while reading history.
    function scrollBy(delta) {
        flick.cancelFlick()
        beginUserScroll()
        flick.contentY = clampOffset(flick.contentY + delta)
        endUserScroll()
    }

    onMaximumOffsetChanged: requestScrollToBottom()
    onModelChanged: jumpToBottom()
    Component.onCompleted: requestScrollToBottom()

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.rightMargin: 14
        clip: true
        contentWidth: width
        contentHeight: messageColumn.height
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        onMovementStarted: root.beginUserScroll()
        onMovementEnded: Qt.callLater(root.endUserScroll)

        Column {
            id: messageColumn
            width: flick.width
            Repeater {
                id: messages
            }
        }

        WheelHandler {
            target: null
            onWheel: function(event) {
                const delta = event.pixelDelta.y !== 0
                    ? event.pixelDelta.y : event.angleDelta.y / 120 * 60
                if (delta !== 0) {
                    root.scrollBy(-delta)
                    event.accepted = true
                }
            }
        }
    }

    // The thumb and content use the same exact scroll range. Minimum thumb
    // height only affects the visual travel, not the endpoint mapping.
    Item {
        id: track
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 14
        visible: root.maximumOffset > 0
        readonly property real travel: Math.max(0, height - thumb.height)

        Rectangle {
            id: thumb
            width: scrollMouse.containsMouse || scrollMouse.pressed ? 8 : 6
            anchors.horizontalCenter: parent.horizontalCenter
            height: Math.min(track.height, Math.max(32,
                        track.height * flick.height / Math.max(1, flick.contentHeight)))
            y: root.maximumOffset > 0
                ? track.travel * root.clampOffset(flick.contentY) / root.maximumOffset : 0
            radius: width / 2
            color: root.scrollBarColor
            opacity: scrollMouse.pressed ? 0.9 : (scrollMouse.containsMouse ? 0.7 : 0.4)
        }

        MouseArea {
            id: scrollMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            property real grabOffset: 0

            function seek(pointerY) {
                if (track.travel <= 0)
                    return
                const fraction = Math.max(0, Math.min(1,
                    (pointerY - grabOffset) / track.travel))
                flick.contentY = fraction * root.maximumOffset
            }

            onPressed: function(mouse) {
                flick.cancelFlick()
                root.beginUserScroll()
                grabOffset = mouse.y >= thumb.y && mouse.y <= thumb.y + thumb.height
                    ? mouse.y - thumb.y : thumb.height / 2
                seek(mouse.y)
            }
            onPositionChanged: function(mouse) {
                if (pressed)
                    seek(mouse.y)
            }
            // Let the derived interaction state update before restoring
            // follow after either a release or a canceled grab.
            onPressedChanged: {
                if (!pressed)
                    Qt.callLater(root.endUserScroll)
            }
            onWheel: function(wheel) {
                const delta = wheel.pixelDelta.y !== 0
                    ? wheel.pixelDelta.y : wheel.angleDelta.y / 120 * 60
                root.scrollBy(-delta)
                wheel.accepted = true
            }
        }
    }
}
