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
}
