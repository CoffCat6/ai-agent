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
                    logList.applyFilterChange(function() {
                        AgentVM.logModel.filterCategory = categoryValues[index]
                    })
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
                    logList.applyFilterChange(function() {
                        AgentVM.logModel.filterLevel = index - 1
                    })
                }
                Component.onCompleted: currentIndex = AgentVM.logModel.filterLevel + 1
            }

            // Date scope. The entries are the days actually present in the
            // session mirror (plus "all"), so no date-picker widget is needed
            // and an empty day can never be offered. LogModel keeps the
            // selected day listed even after its rows are gone.
            ComboBox {
                id: dateBox
                Layout.preferredWidth: 132
                model: {
                    const out = [qsTr("全部日期")]
                    const days = AgentVM.logModel.availableDates
                    for (let i = 0; i < days.length; ++i)
                        out.push(days[i])
                    return out
                }
                // Index-based on purpose: binding the *display text* as the
                // filter value blanked the view once already (see categoryBox).
                function syncIndex() {
                    const cur = AgentVM.logModel.filterDate
                    const i = cur.length === 0
                        ? -1 : AgentVM.logModel.availableDates.indexOf(cur)
                    currentIndex = i < 0 ? 0 : i + 1
                }
                Component.onCompleted: syncIndex()
                onActivated: function(index) {
                    const days = AgentVM.logModel.availableDates
                    logList.applyFilterChange(function() {
                        AgentVM.logModel.filterDate =
                            index === 0 ? "" : days[index - 1]
                    })
                }
                Connections {
                    target: AgentVM.logModel
                    // A new day rolled in (or the selection got re-listed):
                    // keep the shown item on the actual filter value.
                    function onAvailableDatesChanged() { dateBox.syncIndex() }
                }
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
                    onTriggered: logList.applyFilterChange(function() {
                        AgentVM.logModel.filterText = keywordField.text
                    })
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
            //
            // The gap is measured from the TRUE content end. ListView reports
            // contentHeight relative to originY (the estimated extent of the
            // rows above that are not instantiated yet), so
            // `contentHeight - contentY - height` under-reports the gap by
            // exactly originY — several hundred px at the tail of a long log,
            // which inflated the unpin threshold by the same amount (the user
            // had to scroll up ~originY + 80 px before auto-scroll let go).
            function bottomGap() {
                return originY + contentHeight - contentY - height
            }
            property bool pinnedToBottom: true
            onContentYChanged: {
                const gap = bottomGap()
                pinnedToBottom = gap < 80 && gap > -height
            }
            onCountChanged: if (pinnedToBottom)
                Qt.callLater(positionViewAtEnd)

            // Every filter change is a model reset (LogModel::setFilter*), and
            // a reset drops the view to the top and clears pinnedToBottom — so
            // the panel silently stopped following the live tail after any
            // filter interaction, including each debounced keystroke in the
            // keyword field. Capture the intent first and restore it once the
            // rebuilt rows are laid out.
            function applyFilterChange(change) {
                const wasPinned = pinnedToBottom
                change()
                if (wasPinned) {
                    Qt.callLater(function() {
                        pinnedToBottom = true
                        positionViewAtEnd()
                    })
                }
            }

            ScrollBar.vertical: ScrollBar {}

            // One entry per band. With wrapped messages the rows ran together
            // and there was no way to see where an entry stopped, so each row
            // now carries a hairline separator (borderSubtle — the same token
            // the rest of the app uses for its hairline dividers) and a hover
            // highlight for tracking a long line across the message column.
            // Zebra striping was considered and rejected: the only fill light
            // enough (bgPrimary) sits ~4% off the dialog surface and is not
            // readable as a stripe, while surfaceHover is already the hover
            // feedback — reusing it for stripes would erase the hover.
            delegate: Item {
                id: logRow
                width: logList.width
                height: rowLayout.implicitHeight + 6

                Rectangle {
                    anchors.fill: parent
                    color: rowHover.hovered ? Theme.surfaceHover : "transparent"
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.borderSubtle
                }
                HoverHandler { id: rowHover }

                RowLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 3
                    spacing: 8

                    // Local calendar day included: without it rows from
                    // different days were indistinguishable (backfill spans
                    // several days). Second precision only — millisecond digits
                    // are noise when scanning (AuditDialog uses the same
                    // granularity). The file on disk keeps ms:
                    // backfillFromFile parses that exact format.
                    //
                    // Backfilled rows (T9) carry no marker of their own: a lone
                    // glyph parked left of the clock was read as part of the
                    // timestamp, and a self-explanatory word would need a column
                    // plus a header row. They are signalled by the italic
                    // typeface alone. What must never come back is the old
                    // build's trick of printing "历史" IN the level column,
                    // which swallowed the severity of every backfilled row.
                    Text {
                        Layout.preferredWidth: 108
                        text: Qt.formatDateTime(new Date(timestamp), "MM-dd hh:mm:ss")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontMono
                        font.italic: historical
                    }
                    Text {
                        Layout.preferredWidth: 46
                        text: level
                        color: (level === "WARN" || level === "CRIT" || level === "FATAL")
                            ? Theme.brandPrimary : Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontMono
                        font.bold: level === "WARN" || level === "CRIT" || level === "FATAL"
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
                onClicked: {
                    // The view is being emptied, so there is no reading position
                    // to preserve — always resume following the live tail.
                    logList.pinnedToBottom = true
                    AgentVM.logModel.clearView()
                    logList.positionViewAtEnd()
                }
            }
        }
    }
}
