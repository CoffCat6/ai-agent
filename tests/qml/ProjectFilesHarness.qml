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
    // Search debounce seams: shorten the window in tests, and count how many
    // real async searches were issued (rapid typing must coalesce to one).
    function setSearchDebounceMs(ms) { panel.searchDebounceMs = ms }
    function searchDebounceMs() { return panel.searchDebounceMs }
    function searchRunCount() { return panel.searchRunCount }
    function pendingSearchId() { return panel.pendingSearchId }
    function searchMode() { return panel.searchMode }
    // The ListView's actual model: the tree rows, or search hits in search
    // mode. Assertions about "what the list shows" must read this, not
    // panel.rows, or search-mode results are invisible to the test.
    function rowCount() { return panel.searchMode ? panel.searchResults.length : panel.rows.length }
    function rowNames() {
        var out = []
        for (var i = 0; i < panel.rows.length; ++i) out.push(panel.rows[i].name)
        return out
    }
    function rowPathAt(index) {
        var rows = panel.searchMode ? panel.searchResults : panel.rows
        return index >= 0 && index < rows.length ? rows[index].relPath : ""
    }
    function rowIsDirAt(index) {
        var rows = panel.searchMode ? panel.searchResults : panel.rows
        return index >= 0 && index < rows.length ? rows[index].isDir : false
    }
    function rowDepthAt(index) {
        return index >= 0 && index < panel.rows.length ? panel.rows[index].depth : -1
    }
    function toggleFolder(relPath) { panel.toggleFolder(relPath) }
    function collapseAll() { panel.collapseAll() }
    function collapseAllFromToolbar() { panel.collapseAll() }
    function refresh() { panel.refresh() }
    // Copy path via the VM, bypassing the button click (the click path is
    // covered by the visible-button assertions).
    function copyPathDirect(relPath) {
        var abs = ProjectFileVM.absolutePath(relPath)
        if (abs.length > 0) AgentVM.copyToClipboard(abs)
        return abs
    }
    // Emergency open path (broken file association): exposed for tests.
    function openWithKnownEditorProbe(relPath) {
        return ProjectFileVM.openWithKnownEditor(relPath)
    }
    // Reveal-in-tree: returns whether the row was found, and exposes the
    // list's own position so a test can tell "selected" from "scrolled to".
    function revealInTree(relPath) { return panel.revealInTree(relPath) }
    function listContentY() { return panel.listContentY }
    function listContentHeight() { return panel.listContentHeight }
    // Line-number gutter: the text it renders, its offset, and the toggle.
    function setShowLineNumbers(on) { panel.showLineNumbers = on }
    function showLineNumbers() { return panel.showLineNumbers }
    // The gutter is a Column of per-logical-line Texts now; a test reads a
    // row's label by index and the row's height (its visual span).
    function lineNumberRowCount() { return panel.lineNumberCount }
    function lineNumberAt(index) {
        var items = []
        collectByName(panel, "projectFileLineNumbersColumn", items)
        if (items.length === 0) return ""
        var col = items[0]
        if (index < 0 || index >= col.children.length) return ""
        var child = col.children[index]
        return child ? child.text : ""
    }
    function lineNumberRowHeight(index) {
        var items = []
        collectByName(panel, "projectFileLineNumbersColumn", items)
        if (items.length === 0) return -1
        var col = items[0]
        if (index < 0 || index >= col.children.length) return -1
        var child = col.children[index]
        return child ? child.height : -1
    }
    function lineNumberGutterWidth() { return panel.lineNumberWidth }
    function lineNumberGutterY() {
        var items = []
        collectByName(panel, "projectFileLineNumbersColumn", items)
        return items.length > 0 ? items[0].y : 0
    }
    function lineNumberGutterVisible() {
        var items = []
        collectByName(panel, "projectFileLineNumbers", items)
        return items.length > 0 ? items[0].visible : false
    }
    function editorLineCount() {
        var items = []
        collectTextAreas(panel, items)
        return items.length > 0 ? items[0].lineCount : -1
    }
    function scrollPreviewTo(y) {
        var items = []
        collectByName(panel, "projectFilePreviewScroll", items)
        if (items.length === 0) return false
        items[0].contentItem.contentY = y
        return true
    }
    function rowIndexOf(relPath) {
        var rows = panel.searchMode ? panel.searchResults : panel.rows
        for (var i = 0; i < rows.length; ++i)
            if (rows[i].relPath === relPath) return i
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
    function editorRowHeight() { return panel.editorRowHeight }

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
