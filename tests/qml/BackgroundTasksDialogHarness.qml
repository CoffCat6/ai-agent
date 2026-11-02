import QtQuick
import QtQuick.Controls
import NexusAI

// docs/35 P4.3: BackgroundTasksDialog 的测试宿主。弹窗必须挂在
// Overlay.overlay 上（main.qml 的真实宿主方式），数据通过 tasks 注入，
// 与 AgentVM 解耦——动作只会在用户点按后访问 AgentVM。
ApplicationWindow {
    id: win
    width: 800
    height: 640
    visible: true
    color: "black"

    property var tasks: []

    function setTasks(list) { win.tasks = list }
    function openDialog() { bg.open(); return bg.visible }
    function closeDialog() { bg.close() }

    BackgroundTasksDialog {
        id: bg
        parent: Overlay.overlay
        tasks: win.tasks
    }
}