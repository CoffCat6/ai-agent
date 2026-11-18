import QtQuick
import QtQuick.Controls
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// 思考强度分段条的真实交互宿主。AgentVM 装配（agent 工厂 / 模型配置注入）
// 由测试在加载前完成（main.cpp 的装配链在测试进程里不存在，单例构造器已
// 保证有会话标签），本文件只承载组件并提供整窗截图入口。
Window {
    id: win
    width: 420
    height: 200
    visible: true
    color: Theme.bgPrimary

    ThinkingEffortSelector {
        objectName: "thinkingEffortSelector"
        anchors.centerIn: parent
    }

    // 整窗截图（contentItem 含 Overlay 弹层），保存路径由测试侧给。
    function snapshot(path) {
        win.contentItem.grabToImage(function (result) {
            result.saveAs(String(path))
        }, Qt.size(win.width, win.height))
    }
}
