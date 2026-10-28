import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for SessionTemplateMenu: mounts the real component beside a
// trigger item (the same shape the sidebar / tab bar / empty state use) and
// exposes the internals the C++ case asserts.
Window {
    id: win
    width: 520
    height: 460
    visible: true
    color: Theme.bgSecondary

    // 触发器：模拟侧栏「新对话」右缘那条窄条。
    Rectangle {
        id: trigger
        objectName: "templateTrigger"
        x: 20
        y: 120
        width: 22
        height: 36
        color: "#cccccc"

        // 与真实宿主同样的接线（onPressed + toggleBelow）。
        MouseArea {
            anchors.fill: parent
            onPressed: menu.toggleBelow(trigger)
        }
    }

    SessionTemplateMenu {
        id: menu
        onManageRequested: win.manageCount += 1
    }

    property int manageCount: 0

    function open() { menu.openBelow(trigger) }
    function closeIfOpen() { if (menu.popped) menu.close() }
    function popped() { return menu.popped }
    function templateCount() { return menu.templateCount }
    function manageRequestedCount() { return win.manageCount }
    function triggerManage() { menu.manageRequested() }
    function subtitle(tmpl) { return menu.templateSubtitle(tmpl) }
    function profileText(id) { return menu.profileText(id) }
    function permissionText(mode) { return menu.permissionText(mode) }
    function popupTop() { return menu.popupTop }
    function popupBottom() { return menu.popupBottom }
    function windowHeight() { return win.height }
    function minRowHeight() { return menu.minRowHeight() }
    function backgroundColor() { return Theme.bgSecondary.toString() }
    function openDebug() { return menu.lastOpenDebug }
}
