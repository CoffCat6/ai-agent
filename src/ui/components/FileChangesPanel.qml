import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Review section of the right panel: the session's write_file / edit_file
// checkpoints, one row per file, arranged in a collapsible directory tree.
//
// Layout: the tree and the diff share one area, separated by a draggable
// divider (SplitBar). Two axes are supported:
//
//   "vertical"   - the tree sits above the diff (stacked). This is the
//                  narrow-column layout: at the 220px sidebar minimum two
//                  columns would leave ~90px each, which is unusable for
//                  both a path list and a wrapped diff.
//
//   "horizontal" - the tree sits left of the diff (side by side). This is
//                  the review dialog's layout: with ~1000px of width the
//                  tree keeps a path-list column and the diff gets the
//                  rest of the width *and* the full height, so a long diff
//                  is read without scrolling the file list out of sight.
//
// The divider position is stored as a ratio of the split area, so the
// proportion survives a resize in either axis.
//
// Data flows AgentVM.fileCheckpoints -> here; per-file undo is routed back
// through AgentVM.undoFileCheckpoint. Path-only rows (oversized or
// unreadable originals) are shown as audit history and cannot be undone or
// diffed.
Item {
    id: root

    property var checkpoints: []
    property bool collapsed: false
    property string errorText: ""
    // Test/embedding hook: overrides AgentVM.fileCheckpointDetail(id).
    property var detailProvider: null
    // Active project root: used to display repo-relative paths when set.
    property string projectRoot: ""
    // Tree height cap in bounded contexts; 0 = fill the available height
    // (used when the panel is the body of a fill-height slot, e.g. the
    // review dialog).
    property int listMaxHeight: 260
    // Split axis of the inline tree ↔ diff divider:
    //   "vertical"   - tree above, diff below (stacked; narrow columns)
    //   "horizontal" - tree left, diff right (side by side; wide hosts)
    property string splitAxis: "vertical"
    // Whether the panel draws its own summary header. Hosts that show the
    // summary themselves (the review dialog) turn it off, so the file count
    // and the +/- stats are not printed twice.
    property bool showHeader: true

    readonly property bool sideBySide: splitAxis === "horizontal"

    // ── Inline split (file tree ↔ diff) ────────────────────
    // The divider position is a *share of the split area*, not a pixel
    // count: the host is resizable and the split has to keep its
    // proportion when the window or the panel width changes.
    property real listRatio: root.defaultListRatio
    // Share the tree gets by default. Stacked: enough for its rows (plus a
    // little air), but never more than 45% - the diff needs the other
    // half. Side by side: the tree is a path list and wants a roughly
    // constant width instead of a share of the host.
    readonly property real defaultListRatio: root.computeDefaultRatio()
    // Hard limits, so neither half can be dragged out of existence: the
    // tree keeps ~2 rows (or a readable path column), the diff pane keeps
    // a readable height (or width).
    readonly property int minTreeExtent: sideBySide ? 170 : 56
    readonly property int minDetailExtent: sideBySide ? 420 : 150
    readonly property int splitterThickness: 7
    // Height of the diff pane in bounded contexts (listMaxHeight > 0),
    // where the panel sizes itself from its content instead of filling a
    // host slot.
    readonly property real boundedDetailHeight: 240
    // Height the tree would need to show every row. In bounded contexts
    // the cap applies; listMaxHeight 0 means "fill the slot", not "zero".
    readonly property real naturalTreeHeight: root.listMaxHeight > 0
        ? Math.min(changeTree.contentHeight, root.listMaxHeight)
        : changeTree.contentHeight
    // Total height of the split area in bounded contexts. Side by side the
    // two halves stand next to each other, so the area only has to be as
    // tall as the taller of them.
    readonly property real boundedAreaHeight: root.sideBySide
        ? Math.max(root.naturalTreeHeight, root.boundedDetailHeight)
        : Math.max(root.naturalTreeHeight, root.minTreeExtent)
          + root.splitterThickness + root.boundedDetailHeight

    // File currently open in the detail pane (null = nothing selected).
    property var selectedEntry: null
    readonly property bool hasSelection: selectedEntry !== null

    readonly property int itemCount: checkpoints ? checkpoints.length : 0
    readonly property bool hasItems: itemCount > 0
    readonly property bool listVisible: hasItems && !collapsed
    readonly property int renderedCount: changeTree.fileRowCount
    readonly property int treeRowCount: changeTree.rowCount
    readonly property int treeFolderRowCount: changeTree.folderRowCount
    readonly property alias detailVisible: detailPane.active
    readonly property alias detailDialogItem: detailPane
    readonly property alias undoDialogVisible: undoDialog.visible
    readonly property int activeAdded: sumStat(true)
    readonly property int activeRemoved: sumStat(false)

    // Length of the split area along the split axis: the height when the
    // tree sits above the diff, the width when it sits beside it.
    readonly property real splitExtent: sideBySide
        ? splitArea.width : splitArea.height
    // Size of the tree along the split axis.
    readonly property real treeExtent: root.treeExtentFor(root.splitExtent)
    // The tree fills the cross axis: as wide as the area when it sits
    // above the diff, as tall as the area when it sits beside it.
    readonly property real treeHeight: sideBySide
        ? splitArea.height : root.treeExtent
    readonly property real treeWidth: sideBySide
        ? root.treeExtent : splitArea.width

    function activeCount(entry) {
        if (entry.activeCount !== undefined) return entry.activeCount
        return entry.undone ? 0 : 1
    }

    function changeCount(entry) {
        return entry.changeCount !== undefined ? entry.changeCount : 1
    }

    function statusColor(entry) {
        if (root.activeCount(entry) === 0) return Theme.textSecondary
        if (entry.truncated) return Theme.statusWarning
        return Theme.brandPrimary
    }

    function sumStat(added) {
        let total = 0
        for (let i = 0; i < itemCount; ++i) {
            const entry = checkpoints[i]
            if (root.activeCount(entry) === 0) continue
            total += added ? (entry.addedLines || 0) : (entry.removedLines || 0)
        }
        return total
    }

    function detailFor(entry) {
        if (root.detailProvider) return root.detailProvider(entry.id)
        return AgentVM.fileCheckpointDetail(entry.id)
    }

    // Tallest the tree may become: it must leave the diff pane its minimum
    // size. The tree is not capped at its own content - a short list may
    // still be given more room by dragging, and the empty rows are cheaper
    // than a divider that only moves one way.
    function maxTreeExtent(extent) {
        return Math.max(root.minTreeExtent,
                        extent - root.splitterThickness - root.minDetailExtent)
    }

    // Tree size for a given split-area size: the divider's share,
    // clamped so the tree keeps ~2 rows (or a readable path column) and the
    // diff keeps a usable size.
    function treeExtentFor(extent) {
        if (extent <= 0) return root.minTreeExtent
        return Math.max(root.minTreeExtent,
                        Math.min(root.maxTreeExtent(extent),
                                 Math.round(extent * root.listRatio)))
    }

    // Default share of the split area the tree takes.
    function computeDefaultRatio() {
        const extent = root.splitExtent
        if (root.sideBySide) {
            // The tree is a path list: it wants a roughly constant width
            // (a folder column plus a few characters of the file name),
            // not a share of the host - everything else is the diff's.
            if (extent <= 0) return 0.26
            return Math.max(0.16, Math.min(0.45, (280 + 12) / extent))
        }
        if (extent <= 0) return 0.4
        return Math.max(0.15,
                        Math.min(0.45, (root.naturalTreeHeight + 20) / extent))
    }

    // Dragging the divider: the delta arrives in pixels but the split is
    // stored as a ratio, so the proportion survives a panel resize. The
    // ratio is capped by what the tree can actually take, otherwise the
    // divider would keep "eating" pixels that go nowhere.
    function adjustSplit(delta) {
        const extent = root.splitExtent
        if (extent <= 0) return
        const maxShare = root.maxTreeExtent(extent) / extent
        const next = Math.min(root.listRatio + delta / extent, maxShare)
        root.listRatio = Math.max(0.08, next)
    }

    function resetSplit() {
        root.listRatio = root.defaultListRatio
    }

    function openDetail(entry) {
        if (!entry) return
        detailPane.openFor(entry, root.detailFor(entry))
    }

    function closeDetail() {
        root.selectedEntry = null
        detailPane.clearSelection()
    }

    function detailPath() {
        return detailPane.checkpoint ? detailPane.checkpoint.path : ""
    }

    function detailDiffLineCount() {
        return detailPane.diffLineCount
    }

    // Test hooks for the directory tree.
    function toggleTreeFolder(path) {
        changeTree.toggleFolder(path)
    }

    function treeIsCollapsed(path) {
        return changeTree.isCollapsed(path)
    }

    function expandTree() {
        changeTree.expandAll()
    }

    function collapseTree() {
        changeTree.collapseAll()
    }

    function rowAction(entry) {
        const active = root.activeCount(entry)
        if (active > 0 && entry.truncated === true) {
            return { "text": qsTr("不可撤销"),
                     "tooltip": qsTr("原文件过大，仅记录了路径，请用 git 恢复"),
                     "enabled": false }
        }
        return {
            "text": active === 0 ? qsTr("已撤销") : qsTr("撤销"),
            "tooltip": active === 0
                ? qsTr("本会话对该文件的改动已撤销")
                : qsTr("撤销该文件在本会话的全部改动"),
            "enabled": active > 0
        }
    }

    function requestUndo(entry) {
        root.errorText = ""
        undoDialog.pending = entry
        undoDialog.open()
    }

    function performUndo(entry) {
        root.errorText = ""
        if (!AgentVM.undoFileCheckpoint(entry.id))
            root.errorText = qsTr("撤销失败：文件或检查点已不可用。")
    }

    visible: hasItems
    implicitHeight: hasItems ? content.implicitHeight : 0

    // While the divider is dragged the split must track the cursor exactly
    // instead of easing behind it; the reset animation keeps its easing.
    Behavior on listRatio {
        enabled: !splitBar.dragActive
        NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutQuad }
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 6

        // Header doubles as the collapse toggle. The MouseArea is declared
        // before the row so the header's action buttons stay clickable; plain
        // text does not consume clicks and falls through to the toggle.
        Item {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            visible: root.showHeader

            HoverHandler { id: headerHover }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.collapsed = !root.collapsed
                Accessible.name: qsTr("展开或折叠审阅列表")
            }

            RowLayout {
                anchors.fill: parent
                spacing: 6

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: "edit"
                    size: 12
                    iconColor: Theme.brandPrimary
                }

                Text {
                    text: qsTr("审阅 · %1").arg(root.itemCount)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Item { Layout.fillWidth: true }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.activeAdded > 0
                    text: "+" + root.activeAdded
                    color: Theme.statusSuccess
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.activeRemoved > 0
                    text: "−" + root.activeRemoved
                    color: Theme.statusDanger
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }

                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.hasItems && !root.collapsed
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    radius: 4
                    icon: "chevron-up"
                    iconSize: 10
                    tooltip: qsTr("折叠全部目录")
                    onClicked: root.collapseTree()
                }

                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.hasItems && !root.collapsed
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    radius: 4
                    icon: "chevron-down"
                    iconSize: 10
                    tooltip: qsTr("展开全部目录")
                    onClicked: root.expandTree()
                }

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: root.collapsed ? "chevron-down" : "chevron-up"
                    size: 11
                    iconColor: Theme.textSecondary
                }
            }
        }

        // ── Split area: file tree │ divider │ diff ─────
        // Manual geometry instead of nested Layouts: the divider dictates
        // both halves, and a Layout would fight the ratio for control.
        // The geometry is axis-aware: in the stacked layout the tree takes
        // the top, in the side-by-side layout it takes the left.
        Item {
            id: splitArea
            Layout.fillWidth: true
            Layout.fillHeight: root.listMaxHeight <= 0
            Layout.preferredHeight: root.listMaxHeight > 0
                ? root.boundedAreaHeight : 0
            Layout.minimumHeight: root.listMaxHeight > 0
                ? root.boundedAreaHeight : 0
            implicitHeight: root.listMaxHeight > 0
                ? root.boundedAreaHeight : 0
            visible: root.listVisible
            clip: true

            ChangeTree {
                id: changeTree
                x: 0
                y: 0
                width: root.treeWidth
                height: root.treeHeight
                items: root.checkpoints
                basePath: root.projectRoot
                actionsEnabled: !AgentVM.stateBusy
                actionFor: function(entry) { return root.rowAction(entry) }
                fileColorFor: function(entry) { return root.statusColor(entry) }
                selectedId: root.hasSelection
                    ? root.selectedEntry.id : null
                onFileActivated: (item) => root.openDetail(item)
                onFileActionRequested: (item) => root.requestUndo(item)
            }

            SplitBar {
                id: splitBar
                objectName: "reviewSplitBar"
                // "horizontal" is a vertical seam: the tree sits left of
                // the diff and the divider resizes the column to its right.
                orientation: root.sideBySide ? "horizontal" : "vertical"
                x: root.sideBySide ? root.treeExtent : 0
                y: root.sideBySide ? 0 : root.treeExtent
                width: root.sideBySide
                    ? root.splitterThickness : splitArea.width
                height: root.sideBySide
                    ? splitArea.height : root.splitterThickness
                onDragged: (delta) => root.adjustSplit(delta)
                onResetRequested: root.resetSplit()
            }

            Item {
                id: detailSlot
                x: root.sideBySide
                    ? root.treeExtent + root.splitterThickness : 0
                y: root.sideBySide
                    ? 0 : root.treeExtent + root.splitterThickness
                width: Math.max(0, splitArea.width - x)
                height: Math.max(0, splitArea.height - y)

                // The pane stays in the layout even with no file open: an
                // invisible subtree is never polished, so its diff list
                // would only learn its size - and create its rows - one
                // frame after the click. The pane shows its own hint while
                // nothing is selected.
                FileChangeDetailDialog {
                    id: detailPane
                    anchors.fill: parent
                    checkpoints: root.checkpoints
                    detailFor: root.detailFor
                    projectRoot: root.projectRoot
                    // The hint points at the file list, which sits above the
                    // diff in the stacked layout and beside it in the wide
                    // one.
                    selectHint: root.sideBySide
                        ? qsTr("在左侧选择一个文件查看差异")
                        : qsTr("在上方选择一个文件查看差异")
                    onEntrySelected: (entry) => root.selectedEntry = entry
                    onUndoRequested: (entry) => root.requestUndo(entry)
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.statusWarning
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: undoDialog

        property var pending: null

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        modal: true
        closePolicy: Popup.CloseOnEscape

        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("撤销文件改动？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            Text {
                Layout.fillWidth: true
                text: undoDialog.pending
                    ? (undoDialog.pending.existed
                        ? qsTr("将把以下文件恢复为本次会话首次改动前的内容：\n%1\n此后对该文件的手工修改也会被覆盖。")
                            .arg(undoDialog.pending.path)
                        : qsTr("将删除模型在本会话新建的文件：\n%1")
                            .arg(undoDialog.pending.path))
                      + (root.activeCount(undoDialog.pending) > 1
                          ? "\n" + qsTr("该文件在本会话共有 %1 次改动，本次将全部撤销。")
                                .arg(root.activeCount(undoDialog.pending))
                          : "")
                    : ""
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WrapAnywhere
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                AppButton {
                    text: qsTr("取消")
                    onClicked: undoDialog.close()
                }
                AppButton {
                    text: qsTr("撤销")
                    textColor: Theme.statusWarning
                    highlighted: true
                    onClicked: {
                        const entry = undoDialog.pending
                        undoDialog.close()
                        if (entry) root.performUndo(entry)
                    }
                }
            }
        }
    }
}
