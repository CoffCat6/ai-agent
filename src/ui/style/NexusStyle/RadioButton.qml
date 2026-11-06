// NexusAI.NexusStyle — RadioButton（docs/44 §2.3.6）
//
// 结构沿用 Qt Quick Controls Basic/RadioButton.qml：保留 T.RadioButton 模板的
// implicitWidth/implicitHeight、padding、indicator/contentItem 语义，以及
// checked/down/hovered/visualFocus/enabled/mirrored/text 等模板属性。
//
// 与 CheckBox 的差异只在形状：指示器 16×16 圆形、选中标记为 6px 实心圆点。
// 四态取值、focus 环、禁用口径与 CheckBox 保持一致，避免同屏两种选择控件
// 出现两套深浅。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.RadioButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    // 同 CheckBox：24px 下限保证单行时与 32px 控件行高对齐。
    implicitHeight: Math.max(24,
                             implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    spacing: Theme.spaceSm

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    NexusMetrics { id: metrics }

    readonly property color indicatorColor: !control.enabled ? Theme.controlBackgroundDisabled
                                           : control.checked
                                             ? (control.down ? Theme.controlBrandPressed
                                                             : control.hovered ? Theme.controlBrandHover
                                                                               : Theme.brandPrimary)
                                             : control.down ? Theme.controlBackgroundPressed
                                                            : control.hovered ? Theme.surfaceHover
                                                                              : "transparent"

    readonly property color indicatorBorderColor: !control.enabled ? Theme.controlBorderDisabled
                                                 : control.checked ? Theme.brandPrimary
                                                 : control.hovered ? Theme.cardBorder
                                                                   : Theme.textTertiary

    readonly property color markColor: control.enabled ? Theme.bgPrimary : Theme.controlTextDisabled

    indicator: Rectangle {
        implicitWidth: metrics.sizeCheckIndicator
        implicitHeight: metrics.sizeCheckIndicator

        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2

        radius: width / 2
        color: control.indicatorColor
        border.width: 1
        border.color: control.indicatorBorderColor

        // 焦点环：圆形外扩 2px，与 CheckBox 同构。
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

        // 选中标记：6px 实心圆（Basic 用 20px，16px 框内会溢出，故按规格缩小）。
        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: width / 2
            visible: control.checked
            color: control.markColor
        }
    }

    contentItem: CheckLabel {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0

        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
    }
}