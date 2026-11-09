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
    // ── 缩放节流（resize throttle）──────────────────
    // 全部 delegate 的宽度都来自 availableWidth（AgentChatPage 的 delegate
    // 绑定）。非虚拟化列里，窗口拖动缩放的每个 WM_SIZE 步都会改这个宽度，
    // 令所有消息的 MarkdownText/CodeBlock 重新断行——整个场景最贵的操作。
    // 实测教训：50ms 节拍只是把"每像素重排"降为"每 50ms 重排"，单次全列
    // 重排本身要 50~200ms（几百条消息），拖动中仍周期性掉帧。所以策略是
    // **拖动中冻结**：delegate 宽度在拖动进行中完全不动（变窄时右缘被
    // clip 裁掉、变宽时暂时留白，均无错误布局），只在两种时机重排——
    //   · 长拖（>250ms 仍不松手）每 250ms 贴合一次，防止留白无限扩大
    //     （单次重排被稀释到 4Hz，感知为内容偶发跳动而非持续卡顿）；
    //   · 停止缩放 80ms 后一次性贴合并恢复逐像素跟随。
    // 一次不可避免的昂贵重排被挪到松手之后，拖动全程帧成本只剩光栅化，
    // 高 DPR 屏（3.2K@175%，每帧物理像素 2.85× 于 1080p）与 1080p 同时
    // 受益。刻意不做宽度渐变动画：Behavior on width 会把一次重排摊成
    // 每帧重排，恰恰是最贵路径（VSCode/Chrome 的内容区同为滞后贴合）。
    // 常态路径零开销（availableWidth 直接绑 liveWidth）。
    readonly property real liveWidth: flick.width
    property real syncedWidth: 0
    property bool resizing: false
    readonly property real availableWidth: resizing ? syncedWidth : liveWidth

    onLiveWidthChanged: {
        // 首帧/初始布局不节流：delegate 必须以正确宽度创建，否则整列以
        // 宽度 0 参与首布局再跳变。
        if (syncedWidth <= 0)
            syncedWidth = liveWidth
        resizing = true
        resizeSettle.restart()
    }
    Timer {
        id: resizeCatchUp
        interval: 250
        running: root.resizing && root.syncedWidth > 0
                 && root.syncedWidth !== root.liveWidth
        repeat: true
        onTriggered: root.syncedWidth = root.liveWidth
    }
    Timer {
        id: resizeSettle
        interval: 80
        onTriggered: {
            root.syncedWidth = root.liveWidth
            root.resizing = false
        }
    }
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

    function syncPosition(forceLayout) {
        if (forceLayout === undefined)
            forceLayout = true
        // 几何驱动调用（onMaximumOffsetChanged）不需要强制布局：contentHeight
        // 的变化本身就意味着布局已经完成。强制反而有害——TextEdit 的重排走
        // polish 调度，缩放节流期间积压的宽度重排会被这里提前同步执行，把
        // 50ms 节拍打穿（拖角缩放时垂直分量每步都触发本函数）。只有模型
        // 驱动的入口（新消息/折叠切换/跳转定位）才需要先量出高度。
        if (forceLayout)
            messageColumn.forceLayout()
        if (userInteracting)
            return
        const next = pinnedToBottom ? maximumOffset : clampOffset(flick.contentY)
        if (Math.abs(flick.contentY - next) > 0.01)
            flick.contentY = next
    }

    // Qt.callLater 按函数引用去重：必须用同一个共享回调，本帧内的多次
    // 请求（几何驱动 + 模型驱动并发时）才合并成一次执行。forceLayout 取
    // "本帧任一调用方要求强制"的并集——几何驱动的 false 不能抹掉模型
    // 驱动的 true。
    property bool pendingForceLayout: false
    function syncPending() {
        const fl = pendingForceLayout
        pendingForceLayout = false
        syncPosition(fl)
    }
    function requestScrollToBottom(forceLayout) {
        if (forceLayout === undefined)
            forceLayout = true
        if (forceLayout)
            pendingForceLayout = true
        Qt.callLater(syncPending)
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

    onMaximumOffsetChanged: requestScrollToBottom(false)
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
