import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 设置页的分组卡片：标题 + 副标题 + 内容插槽。
//
// 模型配置页有十来个语义不同的字段，平铺成一条长列时用户分不清哪些属于同一件事，
// 说明文字也只能挂在字段之间抢注意力。卡片给出边界，副标题接管这些说明。
Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    // 卡片内容直接作为子项声明，它们排进 body 这一层 ColumnLayout
    default property alias content: body.data

    radius: 8
    color: Theme.bgSecondary
    border.width: 1
    border.color: Theme.borderSubtle
    implicitHeight: shell.implicitHeight + 24
    // 故意不绑 implicitWidth：卡片宽度由外层 Layout 给（Layout.fillWidth），
    // 而子项（芯片行、Flow）的自然宽度会累加成上千像素的隐式宽度，一旦上报就会
    // 让 ScrollView 的隐式宽度爆掉，整列被压成几十像素宽。

    Accessible.role: Accessible.Grouping
    Accessible.name: root.title

    ColumnLayout {
        id: shell
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 6

        Text {
            visible: root.title !== ""
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.bold: true
            font.family: Theme.fontSans
        }

        Text {
            visible: root.subtitle !== ""
            text: root.subtitle
            Layout.fillWidth: true
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 10
        }
    }
}
