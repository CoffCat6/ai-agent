import QtQuick
import QtQuick.Controls
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// docs/31 S4.4（4a）：模型配置页的真实交互宿主。底栏「复制」按钮走
// Settings.duplicateProfile；把它放进真实组件树，才能覆盖"点下去之后
// Repeater 重建 + editingId 切换"这条纯 C++ 用例看不见的链路。
Window {
    id: win
    width: 1000
    height: 760
    visible: true

    ModelConfigPage {
        objectName: "modelConfigPage"
        anchors.fill: parent
    }

    // 任务 #11：拉取模型结果在测试里由端点信号驱动。ConnectionTester 是 QML
    // 单例，从 C++ 侧 include 它的头会顺带拉进无关编译告警；宿主暴露一个
    // 语义等价的发射入口，测试仍走页面真实的 onModelsFetched 连接。
    function emitModelsFetched(models) {
        ConnectionTester.modelsFetched(true, models, "ok")
    }
}
