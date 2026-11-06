// NexusAI.NexusStyle — Menu
//
// 结构沿用 Qt Quick Controls Basic/Menu.qml：保留 T.Menu 的
// implicitWidth/implicitHeight 计算、contentModel → ListView 的滚动语义、
// delegate 与 cascade（模板默认 true），只替换弹层视觉。
//
// 为什么弹层不自带阴影：docs/44 与 docs/19 的「零特效」策略要求弹层用
// 卡片底 + 1px 边框建立层级，避免暗色主题下阴影不可见、亮色主题下发灰。
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import NexusAI.ViewModels

T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // 4px 内边距让菜单项圆角与卡片边框之间留出呼吸空间。
    padding: Theme.spaceXs
    margins: 0
    overlap: 1

    // 显式指向同目录的 NexusStyle/MenuItem.qml，确保业务 Menu 也吃到新样式。
    delegate: MenuItem { }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        // 保留 Basic 的判定：内容高于弹层高度时才允许滚轮/拖动。
        interactive: Window.window
                     ? contentHeight + control.topPadding + control.bottomPadding > control.height
                     : false
        clip: true
        currentIndex: control.currentIndex

        // 用样式化的 ScrollBar（8px、hover 品牌色），与 AppScrollBar 视觉一致。
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    background: Rectangle {
        // 最小宽 160 兜底；内容更宽时由 implicitContentWidth 撑开。
        implicitWidth: 160
        // 高度完全由菜单项内容决定，空菜单不应出现 40px 空卡片。
        implicitHeight: 0
        radius: Theme.radiusMd
        color: Theme.cardBackground
        border.width: 1
        border.color: Theme.cardBorder
    }

    // 保留 Basic 的遮罩语义（菜单打开时对父窗口做轻压暗），仅沿用 palette 值。
    T.Overlay.modal: Rectangle {
        color: Color.transparent(control.palette.shadow, 0.5)
    }

    T.Overlay.modeless: Rectangle {
        color: Color.transparent(control.palette.shadow, 0.12)
    }
}