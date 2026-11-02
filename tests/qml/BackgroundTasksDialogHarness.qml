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
    // 默认走应用单例；用例可注入桩以断言按钮真的调了门面（组件预编译后
    // context property 无法遮蔽 AgentVM 单例类型）。
    property var injectedAgent: AgentVM

    function setTasks(list) { win.tasks = list }
    function openDialog() { bg.open(); return bg.visible }
    function closeDialog() { bg.close() }
    function setInjectedAgent(agent) { win.injectedAgent = agent }

    BackgroundTasksDialog {
        id: bg
        parent: Overlay.overlay
        tasks: win.tasks
        agentFacade: win.injectedAgent
    }
}