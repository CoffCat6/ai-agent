import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 统一空态：可选图标 + 标题 + 说明 + 动作区。
//
// 调用方负责定位与宽度（anchors.centerIn / Layout.fillWidth），组件自身只排
// 内容，不假设宿主是 Column、ListView 还是 Flickable。
//
// 此前各页各写一段 Text + 自己的字号/颜色；这里把字号、间距、居中与读屏语义
// 收成一处。动作按钮通过默认属性声明，例如：
//     EmptyState {
//         title: qsTr("暂无记录")
//         AppButton { text: qsTr("刷新"); onClicked: ... }
//     }
ColumnLayout {
    id: root

    property string icon: ""
    property string title: ""
    property string description: ""

    default property alias actions: actionRow.data

    spacing: Theme.spaceSm

    AppIcon {
        visible: root.icon !== ""
        icon: root.icon
        size: 24
        iconColor: Theme.textTertiary
        Layout.alignment: Qt.AlignHCenter
    }

    Text {
        visible: root.title !== ""
        Layout.fillWidth: true
        text: root.title
        color: Theme.textPrimary
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    Text {
        visible: root.description !== ""
        Layout.fillWidth: true
        text: root.description
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    RowLayout {
        id: actionRow
        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.spaceSm
    }
}
