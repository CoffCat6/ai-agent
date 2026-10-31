import QtQuick
import QtQuick.Window
import NexusAI

// docs/31 N2：快捷键设置页的真实交互宿主。
//
// 页面通过 ShortcutMap 读动作表并写 ui.shortcuts；C++ 侧只调用页面暴露的
// beginRecording/applyRecording 与真实 QTest 按键，断言录制、冲突提示和
// 行级恢复是否真的反映到界面。
Window {
    id: win
    width: 900
    height: 720
    visible: true

    ShortcutsPage {
        objectName: "shortcutsPage"
        anchors.fill: parent
    }
}
