// NexusAI.NexusStyle — ScrollBar
//
// 结构沿用 Basic/ScrollBar.qml：保留 orientation、size/position/active、
// policy、visible 判定与水平/垂直共用的 contentItem 尺寸公式。
//
// 视觉与自绘的 AppScrollBar.qml 对齐：8px 圆角滑块、轨道透明、默认
// borderSubtle、hover/press 用 brandPrimary、非活动 opacity 0.55。
// 与 Basic 的关键差异：Basic 让滑块默认完全透明、只在滚动的 450ms 后淡出；
// 本样式改为常驻 0.55 可见，避免用户不知道页面还有内容。
import QtQuick
import QtQuick.Templates as T
import NexusAI.ViewModels

T.ScrollBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    NexusMetrics { id: metrics }

    // 与 AppScrollBar 一致：2px 外边距 + 8px 滑块 = 12px 占位。
    padding: 2
    // 为什么不能只判断 policy：AsNeeded 且内容刚好不溢出时 size === 1.0，
    // 仍会渲染一根撑满全高的滑块（Menu 右缘会出现假滚动条并可能截获点击）。
    // 因此 AsNeeded 仅在确实溢出（size < 1.0）时可见；AlwaysOn 保持恒显，
    // AlwaysOff 恒隐。
    visible: control.policy === T.ScrollBar.AlwaysOn
             || (control.policy === T.ScrollBar.AsNeeded && control.size < 1.0)
    minimumSize: 0.08

    contentItem: Rectangle {
        implicitWidth: metrics.sizeScrollBar
        implicitHeight: metrics.sizeScrollBar

        radius: metrics.sizeScrollBar / 2
        color: (control.pressed || control.hovered)
               ? Theme.brandPrimary : Theme.borderSubtle

        // active 覆盖键盘/程序滚动等「无 hover」场景，语义与 AppScrollBar 一致。
        opacity: (control.active || control.hovered || control.pressed) ? 1.0 : 0.55
        Behavior on opacity {
            NumberAnimation { duration: Theme.motionFast }
        }
    }

    // 轨道不绘制底色，只留滑块，避免列表/编辑器出现第二条视觉边界。
    background: Rectangle {
        color: "transparent"
    }
}