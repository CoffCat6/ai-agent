import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Agent chat page - main content area with trace view and input
Item {
    id: root

    signal settingsRequested()

    Accessible.name: qsTr("AI 对话页面")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Top Bar ────────────────────────────────────
        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
        }

        // ── Session Tabs (Batch 29, F7) ────────────────
        SessionTabs {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
        }

        // ── Trace View (scrollable) ────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                anchors.fill: parent
                clip: true

                ListView {
                    id: traceListView
                    model: AgentVM.traceModel
                    spacing: 0
                    clip: true

                    // Stick-to-bottom only while the user is already near the
                    // end; scrolling up to read pauses the auto-scroll.
                    //
                    // The gap is measured from the TRUE content end. ListView
                    // reports contentHeight relative to originY (the estimated
                    // extent of the rows above that are not instantiated yet),
                    // so `contentHeight - contentY - height` under-reports the
                    // gap by exactly originY — hundreds of px at the tail of a
                    // long trace. That inflated the unpin threshold by the same
                    // amount and made the guard below misfire.
                    //
                    // The second term rejects a *stranded* offset (view parked
                    // far past the end after the content shrank): read as "at
                    // the bottom" it keeps the auto-scroll armed and
                    // re-asserting the bad offset, so scrolling up appears
                    // impossible.
                    function bottomGap() {
                        return originY + contentHeight - contentY - height
                    }
                    property bool pinnedToBottom: true
                    // True for a short window after every contentHeight
                    // change. While huge expanded blocks instantiate lazily,
                    // ListView couples each height correction with a contentY
                    // shift that keeps the viewport visually stable; that
                    // shift reads as DOWNWARD movement and the gap check then
                    // re-arms auto-scroll and yanks the view back to the
                    // bottom mid-scroll-up. Re-pinning from the gap is
                    // suppressed until heights have stopped changing.
                    property bool contentSettling: false
                    Timer {
                        id: settleTimer
                        interval: 200
                        onTriggered: traceListView.contentSettling = false
                    }
                    property real lastContentY: 0
                    onContentYChanged: {
                        const gap = bottomGap()
                        // Mouse-wheel scrolling never sets `moving`, so the
                        // upward check must not be gated on it: ANY upward
                        // shift of the viewport unconditionally unpins; the
                        // gap check only handles downward/programmatic
                        // movement.
                        if (contentY < lastContentY)
                            pinnedToBottom = false
                        else if (!contentSettling)
                            pinnedToBottom = gap < 80 && gap > -height
                        lastContentY = contentY
                    }

                    // Request a deferred scroll-to-bottom. The callback
                    // re-checks pinnedToBottom when it actually runs, so a
                    // scroll queued during streaming is dropped if the user
                    // scrolled away between the request and the next frame.
                    // `!moving` keeps a queued reposition from fighting an
                    // in-progress drag or flick.
                    function requestScrollToBottom() {
                        Qt.callLater(function() {
                            if (pinnedToBottom && !moving)
                                positionViewAtEnd()
                        })
                    }

                    onContentHeightChanged: {
                        contentSettling = true
                        settleTimer.restart()
                        if (pinnedToBottom && !moving)
                            requestScrollToBottom()
                    }

                    // True only while toggleProcess() is swapping the model
                    // over; count transiently reads 0 during the swap and the
                    // empty-state hint must not flash.
                    property bool rebuildingDelegates: false

                    // Expand/collapse state of node detail blocks. Lives on
                    // the view (not the delegate) so it survives the full
                    // trace model resets that follow session switches.
                    property var detailState: ({})

                    // Whether the intermediate agent steps (thinking / tool
                    // calls / results) are shown at all.
                    property bool showProcess: true

                    function detailExpanded(key, defaultExpanded) {
                        return key in detailState ? detailState[key] : defaultExpanded
                    }

                    function toggleDetail(key, defaultExpanded) {
                        const next = Object.assign({}, detailState)
                        next[key] = !(key in next ? next[key] : defaultExpanded)
                        detailState = next
                    }

                    function toggleProcess() {
                        const wasPinned = pinnedToBottom
                        showProcess = !showProcess
                        // Folding changes every intermediate delegate's height
                        // at once. ListView keeps only the delegates inside
                        // cacheBuffer instantiated; the released ones retain the
                        // height they had while hidden (0). Afterwards
                        // contentHeight is under-reported and contentY is left
                        // stranded past the real end, so the viewport sits in
                        // empty space and the stick-to-bottom re-check undoes
                        // every scroll-up. Rebuilding the delegate set makes all
                        // of them re-measure. Only the explicit toggle pays this
                        // cost; streaming keeps the incremental model updates.
                        rebuildingDelegates = true
                        const m = model
                        model = null
                        model = m
                        Qt.callLater(function() { rebuildingDelegates = false })
                        if (wasPinned)
                            Qt.callLater(positionViewAtEnd)
                    }

                    // Computed in C++ (TraceModel); the JS scan over the
                    // whole model on every binding evaluation is gone
                    readonly property int intermediateCount: AgentVM.intermediateCount

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: traceListView.count === 0
                                 && !traceListView.rebuildingDelegates
                        text: "Send a message to start chatting with NexusAI"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                    }

                    delegate: AgentTraceNode {
                        width: traceListView.width
                        // No explicit modelData binding here: AgentTraceNode's
                        // required property is auto-filled by the view from the
                        // list model context. Binding "modelData: modelData"
                        // would self-reference and evaluate to undefined,
                        // blanking every trace node.
                    }
                }
            }

            // ── Process visibility toggle (floating, top right) ──
            Rectangle {
                visible: traceListView.intermediateCount > 0
                anchors.top: parent.top
                anchors.topMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                z: 5
                width: processRow.implicitWidth + 20
                height: 26
                radius: 13
                color: processMA.containsMouse ? Theme.surfaceHover : Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle
                opacity: 0.94

                Row {
                    id: processRow
                    anchors.centerIn: parent
                    spacing: 6

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        icon: traceListView.showProcess ? "chevron-up" : "chevron-down"
                        size: 11
                        iconColor: processMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: traceListView.showProcess
                            ? qsTr("折叠过程")
                            : qsTr("思考过程 · %1").arg(traceListView.intermediateCount)
                        color: processMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontSans
                    }
                }

                MouseArea {
                    id: processMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: traceListView.toggleProcess()
                }

                ToolTip.visible: processMA.containsMouse
                ToolTip.delay: 600
                ToolTip.text: traceListView.showProcess
                    ? qsTr("隐藏思考与工具调用过程") : qsTr("显示思考与工具调用过程")
            }

            // ── Jump to bottom (floating, bottom) ─────────
            Rectangle {
                visible: !traceListView.pinnedToBottom
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 16
                z: 5
                width: 32
                height: 32
                radius: 16
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle
                opacity: 0.94

                AppIcon {
                    anchors.centerIn: parent
                    icon: "chevron-down"
                    size: 15
                    iconColor: jumpMA.containsMouse ? Theme.brandPrimary : Theme.textPrimary
                }

                MouseArea {
                    id: jumpMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: traceListView.positionViewAtEnd()
                }

                ToolTip.visible: jumpMA.containsMouse
                ToolTip.delay: 600
                ToolTip.text: qsTr("回到底部")
            }
        }

        // Auto-scroll the trace view as content updates
        // (streaming responses, new nodes, session loading) - only while
        // the user has not scrolled away to read earlier content.
        Connections {
            target: AgentVM
            function onTraceChanged() {
                if (traceListView.pinnedToBottom)
                    traceListView.requestScrollToBottom()
            }
        }

        // ── Input Console ──────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: inputConsole.height + 52

            InputConsole {
                id: inputConsole
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 14
                anchors.leftMargin: 20
                anchors.rightMargin: 20
            }

            // Active model selector dropdown
            ModelSelector {
                id: modelBadge
                anchors.top: inputConsole.bottom
                anchors.topMargin: 4
                anchors.left: inputConsole.left
                anchors.leftMargin: 4
                onSettingsRequested: root.settingsRequested()
            }

            // Thinking effort of the active model profile (docs: 随 profile 持久)
            ThinkingEffortSelector {
                id: effortBadge
                anchors.top: inputConsole.bottom
                anchors.topMargin: 4
                anchors.left: modelBadge.right
                anchors.leftMargin: 8
            }

            // Session permission mode (confirm / auto-edit / full access)
            PermissionModeSelector {
                id: permissionBadge
                anchors.top: inputConsole.bottom
                anchors.topMargin: 4
                // 锚点必须跟着右移：仍挂在 modelBadge.right 会和思考强度
                // 选择框完全重叠。
                anchors.left: effortBadge.right
                anchors.leftMargin: 8
            }
        }
    }
}
