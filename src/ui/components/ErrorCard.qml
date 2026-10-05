import QtQuick
import NexusAI
import NexusAI.ViewModels

// 错误告示卡（docs/19 §4.3）：危险色左边框 + 浅红底 + 警告图标 + 正文。
Item {
    id: root

    required property var modelData
    readonly property string text: modelData.title !== undefined ? modelData.title : ""

    implicitHeight: card.height
    height: implicitHeight

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.avatarSize + Theme.spaceSm
        radius: Theme.radiusMd
        color: Theme.statusDangerSurface
        border.width: 1
        border.color: Theme.statusDanger
        height: errorCol.implicitHeight + Theme.spaceSm * 2

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: Theme.statusDanger
            topLeftRadius: parent.radius
            bottomLeftRadius: parent.radius
        }

        Row {
            id: errorCol
            anchors.fill: parent
            anchors.margins: Theme.spaceSm
            anchors.leftMargin: Theme.spaceSm + 3
            spacing: Theme.spaceSm

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: "warning"
                size: 16
                iconColor: Theme.statusDanger
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 16 - Theme.spaceSm
                text: root.text
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                renderType: Text.NativeRendering
            }
        }
    }

    Accessible.role: Accessible.Alert
    Accessible.name: qsTr("错误")
    Accessible.description: root.text.substring(0, 80)
}
