// NexusAI.NexusStyle — Button
//
// 结构沿用 Qt Quick Controls Basic/Button.qml：保留 T.Button 模板的
// implicitWidth/implicitHeight 公式、padding、contentItem 与 icon/text/display/
// checked/highlighted/flat 语义，仅替换视觉部分。
//
// 两族配色（docs/44 §2.2）：
//   * 中性按钮：底 surfaceHover，hover 提亮/压暗一档，press 用 controlBackgroundPressed；
//   * 品牌填充（highlighted）：底 brandPrimary，前景 bgPrimary，hover/press 用
//     controlBrandHover/controlBrandPressed。品牌填充态焦点环改用 bgPrimary，
//     否则与同色 brandPrimary 边框重叠不可见。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // 垂直 6 配合 32 高背景；水平 12 与 AppButton 的文字留白一致。
    padding: 6
    horizontalPadding: 12
    spacing: 6

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    icon.width: 16
    icon.height: 16
    icon.color: control.highlighted ? Theme.bgPrimary : Theme.textPrimary

    // 中性按钮默认底就是 surfaceHover，若 hover 仍用同一 token 会毫无反馈。
    // Theme 没有中性 hover 专用 token，按 docs/44 §2.2 的既定口径在本地派生
    // （暗色提亮、亮色压暗）。
    readonly property color neutralHoverColor: Theme.isDark
        ? Qt.lighter(Theme.surfaceHover, 1.20)
        : Qt.darker(Theme.surfaceHover, 1.07)

    // 为什么亮色 press 不能直接用 controlBackgroundPressed：亮色下该 token
    // 为 #DEE2E7，比 hover 的 Qt.darker(surfaceHover, 1.07)（约 #DCDDE0）
    // 更亮，按压反馈方向会反过来（越按越亮）。改为在 hover 基础上再压一档；
    // 暗色下 controlBackgroundPressed(#20202C) 本身足够深，继续沿用。
    readonly property color neutralPressedColor: Theme.isDark
        ? Theme.controlBackgroundPressed
        : Qt.darker(Theme.surfaceHover, 1.14)

    // 禁用用整体透明度而非颜色 token：与现有 204 处 AppButton 的观感保持一致。
    opacity: control.enabled ? 1.0 : 0.5

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon: control.icon
        text: control.text
        font: control.font
        color: control.highlighted ? Theme.bgPrimary : Theme.textPrimary
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: metrics.heightControl
        radius: Theme.radiusField

        // flat 按钮仅在交互/选中时显底，与 Basic 的 flat 语义一致。
        visible: !control.flat || control.down || control.hovered
                 || control.checked || control.highlighted

        color: {
            // 禁用态整体 0.5 透明度即可；品牌填充仍保持品牌色，避免「禁用就变灰」。
            if (!control.enabled)
                return control.highlighted ? Theme.brandPrimary : Theme.surfaceHover
            if (control.highlighted)
                return control.down ? Theme.controlBrandPressed
                     : control.hovered ? Theme.controlBrandHover
                     : Theme.brandPrimary
            if (control.down)
                return control.neutralPressedColor
            if (control.hovered)
                return control.neutralHoverColor
            return Theme.surfaceHover
        }

        border.width: control.visualFocus ? metrics.focusRingWidth : 1
        border.color: {
            if (control.visualFocus)
                return control.highlighted ? Theme.bgPrimary : Theme.brandPrimary
            return control.highlighted ? Theme.brandPrimary : Theme.borderSubtle
        }
    }

    // 按压缩放与 AppButton 一致；reduceMotion 时 Theme.motionFast 为 0，动画瞬时落定。
    scale: control.down ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.motionFast } }

    NexusMetrics { id: metrics }
}