import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// In-app runtime log viewer (日志开发 doc 6, M2/M3): live ring-buffer feed
// with severity + keyword filtering, pinned-to-bottom scrolling and quick
// links to the rotated log files. Deliberately distinct from the audit
// dialog: "日志" = application diagnostics, "审计日志" = tool activity.
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 760
    height: 540

    // Close panel polling when hidden (zero idle cost, doc 6 §3.7)
    onAboutToShow: AgentVM.logModel.polling = true
    onAboutToHide: AgentVM.logModel.polling = false

    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.leftMargin: 24
            Layout.topMargin: 20
            text: qsTr("运行日志")
            color: Theme.textPrimary
            font.pixelSize: 16
            font.bold: true
            font.family: Theme.fontSans
        }

        // ── Filter row: severity + keyword ────────────
        RowLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                id: levelBox
                Layout.preferredWidth: 150
                model: [
                    qsTr("调试 (Debug)"),
                    qsTr("信息 (Info)"),
                    qsTr("警告 (Warning)"),
                    qsTr("严重 (Critical)")
                ]
                currentIndex: 1   // Info and above, matching the file default
                onActivated: function(index) {
                    // display index → QtMsgType (Debug=0 .. Critical=3)
                    AgentVM.logModel.filterLevel = index
                }
                Component.onCompleted: currentIndex = AgentVM.logModel.filterLevel
            }

            TextField {
                id: keywordField
                Layout.fillWidth: true
                placeholderText: qsTr("关键词过滤")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                color: Theme.textPrimary
                // 200 ms debounce so typing does not thrash the model
                Timer {
                    id: keywordDebounce
                    interval: 200
                    onTriggered: AgentVM.logModel.filterText = keywordField.text
                }
                onTextChanged: keywordDebounce.restart()
            }

        }

        // ── Log list ──────────────────────────────────
        ListView {
            id: logList
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: AgentVM.logModel

            // Stick to the bottom while the user is already near it;
            // scrolling up pauses auto-scroll (AgentChatPage pattern).
            property bool pinnedToBottom: true
            onContentYChanged: {
                const distance = contentHeight - contentY - height
                pinnedToBottom = distance < 80
            }
            onCountChanged: if (pinnedToBottom)
                Qt.callLater(positionViewAtEnd)

            ScrollBar.vertical: ScrollBar {}

            delegate: RowLayout {
                width: logList.width
                spacing: 8

                Text {
                    Layout.preferredWidth: 118
                    text: Qt.formatDateTime(new Date(timestamp), "hh:mm:ss.zzz")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }
                Text {
                    Layout.preferredWidth: 46
                    text: level
                    color: level === "WARN" || level === "CRIT" || level === "FATAL"
                           ? Theme.brandPrimary : Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                    font.bold: level === "WARN" || level === "CRIT" || level === "FATAL"
                }
                Text {
                    Layout.fillWidth: true
                    text: message
                    color: Theme.textPrimary
                    font.pixelSize: 11
                    font.family: Theme.fontMono
                    wrapMode: Text.WrapAnywhere
                }
            }
        }

        // Jump back to the live tail after scrolling away
        Button {
            id: jumpToBottom
            anchors.right: parent.right
            anchors.bottom: logList.bottom
            anchors.margins: 12
            visible: !logList.pinnedToBottom
            text: qsTr("回到底部")
            onClicked: {
                logList.pinnedToBottom = true
                logList.positionViewAtEnd()
            }
        }

        // ── Tool row: copy / open folder / clear ──────
        RowLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 16
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: qsTr("复制可见日志")
                onClicked: AgentVM.copyToClipboard(AgentVM.logModel.toClipboardText())
            }

            Button {
                text: qsTr("打开日志目录")
                onClicked: AgentVM.openLogFolder()
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("清除视图")
                onClicked: AgentVM.logModel.clearView()
            }
        }
    }
}
