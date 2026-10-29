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
    // The file list column is capped: an uncapped fillWidth column would stretch
    // and leave the diff 3px wide (measured on the review dialog).
    property real treePaneWidth: 260

    readonly property string labelA: shortOf(commitA, shaA)
    readonly property string labelB: shortOf(commitB, shaB)
    readonly property int fileCount: files ? files.length : 0
    readonly property int diffLineCount: compareDiffView.renderedCount

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
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            spacing: 10

            ColumnLayout {
                Layout.preferredWidth: dialog.treePaneWidth
                Layout.maximumWidth: dialog.treePaneWidth
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
