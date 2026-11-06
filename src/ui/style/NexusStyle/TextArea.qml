// NexusAI.NexusStyle — TextArea
//
// 结构沿用 Qt Quick Controls Basic/TextArea.qml：保留 T.TextArea 模板的
// implicitWidth/implicitHeight 公式、placeholderText 渲染与右键编辑菜单，
// 输入语义（readOnly/echoMode 等）全部由模板负责。
//
// 与 TextField 同族，差异只在几何：最小高 64（由 background 隐式高兜底，
// 内容多时仍随 implicitHeight 公式增长），上下 padding 6、左右 12
// （上下与 Basic 原行为对齐，避免局部 background 覆盖时比 Basic 多出 4px）。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Basic.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.TextArea {
    id: control

    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding,
                            implicitBackgroundWidth + leftInset + rightInset,
                            placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight + topInset + bottomInset,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    // 上下 6、左右 12，与 Basic 原行为对齐。
    //
    // 为什么上下不用 8：业务局部覆盖 background 时没有 implicitHeight，
    // implicitHeight 会退化为 contentHeight + topPadding + bottomPadding。
    // 若沿用 8+8 会比 Basic 多出 4px，导致同一行内输入框高度不一致。
    padding: 6
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceMd

    hoverEnabled: true

    // 与 TextField 同理：T.TextArea 只有 pressed 信号，没有 pressed 属性。
    property bool pressActive: false
    onPressed: control.pressActive = true
    onReleased: control.pressActive = false
    onEnabledChanged: {
        if (!control.enabled)
            control.pressActive = false
    }

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
    placeholderTextColor: control.enabled ? Theme.textSecondary : Theme.controlTextDisabled
    selectionColor: Theme.brandPrimary
    selectedTextColor: Theme.selectedTextColor

    // 同 TextField：附加属性必须经 QtQuick.Templates 别名限定。
    T.ContextMenu.menu: TextEditingContextMenu {
        control: control
    }

    PlaceholderText {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)

        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        elide: Text.ElideRight
        renderType: control.renderType
    }

    background: Rectangle {
        implicitWidth: 200
        // 最小高 64；内容增长时 implicitHeight 公式取 contentHeight 更大值。
        implicitHeight: metrics.heightTextAreaMin
        radius: Theme.radiusField

        color: {
            if (!control.enabled)
                return Theme.controlBackgroundDisabled
            if (control.pressActive)
                return Theme.controlBackgroundPressed
            if (control.hovered)
                return Theme.surfaceHover
            return Theme.bgPrimary
        }

        border.width: control.activeFocus ? metrics.focusRingWidth : 1
        border.color: {
            if (!control.enabled)
                return Theme.controlBorderDisabled
            if (control.activeFocus)
                return Theme.brandPrimary
            if (control.pressActive || control.hovered)
                return Theme.cardBorder
            return Theme.borderSubtle
        }
    }

    NexusMetrics { id: metrics }
}