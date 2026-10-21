import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for TodoListPanel: exposes state the C++ case asserts on and
// lets it inject todo_write-shaped items without going through AgentVM.
Window {
    id: win
    width: 360
    height: 420
    visible: true
    color: Theme.bgSecondary

    TodoListPanel {
        id: panel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    function setTodos(items) { panel.todos = items }
    function clearTodos() { panel.todos = [] }
    function itemCount() { return panel.itemCount }
    function completedCount() { return panel.completedCount }
    function inProgressCount() { return panel.inProgressCount }
    function pendingCount() { return panel.pendingCount }
    function allCompleted() { return panel.allCompleted }
    function progressRatio() { return panel.progressRatio }
    function stateSummary() { return panel.stateSummary }
    function listViewportHeight() { return panel.listViewportHeight }
    function listContentHeight() { return panel.listContentHeight }
    function listScrollable() { return panel.listScrollable }
    function moreBelowFold() { return panel.moreBelowFold }
    function listMaxHeight() { return panel.listMaxHeight }
    function listHeightCeiling() { return panel.listHeightCeiling }
    function chromeHeight() { return panel.chromeHeight }
    function panelVisible() { return panel.visible }
    function panelHeight() { return panel.implicitHeight }
    function listVisible() { return panel.listVisible }
    function renderedCount() { return panel.renderedCount }
    function setCollapsed(value) { panel.collapsed = value }
    function backgroundColor() { return Theme.bgSecondary.toString() }
}
