import QtQuick
import NexusAI
import NexusAI.ViewModels

// 任务指标行（docs/19 §4.3）：细线分隔的弱化单行，不再作为主节点参与气泡流。
Item {
    id: root

    required property var modelData
    readonly property string text: modelData.title !== undefined ? modelData.title : ""

    implicitHeight: line.height
    height: implicitHeight

    Item {
        id: line
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.avatarSize + Theme.spaceSm
        height: label.implicitHeight

        Rectangle {
            anchors.left: parent.left
            anchors.right: label.left
            anchors.rightMargin: Theme.spaceSm
            anchors.verticalCenter: parent.verticalCenter
            height: 1
            color: Theme.borderSubtle
        }

        Text {
            id: label
            anchors.right: parent.right
            text: root.text
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("任务指标")
    Accessible.description: root.text
}
