// NexusAI.NexusStyle — TextEditingContextMenu
//
// TextArea / TextField 共用的右键编辑菜单。自绘而非复用 Qt 的
// QtQuick.Controls.Basic.impl/TextEditingContextMenu：后者是 Qt 内部 API，
// 其 required property 在 6.9 叫 control、6.11 改名 editor（并新增
// Undo/Redo action）——直接引用会把样式钉死在单一 Qt 版本上。这里只用
// TextArea/TextField 的稳定公开 API（cut/copy/paste/undo/redo 与
// canUndo/canRedo/canPaste/selectedText，Qt 5 起未变），Qt 6.5+ 全版本
// 一套代码，Windows/Linux 各自升级 Qt 互不牵连（docs/49 多 Qt 版本矩阵）。
//
// 菜单项按 Qt impl 同款分组：撤销/重做 ｜ 剪切/复制/粘贴/删除 ｜ 全选。
// 视觉走同目录 NexusStyle/Menu.qml + MenuItem.qml（同模块隐式导入，
// 与 NexusMetrics 同理，不进 qmldir、不对外暴露）。
import QtQuick
import QtQuick.Templates as T

Menu {
    id: menu

    // 宿主编辑控件（TextArea 或 TextField）。普通 var 属性而非 required：
    // required property 本身也是 impl 类型在 6.9→6.11 间的破坏点之一。
    property var editor: null

    // Qt impl 菜单在桌面端用 Popup.Window（避免菜单被输入框裁剪），但
    // Wayland 上 Window 弹层定位/焦点有坑（Qt 6.11 起官方也按平台分流）。
    popupType: Qt.platform.pluginName !== "wayland" ? T.Popup.Window : T.Popup.Item

    // ── enabled 判定 ─────────────────────────────────────────
    // TextField（TextInput）有 echoMode，TextArea（TextEdit）没有：访问
    // undefined。掩码输入（密码框）禁止复制/剪切，防止密钥走剪贴板。
    function isMasked() {
        return menu.editor !== null
            && menu.editor.echoMode !== undefined
            && menu.editor.echoMode !== TextInput.Normal
    }
    function hasSelection() {
        return menu.editor !== null && menu.editor.selectedText.length > 0
    }

    MenuItem {
        text: qsTr("撤销")
        enabled: menu.editor !== null && menu.editor.canUndo
        onTriggered: menu.editor.undo()
    }
    MenuItem {
        text: qsTr("重做")
        enabled: menu.editor !== null && menu.editor.canRedo
        onTriggered: menu.editor.redo()
    }

    MenuSeparator {}

    MenuItem {
        text: qsTr("剪切")
        enabled: menu.editor !== null && !menu.editor.readOnly && menu.hasSelection()
        onTriggered: menu.editor.cut()
    }
    MenuItem {
        text: qsTr("复制")
        enabled: menu.editor !== null && !menu.isMasked() && menu.hasSelection()
        onTriggered: menu.editor.copy()
    }
    MenuItem {
        text: qsTr("粘贴")
        enabled: menu.editor !== null && !menu.editor.readOnly && menu.editor.canPaste
        onTriggered: menu.editor.paste()
    }
    MenuItem {
        text: qsTr("删除")
        enabled: menu.editor !== null && !menu.editor.readOnly && menu.hasSelection()
        onTriggered: menu.editor.removeSelectedText()
    }

    MenuSeparator {}

    MenuItem {
        text: qsTr("全选")
        enabled: menu.editor !== null && menu.editor.length > 0
        onTriggered: menu.editor.selectAll()
    }
}
