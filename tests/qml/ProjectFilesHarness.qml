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
    // Reveal-in-tree: returns whether the row was found, and exposes the
    // list's own position so a test can tell "selected" from "scrolled to".
    function revealInTree(relPath) { return panel.revealInTree(relPath) }
    function listContentY() { return panel.listContentY }
    function listContentHeight() { return panel.listContentHeight }
    function rowIndexOf(relPath) {
        for (var i = 0; i < panel.rows.length; ++i)
            if (panel.rows[i].relPath === relPath) return i
        return -1
    }
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
    // Preview seam: the panel applies the raw pointer delta, so a test drives
    // it exactly like SplitBar would and reads the resulting geometry.
    function previewHeight() { return panel.previewHeight }
    function previewMinHeight() { return panel.previewMinHeight }
    function previewDefaultHeight() { return panel.previewDefaultHeight }
    function dragSeam(delta) { panel.dragPreviewSeam(delta) }
    function resetPreviewHeight() { panel.resetPreviewHeight() }
    // Wrap toggle + the editor's effective wrap state.
    function setPreviewWrap(on) { panel.previewWrap = on }
    function previewWrap() { return panel.previewWrap }
    // Syntax highlighting: the language the preview settled on, and whether
    // the file's text actually carries colour ranges.
    function previewLanguage() { return panel.previewLanguage() }
    // Asks the highlighter itself: QML cannot walk QTextDocument blocks, so
    // "language recognised" and "colours were actually applied" would
    // otherwise be indistinguishable from the QML side.
    function previewHasHighlight() { return panel.previewHighlighted() }
    function collectByName(node, name, out) {
        for (var i = 0; i < node.children.length; ++i) {
            var child = node.children[i]
            if (child.objectName === name) out.push(child)
            collectByName(child, name, out)
        }
    }
    function previewNoticeText() {
        var items = []
        collectByName(panel, "projectFileNotices", items)
        return items.length > 0 ? items[0].text : ""
    }
    function previewWrapMode() {
        // 0 = NoWrap, 1 = WordWrap, 2 = WrapAnywhere, 3 = Wrap (Qt enum order)
        var items = []
        collectTextAreas(panel, items)
        return items.length > 0 ? items[0].wrapMode : -1
    }
    function previewEditorWidth() {
        var items = []
        collectTextAreas(panel, items)
        return items.length > 0 ? items[0].width : -1
    }
    // The decisive evidence of soft wrapping: the same text needs more lines
    // (i.e. more height) once it wraps. Width alone can pass by accident when
    // the unwrapped text happens to be narrower than the viewport.
    function previewEditorHeight() {
        var items = []
        collectTextAreas(panel, items)
        return items.length > 0 ? items[0].contentHeight : -1
    }
    function collectTextAreas(node, out) {
        for (var i = 0; i < node.children.length; ++i) {
            var child = node.children[i]
            if (child.objectName === "projectFilePreviewText") out.push(child)
            collectTextAreas(child, out)
        }
    }
    // Diagnostics: what the VM holds, independent of the panel's own state.
    function vmRoot() { return ProjectFileVM.root }
    function vmChildCount(relPath) { return ProjectFileVM.children(relPath).length }
}
