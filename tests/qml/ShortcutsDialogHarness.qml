import QtQuick
import QtQuick.Window
import NexusAI

// docs/31 N2：速查表不再静态抄序列，必须跟随 ShortcutMap 的动作表与
// 用户改键实时刷新；打开时立即显示，便于断言行数与行内容。
Window {
    id: win
    width: 700
    height: 760
    visible: true

    ShortcutsDialog {
        id: dialog
        objectName: "shortcutsDialog"
        parent: win.contentItem
        visible: true
    }
}
