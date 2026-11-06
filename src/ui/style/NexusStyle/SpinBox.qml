// NexusAI.NexusStyle — SpinBox（docs/44 §2.3.8）
//
// 结构沿用 Qt Quick Controls Basic/SpinBox.qml：保留 T.SpinBox 模板的
// implicitWidth/implicitHeight 公式、validator、contentItem 语义，以及
// editable / value / from / to / stepSize / wrap / live / increase() /
// decrease() / displayText 等全部行为——本文件只改视觉，不改数值逻辑。
//
// 与 Basic 的布局差异：Basic 的 up/down 是“左右各一列整高按钮”，本方案按
// 规格改成右侧“上下各 16px”的按钮列，因此 leftPadding/rightPadding 直接
// 按按钮列宽度计算（Basic 的 padding 公式对应的是左右双列，不能照搬）。
import QtQuick
import QtQuick.Templates as T
import NexusAI.ViewModels

T.SpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             up.implicitIndicatorHeight, down.implicitIndicatorHeight)

    // 上下 padding 归零：高度由 32px 背景决定，避免 padding + 行高把控件撑高。
    topPadding: 0
    bottomPadding: 0
    // 非按钮侧 12px；按钮侧 12 + 20，为上下按钮列让位。mirrored 时左右互换。
    leftPadding: Theme.spaceMd + (control.mirrored ? metrics.widthSpinButtons : 0)
    rightPadding: Theme.spaceMd + (control.mirrored ? 0 : metrics.widthSpinButtons)

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    NexusMetrics { id: metrics }

    // SpinBox 本身没有“整体按下”状态：用任一按钮的 pressed 代表按压态，
    // 这样点加减按钮时输入框底色与 TextField 的按压态保持一致。
    readonly property bool pressed: control.up.pressed || control.down.pressed
    // 编辑态焦点可能落在 contentItem(TextInput) 上，因此三个来源取并集，
    // 保证“点进输入框”与“Tab 聚焦控件”都能画出品牌色焦点框。
    readonly property bool focused: control.visualFocus || control.activeFocus
                                    || (control.contentItem && control.contentItem.activeFocus)

    // 上下按钮底色：透明 → hover surfaceHover → press controlBackgroundPressed。
    // 禁用时保持透明，由边框/文字 token 表达。
    function buttonColor(button) {
        if (!control.enabled)
            return "transparent"
        if (button.pressed)
            return Theme.controlBackgroundPressed
        if (button.hovered)
            return Theme.surfaceHover
        return "transparent"
    }

    // 箭头颜色：hover 时提亮到 textPrimary，禁用时 controlTextDisabled。
    function arrowColor(button) {
        if (!control.enabled)
            return Theme.controlTextDisabled
        return button.hovered ? Theme.textPrimary : Theme.textSecondary
    }

    // 保留 Basic 的校验器：限制输入落在 [from, to] 内（locale 用于本地化数字）。
    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        clip: width < implicitWidth
        padding: 0

        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
        selectionColor: Theme.brandPrimary
        selectedTextColor: Theme.selectedTextColor
        // 保留 Basic 的居中排版：数值在加减按钮左侧的可用区内居中。
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter

        // 关键行为：editable=false 时只读，仍可由 up/down 改值。
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: 0
        implicitWidth: metrics.widthSpinButtons
        implicitHeight: metrics.heightSpinButton
        width: metrics.widthSpinButtons
        height: metrics.heightSpinButton

        // 上按钮只圆右上角（镜像时左上），与 32px 外框的 6px 圆角贴合。
        // topRightRadius 等分角属性是 Qt 6.7+ API；本工程运行环境为 Qt 6.9。
        topLeftRadius: control.mirrored ? Theme.radiusField : 0
        topRightRadius: control.mirrored ? 0 : Theme.radiusField
        bottomLeftRadius: 0
        bottomRightRadius: 0

        color: control.buttonColor(control.up)

        Text {
            anchors.centerIn: parent
            text: "▲"
            font.family: Theme.fontSans
            font.pixelSize: 8
            color: control.arrowColor(control.up)
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: control.height - height
        implicitWidth: metrics.widthSpinButtons
        implicitHeight: metrics.heightSpinButton
        width: metrics.widthSpinButtons
        height: metrics.heightSpinButton

        // 下按钮只圆右下角（镜像时左下）。
        topLeftRadius: 0
        topRightRadius: 0
        bottomLeftRadius: control.mirrored ? Theme.radiusField : 0
        bottomRightRadius: control.mirrored ? 0 : Theme.radiusField

        color: control.buttonColor(control.down)

        // 1px 分隔线：画在子项层，buttons hover/press 时仍可见。
        Rectangle {
            width: parent.width
            height: 1
            color: control.enabled ? Theme.borderSubtle : Theme.controlBorderDisabled
        }

        Text {
            anchors.centerIn: parent
            text: "▼"
            font.family: Theme.fontSans
            font.pixelSize: 8
            color: control.arrowColor(control.down)
        }
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: metrics.heightControl
        radius: Theme.radiusField

        // 四态与 TextField 对齐（docs/44 §2.2）：
        // 默认 bgPrimary + borderSubtle；hover surfaceHover + cardBorder；
        // 按压 controlBackgroundPressed + cardBorder；禁用三 token。
        color: !control.enabled ? Theme.controlBackgroundDisabled
             : control.pressed ? Theme.controlBackgroundPressed
             : control.hovered ? Theme.surfaceHover
                               : Theme.bgPrimary

        border.width: control.focused ? metrics.focusRingWidth : 1
        border.color: !control.enabled ? Theme.controlBorderDisabled
                    : control.focused ? Theme.brandPrimary
                    : control.hovered || control.pressed ? Theme.cardBorder
                                                         : Theme.borderSubtle
    }
}