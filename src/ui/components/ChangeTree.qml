import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels
import "FileTree.js" as FileTree

// Flattened directory tree used by the review panel, the Git change lists and
// the commit file view. Rows are rebuilt from `items` + `collapsedPaths`, so
// callers only keep data and collapse state; a ListView virtualizes the
// delegates. File rows keep the source item's fields (path, undone,
// truncated, addedLines, ...) and expose it again as `row.item` /
// `modelData.item`.
Item {
    id: root

    property var items: []
    // Prefix stripped from displayed paths; empty falls back to the longest
    // directory shared by every item.
    property string basePath: ""
    // Git status paths are already repository-relative: keep them verbatim
    // instead of falling back to the longest common directory.
    property bool autoBasePath: true
    property var collapsedPaths: ({})
    property int rowHeight: 26
    property bool showFolderRows: true
    // Optional hooks set by the host:
    //   actionFor(item) -> {text, tooltip, enabled, visible} | null
    //   fileColorFor(item) -> color
    //   statusColorFor(item) -> color (used by the statusLabel letter)
    property var actionFor: null
    property var fileColorFor: null
    property var statusColorFor: null
    // Host-level gate (e.g. agent busy) so action buttons re-evaluate when it
    // flips without rebuilding the tree.
    property bool actionsEnabled: true
    // Optional selection marker: the host highlights the file row whose
    // entry id matches. The review panel uses it to mark the file whose
    // diff is open in the inline detail pane below the tree; without it a
    // two-pane layout leaves the user guessing which file is being shown.
    // null / undefined = nothing selected (the default for every other
    // host: Git status lists, commit file lists).
    property var selectedId: null

    readonly property var rows: FileTree.buildRows(items, {
        "basePath": effectiveBasePath(),
        "collapsed": collapsedPaths,
        "folders": showFolderRows
    })
    readonly property int rowCount: rows.length
    readonly property int fileRowCount: countKind(rows, "file")
    readonly property int folderRowCount: countKind(rows, "folder")
    readonly property int contentHeight: rowCount * rowHeight
    readonly property alias renderedCount: rowList.count

    signal fileActivated(var item, int index)
    signal fileActionRequested(var item)

    function effectiveBasePath() {
        return (root.basePath.length > 0 || !root.autoBasePath)
            ? root.basePath : commonBase()
    }

    function commonBase() {
        const paths = []
        for (let i = 0; i < items.length; ++i) {
            if (items[i] && items[i].path) paths.push(items[i].path)
        }
        return FileTree.commonDir(paths)
    }

    function countKind(rowsIn, kind) {
        let n = 0
        for (let i = 0; i < rowsIn.length; ++i) {
            if (rowsIn[i].kind === kind) n += 1
        }
        return n
    }

    function isCollapsed(path) {
        return collapsedPaths[path] === true
    }

    function toggleFolder(path) {
        const next = {}
        for (const key in collapsedPaths) next[key] = collapsedPaths[key]
        if (next[path] === true) delete next[path]
        else next[path] = true
        collapsedPaths = next
    }

    function setFolderCollapsed(path, value) {
        if (isCollapsed(path) === value) return
        toggleFolder(path)
    }

    function expandAll() {
        collapsedPaths = ({})
    }

    function collapseAll() {
        const paths = FileTree.allFolderPaths(items, effectiveBasePath())
        const next = {}
        for (let i = 0; i < paths.length; ++i) next[paths[i]] = true
        collapsedPaths = next
    }

    function actionForItem(item) {
        return root.actionFor ? root.actionFor(item) : null
    }

    function colorForItem(item) {
        return root.fileColorFor ? root.fileColorFor(item) : Theme.brandPrimary
    }

    function statusColorForItem(item) {
        if (root.statusColorFor) return root.statusColorFor(item)
        // Git letter fallback: added/deleted/renamed/untracked/modified.
        const label = String(item.statusLabel || "")
        if (label === "A") return Theme.statusSuccess
        if (label === "D" || label === "U") return Theme.statusDanger
        if (label === "R" || label === "C") return Theme.brandPrimary
        if (label === "?") return Theme.textSecondary
        return Theme.statusWarning
    }

    ListView {
        id: rowList
        objectName: "changeTree"
        anchors.fill: parent
        clip: true
        model: root.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        delegate: Item {
            id: rowItem
            required property var modelData
            required property int index

            readonly property bool isFolder: modelData.kind === "folder"
            readonly property var entry: modelData.item !== undefined
                                         ? modelData.item : modelData
            readonly property bool truncated: modelData.truncated === true
            readonly property bool undone: modelData.undone === true
            // Review rows: the user accepted this change (kept, no undo).
            readonly property bool accepted: modelData.accepted === true
            readonly property var rowAction: root.actionForItem(rowItem.entry)
            // Review entries carry activeCount (remaining writes); status
            // entries only changeCount.
            readonly property int badgeCount: modelData.activeCount !== undefined
                ? modelData.activeCount : (modelData.changeCount || 0)
            // Selected row: the host marks the file shown in its detail
            // pane. Only file rows can be selected.
            readonly property bool selected: !rowItem.isFolder
                && root.selectedId !== null
                && root.selectedId !== undefined
                && rowItem.entry !== null
                && rowItem.entry !== undefined
                && rowItem.entry.id !== undefined
                && rowItem.entry.id === root.selectedId

            objectName: isFolder ? "fileTreeFolderRow" : "fileChangeRow"
            width: ListView.view ? ListView.view.width : 0
            height: root.rowHeight

            Rectangle {
                anchors.fill: parent
                radius: 5
                color: rowItem.selected
                    ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                              Theme.brandPrimary.b, Theme.isDark ? 0.20 : 0.12)
                    : (rowHover.hovered ? Theme.surfaceHover : "transparent")
            }

            // Accent stripe on the tree's left edge: the selection must stay
            // visible even when the row is only half covered by the scroll
            // area's rounded corners.
            Rectangle {
                visible: rowItem.selected
                x: 0
                y: 3
                width: 2
                height: Math.max(0, parent.height - 6)
                radius: 1
                color: Theme.brandPrimary
            }

            HoverHandler { id: rowHover }

            // Below the content row: folder toggling / file activation. The
            // per-row action button consumes its own presses.
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (rowItem.isFolder)
                        root.toggleFolder(rowItem.modelData.path)
                    else
                        root.fileActivated(rowItem.entry, rowItem.index)
                }
                Accessible.name: rowItem.isFolder
                    ? qsTr("折叠或展开目录 %1").arg(rowItem.modelData.name)
                    : rowItem.modelData.path

                ToolTip.visible: containsMouse && !rowItem.isFolder
                ToolTip.delay: 500
                ToolTip.text: rowItem.modelData.path
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6 + rowItem.modelData.depth * 12
                anchors.rightMargin: 4
                spacing: 6

                // ── Folder row ─────────────────────────
                AppIcon {
                    visible: rowItem.isFolder
                    Layout.alignment: Qt.AlignVCenter
                    icon: rowItem.modelData.collapsed === true
                        ? "chevron-right" : "chevron-down"
                    size: 10
                    iconColor: Theme.textSecondary
                }

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: rowItem.isFolder ? "folder" : "file"
                    size: 13
                    iconColor: rowItem.isFolder
                        ? (rowHover.hovered ? Theme.brandPrimary
                                            : Theme.textSecondary)
                        : root.colorForItem(rowItem.entry)
                }

                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: rowItem.modelData.name
                    color: rowItem.isFolder
                        ? Theme.textPrimary
                        : (rowItem.undone ? Theme.textSecondary
                                          : Theme.textPrimary)
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                    font.bold: rowItem.isFolder
                    elide: Text.ElideMiddle
                    Accessible.name: text
                }

                // Git status letter (M/A/D/R/?/U); review rows have no label.
                Text {
                    visible: !rowItem.isFolder
                             && String(rowItem.modelData.statusLabel || "").length > 0
                    Layout.alignment: Qt.AlignVCenter
                    text: visible ? rowItem.modelData.statusLabel : ""
                    color: root.statusColorForItem(rowItem.entry)
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeNano
                    font.bold: true
                }

                // Folder file count
                Text {
                    visible: rowItem.isFolder && rowItem.modelData.fileCount > 0
                    Layout.alignment: Qt.AlignVCenter
                    text: rowItem.isFolder
                        ? String(rowItem.modelData.fileCount || 0) : ""
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeNano
                    font.family: Theme.fontMono
                }

                // Path-only checkpoints cannot be undone or diffed.
                AppIcon {
                    visible: !rowItem.isFolder && rowItem.truncated
                    Layout.alignment: Qt.AlignVCenter
                    icon: "warning"
                    size: 11
                    iconColor: Theme.statusWarning

                    ToolTip.visible: truncHover.hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("原文件过大，仅记录了路径，无法展示内容差异或撤销")

                    HoverHandler { id: truncHover }
                }

                Text {
                    objectName: "fileChangeCountBadge"
                    visible: !rowItem.isFolder && rowItem.badgeCount > 1
                    Layout.alignment: Qt.AlignVCenter
                    text: visible ? "×" + rowItem.badgeCount : ""
                    color: Theme.textSecondary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeMicro
                }

                Text {
                    visible: !rowItem.isFolder
                             && (rowItem.modelData.addedLines || 0) > 0
                    Layout.alignment: Qt.AlignVCenter
                    text: "+" + rowItem.modelData.addedLines
                    color: Theme.statusSuccess
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeMicro
                }

                Text {
                    visible: !rowItem.isFolder
                             && (rowItem.modelData.removedLines || 0) > 0
                    Layout.alignment: Qt.AlignVCenter
                    text: "−" + rowItem.modelData.removedLines
                    color: Theme.statusDanger
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeMicro
                }

                // Settled states share one slot: a row is either still
                // pending, accepted or undone, never two of them at once.
                Text {
                    visible: !rowItem.isFolder && (rowItem.undone || rowItem.accepted)
                    Layout.alignment: Qt.AlignVCenter
                    text: rowItem.accepted ? qsTr("已确认") : qsTr("已撤销")
                    color: rowItem.accepted ? Theme.statusSuccess
                                            : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }

                IconButton {
                    objectName: "fileRowAction"
                    visible: !rowItem.isFolder && rowItem.rowAction !== null
                             && rowItem.rowAction !== undefined
                             && rowItem.rowAction.visible !== false
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    radius: 4
                    icon: rowItem.rowAction && rowItem.rowAction.icon
                        ? rowItem.rowAction.icon : "undo"
                    iconSize: 11
                    enabled: root.actionsEnabled
                             && rowItem.rowAction !== null
                             && rowItem.rowAction.enabled !== false
                    opacity: enabled ? 1.0 : 0.35
                    tooltip: rowItem.rowAction ? (rowItem.rowAction.tooltip || "")
                                               : ""
                    onClicked: root.fileActionRequested(rowItem.entry)
                }
            }
        }
    }
}