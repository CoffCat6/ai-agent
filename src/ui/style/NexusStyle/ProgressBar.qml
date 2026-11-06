// NexusAI.NexusStyle — ProgressBar
//
// 结构沿用 Basic/ProgressBar.qml 的 implicitWidth/implicitHeight 公式、
// contentItem/background 分区与 position/indeterminate 语义。
//
// 为什么不用 Basic 的 ProgressBarImpl：它是 C++ 绘制的纯矩形，无法设置
// 圆角（本规格要求 4px 高、2px 圆角）。这里用 contentItem 自绘：
//   · 确定态：宽度 = position × 可用宽度的品牌色填充；
//   · 不确定态：保留 Basic 的「循环扫动」动画语义，一段 1/3 宽的填充从
//     轨道左侧进入、右侧移出，无限循环。
// 颜色与厚度全部走 token/常量，不硬编码。
import QtQuick
import QtQuick.Templates as T
import NexusAI.ViewModels

T.ProgressBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // 禁用态整体降透明度，与 docs/44 §2.3.14 一致。
    opacity: control.enabled ? 1.0 : 0.4

    contentItem: Item {
        implicitWidth: 116
        implicitHeight: 4
        scale: control.mirrored ? -1 : 1

        // ── 确定态填充 ──
        Rectangle {
            visible: !control.indeterminate
            width: control.position * parent.width
            height: parent.height
            radius: 2
            color: Theme.brandPrimary
        }

        // ── 不确定态：裁剪出轨道范围，让 1/3 宽的滑块进出时被裁掉 ──
        Item {
            id: indeterminateArea
            anchors.fill: parent
            visible: control.indeterminate && control.visible
            clip: true

            Rectangle {
                id: indeterminateBar
                width: Math.max(parent.width / 3, 8)
                height: parent.height
                radius: 2
                color: Theme.brandPrimary

                NumberAnimation on x {
                    // 为什么加 Theme.motionFast > 0：reduceMotion 开启时
                    // motionFast === 0，不确定态不应继续无限扫动（无障碍干扰）。
                    // 此时停在起点静态位置即可——indeterminate 本身已表达“进行中”。
                    // 1600ms 是扫动周期，与交互动画 motionFast 语义不同，故不替换。
                    running: control.indeterminate && control.visible && Theme.motionFast > 0
                    from: -indeterminateBar.width
                    to: indeterminateArea.width
                    duration: 1600
                    loops: Animation.Infinite
                }
            }
        }
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 4
        y: (control.height - height) / 2
        height: 4
        radius: 2
        color: Theme.borderSubtle
    }
}