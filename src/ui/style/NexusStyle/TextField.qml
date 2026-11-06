// NexusAI.NexusStyle — TextField
//
// 结构沿用 Qt Quick Controls Basic/TextField.qml：保留 T.TextField 模板的
// implicitWidth/implicitHeight 公式、placeholderText 渲染、右键编辑菜单与
// echoMode/validator/readOnly 等输入语义，仅替换视觉部分。
//
// 四态（docs/44 §2.3.7）：默认 bgPrimary + borderSubtle；hover surfaceHover +
// cardBorder；press controlBackgroundPressed + cardBorder；focus 2px brandPrimary。
// T.TextField 只提供 hovered 与 pressed 信号（无 pressed 属性），因此用局部
// pressActive 跟随按下/释放信号，避免为视觉状态引入额外输入控件。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Basic.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.TextField {
    id: control

    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
                   || Math.max(contentWidth, placeholder.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    // 上下 6、左右 12，与 Basic 原行为一致。
    //
    // 为什么必须保留 6 的上下 padding：业务有 29 处局部 `background: Rectangle`
    // 覆盖（如 SettingsField.qml:114、McpServerEditor.qml:183），局部背景没有
    // implicitHeight，此时 T.TextField 的 implicitHeight 退化为
    // contentHeight + topPadding + bottomPadding。若上下 padding 为 0，输入框
    // 只有约 17~20px 会被压塌；保留 6+6 后恢复到约 29px，与新样式默认背景的
    // 32px 兜底并存不冲突（默认 background.implicitHeight 仍为 heightControl）。
    padding: 6
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceMd

    // 32 高控件需要 hover 反馈；T.TextField 默认 hoverEnabled=false，样式层打开。
    hoverEnabled: true

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
    selectionColor: Theme.brandPrimary
    selectedTextColor: Theme.selectedTextColor
    placeholderTextColor: control.enabled ? Theme.textSecondary : Theme.controlTextDisabled
    verticalAlignment: TextInput.AlignVCenter

    // ContextMenu 是 QtQuick.Templates 6.9 的附加类型；样式文件不在 Basic
    // 模块作用域内，必须经由 T 别名限定，否则运行时找不到该附加属性。
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
        implicitHeight: metrics.heightControl
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

        // 焦点环绘制在边框内侧，控件尺寸不随聚焦变化。
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