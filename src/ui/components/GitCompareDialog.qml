import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Read-only comparison between any two commits: which files differ and, for the
// selected file, the A→B diff. Both revs are hex SHAs validated by GitService
// before git sees them, so nothing here can be steered by UI text. Swapping
// reverses the direction (A→B becomes B→A) without a new query shape.
Dialog {
    id: dialog

    property var commitA: null
    property var commitB: null
    property string shaA: ""
    property string shaB: ""
    property var files: []
    property bool filesLoading: false
    property string filesError: ""
    property string selectedPath: ""
    property string diffText: ""
    property bool diffTruncated: false
    property bool diffLoading: false
    property string diffError: ""
    property string filesToken: ""
    property string diffToken: ""
    property string viewMode: "unified"

    // ── Inline split (file list ↔ diff) ────────────────────
    // Same mechanism as FileChangesPanel: the divider position is stored as
    // a share of the split area instead of a pixel count, so a resized
    // dialog keeps the proportion. treePaneWidth stays as a read-only
    // derived size so existing geometry references keep working.
    property real listRatio: dialog.defaultListRatio
    readonly property int splitterThickness: 7
    readonly property int minTreeWidth: 140
    readonly property int minDetailWidth: 320
    // Width of the split row: left pane + divider + right pane.
    readonly property real splitExtent: splitRow.width
    // Default share of the split area the file list takes (a 260px column
    // plus the divider), capped so the diff always keeps at least half.
    readonly property real defaultListRatio: dialog.computeDefaultRatio()
    readonly property real maxTreeWidth: Math.max(dialog.minTreeWidth,
        dialog.splitExtent - dialog.splitterThickness - dialog.minDetailWidth)
    readonly property real treePaneWidth: Math.max(dialog.minTreeWidth,
        Math.min(dialog.maxTreeWidth,
                 Math.round(dialog.splitExtent * dialog.listRatio)))

    // Animate ratio changes (resize, reset) unless the user is dragging the
    // divider: then the pane has to track the cursor exactly.
    Behavior on listRatio {
        enabled: !compareSplitBar.dragActive
        NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutQuad }
    }

    readonly property string labelA: shortOf(commitA, shaA)
    readonly property string labelB: shortOf(commitB, shaB)
    readonly property int fileCount: files ? files.length : 0
    readonly property int diffLineCount: compareDiffView.renderedCount

    // Default share of the split area the file list takes. The list is a
    // path list and wants a roughly constant width (~252px including the
    // divider), not a share of the host - the rest is the diff's.
    function computeDefaultRatio() {
        const extent = dialog.splitExtent
        if (extent <= 0) return 0.24
        return Math.max(0.16, Math.min(0.45, (240 + 12) / extent))
    }

    // Dragging the divider: the delta arrives in pixels but the split is
    // stored as a ratio. The ratio is capped by what the list can actually
    // take, otherwise the divider would keep "eating" pixels that go
    // nowhere.
    function adjustSplit(delta) {
        const extent = dialog.splitExtent
        if (extent <= 0) return
        const maxShare = dialog.maxTreeWidth / extent
        dialog.listRatio = Math.max(0.08,
            Math.min(dialog.listRatio + delta / extent, maxShare))
    }

    // Double-click on the divider restores the default proportion.
    function resetSplit() {
        dialog.listRatio = dialog.defaultListRatio
    }

    function shortOf(commit, sha) {
        if (commit && commit.shortSha) return commit.shortSha
        return sha.length > 7 ? sha.substring(0, 7) : sha
    }

    function subjectOf(commit) {
        return commit && commit.subject ? String(commit.subject) : ""
    }

    function fileItems() {
        const rows = []
        for (let i = 0; i < files.length; ++i) {
            const file = files[i]
            const row = {}
            for (const key in file)
                row[key] = file[key]
            row.statusLabel = String(file.status || "").charAt(0)
            row.addedLines = Number(file.additions || 0)
            row.removedLines = Number(file.removals || 0)
            rows.push(row)
        }
        return rows
    }

    function reloadFiles() {
        if (shaA.length === 0 || shaB.length === 0) return
        files = []
        filesError = ""
        filesLoading = true
        filesToken = GitVM.requestCompareFiles(shaA, shaB)
    }

    function reloadDiff() {
        if (shaA.length === 0 || shaB.length === 0) return
        diffText = ""
        diffError = ""
        diffToken = ""
        if (selectedPath.length === 0) return
        diffLoading = true
        diffToken = GitVM.requestCompareDiff(shaA, shaB, selectedPath)
    }

    function selectFile(path) {
        if (selectedPath === path) return
        selectedPath = path
        reloadDiff()
    }

    function swap() {
        const oldA = commitA
        const oldShaA = shaA
        commitA = commitB
        shaA = shaB
        commitB = oldA
        shaB = oldShaA
        reloadFiles()
    }

    function openFor(a, b) {
        if (!a || !b || !a.sha || !b.sha || a.sha === b.sha) return
        commitA = a
        commitB = b
        shaA = a.sha
        shaB = b.sha
        selectedPath = ""
        viewMode = "unified"
        reloadFiles()
        open()
    }

    Connections {
        target: GitVM

        function onCompareFilesReady(requestId, reqShaA, reqShaB, files, error) {
            if (requestId !== dialog.filesToken) return
            dialog.files = files
            dialog.filesError = error
            dialog.filesLoading = false
            // Auto-pick the first file so the diff pane is never an empty box.
            if (error.length === 0 && dialog.selectedPath.length === 0
                    && dialog.fileCount > 0) {
                dialog.selectFile(String(files[0].path || ""))
            }
        }

        function onCompareDiffReady(requestId, reqShaA, reqShaB, path, diff,
                                   truncated, error) {
            if (requestId !== dialog.diffToken) return
            dialog.diffText = diff
            dialog.diffTruncated = truncated
            dialog.diffError = error
            dialog.diffLoading = false
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1120, parent ? parent.width - 48 : 1120)
    height: Math.min(720, parent ? parent.height - 80 : 720)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 8

        // ── A / B header ───────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            spacing: 8

            Text {
                text: qsTr("对比")
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
            }

            Text {
                objectName: "compareLabelA"
                text: "A " + dialog.labelA + "  " + dialog.subjectOf(dialog.commitA)
                color: Theme.statusWarning
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideRight
                Layout.maximumWidth: 330
            }

            Text {
                text: "→"
                color: Theme.textTertiary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
            }

            Text {
                objectName: "compareLabelB"
                text: "B " + dialog.labelB + "  " + dialog.subjectOf(dialog.commitB)
                color: Theme.statusSuccess
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideRight
                Layout.maximumWidth: 330
            }

            Item { Layout.fillWidth: true }

            AppButton {
                objectName: "compareSwapButton"
                text: qsTr("交换方向")
                implicitHeight: 24
                onClicked: dialog.swap()
            }

            IconButton {
                icon: "refresh"
                enabled: !dialog.filesLoading
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("重新读取对比结果")
                onClicked: dialog.reloadFiles()
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭对比视图")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: dialog.filesLoading
                    ? qsTr("正在读取差异文件…")
                    : qsTr("差异文件 · %1").arg(dialog.fileCount)
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeMicro
            }

            AppButton {
                visible: dialog.diffText.length > 0
                text: qsTr("统一")
                implicitHeight: 22
                highlighted: dialog.viewMode === "unified"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "unified"
            }

            AppButton {
                visible: dialog.diffText.length > 0
                text: qsTr("并排")
                implicitHeight: 22
                highlighted: dialog.viewMode === "split"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "split"
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.filesError.length > 0
            text: dialog.filesError
            color: Theme.statusWarning
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.Wrap
        }

        RowLayout {
            id: splitRow
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            spacing: 10

            ColumnLayout {
                Layout.preferredWidth: dialog.treePaneWidth
                Layout.maximumWidth: dialog.treePaneWidth
                // The column must be free to shrink below its content's
                // implicit width, otherwise the drag clamp would be ignored.
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    visible: dialog.fileCount > 0
                    text: qsTr("A 到 B 之间变化的文件")
                    color: Theme.textTertiary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeNano
                    elide: Text.ElideRight
                }

                ChangeTree {
                    objectName: "compareFileTree"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: dialog.fileCount > 0
                    items: dialog.fileItems()
                    basePath: ""
                    autoBasePath: false
                    rowHeight: 24
                    onFileActivated: (item) => dialog.selectFile(String(item.path || ""))
                }

                Text {
                    Layout.fillWidth: true
                    visible: !dialog.filesLoading && dialog.fileCount === 0
                             && dialog.filesError.length === 0
                    text: qsTr("两个提交之间没有文件差异。")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                    wrapMode: Text.Wrap
                }
            }

            // "horizontal" is a vertical seam: the strip resizes the file
            // list column on its left and the diff column on its right.
            SplitBar {
                id: compareSplitBar
                objectName: "compareSplitBar"
                orientation: "horizontal"
                Layout.fillHeight: true
                Layout.preferredWidth: implicitWidth
                onDragged: (delta) => dialog.adjustSplit(delta)
                onResetRequested: dialog.resetSplit()
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    visible: dialog.selectedPath.length > 0
                    text: dialog.selectedPath
                    color: Theme.textPrimary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeCaption
                    elide: Text.ElideMiddle
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.diffLoading
                    text: qsTr("正在读取差异…")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }

                Text {
                    Layout.fillWidth: true
                    visible: !dialog.diffLoading && dialog.diffError.length > 0
                    text: dialog.diffError
                    color: Theme.statusWarning
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                DiffView {
                    id: compareDiffView
                    objectName: "compareDiffView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !dialog.diffLoading && dialog.diffError.length === 0
                    diffText: dialog.diffText
                    diffTruncated: dialog.diffTruncated
                    viewMode: dialog.viewMode
                    emptyText: dialog.selectedPath.length > 0
                        ? qsTr("该文件在这两个提交之间没有差异。")
                        : qsTr("选择一个文件查看差异。")
                }
            }
        }
    }
}
