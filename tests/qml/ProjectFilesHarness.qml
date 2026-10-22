import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Harness for the sidebar project file browser.
//
// The project root is injected the way ContextPanel does it (a property
// bound to AgentVM.activeProjectRoot there, a plain string here), so the
// panel's root-change path is the one under test.
Window {
    id: win
    width: 420
    height: 700
    visible: true
    color: Theme.bgSecondary

    ProjectFilesPanel {
        id: panel
        anchors.fill: parent
        anchors.margins: 12
        projectRoot: ""
        active: true
    }

    function setProjectRoot(path) { panel.projectRoot = path }
    function projectRoot() { return panel.projectRoot }
    function setFilter(text) { panel.filterText = text }
    function rowCount() { return panel.rows.length }
    function rowNames() {
        var out = []
        for (var i = 0; i < panel.rows.length; ++i) out.push(panel.rows[i].name)
        return out
    }
    function rowPathAt(index) {
        return index >= 0 && index < panel.rows.length ? panel.rows[index].relPath : ""
    }
    function rowIsDirAt(index) {
        return index >= 0 && index < panel.rows.length ? panel.rows[index].isDir : false
    }
    function rowDepthAt(index) {
        return index >= 0 && index < panel.rows.length ? panel.rows[index].depth : -1
    }
    function toggleFolder(relPath) { panel.toggleFolder(relPath) }
    function collapseAll() { panel.collapseAll() }
    function collapseAllFromToolbar() { panel.collapseAll() }
    function refresh() { panel.refresh() }
    function selectFile(relPath) { panel.selectFile(relPath) }
    function selectedPath() { return panel.selectedPath }
    function previewVisible() { return panel.previewVisible }
    function previewText() { return panel.previewText }
    function previewTruncated() { return panel.previewTruncated }
    function previewBinary() { return panel.previewBinary }
    function previewError() { return panel.previewError }
    function previewName() { return panel.previewName }
    function closePreview() { panel.closePreview() }
    function hasProject() { return panel.hasProject }
    function errorText() { return panel.errorText }
    function backgroundColor() { return Theme.bgSecondary.toString() }
    // Diagnostics: what the VM holds, independent of the panel's own state.
    function vmRoot() { return ProjectFileVM.root }
    function vmChildCount(relPath) { return ProjectFileVM.children(relPath).length }
}
