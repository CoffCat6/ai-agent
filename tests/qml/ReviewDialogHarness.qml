import QtQuick
import QtQuick.Window
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Test harness for the review dialog: the dialog exactly as main.qml
// hosts it (parented to the window overlay), with the checkpoint list and
// the diff details injected so no database or agent is needed.
//
// The window is deliberately large (1280x900): the dialog is clamped to
// the window minus its margins, so the harness gets the full 1180px
// review width - the wide layout the dialog exists for.
Window {
    id: win
    width: 1280
    height: 900
    visible: true
    // Deliberately not the dialog's own background: the window colour is
    // what the dialog must cover, so a grab can tell "the dialog is on
    // screen" from "the dialog is an empty hole".
    color: Theme.bgPrimary

    ReviewDialog {
        id: dialog
        parent: Overlay.overlay
        // The harness has no session: the test injects the checkpoints.
        checkpoints: []
        reviewAcknowledger: function() { win.acks += 1 }
    }

    // UE-1: the dialog acks through this hook on close. Counted here so a
    // test can assert "closing once acknowledges once" without the AgentVM
    // singleton's real checkpoint chain (test_agent covers that half).
    property int acks: 0
    function ackCount() { return acks }

    function setCheckpoints(items) { dialog.checkpoints = items }
    function clearCheckpoints() { dialog.checkpoints = [] }
    function setDetail(detail) {
        dialog.detailProvider = function(id) {
            var copy = detail
            copy.id = id
            return copy
        }
    }
    function setDetailById(details) {
        dialog.detailProvider = function(id) {
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
    function openDialog() { dialog.openFor() }
    function closeDialog() { dialog.close() }
    function dialogVisible() { return dialog.opened }
    function dialogWidth() { return dialog.width }
    function dialogHeight() { return dialog.height }
    function fileCount() { return dialog.fileCount }
    function addedLines() { return dialog.addedLines }
    function removedLines() { return dialog.removedLines }
    function emptyStateVisible() { return dialog.emptyStateVisible }
    function renderedCount() { return dialog.reviewPanel.renderedCount }
    function treeRowCount() { return dialog.reviewPanel.treeRowCount }
    // Split geometry: the tree stands left of the diff, so the divider
    // moves the tree's *width*.
    function treeWidth() { return dialog.reviewPanel.treeWidth }
    function treeHeight() { return dialog.reviewPanel.treeHeight }
    function dragSplit(delta) { dialog.reviewPanel.adjustSplit(delta) }
    function resetSplit() { dialog.reviewPanel.resetSplit() }
    function splitRatio() { return dialog.reviewPanel.listRatio }
    function detailVisible() { return dialog.reviewPanel.detailVisible }
    function detailPath() { return dialog.reviewPanel.detailPath() }
    function detailDiffLineCount() { return dialog.reviewPanel.detailDiffLineCount() }
    function openDetail(index) {
        var list = dialog.reviewPanel.checkpoints
        if (index >= 0 && index < list.length)
            dialog.reviewPanel.openDetail(list[index])
    }
    function selectEntry(index) {
        var list = dialog.reviewPanel.checkpoints
        if (index >= 0 && index < list.length)
            dialog.reviewPanel.detailDialogItem.selectEntry(list[index])
    }
    function stepEntry(delta) { dialog.reviewPanel.detailDialogItem.stepEntry(delta) }
    function setViewMode(mode) { dialog.reviewPanel.detailDialogItem.viewMode = mode }
    function collapseTree() { dialog.reviewPanel.collapseTree() }
    // Review verdict plumbing. The real accepted state comes from AgentVM's
    // checkpoint list; the harness feeds it fixed rows, so a test drives the
    // panel's accept path against its own detailProvider instead.
    function pendingCount() { return dialog.reviewPanel.pendingCount }
    // Bulk-revert plumbing: request opens the confirmation, perform runs it.
    function requestUndoAll() { dialog.reviewPanel.requestUndoAll() }
    function undoAllDialogVisible() {
        return dialog.reviewPanel.undoAllDialogVisible
    }
    function undoableCount() { return dialog.reviewPanel.undoableCount }
    function unrestorableCount() { return dialog.reviewPanel.unrestorableCount }
    function pendingPaths() { return dialog.reviewPanel.pendingPaths }
    function errorText() { return dialog.reviewPanel.errorText }
    // A Dialog is a Popup, so it is not in the item tree; tests reach it
    // through its own accessors instead of an objectName search.
    function undoAllDialogItem() {
        return dialog.reviewPanel.undoAllDialogItem
    }
    function undoAllDialogClose() { dialog.reviewPanel.undoAllDialogItem.close() }
    // The confirm button's own handler, so the test exercises the same path a
    // click would (close the dialog, then run the batch).
    function confirmUndoAll() { return dialog.reviewPanel.performUndoAll() }
    function undoAllListedText() {
        // Everything the confirmation would show as a file line.
        const paths = dialog.reviewPanel.pendingPaths
        const out = []
        for (let i = 0; i < paths.length; ++i)
            out.push(paths[i].path)
        return out
    }
    function hasPending() { return dialog.reviewPanel.hasPending }
    function performAccept(index) {
        var list = dialog.reviewPanel.checkpoints
        if (index >= 0 && index < list.length)
            return dialog.reviewPanel.performAccept(list[index])
        return 0
    }
    function setAccepted(index) {
        // Mirror what the SQL layer returns after an accept: a per-file flag.
        var list = dialog.reviewPanel.checkpoints.slice()
        if (index < 0 || index >= list.length) return
        var row = {}
        for (var key in list[index]) row[key] = list[index][key]
        row.accepted = true
        row.acceptedAt = Date.now()
        list[index] = row
        dialog.checkpoints = list
    }
    function expandTree() { dialog.reviewPanel.expandTree() }
    function windowBackgroundColor() { return Theme.bgPrimary.toString() }
    function dialogBackgroundColor() { return Theme.bgSecondary.toString() }
}
