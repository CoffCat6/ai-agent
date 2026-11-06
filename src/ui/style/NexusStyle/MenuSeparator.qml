// NexusAI.NexusStyle — MenuSeparator
//
// 结构沿用 Basic/MenuSeparator.qml：保留 implicitWidth/implicitHeight 公式
// 与 contentItem 语义，只替换颜色/间距。
//
// 规格：1px 线 + 上下各 4px → 总高 9；左右各缩进 8，菜单宽度不被撑大。
import QtQuick
import QtQuick.Templates as T
import NexusAI.ViewModels

T.MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // 上下各留 4px；左右缩进 8px（与菜单项 12px 内边距形成层次）。
    leftPadding: Theme.spaceSm
    rightPadding: Theme.spaceSm
    topPadding: Theme.spaceXs
    bottomPadding: Theme.spaceXs
    padding: 0

    contentItem: Rectangle {
        // implicitWidth 为 0：分隔线宽度由 ListView 拉伸到菜单可用宽度。
        implicitWidth: 0
        implicitHeight: 1
        color: Theme.borderSubtle
    }
}