// NexusAI.NexusStyle — MenuItem
//
// 结构沿用 Basic/MenuItem.qml：保留 implicitWidth/implicitHeight 公式、padding、
// IconLabel(contentItem)、indicator/arrow 的镜像定位，以及 highlighted /
// pressed(down) / hovered / visualFocus / checkable / checked / subMenu 语义。
//
// 规格：行高 30、左右内边距 12、圆角 4；默认透明底、hover surfaceHover、
// press controlBackgroundPressed；禁用文字 controlTextDisabled；
// 勾选标记与子菜单箭头分别用 brandPrimary / textSecondary。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.MenuItem {
    id: control

    // 菜单项几何常量（30 行高、16 图标）与主题无关，集中取自 NexusMetrics。
    NexusMetrics { id: metrics }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    // verticalPadding 6 + 18px 文字行高 ≈ 30，由 background.implicitHeight 兜底。
    padding: 6
    horizontalPadding: Theme.spaceMd
    spacing: 6

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    icon.width: metrics.sizeIcon
    icon.height: metrics.sizeIcon
    // 图标默认次级色；悬停/高亮/按压时提升为主文本色，禁用时用禁用色。
    icon.color: !control.enabled ? Theme.controlTextDisabled
                : (control.hovered || control.highlighted || control.down)
                  ? Theme.textPrimary : Theme.textSecondary

    contentItem: IconLabel {
        // 右侧为快捷键文字预留宽度，避免长文本覆盖快捷键。
        readonly property real arrowPadding: control.subMenu && control.arrow
                                             ? control.arrow.width + control.spacing : 0
        readonly property real indicatorPadding: control.checkable && control.indicator
                                                 ? control.indicator.width + control.spacing : 0
        readonly property real shortcutPadding: shortcutLabel.visible
                                                ? shortcutLabel.implicitWidth + control.spacing : 0

        leftPadding: !control.mirrored ? indicatorPadding : arrowPadding
        rightPadding: (control.mirrored ? indicatorPadding : arrowPadding) + shortcutPadding

        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: Qt.AlignLeft

        icon: control.icon
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
    }

    // T.MenuItem 没有 shortcut 属性；按 Qt 菜单惯例从关联 Action 的 shortcut 读取。
    // 业务未绑定 action 时保持不可见，不改变既有菜单布局。
    Text {
        id: shortcutLabel
        z: 2
        readonly property string shortcutText: control.action && control.action.shortcut
                                              ? String(control.action.shortcut) : ""

        visible: shortcutText.length > 0 && !control.subMenu
        text: shortcutText
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontSizeCaption
        color: control.enabled ? Theme.textSecondary : Theme.controlTextDisabled

        anchors.right: parent.right
        anchors.rightMargin: control.rightPadding
                             + (control.subMenu && control.arrow
                                ? control.arrow.width + control.spacing : 0)
                             + Theme.spaceSm
        anchors.verticalCenter: parent.verticalCenter
    }

    // 勾选标记：12px 字符勾，品牌色。位置沿用 Basic 的左侧（indicator 列），
    // 与右侧 subMenu 箭头互不挤占。
    indicator: Text {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2

        // 与 Qt Basic 的 `visible: checked` 对齐：业务若只设 checked 而未显式
        // 设 checkable，勾选标记也必须显示，否则选项会被误认为未选中。
        visible: control.checked
        text: "\u2713"
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontSizeCaption
        color: control.enabled ? Theme.brandPrimary : Theme.controlTextDisabled
    }

    // 子菜单箭头：字符 chevron，避免依赖 Basic 的 PNG 资源。
    arrow: Text {
        x: control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2

        visible: !!control.subMenu
        text: control.mirrored ? "\u2039" : "\u203A"
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontSizeBody
        color: control.enabled ? Theme.textSecondary : Theme.controlTextDisabled
    }

    background: Rectangle {
        // 宽度随内容（不写死 200），由 Menu 的最小宽 160 兜底。
        implicitWidth: 0
        implicitHeight: metrics.heightOption
        radius: Theme.radiusSm

        color: !control.enabled ? "transparent"
               : control.down ? Theme.controlBackgroundPressed
               : (control.hovered || control.highlighted) ? Theme.surfaceHover
               : "transparent"

        // 键盘焦点：1px 品牌色内描边，不改变行高与布局。
        border.width: control.visualFocus ? 1 : 0
        border.color: Theme.brandPrimary
    }
}