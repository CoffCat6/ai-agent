import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 设置页的一行：左侧标签/说明，右侧控件。
//
// 原来每个页面都手写一遍 RowLayout + 标签 Text + 控件，标签宽度、说明字号和
// 行距在各页间漂移。这里把布局收成一处：控件声明为默认子项（进 controlBox），
// 宽度由控件自己的 Layout.preferredWidth 决定；标签列吃掉剩余空间，因此同一
// 页里所有控件的右边界自动对齐。
//
// 多控件行（如滑块 + 数值框）直接并列声明；需要整行输入框或标签在上的布局时
// 用 SettingsField / 自定义块，不要硬塞进这里。
//
// 读屏名称仍由控件自己设置（Accessible.name）——根节点的 name 不会传给真正
// 接收焦点的子控件（docs/25 D3）。
RowLayout {
    id: root

    property string label: ""
    property string description: ""

    default property alias content: controlBox.data

    Layout.fillWidth: true
    spacing: Theme.spaceMd

    Accessible.role: Accessible.Grouping
    Accessible.name: root.label

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: Theme.spaceXs

        Text {
            Layout.fillWidth: true
            text: root.label
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
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

    ColumnLayout {
        id: controlBox
        Layout.alignment: Qt.AlignVCenter
        spacing: Theme.spaceXs
    }
}
