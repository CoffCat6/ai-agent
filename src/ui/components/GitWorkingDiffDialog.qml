import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Working-tree diff for one path, opened from the Git panel's staged/unstaged
// trees. `staged` selects index-vs-HEAD, otherwise the worktree-vs-index diff;
// untracked files are shown as all-additions against an empty file. GitVM
// carries the active project root. The header also holds the file-level
// actions (open, copy path, history) and the discard entry point, which the
// panel turns into a confirmation.
Dialog {
    id: dialog

    property string path: ""
    property string displayPath: ""
    property bool staged: false
    property bool untracked: false
    property string viewMode: "unified"
    property string diffText: ""
    property bool diffTruncated: false
    property bool loading: false
    property string errorText: ""
    property string requestToken: ""
    property string actionNotice: ""

    // Partial staging: one row per hunk of the diff shown below.
    property var hunks: []
    property string hunkToken: ""
    property string hunkError: ""
    property bool hunkLoading: false
    property string writeToken: ""
    property bool hunkSectionOpen: true

    readonly property bool hasDiff: diffText.length > 0
    readonly property int diffLineCount: diffView.renderedCount
    readonly property int hunkCount: hunks.length
    readonly property string scopeLabel: untracked ? qsTr("未跟踪")
        : (staged ? qsTr("已暂存") : qsTr("未暂存"))
    // Untracked and whole-file-only cases are refused by GitService; the row
    // buttons would just fail, so the section only offers actions it can do.
    readonly property bool canPatchHunks: !untracked && hunkCount > 0

    signal historyRequested(string path)
    signal blameRequested(string path)
    signal discardRequested(string path, bool untracked)

    function emptyText() {
        if (untracked)
            return qsTr("未跟踪文件没有可展示的内容。")
        if (staged)
            return qsTr("没有已暂存的差异。")
        return qsTr("没有未暂存的改动。")
    }

    function reload() {
        diffText = ""
        diffTruncated = false
        errorText = ""
        requestToken = ""
        loading = true
        requestToken = GitVM.requestWorkingDiff(path, staged)
        reloadHunks()
    }

    function reloadHunks() {
        hunks = []
        hunkError = ""
        hunkToken = ""
        if (untracked) return
        hunkLoading = true
        hunkToken = GitVM.requestDiffHunks(path, staged)
        if (hunkToken.length === 0) hunkLoading = false
    }

    function stageHunk(hunk) {
        if (GitVM.operationBusy || !hunk) return
        // Echo the row back: the service re-reads the diff and refuses if this
        // header is no longer at that position.
        const selected = [{ "index": hunk.index, "header": hunk.header }]
        actionNotice = ""
        writeToken = staged ? GitVM.unstageHunks(path, selected)
                            : GitVM.stageHunks(path, selected)
    }

    function openFor(path, staged, untracked, displayPath) {
        dialog.path = path
        dialog.displayPath = displayPath && displayPath.length > 0
            ? displayPath : path
        dialog.staged = staged === true
        dialog.untracked = untracked === true
        dialog.viewMode = "unified"
        dialog.actionNotice = ""
        dialog.writeToken = ""
        dialog.hunkSectionOpen = true
        reload()
        open()
    }

    function copyPath() {
        const absolute = GitVM.absolutePath(path)
        if (absolute.length === 0) {
            dialog.actionNotice = qsTr("无法解析该文件的绝对路径。")
            return
        }
        AgentVM.copyToClipboard(absolute)
        dialog.actionNotice = qsTr("已复制路径")
    }

    function openInApp() {
        if (GitVM.openInDefaultApp(path)) {
            dialog.actionNotice = qsTr("已用系统默认程序打开")
            return
        }
        dialog.actionNotice = qsTr("文件不存在，或系统没有可用的打开方式。")
    }

    Connections {
        target: GitVM

        function onWorkingDiffReady(requestId, path, staged, diff, truncated, error) {
            if (requestId !== dialog.requestToken) return
            dialog.diffText = diff
            dialog.diffTruncated = truncated
            dialog.errorText = error
            dialog.loading = false
        }

        function onDiffHunksReady(requestId, hunkPath, hunkStaged, hunks, error) {
            if (requestId !== dialog.hunkToken) return
            dialog.hunks = hunks
            dialog.hunkError = error
            dialog.hunkLoading = false
        }

        // Only this dialog's own writes: the request id ties the refresh to the
        // click, so a stage from the panel cannot bounce off here.
        function onOperationFinished(requestId, action, success, error) {
            if (requestId !== dialog.writeToken) return
            dialog.writeToken = ""
            if (!success) {
                dialog.actionNotice = error
                return
            }
            dialog.actionNotice = action === "unstageHunk"
                ? qsTr("已把该块退回未暂存") : qsTr("已暂存该块")
            dialog.reload()
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1000, parent ? parent.width - 48 : 1000)
    height: Math.min(700, parent ? parent.height - 80 : 700)
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
                icon: "file"
                size: 14
                iconColor: dialog.untracked ? Theme.textSecondary
                                            : Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: dialog.displayPath
                color: Theme.textPrimary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideMiddle
                Accessible.name: text
            }

            Text {
                text: dialog.scopeLabel
                color: dialog.untracked ? Theme.textSecondary
                    : (dialog.staged ? Theme.statusSuccess : Theme.statusWarning)
                font.family: Theme.fontSans
                font.pixelSize: 10
            }

            IconButton {
                objectName: "workingOpenButton"
                icon: "export"
                iconSize: 13
                tooltip: qsTr("用系统默认程序打开")
                onClicked: dialog.openInApp()
            }

            IconButton {
                objectName: "workingCopyButton"
                icon: "copy"
                iconSize: 13
                tooltip: qsTr("复制绝对路径")
                onClicked: dialog.copyPath()
            }

            IconButton {
                objectName: "workingHistoryButton"
                icon: "history"
                iconSize: 13
                tooltip: qsTr("查看该文件历史")
                onClicked: dialog.historyRequested(dialog.path)
            }

            IconButton {
                objectName: "workingBlameButton"
                icon: "blame"
                iconSize: 13
                visible: !dialog.untracked
                tooltip: qsTr("查看该文件逐行归属")
                onClicked: dialog.blameRequested(dialog.path)
            }

            IconButton {
                objectName: "workingDiscardButton"
                icon: "trash"
                iconSize: 13
                restIconColor: Theme.statusDanger
                hoverIconColor: Theme.statusDanger
                visible: !dialog.staged
                enabled: !dialog.loading && !GitVM.operationBusy
                opacity: enabled ? 1.0 : 0.35
                tooltip: dialog.untracked
                    ? qsTr("删除该未跟踪文件（先备份）")
                    : qsTr("丢弃该文件的工作区改动")
                onClicked: dialog.discardRequested(dialog.path, dialog.untracked)
            }

            AppButton {
                visible: dialog.hasDiff
                text: qsTr("统一")
                implicitHeight: 24
                highlighted: dialog.viewMode === "unified"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "unified"
            }

            AppButton {
                visible: dialog.hasDiff
                text: qsTr("并排")
                implicitHeight: 24
                highlighted: dialog.viewMode === "split"
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.viewMode = "split"
            }

            IconButton {
                icon: "refresh"
                enabled: !dialog.loading
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("重新读取该文件差异")
                onClicked: dialog.reload()
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭工作区差异")
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.actionNotice.length > 0
            text: dialog.actionNotice
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            spacing: 6

            Text {
                Layout.fillWidth: true
                visible: dialog.loading
                text: qsTr("正在读取工作区差异…")
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeCaption
            }

            Text {
                Layout.fillWidth: true
                visible: !dialog.loading && dialog.errorText.length > 0
                text: dialog.errorText
                color: Theme.statusWarning
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeCaption
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                visible: dialog.untracked && dialog.hasDiff
                text: qsTr("未跟踪文件：按当前内容展示全部新增行。")
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }

            // ── Partial staging ────────────────────────
            // One row per hunk of the diff below; staging a row writes only
            // that hunk to the index. Files git cannot patch are refused with
            // the reason shown here instead of a dead button.
            ColumnLayout {
                Layout.fillWidth: true
                visible: !dialog.untracked
                         && (dialog.hunkCount > 0 || dialog.hunkError.length > 0)
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: dialog.hunkCount > 0
                            ? qsTr("分块暂存 · %1 块").arg(dialog.hunkCount)
                            : qsTr("分块暂存")
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Text {
                        visible: dialog.hunkLoading
                        text: qsTr("正在解析差异块…")
                        color: Theme.textTertiary
                        font.family: Theme.fontSans
                        font.pixelSize: 9
                    }

                    AppButton {
                        objectName: "gitHunkToggle"
                        visible: dialog.hunkCount > 0
                        text: dialog.hunkSectionOpen ? qsTr("收起") : qsTr("展开")
                        implicitHeight: 20
                        buttonColor: "transparent"
                        textColor: Theme.textSecondary
                        onClicked: dialog.hunkSectionOpen = !dialog.hunkSectionOpen
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.hunkError.length > 0
                    text: dialog.hunkError
                    color: Theme.textTertiary
                    font.family: Theme.fontSans
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }

                Flickable {
                    objectName: "gitHunkList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(hunkColumn.implicitHeight, 152)
                    visible: dialog.hunkSectionOpen && dialog.hunkCount > 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentHeight: hunkColumn.implicitHeight
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: hunkColumn
                        width: parent ? parent.width : 0
                        spacing: 3

                        Repeater {
                            model: dialog.hunks

                            delegate: Rectangle {
                                id: hunkRow
                                objectName: "gitHunkRow"
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                height: 36
                                radius: 5
                                color: Theme.bgPrimary
                                border.color: Theme.borderSubtle

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 7
                                    anchors.rightMargin: 5
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Text {
                                            Layout.fillWidth: true
                                            text: hunkRow.modelData.summary
                                            color: Theme.textPrimary
                                            font.family: Theme.fontMono
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: hunkRow.modelData.header
                                                  + "  +" + hunkRow.modelData.additions
                                                  + " −" + hunkRow.modelData.removals
                                            color: Theme.textTertiary
                                            font.family: Theme.fontMono
                                            font.pixelSize: 8
                                            elide: Text.ElideMiddle
                                        }
                                    }

                                    AppButton {
                                        objectName: "gitHunkStageButton"
                                        text: dialog.staged ? qsTr("取消暂存")
                                                             : qsTr("暂存")
                                        implicitHeight: 20
                                        enabled: !GitVM.operationBusy
                                                 && dialog.writeToken.length === 0
                                        opacity: enabled ? 1.0 : 0.35
                                        onClicked: dialog.stageHunk(hunkRow.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            DiffView {
                id: diffView
                objectName: "workingDiffDialogView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !dialog.loading && dialog.errorText.length === 0
                diffText: dialog.diffText
                diffTruncated: dialog.diffTruncated
                viewMode: dialog.viewMode
                emptyText: dialog.emptyText()
            }
        }
    }
}