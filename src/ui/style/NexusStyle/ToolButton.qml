// NexusAI.NexusStyle — ToolButton
//
// 结构沿用 Qt Quick Controls Basic/ToolButton.qml：保留 T.ToolButton 模板的
// implicitWidth/implicitHeight 公式、padding、contentItem 与 icon/text/display/
// checked/flat 语义，仅替换视觉部分。
//
// 工具栏语义：默认无底，hover 才浮出 surfaceHover，因此不会在密集工具栏里
// 形成一排"按钮块"。纯图标时固定 32×32；带文字时改为左右 12 padding 自然撑宽。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.ToolButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            control.text.length > 0 ? 0 : metrics.heightControl)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    horizontalPadding: control.text.length > 0 ? 12 : 6
    spacing: 6

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    icon.width: metrics.sizeIcon
    icon.height: metrics.sizeIcon
    // hover/press/checked 时图标转品牌色，给工具栏按钮一个明确的可交互反馈；
    // 禁用态由根 opacity 统一压暗，这里不再套第二层禁用色。
    icon.color: (control.hovered || control.down || control.checked || control.highlighted)
                ? Theme.brandPrimary : Theme.textSecondary

    // 禁用与 Button 同口径：整体 0.5，避免 content/background 分别半透明后叠加变淡。
    opacity: control.enabled ? 1.0 : 0.5

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon: control.icon
        text: control.text
        font: control.font
        // 只有图标在 hover/press 时转品牌色（任务口径），文字仍保持正文色。
        color: Theme.textPrimary
    }

    background: Rectangle {
        implicitWidth: metrics.heightControl
        implicitHeight: metrics.heightControl
        radius: Theme.radiusField

        color: {
            if (!control.enabled)
                return "transparent"
            if (control.down)
                return Theme.controlBackgroundPressed
            if (control.hovered || control.checked || control.highlighted)
                return Theme.surfaceHover
            return "transparent"
        }

        border.width: control.visualFocus ? metrics.focusRingWidth : 0
        border.color: Theme.brandPrimary
    }

    NexusMetrics { id: metrics }
}