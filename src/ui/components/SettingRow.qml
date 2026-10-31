import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
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
    // docs/31 O1: 行级"已修改"标识与单键复原。
    //
    // 键名默认取 objectName——设置页几乎每行都已经为了搜索跳转写了同名
    // 锚点。没有登记为目录条目的行（纯入口按钮）会得到 known=false，
    // 什么也不显示。
    property string settingKey: ""
    readonly property string settingId: settingKey.length > 0 ? settingKey : objectName
    // 先读 revision 建立依赖：SettingsCatalog.describe() 是 Q_INVOKABLE，
    // 没有变更通知，不显式依赖就只会在创建时求值一次。
    readonly property var valueState: {
        SettingsCatalog.revision
        return SettingsCatalog.describe(settingId)
    }

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
            id: labelText

            Layout.fillWidth: true
            text: root.label
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            elide: Text.ElideRight

            // 英文/长标签被省略时用 tooltip 补全，避免只能看到半截。
            HoverHandler { id: labelHover }
            ToolTip.visible: labelText.truncated && labelHover.hovered
            ToolTip.text: root.label
            ToolTip.delay: 500
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

    // 修改状态槽：宽度恒定，避免值一改就让整行的控件左右跳动。
    Item {
        id: statusSlot
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: 24
        Layout.preferredHeight: 24

        IconButton {
            anchors.fill: parent
            // 可复原才给按钮；密钥/凭据等无复位通道的条目只留圆点提示。
            visible: root.valueState.modified === true
                     && root.valueState.revertable === true
            icon: "undo"
            iconSize: 12
            tooltip: qsTr("恢复默认值")
            restIconColor: Theme.brandPrimary
            hoverIconColor: Theme.brandPrimary
            hoverBgColor: Theme.surfaceHover
            Accessible.name: qsTr("将「%1」恢复为默认值").arg(root.label)
            onClicked: SettingsCatalog.resetValue(root.settingId)
        }

        Rectangle {
            anchors.centerIn: parent
            visible: root.valueState.modified === true
                     && root.valueState.revertable !== true
            width: 6
            height: 6
            radius: width / 2
            color: Theme.brandPrimary
            Accessible.ignored: true
        }
    }
}
