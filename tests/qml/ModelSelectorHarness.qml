import QtQuick
import QtQuick.Controls
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// docs/54：模型选择器弹层的真实交互宿主。种子数据由测试在加载前经
// SettingsService 单例注入；Section 1 的聊天模型行由 C++ 侧
// AgentViewModel::updateModelProfiles + selectChatModel 驱动（main.cpp
// 的装配在测试进程里不存在，单例构造器已保证有一个会话标签），
// Section 2 的 profile 行直接读 Settings.profiles。本文件只承载组件
// 并提供弹层开关 / 整窗截图入口。
Window {
    id: win
    width: 420
    height: 380
    visible: true
    color: Theme.bgPrimary

    ModelSelector {
        id: selector
        objectName: "modelSelector"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 56
    }

    function openPopup() {
        if (!selector.opened)
            selector.toggleMenu()
    }

    // 整窗截图（contentItem 含 Overlay 弹层），保存路径由测试侧给。
    // grabToImage 异步回写；测试用 QTRY_VERIFY 等文件落盘。
    function snapshot(path) {
        win.contentItem.grabToImage(function (result) {
            result.saveAs(String(path))
        }, Qt.size(win.width, win.height))
    }
}
