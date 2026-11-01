import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Read-only view of the active tab's task list, maintained by the
// todo_write tool. Data flows AgentVM.todoList -> here; this component owns
// only presentation and the collapse state.
//
// Layout contract for the right sidebar (220-640px wide, user-resizable and
// shared with the review/Git/knowledge sections below):
//   * The header carries the progress summary ("3/7 完成" + a bar), so the
//     state of a long task list is readable without scanning every row.
//   * The rows live in a height-capped viewport that scrolls internally, so
//     a 20-item list can never push the section tabs out of the panel.
//   * The panel keeps hiding itself (zero height) when the list is empty, so
//     an idle session spends no vertical space on it.
//
// The item contract (id / content / status / priority) is owned by todo_write and
// the ViewModel; this file only decides how those fields are shown (the id is
// prefixed to the content line as a referenceable label, docs/33 E4).
Item {
    id: root

    property var todos: []
    property bool collapsed: false

    // Default viewport cap (px) — the share a narrow sidebar column can
    // afford. The host may lower it (ContextPanel ties it to the panel
    // height) or raise it (the todo dialog gives the list a whole body);
    // what the component honours is `listMaxHeight`, which it never
    // exceeds on its own.
    readonly property int listHeightCeiling: 200
    // Viewport height cap actually in effect (0 = fill the offered space).
    property int listMaxHeight: listHeightCeiling
    // Colour of the surface the rows are drawn on. The bottom fade blends
    // into it, so it has to follow whatever the host put the panel on:
    // the sidebar background by default, the dialog's inset list well
    // when the same panel is hosted by the todo dialog. Hard-coding one
    // colour here would paint a mismatched band over the last rows.
    property color surfaceColor: Theme.bgSecondary
    // Hosts that draw their own title bar (the todo dialog) switch the
    // panel's header row off: it would print "任务清单 · 3/7" a second
    // time, and its chevron (a fold toggle) means nothing in a dialog.
    property bool showHeader: true

    readonly property int itemCount: todos ? todos.length : 0
    readonly property bool hasItems: itemCount > 0
    readonly property bool listVisible: hasItems && !collapsed
    readonly property int renderedCount: rowsRepeater.count
    readonly property int completedCount: countStatus("completed")
    readonly property int inProgressCount: countStatus("in_progress")
    // Anything that is neither finished nor running counts as "not started".
    // The row renderer uses the same rule, so the header numbers and the
    // row marks can never disagree.
    readonly property int pendingCount: itemCount - completedCount - inProgressCount
    readonly property bool allCompleted: hasItems && completedCount === itemCount
    // 0..1 share of finished tasks — drives the header progress bar.
    readonly property real progressRatio: itemCount > 0 ? completedCount / itemCount : 0
    // "进行中 1 · 待处理 4": how many tasks are running / waiting. The row
    // marks say which task is which; this line says how many are left.
    readonly property string stateSummary: {
        var parts = []
        if (inProgressCount > 0)
            parts.push(qsTr("进行中 %1").arg(inProgressCount))
        if (pendingCount > 0)
            parts.push(qsTr("待处理 %1").arg(pendingCount))
        return parts.join(" · ")
    }
    // Viewport vs. content height: the difference is what the user still
    // has to scroll for (surfaced by the bottom fade). A folded list has no
    // visible viewport at all.
    readonly property real listViewportHeight: root.listVisible ? scroller.height : 0
    readonly property real listContentHeight: scroller.contentHeight
    readonly property bool listScrollable: root.listVisible
                                            && listContentHeight > listViewportHeight + 1
    readonly property bool moreBelowFold: bottomFade.visible
    // Fixed chrome above the viewport: the header row + the progress bar
    // + the summary line + the spacing between them. Measured from
    // implicit heights so a host can budget panel height without waiting
    // for a layout pass. The header and the summary line are optional
    // (the dialog hides the header, a folded list hides the summary), so
    // the gap count follows what is actually on screen.
    readonly property int chromeHeight: {
        var h = progressTrack.implicitHeight
        var rows = 1                      // the progress bar
        if (root.showHeader) {
            h += header.implicitHeight
            ++rows
        }
        if (root.listVisible) {
            h += summaryText.implicitHeight
            ++rows
        }
        ++rows                            // the list viewport itself
        return h + (rows - 1) * content.spacing
    }
    // Colour the bottom fade ends in, read back from the live gradient
    // (not from `surfaceColor`) so a host — or a test — can prove that the
    // fade really blends into the surface the rows sit on.
    //
    // A function, not a property: gradient stops are not notifiable, so a
    // binding on them would freeze at its first evaluation (and warn about
    // depending on non-bindable properties on every re-evaluation).
    function fadeTerminalColor() {
        var gradient = bottomFade.gradient
        if (!gradient || gradient.stops.length === 0) return "transparent"
        return gradient.stops[gradient.stops.length - 1].color
    }

    visible: hasItems
    implicitHeight: hasItems ? content.implicitHeight : 0

    function countStatus(status) {
        if (!todos) return 0
        var count = 0
        for (var i = 0; i < todos.length; ++i) {
            if (todos[i] && todos[i].status === status)
                ++count
        }
        return count
    }

    function statusLabel(status) {
        if (status === "completed") return qsTr("已完成")
        if (status === "in_progress") return qsTr("进行中")
        return qsTr("待处理")
    }

    function priorityLabel(priority) {
        if (priority === "high") return qsTr("高优先级")
        if (priority === "medium") return qsTr("中优先级")
        return qsTr("低优先级")
    }

    function priorityColor(priority) {
        if (priority === "high") return Theme.statusDanger
        if (priority === "medium") return Theme.statusWarning
        return Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g,
                       Theme.textSecondary.b, 0.45)
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 6

        // Header doubles as the collapse toggle: title, progress count and
        // the chevron all live in one 20px strip, so the whole summary costs
        // a single line even in a 220px-wide panel.
        Item {
            id: header
            // implicitHeight (not only Layout.preferredHeight) so the host
            // can measure the fixed chrome without waiting for layout.
            implicitHeight: 20
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            visible: root.showHeader

            readonly property bool hovered: headerHover.hovered

            HoverHandler { id: headerHover }

            // Hover fill: the header is a button (it folds the list), so it
            // has to look like one.
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: -4
                anchors.rightMargin: -4
                anchors.topMargin: -2
                anchors.bottomMargin: -2
                radius: Theme.radiusSm
                color: header.hovered ? Theme.surfaceHover : "transparent"
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.collapsed = !root.collapsed
            }

            RowLayout {
                anchors.fill: parent
                spacing: 6

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: "check"
                    size: 12
                    iconColor: Theme.brandPrimary
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("任务清单")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Item { Layout.fillWidth: true }

                // "3/7 完成" — the one number that answers "how far along
                // is this?" without reading a single row.
                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("%1/%2 完成").arg(root.completedCount).arg(root.itemCount)
                    color: root.completedCount > 0 ? Theme.textPrimary : Theme.textTertiary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontMono
                }

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: root.collapsed ? "chevron-down" : "chevron-up"
                    size: 12
                    iconColor: header.hovered ? Theme.brandPrimary : Theme.textSecondary
                }
            }

            activeFocusOnTab: true
            Keys.onReturnPressed: root.collapsed = !root.collapsed
            Keys.onSpacePressed: root.collapsed = !root.collapsed
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("任务清单，%1/%2 完成")
                                .arg(root.completedCount).arg(root.itemCount)
            Accessible.description: root.collapsed ? qsTr("已折叠") : qsTr("已展开")
            Accessible.onPressAction: root.collapsed = !root.collapsed

            ToolTip.visible: headerHover.hovered
            ToolTip.delay: 600
            ToolTip.text: root.collapsed ? qsTr("展开任务清单") : qsTr("折叠任务清单")
        }

        // ── Progress bar ─────────────────────────────
        // The one-glance answer to "how far along is this task?". The fill
        // uses the success colour so it reads as "done", the same green as
        // the check mark on finished rows.
        Rectangle {
            id: progressTrack
            objectName: "todoProgressTrack"
            implicitHeight: 4
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            radius: 2
            color: Theme.borderSubtle
            clip: true

            Accessible.role: Accessible.ProgressBar
            Accessible.name: qsTr("任务进度：%1/%2 已完成")
                                .arg(root.completedCount).arg(root.itemCount)

            Rectangle {
                id: progressFill
                objectName: "todoProgressFill"
                width: progressTrack.width * root.progressRatio
                height: progressTrack.height
                radius: progressTrack.radius
                color: Theme.statusSuccess

                // The count changes while the agent works; animate the bar
                // so the progress reads as movement, not as a jump.
                Behavior on width {
                    NumberAnimation {
                        duration: Theme.motionBase
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        // ── State breakdown ──────────────────────────
        // Aggregate counts ("进行中 1 · 待处理 4"). The row marks show the
        // state of one task, this line shows how many of each are left.
        // Hidden while collapsed — folding the list away is the point.
        Text {
            id: summaryText
            objectName: "todoSummary"
            Layout.fillWidth: true
            visible: root.listVisible
            text: root.allCompleted ? qsTr("全部完成") : root.stateSummary
            color: root.allCompleted ? Theme.statusSuccess : Theme.textTertiary
            font.pixelSize: Theme.fontSizeNano
            font.family: Theme.fontMono
            elide: Text.ElideRight
        }

        // ── List ─────────────────────────────────────
        // Fixed-height viewport: the rows scroll inside it, so a long task
        // list can never squeeze the section tabs out of the panel.
        Item {
            id: listArea
            Layout.fillWidth: true
            Layout.preferredHeight: root.listVisible
                ? (root.listMaxHeight > 0
                    ? Math.min(todoColumn.implicitHeight, root.listMaxHeight)
                    : todoColumn.implicitHeight)
                : 0
            Layout.fillHeight: root.listMaxHeight <= 0
            visible: root.listVisible
            clip: true

            Flickable {
                id: scroller
                anchors.fill: parent
                clip: true
                contentHeight: todoColumn.implicitHeight
                contentWidth: width
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                // Repeater (not ListView) keeps every row instantiated regardless
                // of viewport height, so a fresh panel never deadlocks at 0 height
                // waiting for delegates that only appear once the height is > 0.
                Column {
                    id: todoColumn
                    width: scroller.width
                    spacing: 6

                    Repeater {
                        id: rowsRepeater
                        model: root.todos

                        delegate: Item {
                            id: todoRow
                            // Repeater injects modelData as a context property;
                            // todo_write items are plain maps, so everything the
                            // row renders is derived from it here.
                            required property var modelData

                            readonly property string content: modelData.content || ""
                            readonly property string itemStatus: modelData.status || "pending"
                            readonly property string priority: modelData.priority || "medium"
                            readonly property bool isCompleted: itemStatus === "completed"
                            readonly property bool isInProgress: itemStatus === "in_progress"
                            readonly property bool isPending: !isCompleted && !isInProgress

                            objectName: "todoItem"
                            width: todoColumn.width
                            height: rowLayout.implicitHeight

                            // The running task is the only row that gets a
                            // surface tint: it is the one row the user has
                            // to find without reading.
                            Rectangle {
                                objectName: "todoActiveTint"
                                anchors.fill: parent
                                anchors.topMargin: -2
                                anchors.bottomMargin: -2
                                radius: Theme.radiusSm
                                color: todoRow.isInProgress
                                    ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                             Theme.brandPrimary.b, 0.09)
                                    : "transparent"
                            }

                            RowLayout {
                                id: rowLayout
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 7

                                // Priority rail: red / amber / muted.
                                Rectangle {
                                    objectName: "todoPriorityRail"
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 2.5
                                    Layout.preferredHeight: Math.max(12,
                                        Math.min(rowLayout.implicitHeight - 4, 26))
                                    radius: 1.25
                                    color: root.priorityColor(todoRow.priority)
                                }

                                // Status mark: filled check / selected dot /
                                // empty ring. Three different shapes, not
                                // three shades of one shape — the states stay
                                // readable at a glance and for colour-blind
                                // users.
                                Item {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 11
                                        height: 11
                                        radius: 5.5
                                        color: todoRow.isCompleted
                                            ? Theme.statusSuccess : "transparent"
                                        border.width: todoRow.isCompleted ? 0
                                            : todoRow.isInProgress ? 1.8 : 1.3
                                        border.color: todoRow.isInProgress
                                            ? Theme.brandPrimary : Theme.textTertiary
                                    }

                                    AppIcon {
                                        visible: todoRow.isCompleted
                                        anchors.centerIn: parent
                                        icon: "check"
                                        size: 8
                                        iconColor: Theme.bgSecondary
                                    }

                                    Rectangle {
                                        visible: todoRow.isInProgress
                                        anchors.centerIn: parent
                                        width: 5
                                        height: 5
                                        radius: 2.5
                                        color: Theme.brandPrimary
                                    }
                                }

                                Text {
                                    id: todoText
                                    objectName: "todoText"
                                    Layout.fillWidth: true
                                    // docs/33 E4: 稳定 id 徽标——面板项与
                                    // spawn_agent 引用/审计行可人工对应。
                                    text: ((modelData.id || "") !== ""
                                           ? (modelData.id + "  ") : "")
                                          + (modelData.content || "")
                                    color: todoRow.isCompleted ? Theme.textSecondary : Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    font.bold: todoRow.isInProgress
                                    font.strikeout: todoRow.isCompleted
                                    opacity: todoRow.isCompleted ? 0.75 : 1.0
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight

                                    // Long task descriptions are clipped to
                                    // three lines; hovering shows the rest.
                                    HoverHandler { id: textHover }
                                    ToolTip.visible: textHover.hovered && todoText.truncated
                                    ToolTip.delay: 400
                                    ToolTip.text: todoText.text
                                }

                                // The state word is shown for the running
                                // task only: its mark is the one that could
                                // be mistaken for a plain bullet, and every
                                // extra word on the right costs text width
                                // in a 220px panel.
                                Text {
                                    objectName: "todoStateLabel"
                                    Layout.alignment: Qt.AlignTop
                                    visible: todoRow.isInProgress
                                    text: root.statusLabel(todoRow.itemStatus)
                                    color: Theme.brandPrimary
                                    font.pixelSize: Theme.fontSizeNano
                                    font.family: Theme.fontSans
                                }
                            }

                            Accessible.role: Accessible.ListItem
                            Accessible.name: (modelData.content || "") + "，"
                                             + root.statusLabel(todoRow.itemStatus)
                            Accessible.description: root.priorityLabel(todoRow.priority)
                        }
                    }
                }
            }

            // Bottom fade: the only cue that more tasks live below the fold.
            // The panel is short by design, so there is no room for a
            // permanent scrollbar. The fade has to end in the colour of
            // the surface behind the rows — hence `surfaceColor`, not a
            // hard-coded background: the same panel is hosted by the
            // sidebar and by the todo dialog, and the two sit on
            // different surfaces.
            Rectangle {
                id: bottomFade
                objectName: "todoBottomFade"
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 18
                visible: root.listScrollable
                    && scroller.contentY + scroller.height < scroller.contentHeight - 1
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: root.surfaceColor }
                }
            }
        }
    }
}
