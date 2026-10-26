import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Session tab bar (Batch 29, F7): one tab per open session. A running task
// shows a brand-colored dot; switching tabs never interrupts it. The last
// remaining tab cannot be closed (AgentVM::closeTab guards that too).
Rectangle {
    id: root

    implicitHeight: 34
    color: Theme.bgSecondary

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 34
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        contentWidth: tabsRow.width
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: tabsRow
            spacing: 4

            Repeater {
                model: AgentVM.tabModel

                delegate: Rectangle {
                    width: tabsContent.width + 20
                             + (closeButton.visible ? closeButton.width : 0)
                    height: 26
                    radius: 4
                    color: model.modelData.active ? Theme.surfaceHover : "transparent"

                    Accessible.role: Accessible.PageTab
                    Accessible.name: model.modelData.title

                    MouseArea {
                        anchors.fill: parent
                        onClicked: AgentVM.activateTab(model.modelData.index)
                    }

                    Row {
                        id: tabsContent
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        spacing: 6

                        Rectangle {
                            // Running-task indicator
                            width: 6
                            height: 6
                            radius: 3
                            anchors.verticalCenter: parent.verticalCenter
                            visible: model.modelData.busy
                            color: Theme.brandPrimary
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.modelData.title
                            color: model.modelData.active
                                       ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, 160)
                        }
                    }

                    Text {
                        id: closeButton
                        visible: AgentVM.tabModel.length > 1
                        text: "\u00d7"
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: closeArea.containsMouse
                                   ? Theme.statusDanger : Theme.textSecondary
                        font.pixelSize: 14

                        MouseArea {
                            id: closeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: AgentVM.closeTab(model.modelData.index)
                        }
                    }
                }
            }

        }

        // Many open tabs overflow this strip; Shift+wheel pans it like the
        // other horizontally-overflowing views. Flickable child (a sibling
        // AFTER the Row — positioners forbid fill anchors on their children)
        // and topmost, so it sees wheel events before the delegates;
        // NoButton keeps tab clicks and drag-flick untouched.
        HWheelScroll {}
    }

    Text {
        id: addButton
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: "+"
        color: addArea.containsMouse ? Theme.brandPrimary : Theme.textSecondary
        font.pixelSize: 16
        font.family: Theme.fontSans

        MouseArea {
            id: addArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // docs/17: 标签栏「+」同样默认新建普通对话；项目会话只从项目行「+」建。
            onClicked: AgentVM.newLocalChat()
        }

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("新建对话标签页")
        Accessible.onPressAction: AgentVM.newLocalChat()
    }
}
