import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// 工具管理页宿主：真实 SettingsPage 骨架 + 真实 AgentVM 工具注册表。
Window {
    id: win
    width: 760
    height: 700
    visible: true
    color: Theme.bgPrimary

    ToolsPage {
        id: toolsPage
        objectName: "toolsPage"
        anchors.fill: parent
    }

    // 供测试读取分组结果，避免在 C++ 侧猜 QVariant 形状。
    function registryCount() { return AgentVM.toolRegistryModel.length }
    function builtinCount() { return toolsPage.builtinTools.length }
    function mcpCount() { return toolsPage.mcpTools.length }
    function firstBuiltinName() {
        return toolsPage.builtinTools.length > 0
            ? String(toolsPage.builtinTools[0].name) : ""
    }
}
