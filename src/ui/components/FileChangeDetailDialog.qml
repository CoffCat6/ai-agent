import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Inline review pane for one file checkpoint.
//
// HISTORY: this component used to be a modal Dialog (1120x760) that covered
// the whole conversation while a diff was open. The review tab now hosts it
// inline, below a draggable divider, so reviewing a file never hides the
// chat. The file name - and with it the QML type name - is deliberately
// kept: a QML module registers its types by file name, so renaming the
// file would need a CMake change outside the scope of this refactor.
//
// Review is scoped to what the agent wrote in this session: the write-time
// diff is the only view. The live working tree and git history live in the
// Git tab instead. `entries` lists every file of the session so review does
// not need closing/reopening per file; viewMode switches the diff between
// unified and side-by-side renderings.
//
// The chrome is split into three rows - title, meta, actions - so it
// survives a narrow column, and the timestamp drops out below 320px
// instead of squeezing the path. The wide host (the review dialog) shows
// the same three rows in a much wider column, which keeps the row
// structure readable instead of spreading the controls apart.
Item {
    id: pane

    property var checkpoint: null
    // All session entries (used for file stepping); empty -> no stepping.
    property var checkpoints: []
    // function(entry) -> detail map; injected by FileChangesPanel.
    property var detailFor: null
    // Active project root, used for tree display paths.
    property string projectRoot: ""

    property string diffText: ""
    property bool diffTruncated: false
    // The stored diff was missing (row written before diffs were persisted)
    // and was rebuilt from the current file content.
    property bool diffFallback: false
    property string viewMode: "unified"
    // Hint shown while no file is open. The host knows where its file
    // list is - above the diff in the stacked layout, beside it in the
    // wide one - so the wording points at the right place.
    property string selectHint: qsTr("在上方选择一个文件查看差异")

    readonly property bool hasDiff: diffText.length > 0
    readonly property bool undone: checkpoint ? checkpoint.undone === true : false
    readonly property bool truncated: checkpoint ? checkpoint.truncated === true : false
    readonly property int addedLines: checkpoint ? (checkpoint.addedLines || 0) : 0
    readonly property int removedLines: checkpoint ? (checkpoint.removedLines || 0) : 0
    readonly property alias diffLineCount: detailDiffView.renderedCount

    readonly property var entries: (checkpoints && checkpoints.length > 0)
        ? checkpoints : (checkpoint ? [checkpoint] : [])
    readonly property int entryIndex: indexOfEntry(checkpoint)
    // True while a file is open in the pane. The pane itself always stays
    // in the layout (an invisible subtree is never polished, so its diff
    // list would only get its size - and its rows - a frame later); this
    // flag is what the host reports as "detail shown".
    readonly property bool active: checkpoint !== null
    // The modal dialog used this flag for its second (file tree) column.
    // The inline pane reuses the panel's own tree, so the flag now means
    // "the session touched more than one file" - i.e. the file-stepping
    // controls are worth showing.
    readonly property bool treeVisible: entries.length > 1
    readonly property bool canStepBack: entryIndex > 0
    readonly property bool canStepForward: entryIndex >= 0
        && entryIndex < entries.length - 1
    readonly property bool undoOffered: checkpoint !== null
        && !undone && !truncated

    // Emitted whenever the pane switches to another file, so the host can
    // mirror the selection (tree highlight) and show the pane.
    signal entrySelected(var entry)
    signal undoRequested(var entry)

    function indexOfEntry(entry) {
        if (!entry) return -1
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i] && entries[i].id === entry.id) return i
        }
        return -1
    }

    function detailForEntry(entry) {
        if (!entry) return {}
        if (pane.detailFor) return pane.detailFor(entry)
        return AgentVM.fileCheckpointDetail(entry.id)
    }

    function applyDetail(detail) {
        const d = detail || {}
        diffText = d.diff !== undefined ? d.diff : ""
        diffTruncated = d.diffTruncated === true
        diffFallback = d.diffFallback === true
    }

    // Opening = pointing the pane at a file. The host decides whether the
    // pane is on screen (there is no popup to raise any more).
    function openFor(entry, detail) {
        if (!entry) return
        checkpoint = entry
        if (detail === undefined || detail === null)
            applyDetail(detailForEntry(entry))
        else
            applyDetail(detail)
        pane.entrySelected(entry)
    }

    // Switching files inside the pane.
    function selectEntry(entry) {
        if (!entry) return
        if (checkpoint && entry.id === checkpoint.id) return
        checkpoint = entry
        applyDetail(detailForEntry(entry))
        pane.entrySelected(entry)
    }

    function stepEntry(delta) {
        const next = entryIndex + delta
        if (next < 0 || next >= entries.length) return
        selectEntry(entries[next])
    }

    // Closing the review (host-side): forget the current file so the pane
    // can go back to its empty state.
    function clearSelection() {
        checkpoint = null
        diffText = ""
        diffTruncated = false
        diffFallback = false
    }

    function statusText() {
        if (!checkpoint) return ""
        if (undone) return qsTr("已撤销")
        if (truncated) return qsTr("仅记录路径，内容未保存")
        return checkpoint.existed ? qsTr("已修改") : qsTr("新建文件")
    }

    function statusColor() {
        if (!checkpoint) return Theme.textSecondary
        if (undone) return Theme.textSecondary
        if (truncated) return Theme.statusWarning
        return Theme.brandPrimary
    }

    function emptyText() {
        if (truncated) return qsTr("原文件超过单文件上限，仅记录了路径，无法展示内容差异；请用 git 查看或恢复。")
        if (!checkpoint || checkpoint.existed === false)
            return qsTr("该新建文件没有可读取的内容：它可能已被删除或改名。")
        return qsTr("此记录没有保存内容差异。")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        // ── Title row: which file is shown ───────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: pane.active

            AppIcon {
                icon: "file"
                size: 13
                iconColor: pane.statusColor()
            }

            Text {
                id: pathText
                Layout.fillWidth: true
                text: pane.checkpoint ? pane.checkpoint.path : ""
                color: Theme.textPrimary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideMiddle
                Accessible.name: text

                // The path is the first thing to be elided in a narrow
                // panel - keep the full value reachable on hover.
                HoverHandler { id: pathHover }
                ToolTip.visible: pathHover.hovered && pathText.truncated
                ToolTip.delay: 400
                ToolTip.text: pathText.text
            }

            Text {
                visible: pane.addedLines > 0
                text: "+" + pane.addedLines
                color: Theme.statusSuccess
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
            }

            Text {
                visible: pane.removedLines > 0
                text: "−" + pane.removedLines
                color: Theme.statusDanger
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
            }
        }

        // ── Meta row: state, time, file stepping ────
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: pane.active

            Text {
                Layout.fillWidth: true
                text: pane.statusText()
                color: pane.statusColor()
                font.family: Theme.fontSans
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Text {
                // The timestamp is the first thing to go when the panel is
                // narrow: it is reference information, while the status and
                // the stepping controls are actionable.
                visible: pane.width >= 320
                text: pane.checkpoint
                    ? Qt.formatDateTime(new Date(pane.checkpoint.createdAt),
                                        "yyyy-MM-dd HH:mm:ss")
                    : ""
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            IconButton {
                objectName: "prevFileButton"
                visible: pane.treeVisible
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "chevron-left"
                iconSize: 11
                enabled: pane.canStepBack
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("上一个文件")
                onClicked: pane.stepEntry(-1)
            }

            IconButton {
                objectName: "nextFileButton"
                visible: pane.treeVisible
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "chevron-right"
                iconSize: 11
                enabled: pane.canStepForward
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("下一个文件")
                onClicked: pane.stepEntry(1)
            }
        }

        // ── Action row: diff mode + undo ─────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: pane.active

            AppButton {
                objectName: "unifiedModeButton"
                visible: pane.hasDiff
                text: qsTr("统一")
                implicitHeight: 22
                highlighted: pane.viewMode === "unified"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: pane.viewMode = "unified"
            }

            AppButton {
                objectName: "splitModeButton"
                visible: pane.hasDiff
                text: qsTr("并排")
                implicitHeight: 22
                highlighted: pane.viewMode === "split"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: pane.viewMode = "split"
            }

            Item { Layout.fillWidth: true }

            AppButton {
                objectName: "undoButton"
                visible: pane.undoOffered
                text: qsTr("撤销")
                implicitHeight: 22
                enabled: !AgentVM.stateBusy
                opacity: enabled ? 1.0 : 0.4
                onClicked: pane.undoRequested(pane.checkpoint)
                Accessible.name: qsTr("撤销该文件在本会话的全部改动")
            }
        }

        Text {
            Layout.fillWidth: true
            visible: pane.diffFallback
            text: qsTr("该记录未保存差异，以下按当前文件内容重建（文件未再改动时即为当时写入的内容）。")
            color: Theme.statusWarning
            font.family: Theme.fontSans
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }

        DiffView {
            id: detailDiffView
            objectName: "detailDiffView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            diffText: pane.diffText
            diffTruncated: pane.diffTruncated
            viewMode: pane.viewMode
            // Without a file there is no diff to render - the hint takes
            // the empty state's place instead of a blank pane.
            emptyText: pane.checkpoint ? pane.emptyText() : pane.selectHint
        }
    }
}
