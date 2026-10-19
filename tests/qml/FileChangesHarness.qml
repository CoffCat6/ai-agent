import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// G4 test harness: injects checkpoint metadata without a database/agent.
Window {
    id: win
    width: 380
    height: 460
    visible: true
    color: Theme.bgSecondary

    FileChangesPanel {
        id: panel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    function setCheckpoints(items) { panel.checkpoints = items }
    function clearCheckpoints() { panel.checkpoints = [] }
    function setCollapsed(value) { panel.collapsed = value }
    function itemCount() { return panel.itemCount }
    function renderedCount() { return panel.renderedCount }
    function panelVisible() { return panel.visible }
    function panelHeight() { return panel.implicitHeight }
    function listVisible() { return panel.listVisible }
    function backgroundColor() { return Theme.bgSecondary.toString() }
}
