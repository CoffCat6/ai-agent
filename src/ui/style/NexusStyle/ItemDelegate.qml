// NexusAI.NexusStyle — ItemDelegate（列表行 / ComboBox 选项行）
//
// 结构沿用 Qt Quick Controls Basic/ItemDelegate.qml：保留 T.ItemDelegate 模板的
// implicitWidth/implicitHeight 公式、contentItem 与 icon/text/display/highlighted/
// pressed/checkable/checked 语义，仅替换视觉部分。注意 Qt 6.9 的 T.ItemDelegate
// 没有 checkState（三态只在 CheckDelegate 上），勾选态统一读 checked。
//
// 选项行规格（docs/44 §2.3.4）：高 30、圆角 4、左右 12；默认透明底 + textPrimary；
// hover surfaceHover；press controlBackgroundPressed；键盘高亮或当前项（highlighted/
// checked）叠加 surfaceHover + 左侧 3px 品牌竖条；checked 时右侧显示 12px 对勾。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 0
    topPadding: 0
    bottomPadding: 0
    leftPadding: Theme.spaceMd
    // 可勾选时在右侧为 12px 对勾留出入口，避免长文本压住勾选符号。
    rightPadding: control.checkable ? Theme.spaceMd + metrics.sizeIcon : Theme.spaceMd
    spacing: Theme.spaceSm

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    icon.width: metrics.sizeIcon
    icon.height: metrics.sizeIcon
    icon.color: !control.enabled ? Theme.controlTextDisabled
              : (control.hovered || control.highlighted || control.checked)
                ? Theme.textPrimary : Theme.textSecondary

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: control.display === IconLabel.IconOnly || control.display === IconLabel.TextUnderIcon
                   ? Qt.AlignCenter : Qt.AlignLeft

        icon: control.icon
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: metrics.heightOption
        radius: Theme.radiusSm

        color: {
            if (!control.enabled)
                return "transparent"
            if (control.down)
                return Theme.controlBackgroundPressed
            if (control.hovered || control.highlighted || control.checked)
                return Theme.surfaceHover
            return "transparent"
        }

        // 键盘焦点用 1px 内描边，不改变行高。
        border.width: control.visualFocus ? 1 : 0
        border.color: Theme.brandPrimary

        // 左侧选中标记：文档口径 3px × 18 圆角条。
        Rectangle {
            width: metrics.widthSelectionBar
            height: 18
            radius: height / 2
            color: Theme.brandPrimary
            visible: control.enabled && (control.highlighted || control.checked)
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }

        // 右侧勾选符号：用文本字符而非图片，避免为样式引入额外资源。
        // T.ItemDelegate 只有 checkable/checked（checkState 属于 CheckDelegate），
        // 因此勾选态直接看 checked，QML 侧按需把它绑定到当前项。
        Text {
            text: "\u2713"
            visible: control.enabled && control.checkable && control.checked
            color: Theme.brandPrimary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeBody
            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceMd
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    NexusMetrics { id: metrics }
}