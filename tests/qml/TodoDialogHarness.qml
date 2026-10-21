import QtQuick
import QtQuick.Window
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Test harness for TodoDialog: hosts the dialog the way main.qml does
// (`parent: Overlay.overlay`) and exposes the state the C++ case asserts
// on. The dialog is fed plain todo_write-shaped items, so the case never
// has to construct the AgentVM facade.
Window {
    id: win
    width: 1200
    height: 800
    visible: true
    color: Theme.bgPrimary

    TodoDialog {
        id: todoDialog
        parent: Overlay.overlay
    }

    function setTodos(items) { todoDialog.todos = items }
    function clearTodos() { todoDialog.todos = [] }
    function openDialog() { todoDialog.open() }
    function closeDialog() { todoDialog.close() }
    function isOpen() { return todoDialog.opened }
    function itemCount() { return todoDialog.itemCount }
    function completedCount() { return todoDialog.completedCount }
    function inProgressCount() { return todoDialog.inProgressCount }
    function pendingCount() { return todoDialog.pendingCount }
    function unfinishedCount() { return todoDialog.unfinishedCount }
    function hasItems() { return todoDialog.hasItems }
    function progressRatio() { return todoDialog.progressRatio }
    function stateSummary() { return todoDialog.stateSummary }
    function renderedCount() { return todoDialog.renderedCount }
    function listHeightCap() { return todoDialog.listHeightCap }
    function listViewportHeight() { return todoDialog.listViewportHeight }
    function listContentHeight() { return todoDialog.listContentHeight }
    function listScrollable() { return todoDialog.listScrollable }
    function moreBelowFold() { return todoDialog.moreBelowFold }
    function surfaceColor() { return todoDialog.surfaceColor }
    function fadeEndColor() { return todoDialog.fadeEndColor() }
    function dialogWidth() { return todoDialog.width }
    function dialogHeight() { return todoDialog.height }
    function bodyHeight() { return todoDialog.bodyHeight }
}
