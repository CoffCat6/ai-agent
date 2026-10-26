import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for PromptPresetSelector: mounts the real component at the
// bottom edge (exactly where the chat page puts it, so the popup opens
// upward inside the window) and exposes the internals the C++ case asserts.
Window {
    id: win
    width: 460
    height: 420
    visible: true
    color: Theme.bgSecondary

    PromptPresetSelector {
        id: selector
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
    }

    function triggerLabel() { return selector.triggerLabel }
    function isApplied() { return selector.applied }
    function sourceMissing() { return selector.sourceMissing }
    function selectorWidth() { return selector.width }
    function selectorHeight() { return selector.height }
    function openPopup() { selector.toggleMenu() }
    function opened() { return selector.popupOpened }
    function closeIfOpen() { if (selector.popupOpened) selector.toggleMenu() }
    function summarize(text) { return selector.summarize(text) }
    function presetCount() { return Settings.promptPresets.length }
    function manageRequestedCount() { return win.manageCount }
    function triggerManage() { selector.manageRequested() }

    property int manageCount: 0

    Connections {
        target: selector
        function onManageRequested() { win.manageCount += 1 }
    }

    function backgroundColor() { return Theme.bgSecondary.toString() }
    // 几何自检：弹出列表必须完整落在窗口内（贴底向上弹，越界就看不到顶部
    // 分区标题和管理入口），行高必须撑得住"名称 + 两行摘要"。
    // 逐项取标量：QML 的 JS 对象/数组经 Q_RETURN_ARG(QVariant) 取不出来，
    // 标量（real/int）可以。
    function popupTop() { return selector.popupTop }
    function popupBottom() { return selector.popupBottom }
    function popupLeft() { return selector.popupLeft }
    function popupWidth() { return selector.popupWidth }
    function windowHeight() { return win.height }
    function presetRowCount() { return selector.presetRowCount() }
    function minPresetRowHeight() { return selector.minPresetRowHeight() }
}
