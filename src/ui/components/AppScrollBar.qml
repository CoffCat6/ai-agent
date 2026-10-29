import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 主题化滚动条：8px 圆角滑块，悬停/拖动时变为品牌色。
//
// Qt 6 的 Basic 样式默认滚动条与应用的「边框 + 底色分层」体系不一致，
// 设置页与导航统一改用这个组件（contentItem 宽度决定整体宽度）。
ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded
    padding: 2
    minimumSize: 0.08

    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: width / 2
        color: root.pressed || root.hovered
            ? Theme.brandPrimary : Theme.borderSubtle
        opacity: root.active ? 1.0 : 0.55
        Behavior on opacity {
            NumberAnimation { duration: Theme.motionFast }
        }
    }

    background: Rectangle { color: "transparent" }
}
