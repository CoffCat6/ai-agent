import QtQuick
import QtQuick.Window
import NexusAI

// docs/31 S4.1 O8：设置中心尺寸记忆的真实交互回归宿主。
//
// 尺寸键（ui.settingsDialogWidth/Height）是 QML 侧用 PreferenceStore 直接
// 读写的隐藏偏好，纯 C++ 用例只能证明键能存取；用户看到的是"拖动右下角
// 之后下次打开还是这个大小"。这里加载真实 SettingsCenterDialog，由 C++
// 侧开关对话框、在把手上拖动，断言尺寸恢复与父窗口收窄时的 clamp。
//
// 默认停靠在 general 分类（最轻的页面）：本用例只关心对话框几何，其它
// 页面的加载（模型列表、MCP 状态）不应掺进来。
Window {
    id: win
    width: 1000
    height: 800
    visible: true

    SettingsCenterDialog {
        objectName: "settingsCenterDialog"
        parent: win.contentItem
        currentCategory: "general"
    }
}
