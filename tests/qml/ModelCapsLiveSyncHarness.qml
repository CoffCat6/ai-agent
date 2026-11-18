import QtQuick
import QtQuick.Controls
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// docs/54 回归宿主：把**真实设置页**与**真实对话选择器**放进同一个窗口，
// 复现用户路径「在设置里点能力行 → 回对话界面的下拉框看徽章」。
// 两个组件同处一个 engine，共享 Settings / AgentVM 单例，与 main.qml 里
// SettingsCenterDialog 和 AgentChatPage 的共存关系一致。
Window {
    id: win
    width: 1200
    height: 900
    visible: true
    color: Theme.bgPrimary

    ModelConfigPage {
        id: configPage
        objectName: "modelConfigPage"
        anchors.left: parent.left
        anchors.top: parent.top
        width: parent.width - 420
        height: parent.height
    }

    ModelSelector {
        id: selector
        objectName: "modelSelector"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        anchors.bottomMargin: 8
    }

    function openPopup() {
        if (!selector.opened)
            selector.toggleMenu()
    }
}
