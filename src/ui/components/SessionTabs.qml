import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Session tab bar (Batch 29, F7): one tab per open session. A running task
// shows a brand-colored dot; switching tabs never interrupts it. The last
// remaining tab cannot be closed (AgentVM::closeTab guards that too).
Rectangle {
    id: root

    // 模板菜单的「管理模板…」：本组件不认识设置中心，交给
    // AgentChatPage → main 打开对应分类。
    signal templatesRequested()

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

    // 「+」：左键 = 空白新对话（保持原行为，零成本路径不变）；右键 / 长按
    // = 会话模板菜单。侧栏折叠后模板入口原本彻底不可达，这里是它的替代
    // 路径（标签栏始终可见）。
    Item {
        id: addButton
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(addLabel.implicitWidth, 18)
        height: parent.height

        Text {
            id: addLabel
            anchors.centerIn: parent
            text: "+"
            color: addArea.containsMouse || templateMenu.popped
                ? Theme.brandPrimary : Theme.textSecondary
            font.pixelSize: 16
            font.family: Theme.fontSans
        }

        MouseArea {
            id: addArea
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            // docs/17: 标签栏「+」默认新建普通对话；项目会话只从项目行「+」建。
            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton)
                    templateMenu.openBelow(addButton)
                else
                    AgentVM.newLocalChat()
            }
            // 长按同样开模板菜单（触屏/无右键场景）。
            onPressAndHold: templateMenu.openBelow(addButton)
        }

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("新建对话标签页")
        Accessible.description: qsTr("点击新建空白对话；右键或长按从会话模板开始")
        Accessible.onPressAction: AgentVM.newLocalChat()
    }

    SessionTemplateMenu {
        id: templateMenu
        // 标签栏不认识设置中心；转成信号交给 AgentChatPage → main 处理。
        onManageRequested: root.templatesRequested()
    }
}
