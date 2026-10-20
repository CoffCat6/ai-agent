import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// G4 Phase 3: details for one file checkpoint across three views:
//   0. the write-time checkpoint diff (always available)
//   1. the live working-tree diff for the same path (git only)
//   2. the file's git history + the selected commit's diff (git only)
// Git data is loaded asynchronously through GitVM; request ids drop stale
// answers when the dialog is reopened for another file.
Dialog {
    id: dialog

    property var checkpoint: null
    property string diffText: ""
    property bool diffTruncated: false

    property bool autoLoadGit: true
    property bool gitRepo: false
    property bool gitRepoKnown: false
    property int currentTab: 0

    property string workingDiff: ""
    property bool workingDiffTruncated: false
    property bool workingDiffLoading: false
    property string workingDiffError: ""

    property var history: []
    property bool historyLoading: false
    property string historyError: ""
    property string selectedSha: ""

    property string commitDiff: ""
    property bool commitDiffTruncated: false
    property bool commitDiffLoading: false
    property string commitDiffError: ""

    property string workingToken: ""
    property string historyToken: ""
    property string commitToken: ""

    readonly property bool hasDiff: diffText.length > 0
    readonly property bool undone: checkpoint ? checkpoint.undone === true : false
    readonly property bool truncated: checkpoint ? checkpoint.truncated === true : false
    readonly property int addedLines: checkpoint ? (checkpoint.addedLines || 0) : 0
    readonly property int removedLines: checkpoint ? (checkpoint.removedLines || 0) : 0
    readonly property alias diffLineCount: detailDiffView.renderedCount
    readonly property int gitHistoryRenderedCount: commitList.count
    readonly property int effectiveTab: gitRepo ? currentTab : 0

    signal gitDataRequested(string path)

    function openFor(entry, detail) {
        checkpoint = entry || null
        const d = detail || {}
        diffText = d.diff !== undefined ? d.diff : ""
        diffTruncated = d.diffTruncated === true

        currentTab = 0
        gitRepo = false
        gitRepoKnown = false
        workingDiff = ""
        workingDiffTruncated = false
        workingDiffLoading = false
        workingDiffError = ""
        history = []
        historyLoading = false
        historyError = ""
        selectedSha = ""
        commitDiff = ""
        commitDiffTruncated = false
        commitDiffLoading = false
        commitDiffError = ""
        workingToken = ""
        historyToken = ""
        commitToken = ""

        open()
        if (autoLoadGit && checkpoint && checkpoint.path)
            loadGit(checkpoint.path)
    }

    function loadGit(path) {
        gitDataRequested(path)
        GitVM.refreshRepoInfo()
        workingDiffLoading = true
        workingToken = GitVM.requestWorkingDiff(path, false)
        historyLoading = true
        historyToken = GitVM.requestFileHistory(path, 20)
    }

    function selectTab(index) {
        if (index > 0 && !gitRepo) return
        currentTab = index
        if (index === 2 && selectedSha.length === 0 && history.length > 0)
            selectCommit(history[0].sha)
    }

    function selectCommit(sha) {
        if (!sha) return
        selectedSha = sha
        commitDiff = ""
        commitDiffTruncated = false
        commitDiffError = ""
        if (!autoLoadGit) return
        commitDiffLoading = true
        commitToken = GitVM.requestCommitDiff(
            sha, checkpoint ? checkpoint.path : "")
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
        return qsTr("此记录没有保存内容差异。")
    }

    Connections {
        target: GitVM

        function onRepoInfoChanged() {
            const info = GitVM.repoInfo || {}
            dialog.gitRepoKnown = true
            dialog.gitRepo = info.isRepo === true
            if (!dialog.gitRepo) {
                dialog.currentTab = 0
                dialog.workingToken = ""
                dialog.historyToken = ""
                dialog.commitToken = ""
                dialog.workingDiffLoading = false
                dialog.historyLoading = false
                dialog.commitDiffLoading = false
            }
        }

        function onWorkingDiffReady(requestId, path, staged, diff, truncated, error) {
            if (requestId !== dialog.workingToken) return
            dialog.workingDiff = diff
            dialog.workingDiffTruncated = truncated
            dialog.workingDiffError = error
            dialog.workingDiffLoading = false
        }

        function onFileHistoryReady(requestId, path, commits, error) {
            if (requestId !== dialog.historyToken) return
            dialog.history = commits
            dialog.historyError = error
            dialog.historyLoading = false
            if (dialog.currentTab === 2 && dialog.selectedSha.length === 0
                    && commits.length > 0)
                dialog.selectCommit(commits[0].sha)
        }

        function onCommitDiffReady(requestId, sha, path, diff, truncated, error) {
            if (requestId !== dialog.commitToken) return
            dialog.commitDiff = diff
            dialog.commitDiffTruncated = truncated
            dialog.commitDiffError = error
            dialog.commitDiffLoading = false
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(820, parent ? parent.width - 48 : 820)
    height: Math.min(620, parent ? parent.height - 96 : 620)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            spacing: 8

            AppIcon {
                icon: "file"
                size: 14
                iconColor: dialog.statusColor()
            }

            Text {
                Layout.fillWidth: true
                text: dialog.checkpoint ? dialog.checkpoint.path : ""
                color: Theme.textPrimary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideMiddle
                Accessible.name: text
            }

            Text {
                visible: dialog.addedLines > 0
                text: "+" + dialog.addedLines
                color: Theme.statusSuccess
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
            }

            Text {
                visible: dialog.removedLines > 0
                text: "−" + dialog.removedLines
                color: Theme.statusDanger
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭文件改动详情")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            Text {
                text: dialog.statusText()
                color: dialog.statusColor()
                font.family: Theme.fontSans
                font.pixelSize: 10
            }

            Text {
                Layout.fillWidth: true
                text: dialog.checkpoint
                    ? Qt.formatDateTime(new Date(dialog.checkpoint.createdAt),
                                        "yyyy-MM-dd HH:mm:ss")
                    : ""
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: 10
            }

            Text {
                visible: dialog.checkpoint
                         && (dialog.checkpoint.changeCount || 1) > 1
                text: qsTr("本次会话共 %1 次改动").arg(dialog.checkpoint
                                                     ? dialog.checkpoint.changeCount : 1)
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: 10
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 6

            AppButton {
                objectName: "checkpointTab"
                text: qsTr("本次改动")
                implicitHeight: 28
                highlighted: dialog.effectiveTab === 0
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.selectTab(0)
            }

            AppButton {
                objectName: "workingTab"
                visible: dialog.gitRepo
                text: qsTr("工作区")
                implicitHeight: 28
                highlighted: dialog.effectiveTab === 1
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.selectTab(1)
            }

            AppButton {
                objectName: "historyTab"
                visible: dialog.gitRepo
                text: qsTr("Git 历史")
                implicitHeight: 28
                highlighted: dialog.effectiveTab === 2
                buttonColor: "transparent"
                textColor: highlighted ? Theme.bgPrimary : Theme.textSecondary
                onClicked: dialog.selectTab(2)
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: dialog.gitRepoKnown && !dialog.gitRepo && dialog.autoLoadGit
                text: qsTr("不在 Git 仓库中，仅显示本次改动")
                color: Theme.textSecondary
                font.family: Theme.fontSans
                font.pixelSize: 10
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            currentIndex: dialog.effectiveTab

            DiffView {
                id: detailDiffView
                objectName: "detailDiffView"
                diffText: dialog.diffText
                diffTruncated: dialog.diffTruncated
                emptyText: dialog.emptyText()
            }

            ColumnLayout {
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    visible: dialog.workingDiffLoading
                    text: qsTr("正在读取工作区差异…")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }

                Text {
                    Layout.fillWidth: true
                    visible: !dialog.workingDiffLoading
                             && dialog.workingDiffError.length > 0
                    text: dialog.workingDiffError
                    color: Theme.statusWarning
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                DiffView {
                    objectName: "workingDiffView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !dialog.workingDiffLoading
                             && dialog.workingDiffError.length === 0
                    diffText: dialog.workingDiff
                    diffTruncated: dialog.workingDiffTruncated
                    emptyText: qsTr("没有未提交的改动（未跟踪文件不显示内容）。")
                }
            }

            RowLayout {
                spacing: 8

                ListView {
                    id: commitList
                    objectName: "gitCommitList"
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    clip: true
                    model: dialog.history
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        id: commitRow
                        objectName: "gitCommitRow"
                        required property var modelData

                        readonly property bool selected:
                            dialog.selectedSha === modelData.sha

                        width: ListView.view ? ListView.view.width : 0
                        height: 54
                        radius: 6
                        color: selected ? Theme.surfaceHover
                                        : (rowHover.hovered ? Theme.surfaceHover
                                                            : "transparent")

                        HoverHandler { id: rowHover }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: commitRow.selected ? 3 : 0
                            radius: 2
                            color: Theme.brandPrimary
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            anchors.topMargin: 5
                            anchors.bottomMargin: 5
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: commitRow.modelData.subject
                                color: Theme.textPrimary
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.fontSizeCaption
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: commitRow.modelData.shortSha
                                      + " · " + commitRow.modelData.author
                                      + " · " + Qt.formatDateTime(
                                            new Date(commitRow.modelData.timestamp * 1000),
                                            "MM-dd HH:mm")
                                color: Theme.textSecondary
                                font.family: Theme.fontMono
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.selectCommit(commitRow.modelData.sha)
                            Accessible.name: qsTr("查看该提交的差异")
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        visible: dialog.historyLoading
                        text: qsTr("正在读取 Git 历史…")
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeCaption
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !dialog.historyLoading
                                 && dialog.historyError.length > 0
                        text: dialog.historyError
                        color: Theme.statusWarning
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !dialog.historyLoading
                                 && dialog.historyError.length === 0
                                 && dialog.history.length === 0
                        text: qsTr("该文件没有 Git 历史。")
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeCaption
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
                        objectName: "commitDiffView"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !dialog.commitDiffLoading
                                 && dialog.commitDiffError.length === 0
                                 && dialog.history.length > 0
                        diffText: dialog.commitDiff
                        diffTruncated: dialog.commitDiffTruncated
                        emptyText: qsTr("点击左侧提交查看差异。")
                    }
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
