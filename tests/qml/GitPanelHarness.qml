import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Git panel harness wired to a real temporary repository. The C++ test sets
// projectRoot after construction, which is the same path used when
// ContextPanel switches project sessions.
Window {
    id: win
    width: 420
    height: 620
    visible: true
    color: Theme.bgSecondary

    GitPanel {
        id: panel
        anchors.fill: parent
        anchors.margins: 12
        active: true
    }

    function setProjectRoot(path) { panel.projectRoot = path }
    function setSubTab(index) { panel.setSubTab(index) }
    function stagedCount() { return panel.stagedCount }
    function unstagedCount() { return panel.unstagedCount }
    function statusEntryCount() { return panel.statusEntries.length }
    function logCount() { return panel.logCount }
    function topLogSubject() {
        return panel.logEntries.length > 0 ? panel.logEntries[0].subject : ""
    }
    function logShaAt(index) {
        return index < panel.logEntries.length ? panel.logEntries[index].sha : ""
    }
    function repoKnown() { return panel.repoKnown }
    function isRepo() { return panel.isRepo }
    function branchLabel() { return panel.branchLabel }
    function statusLoading() { return GitVM.statusLoading }
    function statusError() { return GitVM.statusError }
    function openWorkingFile(path, staged, untracked) {
        panel.openWorkingFileFor(path, staged, untracked)
    }
    function openCommitAt(index) { panel.openCommitAt(index) }
    function closeWorkingDialog() { panel.workingDialogItem.close() }
    function closeCommitDialog() { panel.commitDialogItem.close() }
    function workingDialogVisible() { return panel.workingDialogVisible }
    function commitDialogVisible() { return panel.commitDialogVisible }
    function workingDialogItem() { return panel.workingDialogItem }
    function commitDialogItem() { return panel.commitDialogItem }

    // P0 write chain hooks.
    function stageFile(path) { panel.stageEntry({ path: path }) }
    function unstageFile(path) { panel.unstageEntry({ path: path }) }
    function setCommitMessage(text) { panel.commitMessage = text }
    function commitMessage() { return panel.commitMessage }
    function canCommit() { return panel.canCommit() }
    function openCommitConfirm() { panel.openCommitConfirm() }
    function commitConfirmVisible() { return panel.commitConfirmVisible }
    function commitConfirmItem() { return panel.commitConfirmItem }
    function confirmCommit() { panel.performCommit() }
    function operationBusy() { return GitVM.operationBusy }
    function operationError() { return GitVM.operationError }
    function operationNotice() { return panel.operationNotice }
    function firstStagedPath() {
        return panel.stagedEntries.length > 0 ? panel.stagedEntries[0].path : ""
    }

    // P1 recovery-action hooks. Buttons themselves are clicked through their
    // objectNames by the C++ test, so only state is exposed here.
    function setBackupRoot(path) { GitVM.discardBackupRoot = path }
    function backupRoot() { return GitVM.discardBackupRoot }
    function lastBackupPath() { return GitVM.lastBackupPath }
    function absolutePath(path) { return GitVM.absolutePath(path) }
    function actionDialogVisible() { return panel.actionDialogVisible }
    function actionDialogTitle() { return panel.actionDialogItem.titleText }
    function actionDialogDetails() { return panel.actionDialogItem.detailLines.length }
    function actionDialogCanConfirm() { return panel.actionDialogItem.canConfirm }
    function closeActionDialog() { panel.actionDialogItem.close() }
    function fileHistoryVisible() { return panel.fileHistoryDialogVisible }
    function fileHistoryCount() { return panel.fileHistoryDialogItem.commitCount }
    function fileHistoryError() { return panel.fileHistoryDialogItem.errorText }
    function fileHistoryPath() { return panel.fileHistoryDialogItem.path }
    function stashCount() { return panel.stashCount }
    function logGrepValue() { return panel.logGrep }
    function logAuthorValue() { return panel.logAuthor }
    function filterActive() { return panel.filterActive() }

    // P3-c comparison state.
    function hasCompareBase() { return panel.hasCompareBase }
    function compareBaseSha() { return panel.compareBase ? panel.compareBase.sha : "" }
    function compareVisible() { return panel.compareDialogVisible }
    function compareItem() { return panel.compareDialogItem }
    function compareFileCount() { return panel.compareDialogItem.fileCount }
    function compareSelectedPath() { return panel.compareDialogItem.selectedPath }
    function compareDiffLines() { return panel.compareDialogItem.diffLineCount }
    function compareError() { return panel.compareDialogItem.filesError }

    // P3-d-2 blame.
    function blameVisible() { return panel.blameDialogVisible }
    function blameItem() { return panel.blameDialogItem }
    function blameLineCount() { return panel.blameDialogItem.lineCount }
    function blameError() { return panel.blameDialogItem.errorText }

    // P3-d-3 commit graph.
    function graphVisible() { return panel.graphDialogVisible }
    function graphItem() { return panel.graphDialogItem }
    function graphLineCount() { return panel.graphDialogItem.lineCount }
    function graphCommitCount() { return panel.graphDialogItem.commitCount }
    function graphError() { return panel.graphDialogItem.errorText }
    function graphAllBranches() { return panel.graphDialogItem.allBranches }
    function stashLoading() { return panel.stashLoading }
    function stashRequested() { return panel.stashRequested }
    function stashError() { return panel.stashError }
    function pendingDiscardPath() { return panel.pendingDiscardPath }

    // P2-b read-only branch view.
    function trackingBadge() { return panel.trackingBadge }
    function branchRowCount() { return panel.branchRowCount }
    function branchDialogVisible() { return panel.branchDialogVisible }
    function branchDialogItem() { return panel.branchDialogItem }
    function branchDialogError() { return panel.branchDialogItem.errorText }
    function branchNames() {
        const out = []
        for (let i = 0; i < panel.branchEntries.length; ++i)
            out.push(panel.branchEntries[i].name)
        return out.join(",")
    }

    // P3-a drafting hooks. The button itself is clicked through its objectName.
    function draftPending() { return panel.draftPending }
    function canDraft() { return panel.canDraft() }
    function requestDraft() { panel.requestDraft() }
    function applyDraft(ok, text, error) { panel.applyDraft(ok, text, error) }
    function setDraftCommitMessage(text) { panel.commitMessage = text }
}