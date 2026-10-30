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
//
// 折叠：`collapsible: true` 时标题行是键盘可达的按钮，`collapsed` 控制说明与
// 内容显隐；标题右侧的 `badge` 可放计数（如「14 个 · 已启用 13」），折叠后
// 仍然可见。默认 `collapsible: false`，既有页面行为不变。
ColumnLayout {
    id: root

    property string title: ""
    property string description: ""
    property string badge: ""
    property bool collapsible: false
    property bool collapsed: false

    // 内容直接作为子项声明，排进 body 这一层 ColumnLayout。
    default property alias content: body.data

    function toggle() {
        if (root.collapsible) root.collapsed = !root.collapsed
    }

    Layout.fillWidth: true
    spacing: Theme.spaceSm

    // 标题行。可折叠时整行可点、可聚焦；焦点环画在外层 Item 上
    // （RowLayout 的子项不能用 fill 锚点，会与布局冲突）。
    Item {
        id: headingItem
        visible: root.title !== ""
        Layout.fillWidth: true
        implicitHeight: heading.implicitHeight

        activeFocusOnTab: root.collapsible
        Accessible.role: root.collapsible ? Accessible.Button : Accessible.StaticText
        Accessible.name: root.title
        Accessible.description: root.collapsible
            ? (root.collapsed ? qsTr("已折叠，按空格展开") : qsTr("已展开，按空格折叠"))
            : ""
        Accessible.onPressAction: root.toggle()

        Keys.onReturnPressed: root.toggle()
        Keys.onSpacePressed: root.toggle()

        RowLayout {
            id: heading
            anchors.fill: parent
            spacing: Theme.spaceXs

            AppIcon {
                visible: root.collapsible
                icon: root.collapsed ? "chevron-right" : "chevron-down"
                size: 12
                iconColor: Theme.textSecondary
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                Layout.fillWidth: true
                text: root.title
                color: root.collapsible && headingMouse.containsMouse
                    ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
                font.letterSpacing: 0.4
                elide: Text.ElideRight
            }

            Text {
                visible: root.badge !== ""
                text: root.badge
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: "transparent"
            border.width: headingItem.activeFocus ? 1 : 0
            border.color: Theme.brandPrimary
        }

        MouseArea {
            id: headingMouse
            anchors.fill: parent
            enabled: root.collapsible
            hoverEnabled: true
            cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                headingItem.forceActiveFocus()
                root.toggle()
            }
        }
    }

    Text {
        visible: root.description !== ""
                 && (!root.collapsible || !root.collapsed)
        Layout.fillWidth: true
        text: root.description
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
        wrapMode: Text.Wrap
    }

    ColumnLayout {
        id: body
        visible: !root.collapsible || !root.collapsed
        Layout.fillWidth: true
        spacing: Theme.spaceMd
    }
}
