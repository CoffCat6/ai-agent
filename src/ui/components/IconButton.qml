import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Square icon button with hover tint and optional tooltip. Defaults fit the
// 32x32 toolbar pattern; override width/height/radius for compact variants.
// `enabled` is the native Item property: disabling it dims externally (via
// an opacity binding) and the mouse area stops reacting on its own.
Rectangle {
    id: root

    property string icon: ""
    property int iconSize: 16
    property string tooltip: ""
    property color restIconColor: Theme.textSecondary
    property color hoverIconColor: Theme.brandPrimary
    property color restBgColor: "transparent"
    property color hoverBgColor: Theme.surfaceHover
    // 图标切换时做一次 Y 轴翻转。默认关闭：chevron-up/down 这类"展开/收起"
    // 图标在同一个界面里可能同时有多个，全都翻一遍会显得躁动。只在
    // "一个开关、两个面互为正反"的地方开启（侧栏 / 上下文面板）。
    property bool flipOnIconChange: false

    signal clicked()

    width: 32
    height: 32
    radius: 6
    color: controlArea.containsMouse ? root.hoverBgColor : root.restBgColor
    // docs/25 D1: 键盘焦点环
    border.width: root.activeFocus ? 2 : 0
    border.color: Theme.brandPrimary

    // displayIcon 故意写成"可以被打断的值"而不是直接绑定 root.icon：
    // 翻转动画要在横向缩到 0 那一刻写入新图标（此时不可见，切换才不跳），
    // 而给绑定赋值会永久打断绑定，之后 root.icon 再变就不联动了。
    // 同步交给下面的 onIconChanged。
    AppIcon {
        id: iconItem
        anchors.centerIn: parent
        property string displayIcon: root.icon
        icon: displayIcon
        size: root.iconSize
        iconColor: controlArea.containsMouse ? root.hoverIconColor : root.restIconColor

        // Item 没有 scaleX（只有等比 scale，会把图标缩成一个小点而不是压扁），
        // 所以用 Transform 里的 Scale 单独缩 X 轴。origin 固定在中心。
        transform: Scale { id: flipScale; origin.x: iconItem.width / 2; xScale: 1 }
    }

    // 翻转：压扁 → 中点换图 → 展开。两次 NumberAnimation 都不写 from，
    // 这样快速连点时从当前 xScale 继续，不会跳回 1 再压下去（那会闪）。
    SequentialAnimation {
        id: flipAnim
        NumberAnimation {
            target: flipScale
            property: "xScale"
            to: 0
            duration: Theme.motionFast
            easing.type: Easing.InQuad
        }
        ScriptAction { script: iconItem.displayIcon = root.icon }
        NumberAnimation {
            target: flipScale
            property: "xScale"
            to: 1
            duration: Theme.motionFast
            easing.type: Easing.OutQuad
        }
    }

    onIconChanged: {
        if (!root.flipOnIconChange) {
            // 关闭动画时直接同步；若动画正跑到一半（另一个按钮切换了
            // 开关状态），也要把它停回平铺，否则图标会卡在压扁状态。
            flipAnim.stop()
            flipScale.xScale = 1
            iconItem.displayIcon = root.icon
            return
        }
        flipAnim.restart()
    }

    MouseArea {
        id: controlArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // docs/25 D1: 键盘可达（Tab 聚焦 + 回车/空格激活）
    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    ToolTip.visible: root.tooltip.length > 0 && controlArea.containsMouse
    ToolTip.delay: 600
    ToolTip.text: root.tooltip

    Accessible.role: Accessible.Button
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : root.icon
    Accessible.description: root.tooltip
    Accessible.onPressAction: root.clicked()
}
