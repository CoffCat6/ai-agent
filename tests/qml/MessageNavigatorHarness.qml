import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for MessageNavigator: lets the C++ case inject
// AgentVM.userMessageIndex-shaped entries without going through AgentVM.
Window {
    id: win
    width: 420
    height: 560
    visible: true
    color: Theme.bgSecondary

    // 记录每次点击请求的 trace 行号，供测试断言。
    property var jumps: []

    MessageNavigator {
        id: nav
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        viewportHeight: win.height
        onJumpRequested: function(row) {
            const next = win.jumps.slice()
            next.push(row)
            win.jumps = next
        }
    }

    function setEntries(items) { nav.entries = items }
    function setActive(index) { nav.activeIndex = index }
    function clearJumps() { win.jumps = [] }
    function readJumps() { return win.jumps }

    function entryCount() { return nav.count }
    function navigable() { return nav.navigable }
    function navVisible() { return nav.visible }
    function trackHeight() { return nav.trackHeight }
    function effectiveSlot() { return nav.effectiveSlot }
    function barWidth(length) { return nav.barWidth(length) }
    function navWidth() { return nav.width }
    function navX() { return nav.x }

    // 视口中心（用点击测试，不依赖 MouseArea 的内部结构）。
    function navCenterY() { return win.height / 2 }
}
