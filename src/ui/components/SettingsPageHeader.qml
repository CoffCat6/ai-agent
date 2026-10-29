import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 设置页统一页头：标题 + 一行目的说明。
//
// 之前 16 个页面各写一遍标题/说明，字号、间距、上边距都不同，切分类时
// 标题基线会跳。这里只负责排版，不持有任何设置状态。
ColumnLayout {
    id: root

    property string title: ""
    property string description: ""

    Layout.fillWidth: true
    spacing: Theme.spaceXs
    visible: root.title !== "" || root.description !== ""

    Text {
        visible: root.title !== ""
        Layout.fillWidth: true
        text: root.title
        color: Theme.textPrimary
        font.pixelSize: Theme.fontSizeH2
        font.bold: true
        font.family: Theme.fontSans
        elide: Text.ElideRight
    }

    Text {
        visible: root.description !== ""
        Layout.fillWidth: true
        text: root.description
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
        wrapMode: Text.Wrap
    }
}
