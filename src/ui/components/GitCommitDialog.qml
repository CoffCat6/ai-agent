import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Read-only detail for one repository commit: metadata, the changed-file tree
// and the selected file's diff. Files and diff are fetched asynchronously via
// GitVM request ids; merge commits are shown against their first parent by the
// service. The dialog inherits the active project root from GitVM.
Dialog {
    id: dialog

    property var commitData: null
    property string sha: ""
    property var files: []
    property bool filesLoading: false
    property string filesError: ""
    property string selectedPath: ""
    property string commitDiff: ""
    property bool commitDiffTruncated: false
    property bool commitDiffLoading: false
    property string commitDiffError: ""
    property string filesToken: ""
    property string diffToken: ""
    property string viewMode: "unified"

    // ── Inline split (file tree ↔ diff) ────────────────────
    // Same mechanism as FileChangesPanel: the divider position is stored as
    // a share of the split area instead of a pixel count, so the proportion
    // survives a dialog resize. The share is clamped so neither pane can be
    // dragged out of existence.
    property real listRatio: dialog.defaultListRatio
    readonly property int splitterThickness: 7
    readonly property int minTreeWidth: 140
    readonly property int minDetailWidth: 320
    // Width of the split row: left pane + divider + right pane.
    readonly property real splitExtent: splitRow.width
    // Default share of the split area the tree takes: a path column plus a
    // little air, capped so the diff always keeps at least half.
    readonly property real defaultListRatio: dialog.computeDefaultRatio()
    readonly property real maxTreeWidth: Math.max(dialog.minTreeWidth,
        dialog.splitExtent - dialog.splitterThickness - dialog.minDetailWidth)
    readonly property real treePaneWidth: Math.max(dialog.minTreeWidth,
        Math.min(dialog.maxTreeWidth,
                 Math.round(dialog.splitExtent * dialog.listRatio)))

    // Animate ratio changes (resize, reset) unless the user is dragging the
    // divider: then the pane has to track the cursor exactly.
    Behavior on listRatio {
        enabled: !commitSplitBar.dragActive
        NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutQuad }
    }

    readonly property string shortSha: commitData
        ? (commitData.shortSha || sha.substring(0, 7)) : ""
    readonly property string subject: commitData ? (commitData.subject || "") : ""
    readonly property string body: commitData ? (commitData.body || "") : ""
    readonly property string author: commitData ? (commitData.author || "") : ""
    readonly property real timestamp: commitData ? Number(commitData.timestamp || 0) : 0
    readonly property int fileCount: files ? files.length : 0
    readonly property int diffLineCount: commitDiffView.renderedCount
    readonly property var selectedFile: findFile(selectedPath)
    readonly property int selectedAdded:
        selectedFile ? Number(selectedFile.additions || 0) : 0
    readonly property int selectedRemoved:
        selectedFile ? Number(selectedFile.removals || 0) : 0

    // Default share of the split area the tree takes. The tree is a path
    // list and wants a roughly constant width (~252px including the
    // divider), not a share of the host - the rest is the diff's. The
    // default is deliberately narrower than the old fixed 280px column,
    // which is what prompted the resizable split.
    function computeDefaultRatio() {
        const extent = dialog.splitExtent
        if (extent <= 0) return 0.24
        return Math.max(0.16, Math.min(0.45, (240 + 12) / extent))
    }

    // Dragging the divider: the delta arrives in pixels but the split is
    // stored as a ratio. The ratio is capped by what the tree can actually
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

    function findFile(path) {
        for (let i = 0; i < files.length; ++i) {
            if (files[i].path === path) return files[i]
        }
        return null
    }

    // ChangeTree rows need the checkpoint-style stat names; git reports
    // additions/removals and a full status such as "R100".
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

    function selectFile(file) {
        if (!file || !file.path) return
        selectedPath = file.path
        commitDiff = ""
        commitDiffTruncated = false
        commitDiffError = ""
        commitDiffLoading = true
        diffToken = GitVM.requestCommitDiff(sha, file.path)
    }

    function openFor(commitMap) {
        commitData = commitMap || {}
        sha = commitData.sha || ""
        files = []
        filesError = ""
        selectedPath = ""
        commitDiff = ""
        commitDiffTruncated = false
        commitDiffError = ""
        diffToken = ""
        viewMode = "unified"
        if (sha.length === 0) {
            filesLoading = false
            filesError = qsTr("缺少提交标识，无法读取该提交。")
        } else {
            filesLoading = true
            filesToken = GitVM.requestCommitFiles(sha)
        }
        open()
    }

    function reloadDiff() {
        if (!selectedFile) return
        selectFile(selectedFile)
    }

    Connections {
        target: GitVM

        function onCommitFilesReady(requestId, sha, files, error) {
            if (requestId !== dialog.filesToken) return
            dialog.files = files
            dialog.filesError = error
            dialog.filesLoading = false
            if (error.length === 0 && files.length > 0
                    && dialog.selectedPath.length === 0)
                dialog.selectFile(files[0])
        }

        function onCommitDiffReady(requestId, sha, path, diff, truncated, error) {
            if (requestId !== dialog.diffToken) return
            dialog.commitDiff = diff
            dialog.commitDiffTruncated = truncated
            dialog.commitDiffError = error
            dialog.commitDiffLoading = false
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1100, parent ? parent.width - 48 : 1100)
    height: Math.min(760, parent ? parent.height - 80 : 760)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            spacing: 8

            AppIcon {
                icon: "branch"
                size: 14
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: dialog.subject
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                elide: Text.ElideRight
                Accessible.name: text
            }

            Text {
                text: dialog.shortSha
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeMicro
            }

            AppButton {
                visible: dialog.commitDiff.length > 0
                text: qsTr("统一")
                implicitHeight: 24
                highlighted: dialog.viewMode === "unified"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "unified"
            }

            AppButton {
                visible: dialog.commitDiff.length > 0
                text: qsTr("并排")
                implicitHeight: 24
                highlighted: dialog.viewMode === "split"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "split"
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭提交详情")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: dialog.author + " · " + Qt.formatDateTime(
                          new Date(dialog.timestamp * 1000), "yyyy-MM-dd HH:mm")
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeMicro
                elide: Text.ElideRight
            }

            Text {
                visible: dialog.body.length > 0
                Layout.maximumWidth: dialog.width * 0.55
                text: dialog.body
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeMicro
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.Wrap
                Accessible.name: qsTr("提交说明")
            }
        }

        RowLayout {
            id: splitRow
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
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
                    text: qsTr("改动文件 · %1").arg(dialog.fileCount)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.filesLoading
                    text: qsTr("正在读取提交文件…")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }

                Text {
                    Layout.fillWidth: true
                    visible: !dialog.filesLoading && dialog.filesError.length > 0
                    text: dialog.filesError
                    color: Theme.statusWarning
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: !dialog.filesLoading && dialog.filesError.length === 0
                             && dialog.fileCount === 0
                    text: qsTr("该提交没有文件改动。")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }

                ChangeTree {
                    objectName: "commitFileTree"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: dialog.fileCount > 0
                    items: dialog.fileItems()
                    basePath: ""
                    autoBasePath: false
                    rowHeight: 24
                    onFileActivated: (item) => dialog.selectFile(item)
                }
            }

            // "horizontal" is a vertical seam: the strip resizes the tree
            // column on its left and the diff column on its right.
            SplitBar {
                id: commitSplitBar
                objectName: "commitSplitBar"
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: dialog.selectedPath
                        color: Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        elide: Text.ElideMiddle
                    }

                    Text {
                        visible: dialog.selectedAdded > 0
                        text: "+" + dialog.selectedAdded
                        color: Theme.statusSuccess
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMicro
                    }

                    Text {
                        visible: dialog.selectedRemoved > 0
                        text: "−" + dialog.selectedRemoved
                        color: Theme.statusDanger
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMicro
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.commitDiffLoading
                    text: qsTr("正在读取提交差异…")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }

                Text {
                    Layout.fillWidth: true
                    visible: !dialog.commitDiffLoading
                             && dialog.commitDiffError.length > 0
                    text: dialog.commitDiffError
                    color: Theme.statusWarning
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                DiffView {
                    id: commitDiffView
                    objectName: "commitDetailDiffView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !dialog.commitDiffLoading
                             && dialog.commitDiffError.length === 0
                             && dialog.fileCount > 0
                    diffText: dialog.commitDiff
                    diffTruncated: dialog.commitDiffTruncated
                    viewMode: dialog.viewMode
                    emptyText: qsTr("该文件在此提交中没有可显示的差异。")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("关闭")
                onClicked: dialog.close()
            }
        }
    }
}