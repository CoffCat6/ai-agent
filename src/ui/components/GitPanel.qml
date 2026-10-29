import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Project-scoped Git view for the right sidebar:
//   - 改动: staged/unstaged trees (row action stages or unstages; click opens
//     the working diff) plus a commit composer and confirmation dialog
//   - 历史: repository commit log with paging (click opens commit details)
// Writes run only after an explicit user action and one at a time. The panel
// pins GitVM to the active project root so every command runs in that
// repository; local chats never show this panel (ContextPanel hides the tab).
Item {
    id: root

    property string projectRoot: ""
    // True while the Git tab is the visible right-panel section: refreshes
    // are deferred otherwise so hidden projects do not spawn git processes.
    property bool active: false
    property int subTab: 0
    property string commitMessage: ""
    property string operationNotice: ""
    property string pendingAction: ""
    // Which confirmation the shared dialog is currently showing.
    property string pendingIntent: ""
    property string pendingDiscardPath: ""
    property bool pendingDiscardUntracked: false
    property int pendingStashIndex: -1

    property var stashEntries: []
    property bool stashLoading: false
    property bool stashRequested: false
    property string stashError: ""
    property string stashToken: ""

    // Read-only branch view, loaded on demand (opening the tab or the dialog).
    property var branchEntries: []
    property var remoteEntries: []
    property bool branchLoading: false
    property bool branchRequested: false
    property string branchError: ""
    property string branchToken: ""

    // Commit-message drafting. `draftPending` is the panel's own generation:
    // a draft that lands after a root switch or a cancel must be dropped.
    property bool draftPending: false
    property string draftDiffToken: ""
    property string draftBaseline: ""

    readonly property var currentBranch: {
        for (let i = 0; i < branchEntries.length; ++i) {
            if (branchEntries[i].current === true)
                return branchEntries[i]
        }
        return null
    }
    // How far the checked-out branch has drifted from its upstream. Empty when
    // there is nothing to say (no upstream known locally, or already in sync).
    readonly property string trackingBadge: {
        const branch = currentBranch
        if (!branch || !branch.upstream || branch.upstream.length === 0)
            return ""
        if (branch.trackState === "gone")
            return qsTr("上游已删除")
        if (branch.trackState === "diverged")
            return qsTr("↑%1 ↓%2").arg(branch.ahead).arg(branch.behind)
        if (branch.trackState === "ahead")
            return qsTr("↑%1").arg(branch.ahead)
        if (branch.trackState === "behind")
            return qsTr("↓%1").arg(branch.behind)
        return qsTr("已同步")
    }

    property var logEntries: []
    property int logOffset: 0
    property bool logLoading: false
    property string logError: ""
    property bool logHasMore: false
    property bool logRequested: false
    property string logToken: ""
    // History filter, applied on demand (see applyLogFilter).
    property string logGrep: ""
    property string logAuthor: ""
    // First commit picked for the any-two-commits comparison.
    property var compareBase: null
    readonly property int pageSize: 20

    readonly property var repoInfo: GitVM.repoInfo || {}
    readonly property bool repoKnown: repoInfo.isRepo !== undefined
    readonly property bool isRepo: repoInfo.isRepo === true
    // Three distinct states, not two. A local chat has no project root at all,
    // so there is nothing to identify and nothing to report: treating "no
    // root" like "identification pending" left the panel showing
    // 「正在识别 Git 仓库…」 forever, because a refresh only ever runs once a
    // root exists - so the answer it was waiting for could never arrive.
    readonly property bool hasProject: projectRoot.length > 0
    // "Still identifying" is only meaningful within a project.
    readonly property bool identifying: hasProject && !repoKnown
    readonly property var statusEntries: GitVM.statusEntries || []
    readonly property var stagedEntries: entriesFor(true)
    readonly property var unstagedEntries: entriesFor(false)
    readonly property int stagedCount: stagedEntries.length
    readonly property int unstagedCount: unstagedEntries.length
    readonly property int dirtyCount: statusEntries.length
    readonly property int logCount: logEntries.length
    readonly property string branchLabel: {
        if (!hasProject) return qsTr("未关联项目")
        if (!repoKnown) return qsTr("正在识别 Git 仓库…")
        if (!isRepo) return qsTr("不在 Git 仓库")
        if (repoInfo.detached === true)
            return qsTr("分离 HEAD @ %1").arg(repoInfo.branch || "")
        return repoInfo.branch ? repoInfo.branch : qsTr("(无分支)")
    }
    readonly property alias workingDialogVisible: workingDialog.visible
    readonly property alias commitDialogVisible: commitDialog.visible
    readonly property alias workingDialogItem: workingDialog
    readonly property alias commitDialogItem: commitDialog
    readonly property alias commitConfirmVisible: commitConfirm.visible
    readonly property alias commitConfirmItem: commitConfirm
    readonly property alias actionDialogVisible: actionDialog.visible
    readonly property alias actionDialogItem: actionDialog
    readonly property alias fileHistoryDialogVisible: fileHistoryDialog.visible
    readonly property alias fileHistoryDialogItem: fileHistoryDialog
    readonly property alias branchDialogVisible: branchDialog.visible
    readonly property alias branchDialogItem: branchDialog
    readonly property alias compareDialogVisible: compareDialog.visible
    readonly property alias compareDialogItem: compareDialog
    readonly property alias blameDialogVisible: blameDialog.visible
    readonly property alias blameDialogItem: blameDialog
    readonly property alias graphDialogVisible: graphDialog.visible
    readonly property alias graphDialogItem: graphDialog
    readonly property bool hasCompareBase: compareBase !== null
    readonly property string compareBaseLabel: compareBase
        ? root.shortOf(compareBase) : ""
    readonly property int stashCount: stashEntries.length
    readonly property int branchRowCount: branchEntries.length

    // Status rows: a file with both staged and unstaged edits appears in both
    // groups, using the code relevant to that group.
    function entriesFor(staged) {
        const out = []
        for (let i = 0; i < statusEntries.length; ++i) {
            const entry = statusEntries[i]
            const code = String(staged ? entry.indexStatus : entry.worktreeStatus)
            if (!code || code === " ") continue
            if (staged && code === "?") continue
            const row = {}
            for (const key in entry)
                row[key] = entry[key]
            row.statusLabel = code
            row.stagedView = staged
            out.push(row)
        }
        return out
    }

    function resetData() {
        root.dropDraft()
        logEntries = []
        logOffset = 0
        logLoading = false
        logError = ""
        logHasMore = false
        logRequested = false
        logToken = ""
        logGrep = ""
        logAuthor = ""
        compareBase = null
        // The fields are not bound, so clear them or they would show a filter
        // that no longer applies to the new repository.
        grepField.text = ""
        authorField.text = ""
        commitMessage = ""
        operationNotice = ""
        pendingAction = ""
        pendingIntent = ""
        pendingDiscardPath = ""
        pendingDiscardUntracked = false
        pendingStashIndex = -1
        stashEntries = []
        stashLoading = false
        stashRequested = false
        stashError = ""
        stashToken = ""
        branchEntries = []
        remoteEntries = []
        branchLoading = false
        branchRequested = false
        branchError = ""
        branchToken = ""
        if (workingDialog.visible) workingDialog.close()
        if (commitDialog.visible) commitDialog.close()
        if (commitConfirm.visible) commitConfirm.close()
        if (actionDialog.visible) actionDialog.close()
        if (fileHistoryDialog.visible) fileHistoryDialog.close()
        if (branchDialog.visible) branchDialog.close()
        if (compareDialog.visible) compareDialog.close()
        if (blameDialog.visible) blameDialog.close()
        if (graphDialog.visible) graphDialog.close()
    }

    // Drafting asks the model for a message; the composer is still the user's,
    // and committing still requires pressing 提交 and confirming.
    function canDraft() {
        return root.stagedCount > 0 && !GitVM.operationBusy && !CommitDrafter.busy
            && !root.draftPending
    }

    function requestDraft() {
        if (!root.canDraft()) return
        root.draftPending = true
        root.draftBaseline = root.commitMessage
        root.draftDiffToken = GitVM.requestWorkingDiff("", true)
        if (root.draftDiffToken.length === 0) {
            root.draftPending = false
            root.operationNotice = qsTr("当前没有可用的 Git 项目。")
        }
    }

    function dropDraft() {
        if (!root.draftPending) return
        root.draftPending = false
        root.draftDiffToken = ""
        CommitDrafter.cancel()
    }

    // A drafted message only ever lands in the composer, and never overwrites
    // text the user typed meanwhile. Committing still requires its own click
    // plus the confirmation dialog.
    function applyDraft(ok, text, error) {
        if (!root.draftPending) return
        root.draftPending = false
        if (!ok) {
            root.operationNotice = error
            return
        }
        if (root.commitMessage !== root.draftBaseline) {
            root.operationNotice = qsTr("提交说明已被修改，未覆盖生成结果。")
            return
        }
        root.commitMessage = text
        root.operationNotice = qsTr("已生成提交说明，可继续编辑后再提交")
    }

    function subjectsForPrompt() {
        const out = []
        for (let i = 0; i < root.logEntries.length && out.length < 5; ++i)
            out.push(String(root.logEntries[i].subject || ""))
        return out
    }

    function reloadBranchView() {
        if (projectRoot.length === 0) return
        branchLoading = true
        branchRequested = true
        branchError = ""
        branchToken = GitVM.requestBranchView()
        if (branchToken.length === 0) branchLoading = false
    }

    function refreshAll() {
        if (projectRoot.length === 0) return
        GitVM.projectRoot = projectRoot
        GitVM.refreshRepoInfo()
        GitVM.refreshStatus()
        reloadBranchView()
        if (subTab === 1) reloadLog()
    }

    function reloadLog() {
        if (projectRoot.length === 0) return
        logEntries = []
        logOffset = 0
        logError = ""
        logHasMore = false
        logRequested = true
        logLoading = true
        logToken = GitVM.requestRepoLog(pageSize, 0, root.logGrep, root.logAuthor)
    }

    // Filters are applied on purpose (Enter or 筛选), never per keystroke: each
    // one runs a git process, and a hidden project must not spawn them.
    function applyLogFilter(grep, author) {
        root.logGrep = grep
        root.logAuthor = author
        if (root.projectRoot.length === 0) return
        root.reloadLog()
    }

    function clearLogFilter() {
        root.applyLogFilter("", "")
    }

    function filterActive() {
        return root.logGrep.length > 0 || root.logAuthor.length > 0
    }

    function shortOf(commit) {
        if (!commit) return ""
        if (commit.shortSha) return String(commit.shortSha)
        return String(commit.sha || "").substring(0, 7)
    }

    // Two-step pick, so comparing never needs a selection mode: the first click
    // sets A, the second click on a different commit opens A→B, clicking A again
    // cancels it.
    function pickCompare(commit) {
        if (!commit || !commit.sha) return
        if (!root.compareBase) {
            root.compareBase = commit
            root.operationNotice = qsTr("已选为对比基准 %1，再点另一行开始对比")
                .arg(root.shortOf(commit))
            return
        }
        if (root.compareBase.sha === commit.sha) {
            root.compareBase = null
            root.operationNotice = qsTr("已取消对比基准")
            return
        }
        compareDialog.openFor(root.compareBase, commit)
    }

    function clearCompareBase() {
        root.compareBase = null
    }

    function loadMore() {
        if (logLoading || !logHasMore) return
        logLoading = true
        logToken = GitVM.requestRepoLog(pageSize, logOffset,
                                        root.logGrep, root.logAuthor)
    }

    // A write that changes history: refresh the page already on screen, or
    // drop it and let the history tab reload when someone opens it.
    function refreshLog(hadLog) {
        if (hadLog || root.subTab === 1) {
            root.reloadLog()
            return
        }
        logRequested = false
        logEntries = []
        logOffset = 0
    }

    function setSubTab(index) {
        subTab = index
    }

    function stageEntry(entry) {
        if (!entry || !entry.path || GitVM.operationBusy) return
        pendingAction = "stage"
        operationNotice = ""
        GitVM.stageFiles([entry.path])
    }

    function unstageEntry(entry) {
        if (!entry || !entry.path || GitVM.operationBusy) return
        pendingAction = "unstage"
        operationNotice = ""
        GitVM.unstageFiles([entry.path])
    }

    function canCommit() {
        return root.stagedCount > 0
            && root.commitMessage.trim().length > 0
            && !GitVM.operationBusy
    }

    function openCommitConfirm() {
        if (!canCommit()) return
        commitConfirm.open()
    }

    function performCommit() {
        if (!canCommit()) return
        commitConfirm.close()
        pendingAction = "commit"
        operationNotice = ""
        GitVM.commitStaged(root.commitMessage.trim())
    }

    function openWorkingFile(entry, staged) {
        if (!entry || !entry.path) return
        const markUntracked = !staged
            && String(entry.worktreeStatus || "") === "?"
        workingDialog.openFor(entry.path, staged, markUntracked, entry.path)
    }

    // ── P1 recovery actions ────────────────────────────
    // amend and undo rewrite HEAD, so the dialog must show the commit that is
    // about to disappear. When the log page is not loaded yet the request is
    // remembered and the dialog opens as soon as it arrives.
    function headCommit() {
        return root.logCount > 0 ? root.logEntries[0] : null
    }

    function withHeadCommit(intent) {
        if (GitVM.operationBusy) return
        const head = root.headCommit()
        if (head) {
            root.openIntentDialog(intent, head)
            return
        }
        root.pendingIntent = intent
        if (root.logCount === 0) root.reloadLog()
    }

    function openIntentDialog(intent, head) {
        // amend and undo rewrite HEAD, so they need the commit that is about
        // to disappear; stash only touches the worktree.
        if (intent !== "stashPush" && (!head || !head.sha)) return
        const subject = head ? String(head.subject || "") : ""
        const shortSha = head ? String(head.shortSha || "") : ""
        // performIntent() reads this to route the confirm button.
        root.pendingIntent = intent
        if (intent === "amend") {
            actionDialog.openWith({
                "title": qsTr("改写最后一次提交？"),
                "details": [
                    qsTr("git commit --amend"),
                    qsTr("当前 HEAD：%1 %2").arg(shortSha).arg(subject),
                    qsTr("同时把已暂存的 %1 个文件并入该提交。").arg(root.stagedCount),
                    qsTr("已推送到远端的提交会被拒绝；旧提交仍可从 reflog 找回。")
                ],
                "confirm": qsTr("改写提交"),
                "editable": true,
                "requireText": true,
                "placeholder": qsTr("新的提交说明（必填）"),
                "text": subject,
                "enabled": !GitVM.operationBusy
            })
            return
        }
        if (intent === "undo") {
            actionDialog.openWith({
                "title": qsTr("撤销最后一次提交？"),
                "details": [
                    qsTr("git reset --soft HEAD^"),
                    qsTr("将撤销：%1 %2").arg(shortSha).arg(subject),
                    qsTr("改动全部退回暂存区，不会丢失。"),
                    qsTr("已推送、合并提交或首次提交会被拒绝。")
                ],
                "confirm": qsTr("撤销提交"),
                "enabled": !GitVM.operationBusy
            })
            return
        }
        if (intent === "stashPush") {
            actionDialog.openWith({
                "title": qsTr("暂存当前改动（stash）？"),
                "details": [
                    qsTr("git stash push"),
                    qsTr("只收起已跟踪的改动，未跟踪文件保持原地。"),
                    qsTr("取出前这些改动不在工作区，其他工具看不到它们。")
                ],
                "confirm": qsTr("暂存改动"),
                "editable": true,
                "placeholder": qsTr("备注（可选）"),
                "enabled": !GitVM.operationBusy
            })
        }
    }

    function performIntent(text) {
        if (root.pendingIntent.length === 0) return
        const intent = root.pendingIntent
        if (intent === "amend") {
            const message = text.trim()
            if (message.length === 0) return
            root.pendingAction = "amend"
            root.operationNotice = ""
            GitVM.amendLast(message)
        } else if (intent === "undo") {
            root.pendingAction = "undo"
            root.operationNotice = ""
            GitVM.undoLastCommit()
        } else if (intent === "stashPush") {
            root.pendingAction = "stashPush"
            root.operationNotice = ""
            GitVM.stashPush(text.trim())
        }
        root.pendingIntent = ""
        actionDialog.close()
    }

    function openDiscardConfirm(path, untracked) {
        if (!path || GitVM.operationBusy) return
        root.pendingDiscardPath = path
        root.pendingDiscardUntracked = untracked === true
        const details = []
        if (untracked === true) {
            details.push(qsTr("该文件未被 git 跟踪，git 里没有它的副本。"))
            details.push(qsTr("会先复制到备份目录，再删除原文件。"))
            details.push(qsTr("备份位置：%1").arg(GitVM.discardBackupRoot))
        } else {
            details.push(qsTr("git restore -- %1").arg(path))
            details.push(qsTr("工作区内容回到暂存区版本，已暂存的改动保留。"))
            details.push(qsTr("该操作无法通过 git 撤销。"))
        }
        actionDialog.openWith({
            "title": untracked === true
                ? qsTr("删除未跟踪文件？") : qsTr("丢弃该文件的改动？"),
            "details": details,
            "confirm": untracked === true ? qsTr("备份并删除") : qsTr("丢弃改动"),
            "enabled": !GitVM.operationBusy
        })
    }

    function performDiscard() {
        const path = root.pendingDiscardPath
        root.pendingDiscardPath = ""
        actionDialog.close()
        if (!path || GitVM.operationBusy) return
        root.pendingAction = "discard"
        root.operationNotice = ""
        GitVM.discardPath(path)
        if (workingDialog.visible
                && workingDialog.path === path)
            workingDialog.close()
    }

    function reloadStashList() {
        if (GitVM.operationBusy) return
        root.stashRequested = true
        root.stashLoading = true
        root.stashError = ""
        root.stashToken = GitVM.requestStashList()
        if (root.stashToken.length === 0) {
            root.stashLoading = false
            root.stashError = qsTr("当前没有可用的 Git 项目。")
        }
    }

    function openStashPopConfirm(index, subject) {
        if (GitVM.operationBusy) return
        root.pendingStashIndex = index
        actionDialog.openWith({
            "title": qsTr("取出这条暂存？"),
            "details": [
                qsTr("git stash pop stash@{%1}").arg(index),
                subject.length > 0 ? subject : qsTr("(无备注)"),
                qsTr("改动会立即进入工作区；与工作区现有改动冲突时 git 会拒绝，暂存条目保持不动。")
            ],
            "confirm": qsTr("取出暂存"),
            "enabled": !GitVM.operationBusy
        })
    }

    function performStashPop() {
        const index = root.pendingStashIndex
        root.pendingStashIndex = -1
        actionDialog.close()
        if (index < 0 || GitVM.operationBusy) return
        root.pendingAction = "stashPop"
        root.operationNotice = ""
        GitVM.stashPop(index)
    }

    // Test/convenience entry that does not need a row map.
    function openWorkingFileFor(path, staged, untracked) {
        workingDialog.openFor(path, staged, untracked === true, path)
    }

    function openCommit(commitMap) {
        if (!commitMap || !commitMap.sha) return
        commitDialog.openFor(commitMap)
    }

    function openCommitAt(index) {
        if (index >= 0 && index < logEntries.length)
            openCommit(logEntries[index])
    }

    Component.onCompleted: {
        GitVM.projectRoot = root.projectRoot
        if (root.active && root.projectRoot.length > 0)
            root.refreshAll()
    }

    onProjectRootChanged: {
        GitVM.projectRoot = root.projectRoot
        root.resetData()
        if (root.active && root.projectRoot.length > 0)
            root.refreshAll()
    }

    onActiveChanged: {
        if (root.active && root.projectRoot.length > 0)
            root.refreshAll()
    }

    onSubTabChanged: {
        if (root.subTab === 1 && !root.logRequested
                && root.projectRoot.length > 0)
            root.reloadLog()
    }

    Connections {
        target: GitVM

        function onRepoLogReady(requestId, commits, offset, error) {
            if (requestId !== root.logToken) return
            root.logError = error
            if (offset === 0) {
                root.logEntries = commits
            } else {
                const merged = root.logEntries.slice()
                for (let i = 0; i < commits.length; ++i)
                    merged.push(commits[i])
                root.logEntries = merged
            }
            root.logOffset = offset + commits.length
            root.logHasMore = error.length === 0
                && commits.length >= root.pageSize
            root.logLoading = false
            // A dialog waiting on HEAD opens now.
            if (root.pendingIntent.length > 0 && error.length === 0
                    && root.logCount > 0) {
                const intent = root.pendingIntent
                root.pendingIntent = ""
                root.openIntentDialog(intent, root.logEntries[0])
            }
        }

        function onStashListReady(requestId, entries, error) {
            if (requestId !== root.stashToken) return
            root.stashEntries = entries
            root.stashError = error
            root.stashLoading = false
        }

        function onBranchViewReady(requestId, payload, error) {
            if (requestId !== root.branchToken) return
            root.branchEntries = payload.branches || []
            root.remoteEntries = payload.remotes || []
            root.branchError = error
            root.branchLoading = false
        }

        // The staged diff is fetched first so the panel, not the model layer,
        // decides when a request is actually worth making.
        function onWorkingDiffReady(requestId, path, staged, diff, truncated, error) {
            if (requestId !== root.draftDiffToken) return
            root.draftDiffToken = ""
            if (!root.draftPending) return
            if (error.length > 0 || diff.trim().length === 0) {
                root.draftPending = false
                root.operationNotice = error.length > 0 ? error
                                                        : qsTr("没有已暂存的改动。")
                return
            }
            // A truncated stage is still useful: the drafter cuts it to a line
            // boundary before sending.
            CommitDrafter.draft(diff, root.subjectsForPrompt())
        }

        function onOperationFinished(requestId, action, success, error) {
            root.pendingAction = ""
            const hadLog = root.logCount > 0
            if (!success) {
                root.operationNotice = ""
                root.pendingIntent = ""
                return
            }
            if (action === "commit") {
                root.commitMessage = ""
                root.operationNotice = qsTr("提交成功")
                root.refreshLog(hadLog)
                if (root.branchRequested) root.reloadBranchView()
            } else if (action === "stage") {
                root.operationNotice = qsTr("已暂存")
            } else if (action === "unstage") {
                root.operationNotice = qsTr("已取消暂存")
            } else if (action === "discard") {
                // An untracked delete leaves a backup behind; say where.
                root.operationNotice = GitVM.lastBackupPath.length > 0
                    ? qsTr("已删除，备份在 %1").arg(GitVM.lastBackupPath)
                    : qsTr("已丢弃改动")
            } else if (action === "amend" || action === "undo") {
                root.operationNotice = action === "amend"
                    ? qsTr("已改写上次提交") : qsTr("已撤销上次提交，改动退回暂存区")
                root.refreshLog(hadLog)
                if (root.branchRequested) root.reloadBranchView()
            } else if (action === "stashPush") {
                root.operationNotice = qsTr("改动已暂存到 stash")
                root.reloadStashList()
            } else if (action === "stashPop") {
                root.operationNotice = qsTr("已取出暂存")
                root.reloadStashList()
            }
        }
    }

    Connections {
        target: AgentVM

        // The agent writes files through tools; refresh the repository view
        // shortly after the checkpoint list changes instead of polling.
        function onFileCheckpointsChanged() {
            if (root.active) refreshTimer.restart()
        }
    }

    Connections {
        target: CommitDrafter

        function onDrafted(ok, text, error) {
            root.applyDraft(ok, text, error)
        }
    }

    Timer {
        id: refreshTimer
        interval: 500
        repeat: false
        onTriggered: {
            if (root.active && root.projectRoot.length > 0) {
                GitVM.refreshStatus()
                GitVM.refreshRepoInfo()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ── Repository header ─────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                icon: "branch"
                size: 12
                iconColor: root.isRepo ? Theme.brandPrimary
                                       : Theme.textSecondary
            }

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: root.branchLabel
                color: Theme.textPrimary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideMiddle
                Accessible.name: text
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                // Drift from the upstream, straight from the repository's own
                // refs: no fetch, so a stale count is exactly what git knows.
                visible: root.isRepo && root.trackingBadge.length > 0
                text: root.trackingBadge
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeMicro
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: root.isRepo && root.dirtyCount > 0
                text: qsTr("%1 项改动").arg(root.dirtyCount)
                color: Theme.statusWarning
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeMicro
            }

            IconButton {
                objectName: "gitBranchButton"
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 4
                icon: "branch"
                iconSize: 11
                tooltip: qsTr("查看分支与远端")
                onClicked: branchDialog.openFor(true)
            }

            IconButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 4
                icon: "refresh"
                iconSize: 11
                enabled: !(GitVM.repoInfoLoading || GitVM.statusLoading
                           || root.logLoading || GitVM.operationBusy)
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("刷新 Git 状态")
                onClicked: root.refreshAll()
            }
        }

        // ── Sub tabs ──────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppButton {
                objectName: "gitChangesTab"
                text: qsTr("改动")
                implicitHeight: 24
                highlighted: root.subTab === 0
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: root.setSubTab(0)
            }

            AppButton {
                objectName: "gitHistoryTab"
                text: qsTr("历史")
                implicitHeight: 24
                highlighted: root.subTab === 1
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: root.setSubTab(1)
            }

            Item { Layout.fillWidth: true }
        }

        // ── Local recovery actions ────────────────────
        // Every button here only opens a confirmation; the git command runs
        // after the user presses confirm, one write at a time.
        RowLayout {
            objectName: "gitLocalOpsRow"
            Layout.fillWidth: true
            visible: root.isRepo
            spacing: 4

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("本地操作")
                color: Theme.textTertiary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeNano
            }

            IconButton {
                objectName: "gitUndoButton"
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "undo"
                iconSize: 11
                enabled: !GitVM.operationBusy
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("撤销最后一次提交（改动退回暂存区）")
                onClicked: root.withHeadCommit("undo")
            }

            IconButton {
                objectName: "gitAmendButton"
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "edit"
                iconSize: 11
                enabled: !GitVM.operationBusy
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("改写最后一次提交（并入已暂存改动）")
                onClicked: root.withHeadCommit("amend")
            }

            IconButton {
                objectName: "gitStashButton"
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "archive"
                iconSize: 11
                enabled: !GitVM.operationBusy
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("把已跟踪改动收进 stash")
                onClicked: root.openIntentDialog("stashPush", root.headCommit())
            }

            IconButton {
                objectName: "gitStashListButton"
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "layers"
                iconSize: 11
                enabled: !GitVM.operationBusy
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("查看 stash 列表")
                onClicked: root.reloadStashList()
            }

            Item { Layout.fillWidth: true }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.subTab

            // ── Changes ───────────────────────────────
            ColumnLayout {
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    visible: GitVM.statusLoading && root.statusEntries.length === 0
                    text: qsTr("正在读取 Git 状态…")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }

                Text {
                    Layout.fillWidth: true
                    visible: GitVM.statusError.length > 0
                    text: GitVM.statusError
                    color: Theme.statusWarning
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                // Local chats: no project, so no repository to talk about.
                // This is the line the panel was missing - without it the
                // empty state was just the (never-ending) identifying one.
                Text {
                    objectName: "gitNoProjectHint"
                    Layout.fillWidth: true
                    visible: !root.hasProject
                    text: qsTr("当前会话未关联项目；在项目中打开的会话才能使用 Git 管理。")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.identifying
                    text: qsTr("正在识别 Git 仓库…")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.repoKnown && !root.isRepo
                    text: qsTr("当前项目不是 Git 仓库，无法使用 Git 管理。")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.isRepo && !GitVM.statusLoading
                             && GitVM.statusError.length === 0
                             && root.statusEntries.length === 0
                    text: qsTr("工作区干净，没有未提交的改动。")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                    wrapMode: Text.Wrap
                }

                // Stage markers are read-only in this phase; Phase 3 adds the
                // stage/unstage actions and commit composer here.
                Text {
                    Layout.fillWidth: true
                    visible: root.isRepo && root.stagedCount > 0
                    text: qsTr("已暂存 · %1").arg(root.stagedCount)
                    color: Theme.statusSuccess
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                }

                ChangeTree {
                    id: stagedTree
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(stagedTree.contentHeight, 170)
                    visible: root.isRepo && root.stagedCount > 0
                    items: root.stagedEntries
                    basePath: ""
                    autoBasePath: false
                    rowHeight: 24
                    actionsEnabled: !GitVM.operationBusy
                    actionFor: function(entry) {
                        return { "icon": "minus",
                                 "tooltip": qsTr("取消暂存该文件"),
                                 "enabled": !GitVM.operationBusy }
                    }
                    onFileActivated: (item) => root.openWorkingFile(item, true)
                    onFileActionRequested: (item) => root.unstageEntry(item)
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.isRepo && root.unstagedCount > 0
                    text: qsTr("未暂存 · %1").arg(root.unstagedCount)
                    color: Theme.statusWarning
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                }

                ChangeTree {
                    id: unstagedTree
                    Layout.fillWidth: true
                    Layout.fillHeight: root.isRepo && root.unstagedCount > 0
                    Layout.preferredHeight: root.isRepo
                        ? unstagedTree.contentHeight : 0
                    visible: root.isRepo && root.unstagedCount > 0
                    items: root.unstagedEntries
                    basePath: ""
                    autoBasePath: false
                    rowHeight: 24
                    actionsEnabled: !GitVM.operationBusy
                    actionFor: function(entry) {
                        return { "icon": "plus",
                                 "tooltip": qsTr("暂存该文件"),
                                 "enabled": !GitVM.operationBusy }
                    }
                    onFileActivated: (item) => root.openWorkingFile(item, false)
                    onFileActionRequested: (item) => root.stageEntry(item)
                }

                // ── Commit composer ────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.isRepo
                    spacing: 4

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        radius: 6
                        color: Theme.bgPrimary
                        border.color: commitInput.activeFocus
                            ? Theme.brandPrimary : Theme.borderSubtle

                        TextArea {
                            id: commitInput
                            objectName: "gitCommitMessage"
                            anchors.fill: parent
                            anchors.margins: 6
                            padding: 0
                            text: root.commitMessage
                            placeholderText: qsTr("提交说明（必填）")
                            placeholderTextColor: Theme.textTertiary
                            color: Theme.textPrimary
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.fontSizeCaption
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            background: Rectangle { color: "transparent" }
                            onTextChanged: {
                                if (root.commitMessage !== text)
                                    root.commitMessage = text
                            }
                            Keys.onPressed: (event) => {
                                if ((event.key === Qt.Key_Return
                                     || event.key === Qt.Key_Enter)
                                        && (event.modifiers & Qt.ControlModifier)) {
                                    root.openCommitConfirm()
                                    event.accepted = true
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("已暂存 %1 个文件").arg(root.stagedCount)
                            color: root.stagedCount > 0 ? Theme.statusSuccess
                                                        : Theme.textSecondary
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.fontSizeMicro
                        }

                        AppButton {
                            objectName: "gitDraftButton"
                            text: qsTr("AI 起草")
                            implicitHeight: 24
                            enabled: root.canDraft()
                            opacity: enabled ? 1.0 : 0.4
                            Accessible.name: qsTr("用已暂存的差异起草提交说明")
                            onClicked: root.requestDraft()
                        }

                        AppButton {
                            objectName: "gitCommitButton"
                            text: qsTr("提交")
                            implicitHeight: 24
                            enabled: root.canCommit()
                            opacity: enabled ? 1.0 : 0.4
                            onClicked: root.openCommitConfirm()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.draftPending || CommitDrafter.busy
                        text: qsTr("正在起草提交说明…")
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: GitVM.operationBusy
                        text: root.pendingAction === "commit"
                            ? qsTr("正在提交…") : qsTr("正在更新暂存区…")
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: GitVM.operationError.length > 0
                        text: GitVM.operationError
                        color: Theme.statusWarning
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.operationNotice.length > 0
                                 && !GitVM.operationBusy
                        text: root.operationNotice
                        color: Theme.statusSuccess
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                    }
                }

                // ── Stash list ─────────────────────────
                // Read on demand: the panel never runs `git stash list` for a
                // project whose tab nobody opened.
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.isRepo && root.stashRequested
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        visible: root.stashLoading
                        text: qsTr("正在读取 stash…")
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.stashLoading && root.stashError.length > 0
                        text: root.stashError
                        color: Theme.statusWarning
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.stashLoading && root.stashError.length === 0
                                 && root.stashCount === 0
                        text: qsTr("stash 是空的。")
                        color: Theme.textTertiary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                    }

                    Flickable {
                        objectName: "gitStashList"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(stashColumn.implicitHeight, 130)
                        visible: !root.stashLoading && root.stashCount > 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentHeight: stashColumn.implicitHeight
                        ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

                        ColumnLayout {
                            id: stashColumn
                            width: parent ? parent.width : 0
                            spacing: 2

                            Repeater {
                                model: root.stashEntries

                                delegate: Rectangle {
                                    id: stashRow
                                    objectName: "gitStashRow"
                                    required property var modelData

                                    Layout.fillWidth: true
                                    height: 30
                                    radius: 5
                                    color: Theme.bgPrimary

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 4
                                        spacing: 6

                                        Text {
                                            Layout.fillWidth: true
                                            text: "stash@{" + modelData.index + "} · "
                                                  + (modelData.subject.length > 0
                                                     ? modelData.subject : qsTr("(无备注)"))
                                            color: Theme.textPrimary
                                            font.family: Theme.fontMono
                                            font.pixelSize: Theme.fontSizeNano
                                            elide: Text.ElideRight
                                        }

                                        AppButton {
                                            objectName: "gitStashPopButton"
                                            text: qsTr("取出")
                                            implicitHeight: 20
                                            enabled: !GitVM.operationBusy
                                            opacity: enabled ? 1.0 : 0.35
                                            onClicked: root.openStashPopConfirm(
                                                modelData.index,
                                                String(modelData.subject || ""))
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                    visible: !root.isRepo || root.unstagedCount === 0
                }
            }

            // ── History ───────────────────────────────
            ColumnLayout {
                spacing: 6

                // Filter row: keyword over the message, plus author. Applied on
                // Enter or 筛选 so typing never fans out git processes.
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.isRepo
                    spacing: 4

                    TextField {
                        id: grepField
                        objectName: "gitLogGrepInput"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        placeholderText: qsTr("搜索提交说明")
                        color: Theme.textPrimary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                        selectByMouse: true
                        onAccepted: root.applyLogFilter(text, authorField.text)
                    }

                    TextField {
                        id: authorField
                        objectName: "gitLogAuthorInput"
                        Layout.preferredWidth: 68
                        Layout.preferredHeight: 24
                        placeholderText: qsTr("作者")
                        color: Theme.textPrimary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeMicro
                        selectByMouse: true
                        onAccepted: root.applyLogFilter(grepField.text, text)
                    }

                    AppButton {
                        objectName: "gitGraphButton"
                        text: qsTr("图")
                        implicitHeight: 22
                        enabled: root.isRepo
                        opacity: enabled ? 1.0 : 0.4
                        Accessible.name: qsTr("打开多分支提交图")
                        onClicked: graphDialog.openFor()
                    }

                    AppButton {
                        objectName: "gitLogFilterButton"
                        text: qsTr("筛选")
                        implicitHeight: 22
                        enabled: root.isRepo && !root.logLoading
                        opacity: enabled ? 1.0 : 0.4
                        onClicked: root.applyLogFilter(grepField.text, authorField.text)
                    }

                    IconButton {
                        objectName: "gitLogClearButton"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        radius: 4
                        icon: "close"
                        iconSize: 10
                        visible: root.filterActive()
                        tooltip: qsTr("清除筛选条件")
                        onClicked: {
                            grepField.text = ""
                            authorField.text = ""
                            root.clearLogFilter()
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.filterActive() && !root.logLoading
                             && root.logError.length === 0
                    text: root.logCount === 0
                        ? qsTr("没有符合条件的提交。")
                        : qsTr("筛选后显示 %1 条（关键词 %2，作者 %3）")
                              .arg(root.logCount)
                              .arg(root.logGrep.length > 0 ? root.logGrep : "—")
                              .arg(root.logAuthor.length > 0 ? root.logAuthor : "—")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeNano
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.hasCompareBase
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("对比基准 A：%1").arg(root.compareBaseLabel)
                        color: Theme.statusWarning
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeNano
                        elide: Text.ElideRight
                    }

                    AppButton {
                        objectName: "gitCompareBaseClearButton"
                        text: qsTr("取消")
                        implicitHeight: 20
                        onClicked: root.clearCompareBase()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.logLoading && root.logCount === 0
                    text: qsTr("正在读取 Git 历史…")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.logError.length > 0
                    text: root.logError
                    color: Theme.statusWarning
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.logRequested && !root.logLoading
                             && root.logError.length === 0 && root.logCount === 0
                    text: qsTr("仓库还没有提交。")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                }

                ListView {
                    objectName: "gitRepoLog"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.logCount > 0
                    clip: true
                    model: root.logEntries
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: Rectangle {
                        id: logRow
                        objectName: "gitRepoLogRow"
                        required property var modelData

                        width: ListView.view ? ListView.view.width : 0
                        height: 52
                        radius: 6
                        color: logHover.hovered ? Theme.surfaceHover : "transparent"

                        HoverHandler { id: logHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openCommit(logRow.modelData)
                            Accessible.name: qsTr("查看提交详情")
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 28
                            anchors.topMargin: 5
                            anchors.bottomMargin: 5
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    text: logRow.modelData.subject
                                    color: Theme.textPrimary
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.fontSizeCaption
                                    elide: Text.ElideRight
                                }

                                Text {
                                    visible: root.repoInfo.head === logRow.modelData.sha
                                    text: "HEAD"
                                    color: Theme.brandPrimary
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSizeNano
                                    font.bold: true
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: logRow.modelData.shortSha
                                      + " · " + logRow.modelData.author
                                      + " · " + Qt.formatDateTime(
                                            new Date(logRow.modelData.timestamp * 1000),
                                            "MM-dd HH:mm")
                                color: Theme.textSecondary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeNano
                                elide: Text.ElideRight
                            }
                        }

                        // Declared after the row's MouseArea so it wins the
                        // click: this is the two-step comparison pick.
                        IconButton {
                            objectName: "gitComparePickButton"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: 4
                            width: 20
                            height: 20
                            radius: 4
                            icon: "layers"
                            iconSize: 10
                            tooltip: root.hasCompareBase
                                && root.compareBase.sha === logRow.modelData.sha
                                ? qsTr("取消对比基准") : qsTr("选为对比基准 / 与基准对比")
                            onClicked: root.pickCompare(logRow.modelData)
                        }
                    }
                }

                AppButton {
                    Layout.fillWidth: true
                    visible: root.logHasMore && !root.logLoading
                    text: qsTr("加载更多提交")
                    implicitHeight: 26
                    onClicked: root.loadMore()
                }

                Item {
                    Layout.fillHeight: true
                    visible: root.logCount === 0
                }
            }
        }
    }

    GitWorkingDiffDialog {
        id: workingDialog

        onHistoryRequested: (path) => fileHistoryDialog.openFor(path)
        onBlameRequested: (path) => blameDialog.openFor(path)
        onDiscardRequested: (path, untracked) => root.openDiscardConfirm(path, untracked)
    }

    GitBlameDialog {
        id: blameDialog

        onCommitSelected: (commit) => {
            blameDialog.close()
            root.openCommit(commit)
        }
    }

    GitFileHistoryDialog {
        id: fileHistoryDialog

        onCommitSelected: (commit) => {
            fileHistoryDialog.close()
            root.openCommit(commit)
        }
    }

    GitActionDialog {
        id: actionDialog

        // Cancelled confirmations must not leave a runnable intent behind.
        onClosed: {
            root.pendingIntent = ""
            root.pendingDiscardPath = ""
            root.pendingDiscardUntracked = false
            root.pendingStashIndex = -1
        }

        onConfirmed: (text) => {
            if (root.pendingDiscardPath.length > 0) {
                root.performDiscard()
                return
            }
            if (root.pendingStashIndex >= 0) {
                root.performStashPop()
                return
            }
            root.performIntent(text)
        }
    }

    GitCommitDialog {
        id: commitDialog
    }

    GitBranchDialog {
        id: branchDialog
    }

    GitCompareDialog {
        id: compareDialog
    }

    GitGraphDialog {
        id: graphDialog

        onCommitSelected: (commit) => {
            graphDialog.close()
            root.openCommit(commit)
        }
    }

    // Commit confirmation: only staged files are included, and the message is
    // shown verbatim so the user confirms exactly what git will receive.
    Dialog {
        id: commitConfirm
        objectName: "gitCommitConfirm"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 32 : 420)
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: qsTr("提交已暂存的改动？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("将提交 %1 个文件：").arg(root.stagedCount)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }

            ListView {
                id: confirmFileList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 120)
                visible: root.stagedCount > 0
                clip: true
                model: root.stagedEntries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}

                delegate: Text {
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    height: 20
                    text: modelData.path
                    color: Theme.textPrimary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeCaption
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(messagePreview.implicitHeight + 12, 80)
                radius: 6
                color: Theme.bgPrimary
                border.color: Theme.borderSubtle

                Text {
                    id: messagePreview
                    anchors.fill: parent
                    anchors.margins: 6
                    text: root.commitMessage.trim()
                    color: Theme.textSecondary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.WrapAnywhere
                    elide: Text.ElideRight
                    maximumLineCount: 4
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("取消")
                    onClicked: commitConfirm.close()
                }

                AppButton {
                    objectName: "gitCommitConfirmButton"
                    text: qsTr("提交")
                    highlighted: true
                    enabled: root.canCommit()
                    onClicked: root.performCommit()
                }
            }
        }
    }
}