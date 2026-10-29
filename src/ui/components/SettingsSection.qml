import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 设置页的平面分组：组标题 + 可选说明 + 内容插槽。
//
// 与 SettingCard 的分工：
//   * SettingCard 带边框/底色，用于字段少、彼此独立的卡片式页面（模型/网络/日志）；
//   * SettingsSection 无边框，只画标题，用于 Interface/Security 这类字段多、
//     需要连续滚动的长页。两者都只负责排版，不持有任何设置状态。
ColumnLayout {
    id: root

    property string title: ""
    property string description: ""

    // 内容直接作为子项声明，排进 body 这一层 ColumnLayout。
    default property alias content: body.data

    Layout.fillWidth: true
    spacing: 8

    Text {
        visible: root.title !== ""
        text: root.title
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.bold: true
        font.family: Theme.fontMono
    }

    Text {
        visible: root.description !== ""
        Layout.fillWidth: true
        text: root.description
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
        opacity: 0.8
        wrapMode: Text.Wrap
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: 10
    }
}
