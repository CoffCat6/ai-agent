// NexusAI.NexusStyle — CheckBox（docs/44 §2.3.6）
//
// 结构沿用 Qt Quick Controls Basic/CheckBox.qml：保留 T.CheckBox 模板的
// implicitWidth/implicitHeight、padding、indicator/contentItem 语义，以及
// checked/checkState/tristate/down/hovered/visualFocus/enabled/mirrored/
// text 等模板属性；三态（Qt.PartiallyChecked）也按 Basic 行为保留。
//
// 视觉：指示器 16×16、圆角 radiusSm(4)；未选透明底 + 1px textTertiary，
// 选中品牌填充 + bgPrimary 标记，焦点环外扩 2px，不改变布局尺寸。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    // 在模板公式上补 24 的下限：16px 指示器 + 上下 4px padding 恰好 24，
    // 而正文行高可能因用户字号大于 16，此时仍允许内容自然撑高。
    implicitHeight: Math.max(24,
                             implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    spacing: Theme.spaceSm

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    NexusMetrics { id: metrics }

    readonly property bool partiallyChecked: control.checkState === Qt.PartiallyChecked
    readonly property bool active: control.checked || partiallyChecked

    // 底/边框分状态取值：
    // - 禁用：显式三 token（不用 opacity，避免与父级透明度叠加后“灰上加灰”，
    //   且与 Switch 的禁用口径保持一致）；
    // - 未选：透明底 + textTertiary 边框（白卡上 3.2:1，满足非文本 3:1）；
    // - hover/press：只作用于未选态，选中态用品牌色阶，避免把“已选”画成灰底。
    readonly property color indicatorColor: !control.enabled ? Theme.controlBackgroundDisabled
                                           : control.active
                                             ? (control.down ? Theme.controlBrandPressed
                                                             : control.hovered ? Theme.controlBrandHover
                                                                               : Theme.brandPrimary)
                                             : control.down ? Theme.controlBackgroundPressed
                                                            : control.hovered ? Theme.surfaceHover
                                                                              : "transparent"

    readonly property color indicatorBorderColor: !control.enabled ? Theme.controlBorderDisabled
                                                 : control.active ? Theme.brandPrimary
                                                 : control.hovered ? Theme.cardBorder
                                                                   : Theme.textTertiary

    // 标记前景：选中/半选状态下的勾或横杠。禁用时用 controlTextDisabled，
    // 正常态用 bgPrimary（暗色深底、亮色白字，与品牌填充按钮同一对比口径）。
    readonly property color markColor: control.enabled ? Theme.bgPrimary : Theme.controlTextDisabled

    indicator: Rectangle {
        implicitWidth: metrics.sizeCheckIndicator
        implicitHeight: metrics.sizeCheckIndicator

        // 与 Basic 一致：有文字时靠左（镜像靠右），无文字时在可用区居中。
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2

        radius: Theme.radiusSm
        color: control.indicatorColor
        border.width: 1
        border.color: control.indicatorBorderColor

        // 焦点环：作为指示器子项外扩 2px 绘制，不参与 implicit 尺寸计算。
        Rectangle {
            x: -metrics.focusRingWidth
            y: -metrics.focusRingWidth
            width: parent.width + metrics.focusRingWidth * 2
            height: parent.height + metrics.focusRingWidth * 2
            radius: Theme.radiusSm + metrics.focusRingWidth
            color: "transparent"
            border.width: metrics.focusRingWidth
            border.color: Theme.brandPrimary
            visible: control.visualFocus
        }

        // 勾选标记：用 "✓" 字形而不是外链图片，避免样式模块额外依赖 qrc 资源；
        // 11px 在 16px 框内视觉尺寸接近 2px 折线，且随系统字体自动抗锯齿。
        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checkState === Qt.Checked
            font.family: Theme.fontSans
            font.pixelSize: 11
            color: control.markColor
        }

        // 半选（tristate）横杠：Basic 的 CheckBox 同样画横条，尺寸取 8×2。
        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 2
            radius: 1
            visible: control.checkState === Qt.PartiallyChecked
            color: control.markColor
        }
    }

    // 文本让位逻辑与 Basic 完全一致（mirrored 时指示器在右）。
    contentItem: CheckLabel {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0

        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
    }
}