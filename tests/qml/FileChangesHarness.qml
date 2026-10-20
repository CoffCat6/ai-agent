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
        // Tests inject git state directly; never spawn git from the harness.
        gitAutoLoad: false
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    function setCheckpoints(items) { panel.checkpoints = items }
    function clearCheckpoints() { panel.checkpoints = [] }
    function setCollapsed(value) { panel.collapsed = value }
    function setGitAutoLoad(value) { panel.gitAutoLoad = value }
    function setDetail(detail) {
        panel.detailProvider = function(id) {
            var copy = detail
            copy.id = id
            return copy
        }
    }
    function openDetail(index) {
        if (index >= 0 && index < panel.checkpoints.length)
            panel.openDetail(panel.checkpoints[index])
    }
    function closeDetail() { panel.closeDetail() }
    function itemCount() { return panel.itemCount }
    function renderedCount() { return panel.renderedCount }
    function panelVisible() { return panel.visible }
    function panelHeight() { return panel.implicitHeight }
    function listVisible() { return panel.listVisible }
    function detailVisible() { return panel.detailVisible }
    function dialogItem() { return panel.detailDialogItem }
    function detailPath() { return panel.detailPath() }
    function detailDiffLineCount() { return panel.detailDiffLineCount() }
    function backgroundColor() { return Theme.bgSecondary.toString() }
}
