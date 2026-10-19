import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Read-only view of the active tab's task list, maintained by the
// todo_write tool. Data flows AgentVM.todoList -> here; this component owns
// only presentation and the collapse state.
Item {
    id: root

    property var todos: []
    property bool collapsed: false

    readonly property int itemCount: todos ? todos.length : 0
    readonly property bool hasItems: itemCount > 0
    readonly property bool listVisible: hasItems && !collapsed
    readonly property int renderedCount: rowsRepeater.count
    readonly property int completedCount: {
        if (!todos) return 0
        var count = 0
        for (var i = 0; i < todos.length; ++i) {
            if (todos[i] && todos[i].status === "completed")
                ++count
        }
        return count
    }

    visible: hasItems
    implicitHeight: hasItems ? content.implicitHeight : 0

    function statusLabel(status) {
        if (status === "completed") return qsTr("已完成")
        if (status === "in_progress") return qsTr("进行中")
        return qsTr("待处理")
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

        // Header doubles as the collapse toggle.
        Item {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 22

            readonly property bool hovered: headerHover.hovered

            HoverHandler { id: headerHover }

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
                    text: qsTr("任务清单")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Text {
                    text: qsTr("%1/%2 完成").arg(root.completedCount).arg(root.itemCount)
                    color: Theme.textSecondary
                    font.pixelSize: 9
                    font.family: Theme.fontMono
                    opacity: 0.8
                }

                Item { Layout.fillWidth: true }

                AppIcon {
                    icon: root.collapsed ? "chevron-down" : "chevron-up"
                    size: 12
                    iconColor: header.hovered ? Theme.brandPrimary : Theme.textSecondary
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.collapsed = !root.collapsed
            }

            activeFocusOnTab: true
            Keys.onReturnPressed: root.collapsed = !root.collapsed
            Keys.onSpacePressed: root.collapsed = !root.collapsed
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("任务清单，%1/%2 完成")
                                .arg(root.completedCount).arg(root.itemCount)
            Accessible.description: root.collapsed ? qsTr("已折叠") : qsTr("已展开")
            Accessible.onPressAction: root.collapsed = !root.collapsed
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            // Repeater (not ListView) keeps every row instantiated regardless
            // of viewport height, so a fresh panel never deadlocks at 0 height
            // waiting for delegates that only appear once the height is > 0.
            Layout.preferredHeight: visible ? Math.min(todoColumn.implicitHeight, 220) : 0
            visible: root.listVisible
            clip: true
            contentHeight: todoColumn.implicitHeight
            contentWidth: width
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

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

                        objectName: "todoItem"
                        width: todoColumn.width
                        height: rowLayout.implicitHeight

                        RowLayout {
                            id: rowLayout
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 7

                            // Priority stripe: red / amber / muted.
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 3
                                Layout.preferredHeight: 16
                                radius: 1.5
                                color: root.priorityColor(todoRow.priority)
                            }

                            // Status indicator: filled check / ringed dot / empty ring.
                            Item {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 12
                                Layout.preferredHeight: 12

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: todoRow.isCompleted ? Theme.statusSuccess : "transparent"
                                    border.width: todoRow.itemStatus === "pending" ? 1.4
                                                : todoRow.itemStatus === "in_progress" ? 1.6 : 0
                                    border.color: todoRow.itemStatus === "in_progress"
                                        ? Theme.brandPrimary : Theme.textSecondary
                                    opacity: todoRow.itemStatus === "pending" ? 0.55 : 1
                                }

                                AppIcon {
                                    visible: todoRow.isCompleted
                                    anchors.centerIn: parent
                                    icon: "check"
                                    size: 8
                                    iconColor: Theme.bgSecondary
                                }

                                Rectangle {
                                    visible: todoRow.itemStatus === "in_progress"
                                    anchors.centerIn: parent
                                    width: 4
                                    height: 4
                                    radius: 2
                                    color: Theme.brandPrimary
                                }
                            }

                            Text {
                                objectName: "todoText"
                                Layout.fillWidth: true
                                text: modelData.content || ""
                                color: todoRow.isCompleted ? Theme.textSecondary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                font.strikeout: todoRow.isCompleted
                                opacity: todoRow.isCompleted ? 0.7
                                         : todoRow.itemStatus === "pending" ? 0.9 : 1.0
                                wrapMode: Text.Wrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.alignment: Qt.AlignTop
                                text: root.statusLabel(todoRow.itemStatus)
                                color: todoRow.itemStatus === "in_progress"
                                    ? Theme.brandPrimary : Theme.textSecondary
                                font.pixelSize: 9
                                font.family: Theme.fontSans
                                opacity: 0.8
                            }
                        }

                        Accessible.role: Accessible.ListItem
                        Accessible.name: (modelData.content || "") + "，"
                                         + root.statusLabel(todoRow.itemStatus)
                    }
                }
            }
        }
    }
}
