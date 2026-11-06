// NexusAI.NexusStyle — Switch（docs/44 §2.3.5）
//
// 结构沿用 Qt Quick Controls Basic/Switch.qml：保留 T.Switch 模板的
// implicitWidth/implicitHeight 公式、padding、indicator/contentItem 语义，
// 以及 checked/checkState/tristate/down/hovered/visualFocus/enabled/
// mirrored/text 等全部模板属性，仅替换视觉。
//
// 几何：轨道 36×20（圆角 10）、滑块 16×16（圆角 8，垂直居中）。
// 滑块 x 用 visualPosition 推算（关 2 → 开 18），而非直接写 checked 分支，
// 这样模板在 mirrored（RTL）下自动反转方向，且快速连点时动画起点连续。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.Switch {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    // 轨道 20 高 + 上下 2 = 24（docs/44 §2.3.5 的控件隐式高）。
    padding: 2
    spacing: Theme.spaceSm

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    NexusMetrics { id: metrics }

    // 轨道：开态走品牌色阶（hover/press 用新 token，保证与 brandPrimary
    // 明度差可见）；关态用 textTertiary 而不是 cardBorder——卡片/页面底色
    // 在暗色下与 cardBorder 过于接近，会看不出“轨道存在”。
    //
    // 禁用态刻意不用 opacity：CheckBox/RadioButton 同样改用三 token
    // （controlBorderDisabled / controlTextDisabled），三者口径一致，
    // 避免父级已有透明度时再乘一次导致“灰上加灰”（docs/44 §2.2 注）。
    readonly property color trackColor: !control.enabled
                                       ? Theme.controlBorderDisabled
                                       : control.checked
                                         ? (control.down ? Theme.controlBrandPressed
                                                         : control.hovered ? Theme.controlBrandHover
                                                                           : Theme.brandPrimary)
                                         : Theme.textTertiary

    // 滑块：开态 bgPrimary（与品牌填充按钮的前景同族）；关态 cardBackground。
    // 对比度核算：暗色 #111118 对轨道 #6E6E85 ≈ 3.8:1，亮色 #FFFFFF 对
    // #8A9099 ≈ 3.2:1，均达到非文本 3:1，因此暗色无需退化为 textPrimary。
    readonly property color thumbColor: !control.enabled
                                       ? Theme.controlTextDisabled
                                       : control.checked ? Theme.bgPrimary : Theme.cardBackground

    indicator: Item {
        implicitWidth: metrics.widthSwitchTrack
        implicitHeight: metrics.heightSwitchTrack

        // 与 Basic 相同的定位公式：有文字时靠左（镜像靠右），无文字时居中。
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2

        // 焦点环外扩 2px：作为 indicator 的子项绘制，但不计入 implicitWidth，
        // 因此不会挤压文字或撑大控件（docs/44 §2.3.5 明确要求“外扩不挤压布局”）。
        Rectangle {
            x: -metrics.focusRingWidth
            y: -metrics.focusRingWidth
            width: parent.width + metrics.focusRingWidth * 2
            height: parent.height + metrics.focusRingWidth * 2
            radius: width / 2
            color: "transparent"
            border.width: metrics.focusRingWidth
            border.color: Theme.brandPrimary
            visible: control.visualFocus
        }

        Rectangle {
            anchors.fill: parent
            radius: metrics.radiusSwitchTrack
            color: control.trackColor
            Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        }

        Rectangle {
            width: metrics.sizeSwitchThumb
            height: metrics.sizeSwitchThumb
            radius: width / 2
            y: (parent.height - height) / 2
            // 左右各留 2px 内边距：36 - 16 - 2*2 = 16px 行程。
            x: 2 + control.visualPosition * (parent.width - width - 4)
            color: control.thumbColor
            // hover 描边：仅在可交互时提示“可拨动”，按下后由 scale 反馈。
            border.width: control.enabled && control.hovered && !control.down ? 1 : 0
            border.color: Theme.brandPrimary
            scale: control.down ? 0.92 : 1.0

            Behavior on x {
                NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic }
            }
            Behavior on scale { NumberAnimation { duration: Theme.motionFast } }
            Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        }
    }

    // 文本布局完全复用 Basic 的 CheckLabel：指示器在左/右时给文字让位，
    // 这样 mirrored 下文字与滑块不会重叠。
    contentItem: CheckLabel {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0

        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
    }
}