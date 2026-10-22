import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Review dialog: everything the agent wrote in this session, in a dialog
// wide enough to actually read a diff in.
//
// HISTORY: the review lived in the 220..640px right sidebar, where the
// diff pane was a narrow column and a long diff was unreadable. The user
// asked for a big dialog, so the panel moved here - and with ~1000px of
// width the split changed shape: the file tree now stands *beside* the
// diff instead of above it (splitAxis "horizontal"), so the diff gets the
// full dialog height as well as the remaining width, and the file list
// stays visible while a long diff is read.
//
// The panel keeps its own summary header off (showHeader false): this
// dialog prints the file count and the +/- stats in its own header, so
// the numbers are not shown twice. The tree's fold-all buttons moved up
// into that header with them.
//
// The dialog reads the live checkpoint list from AgentVM; the host only
// has to set `parent: Overlay.overlay` (like every other dialog in
// main.qml) and call open().
Dialog {
    id: dialog
    objectName: "reviewDialogItem"

    // Session checkpoints. Bound to the live VM list, so a file written
    // while the dialog is open shows up right away; a host (or a test) may
    // assign its own list instead.
    property var checkpoints: AgentVM.fileCheckpoints
    // Active project root: the tree shows repo-relative paths under it.
    property string projectRoot: AgentVM.activeProjectRoot
    // Test/embedding hook: overrides AgentVM.fileCheckpointDetail(id).
    property var detailProvider: null
    // Test/embedding hook: the "I have seen these changes" action. Defaults to
    // the AgentVM call the top bar's highlight listens to; a harness can swap
    // it out to assert when (and how often) close triggers it.
    property var reviewAcknowledger: null

    readonly property int fileCount: panel.itemCount
    readonly property int addedLines: panel.activeAdded
    readonly property int removedLines: panel.activeRemoved
    readonly property bool hasItems: panel.hasItems
    // The panel hides itself when the session touched no file, so the
    // dialog has to explain the emptiness itself.
    readonly property bool emptyStateVisible: !dialog.hasItems
    // The hosted panel, so a host (or a test) can drive the tree and the
    // diff directly: fold-all, file stepping, split ratio.
    readonly property alias reviewPanel: panel

    // Entry point (house recipe): the panel reads the live list, so
    // opening is just showing the dialog. `items` is optional and used by
    // hosts/tests that want to review a fixed set of checkpoints.
    function openFor(items) {
        if (items !== undefined && items !== null)
            dialog.checkpoints = items
        dialog.open()
    }

    anchors.centerIn: parent
    width: Math.min(1180, parent ? parent.width - 48 : 1180)
    height: Math.min(820, parent ? parent.height - 80 : 820)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}
    // UE-1: closing the review is what counts as "I have seen these changes",
    // so the top bar's highlight clears here rather than on open - a user who
    // opens and closes without reading still got the list in front of them,
    // and keeping the badge past that point would make it impossible to clear
    // without pretending to read. New writes afterwards lift the id watermark
    // and light the badge again on their own.
    onClosed: {
        if (dialog.reviewAcknowledger)
            dialog.reviewAcknowledger()
        else
            AgentVM.acknowledgeFileReview()
    }

    contentItem: ColumnLayout {
        spacing: 8

        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("文件改动审阅")
        Accessible.description: qsTr("按目录列出本次会话改动的文件，并显示所选文件的内容差异")

        // ── Header: what is on review ───────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            spacing: 8

            AppIcon {
                icon: "edit"
                size: 14
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("文件改动审阅")
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                elide: Text.ElideRight
                Accessible.name: text
            }

            Text {
                objectName: "reviewSummaryFiles"
                text: qsTr("%1 个文件").arg(dialog.fileCount)
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                Accessible.name: text
            }

            Text {
                objectName: "reviewSummaryAdded"
                visible: dialog.addedLines > 0
                text: "+" + dialog.addedLines
                color: Theme.statusSuccess
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                Accessible.name: text
            }

            Text {
                objectName: "reviewSummaryRemoved"
                visible: dialog.removedLines > 0
                text: "−" + dialog.removedLines
                color: Theme.statusDanger
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                Accessible.name: text
            }

            // Accept every file still pending. Hidden once nothing is left
            // to accept, so the dialog does not offer an action that would
            // do nothing.
            AppButton {
                objectName: "reviewAcceptAllButton"
                visible: dialog.hasItems && panel.hasPending
                text: qsTr("全部确认")
                implicitHeight: 22
                enabled: !AgentVM.stateBusy
                opacity: enabled ? 1.0 : 0.4
                onClicked: panel.performAcceptAll()
                Accessible.name: qsTr("确认本会话全部待确认的文件改动")
            }

            // The mirror action. Destructive, so it asks for confirmation and
            // names the files; it stays available whenever something is
            // pending, because reverting is the alternative verdict, not a
            // feature to be discovered late.
            AppButton {
                objectName: "reviewUndoAllButton"
                visible: dialog.hasItems && panel.hasPending
                text: qsTr("全部撤销")
                implicitHeight: 22
                textColor: Theme.statusWarning
                enabled: !AgentVM.stateBusy
                opacity: enabled ? 1.0 : 0.4
                onClicked: panel.requestUndoAll()
                Accessible.name: qsTr("撤销本会话全部待确认的文件改动")
            }

            // The tree's fold buttons: they belong to the panel, but the
            // panel's own header is off here, so the dialog hosts them.
            // A plain Row (not a RowLayout) keeps the fixed button size.
            Row {
                spacing: 2
                visible: dialog.hasItems

                IconButton {
                    objectName: "reviewCollapseAllButton"
                    width: 22
                    height: 22
                    radius: 4
                    icon: "chevron-up"
                    iconSize: 10
                    tooltip: qsTr("折叠全部目录")
                    onClicked: panel.collapseTree()
                }

                IconButton {
                    objectName: "reviewExpandAllButton"
                    width: 22
                    height: 22
                    radius: 4
                    icon: "chevron-down"
                    iconSize: 10
                    tooltip: qsTr("展开全部目录")
                    onClicked: panel.expandTree()
                }
            }

            IconButton {
                objectName: "reviewCloseButton"
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭审阅对话框")
            }
        }

        // ── Meta row: which project the paths belong to ──
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.projectRoot.length > 0
            text: dialog.projectRoot
            color: Theme.textTertiary
            font.family: Theme.fontMono
            font.pixelSize: 10
            elide: Text.ElideMiddle
            Accessible.name: text
        }

        // ── Body: the review panel ──────────────────────
        // listMaxHeight 0 = "fill the slot": the dialog's ColumnLayout
        // hands the panel a bounded height, so the panel splits that
        // height between the tree and the diff. The tree stands left of
        // the diff (splitAxis "horizontal"), which is what the dialog's
        // width is for.
        FileChangesPanel {
            id: panel
            objectName: "reviewPanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            checkpoints: dialog.checkpoints
            projectRoot: dialog.projectRoot
            detailProvider: dialog.detailProvider
            listMaxHeight: 0
            splitAxis: "horizontal"
            showHeader: false
        }

        // ── Empty state ────────────────────────────────
        // The panel hides itself when there is nothing to review, so the
        // dialog has to say so itself - otherwise the dialog is a blank
        // rectangle.
        Item {
            objectName: "reviewEmptyState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: dialog.emptyStateVisible

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 460)
                spacing: 10

                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    icon: "file"
                    size: 26
                    iconColor: Theme.textTertiary
                }

                Text {
                    width: parent.width
                    text: qsTr("本次会话暂无可审阅的文件改动。")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Accessible.name: text
                }

                Text {
                    width: parent.width
                    text: qsTr("智能体写入或编辑文件后，这里会按目录列出改动；\n选中文件即可在右侧查看内容差异。")
                    color: Theme.textTertiary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Accessible.name: text
                }
            }
        }

        // ── Footer ─────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14

            Item { Layout.fillWidth: true }

            AppButton {
                objectName: "reviewCloseFooterButton"
                text: qsTr("关闭")
                onClicked: dialog.close()
            }
        }
    }
}
