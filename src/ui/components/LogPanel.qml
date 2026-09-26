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

    Accessible.name: qsTr("运行日志面板")
    Accessible.description: qsTr("查看应用运行日志，支持按类别、级别和关键词筛选")

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
                id: categoryBox
                Layout.preferredWidth: 110
                // display strings for the user; values[] maps to the actual
                // filter value — binding currentValue (display text) as the
                // filter blanks the whole view (fixed bug, 日志增强二轮)
                model: [
                    qsTr("全部"),
                    qsTr("用户操作"),
                    qsTr("网络"),
                    qsTr("Agent"),
                    qsTr("工具"),
                    qsTr("知识库"),
                    qsTr("应用")
                ]
                property var categoryValues: ["", "nexus.user", "nexus.network",
                                              "nexus.agent", "nexus.tool",
                                              "nexus.rag", "nexus.app"]
                currentIndex: 0
                onActivated: function(index) {
                    AgentVM.logModel.filterCategory = categoryValues[index]
                }
                Component.onCompleted: {
                    const i = categoryValues.indexOf(AgentVM.logModel.filterCategory)
                    currentIndex = Math.max(0, i)
                }
            }

            ComboBox {
                id: levelBox
                Layout.preferredWidth: 150
                model: [
                    qsTr("全部 (All)"),        // index 0 → filterLevel -1 (no gate)
                    qsTr("调试 (Debug)"),      // 1 → severity rank 0
                    qsTr("信息 (Info)"),       // 2 → severity rank 1
                    qsTr("警告 (Warning)"),    // 3 → severity rank 2
                    qsTr("严重 (Critical)")    // 4 → severity rank 3
                ]
                currentIndex: 0   // All (matches the file default)
                onActivated: function(index) {
                    // display index 0 = ALL(-1); 1..4 → severity rank 0..3
                    AgentVM.logModel.filterLevel = index - 1
                }
                Component.onCompleted: currentIndex = AgentVM.logModel.filterLevel + 1
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
                    color: historical ? Theme.textSecondary : Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                    font.italic: historical
                }
                Text {
                    Layout.preferredWidth: 46
                    text: historical ? qsTr("历史") : level
                    color: historical ? Theme.textSecondary
                        : (level === "WARN" || level === "CRIT" || level === "FATAL"
                           ? Theme.brandPrimary : Theme.textSecondary)
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                    font.bold: !historical && (level === "WARN" || level === "CRIT" || level === "FATAL")
                }
                Text {
                    Layout.preferredWidth: 56
                    text: category ? String(category).replace("nexus.", "") : ""
                    color: Theme.textSecondary
                    font.pixelSize: 9
                    font.family: Theme.fontMono
                    elide: Text.ElideRight
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
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: 24
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
