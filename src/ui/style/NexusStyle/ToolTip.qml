// NexusAI.NexusStyle — ToolTip
//
// 结构沿用 Basic/ToolTip.qml：保留 x/y 定位、margins、closePolicy，以及模板的
// delay/timeout/visible 逻辑；contentItem 仍绑定 control.text。
//
// 为什么用 bgSecondary + textPrimary 而不是「黑底白字」：应用亮色主题是
// 白卡体系，反色提示会在亮色页面里显得突兀；该组合对比度约 15.8:1。
// 明确不加阴影/模糊（docs/19 §5.4）。
import QtQuick
import QtQuick.Templates as T
import NexusAI.ViewModels

T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - 3

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: 6
    // 上下 6、左右 8：单行提示约 24px 高，不挤压正文。
    padding: 6
    horizontalPadding: 8

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeCaption

    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.textPrimary
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        radius: Theme.radiusField
        color: Theme.bgSecondary
        border.width: 1
        border.color: Theme.borderSubtle
    }
}