// NexusAI.NexusStyle — Slider（docs/44 §2.3.9）
//
// 结构沿用 Qt Quick Controls Basic/Slider.qml：保留 T.Slider 模板的
// implicitWidth/implicitHeight、handle/background 语义，以及
// orientation（horizontal/vertical）、mirrored、from/to/value/stepSize/
// position/visualPosition/pressed 等全部模板行为；本文件只替换视觉，
// 不重写任何 value ↔ position 的换算逻辑。
//
// 视觉：控件高 24；轨道 4px（圆角 2）；滑块 14×14 圆形（2px 品牌描边），
// hover 1.1 / press 0.92，时长 motionFast；focus 环外扩 2px。
import QtQuick
import QtQuick.Templates as T
import NexusAI.ViewModels

T.Slider {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)

    // 水平 5px 左右留白 + 垂直 5px：14px 滑块 + 10 = 24px 控件高。
    padding: 5

    NexusMetrics { id: metrics }

    // 禁用整体降透明度（docs/44 §2.3.9 指定 0.4），同时填充/描边换成
    // controlBorderDisabled，保证最弱对比下仍能分辨轨道与滑块轮廓。
    opacity: control.enabled ? 1.0 : 0.4

    handle: Rectangle {
        // 位置计算逐字复用 Basic：
        // 水平 → x 由 visualPosition 决定，垂直 → y 由 visualPosition 决定。
        // 这样 RTL(mirrored)、vertical、stepSize 的边界都沿用模板实现。
        x: control.leftPadding + (control.horizontal
                                  ? control.visualPosition * (control.availableWidth - width)
                                  : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal
                                 ? (control.availableHeight - height) / 2
                                 : control.visualPosition * (control.availableHeight - height))

        implicitWidth: metrics.sizeSliderThumb
        implicitHeight: metrics.sizeSliderThumb
        radius: width / 2
        // 亮色下 cardBackground 为白色，靠 2px 品牌描边勾出轮廓；
        // 暗色下为 #111118，与页面底色形成细微分层的“实体滑块”观感。
        color: control.enabled ? Theme.cardBackground : Theme.controlBackgroundDisabled
        border.width: metrics.focusRingWidth
        border.color: control.enabled ? Theme.brandPrimary : Theme.controlBorderDisabled

        // hover/press 只缩放，不改 implicit 尺寸，因此布局不抖动。
        scale: !control.enabled ? 1.0
                               : control.pressed ? 0.92
                                                 : control.hovered ? 1.1
                                                                   : 1.0
        Behavior on scale { NumberAnimation { duration: Theme.motionFast } }

        // 焦点环：子项外扩 2px，不参与 implicit 尺寸；随滑块一起缩放。
        Rectangle {
            x: -metrics.focusRingWidth
            y: -metrics.focusRingWidth
            width: parent.width + metrics.focusRingWidth * 2
            height: parent.height + metrics.focusRingWidth * 2
            radius: width / 2
            color: "transparent"
            border.width: metrics.focusRingWidth
            border.color: Theme.brandPrimary
            visible: control.visualFocus
        }
    }

    background: Rectangle {
        // 轨道定位保持 Basic 公式：水平时横向铺满、垂直时纵向铺满并居中。
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
        implicitWidth: control.horizontal ? 200 : metrics.sizeSliderTrack
        implicitHeight: control.horizontal ? metrics.sizeSliderTrack : 200
        width: control.horizontal ? control.availableWidth : implicitWidth
        height: control.horizontal ? implicitHeight : control.availableHeight
        radius: metrics.sizeSliderTrack / 2
        color: control.enabled ? Theme.borderSubtle : Theme.controlBorderDisabled
        // RTL 时把“未填充 ↔ 已填充”的方向一起镜像，滑块位置由 visualPosition
        // 负责，轨道只负责视觉方向。
        scale: control.horizontal && control.mirrored ? -1 : 1

        Rectangle {
            // 已填充段：水平取 position*宽，垂直取 position*高，与 Basic 一致。
            y: control.horizontal ? 0 : control.visualPosition * parent.height
            width: control.horizontal ? control.position * parent.width : parent.width
            height: control.horizontal ? parent.height : control.position * parent.height
            radius: metrics.sizeSliderTrack / 2
            // 为什么禁用填充不用 controlBorderDisabled：轨道已经是该色，
            // 填充段与轨道同色会导致进度完全不可见。改用更深的
            // controlTextDisabled（亮 #A0A6AE / 暗 #55556A），仍与轨道可区分。
            color: control.enabled ? Theme.brandPrimary : Theme.controlTextDisabled
        }
    }
}