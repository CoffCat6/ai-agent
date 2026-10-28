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

    // A1 (hit-locate): scroll so the idx-th row lands near the top of the
    // viewport. The caller (chat page) invokes this after the Repeater has
    // laid out the freshly loaded session; forceLayout() guarantees each
    // delegate's y (relative to messageColumn) is current before we read it.
    // Locating is an explicit user intent, so pinnedToBottom drops to false
    // — geometry updates must not yank the user back to the bottom.
    function scrollToIndex(idx) {
        if (idx < 0 || idx >= messages.count)
            return
        messageColumn.forceLayout()
        const item = messages.itemAt(idx)
        if (!item)
            return
        flick.cancelFlick()
        beginUserScroll()
        flick.contentY = clampOffset(item.y - 8)
    }

    // 某一行相对内容顶部的 y（行不存在返回 -1）。折叠中间节点时
    // delegate 仍在 Repeater 里（只是 height 0），所以 itemAt 依然有效。
    //
    // 刻意不调 forceLayout()：这个函数在滚动热路径上被反复调用（导轨的
    // active 计算），而滚动发生时布局早已稳定。需要强制布局的定位入口
    // （scrollToIndex）自己调。
    function rowY(row) {
        if (row < 0 || row >= messages.count) return -1
        const item = messages.itemAt(row)
        return item ? item.y : -1
    }

    // 用户消息导轨：视口顶端当前落在 where 里的第几条。
    // `rows` 是用户消息的 trace 行号数组（升序，来自 AgentVM.userMessageIndex）。
    //
    // 判据是「这条的顶边已经滚到视口顶之上（含容差）」的最后一条——即
    // 用户此刻正在读的那一段所属的问题。返回 -1 表示还没滚过第一条。
    // 放在 QML 而不是 C++：行高只有 delegate 实测值，跨语言搬运只会引入
    // 同步风险。
    function activeIndexIn(rows) {
        if (!rows || rows.length === 0) return -1
        const top = flick.contentY + 8
        let idx = -1
        for (let i = 0; i < rows.length; ++i) {
            const y = rowY(rows[i])
            if (y < 0) continue
            if (y <= top) idx = i
            else break
        }
        return idx
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
