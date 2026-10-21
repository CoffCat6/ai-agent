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
        anchors.top: parent.top
        anchors.margins: 12
        // Explicit width (instead of anchors.right) so a test can squeeze
        // the panel down to the 220px production minimum.
        width: Math.max(0, parent.width - 24)
    }

    function setCheckpoints(items) { panel.checkpoints = items }
    function clearCheckpoints() { panel.checkpoints = [] }
    function setCollapsed(value) { panel.collapsed = value }
    function setProjectRoot(rootPath) { panel.projectRoot = rootPath }
    function setDetail(detail) {
        panel.detailProvider = function(id) {
            var copy = detail
            copy.id = id
            return copy
        }
    }
    // Per-id detail injection for multi-file dialogs.
    function setDetailById(details) {
        panel.detailProvider = function(id) {
            var source = details[String(id)]
            var copy = {}
            if (source !== undefined) {
                for (var key in source)
                    copy[key] = source[key]
            }
            copy.id = id
            return copy
        }
    }
    function openDetail(index) {
        if (index >= 0 && index < panel.checkpoints.length)
            panel.openDetail(panel.checkpoints[index])
    }
    function closeDetail() { panel.closeDetail() }
    function selectEntry(index) {
        if (index >= 0 && index < panel.checkpoints.length)
            panel.detailDialogItem.selectEntry(panel.checkpoints[index])
    }
    function stepEntry(delta) { panel.detailDialogItem.stepEntry(delta) }
    function setViewMode(mode) { panel.detailDialogItem.viewMode = mode }
    function itemCount() { return panel.itemCount }
    function renderedCount() { return panel.renderedCount }
    function treeRowCount() { return panel.treeRowCount }
    function treeFolderCount() { return panel.treeFolderRowCount }
    function toggleFolder(path) { panel.toggleTreeFolder(path) }
    function folderCollapsed(path) { return panel.treeIsCollapsed(path) }
    function panelVisible() { return panel.visible }
    function panelHeight() { return panel.implicitHeight }
    function panelWidth() { return panel.width }
    function setPanelWidth(px) { panel.width = px }
    function listVisible() { return panel.listVisible }
    function detailVisible() { return panel.detailVisible }
    function undoDialogVisible() { return panel.undoDialogVisible }
    function dialogItem() { return panel.detailDialogItem }
    function detailPath() { return panel.detailPath() }
    function detailDiffLineCount() { return panel.detailDiffLineCount() }
    // Inline split: drive the divider like a drag would and read back the
    // resulting geometry.
    function dragSplit(delta) { panel.adjustSplit(delta) }
    function resetSplit() { panel.resetSplit() }
    function splitRatio() { return panel.listRatio }
    function treeHeight() { return panel.treeHeight }
    function backgroundColor() { return Theme.bgSecondary.toString() }
}
