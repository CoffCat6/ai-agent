// NexusAI.NexusStyle — ComboBox
//
// 结构沿用 Qt Quick Controls Basic/ComboBox.qml：保留 T.ComboBox 模板的
// implicitWidth/implicitHeight 公式、editable 分支（contentItem 是内嵌
// T.TextField，负责 editText/displayText 切换、光标、选中与校验），以及
// delegateModel/highlightedIndex 的键盘导航语义，仅替换视觉部分。
//
// 收起态（docs/44 §2.3.3）：高 32、圆角 6、左右 12，右侧额外预留 20 给 ▾
// （指示器宽 14 + 间距 6）。弹层（§2.3.4）：cardBackground + cardBorder、
// 圆角 8、padding 4、最大高 320，选项行由同目录 ItemDelegate 绘制。
pragma ComponentBehavior: Bound

import QtQuick
// 用 Controls 命名空间引用「按当前 style 解析」的 ScrollBar：若直接写
// ScrollBar.vertical，会被同目录 NexusStyle/ScrollBar.qml 的同名类型遮蔽，
// 附加属性解析失败。
import QtQuick.Controls as Controls
// Window.height 是附加属性，用于把弹层高度限制在窗口可见区域内。
import QtQuick.Window

import QtQuick.Templates as T
import NexusAI.ViewModels

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    // 上下仍取 6：32 高控件里若上下也给 12，可用内容高只剩 8px，文本会被压扁。
    // 左右取 12，右侧再为指示器预留 20（宽 14 + 间距 6），合计 32。
    padding: 6
    spacing: 6

    leftPadding: Theme.spaceMd + (!control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    rightPadding: Theme.spaceMd + (control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)

    // hover 态需要 hovered 变化，T.ComboBox 默认 hoverEnabled=false。
    hoverEnabled: true

    // 为什么不能只用 visualFocus/activeFocus：Qt 在 Tab 进入 editable
    // ComboBox 时会把焦点转交给 contentItem（内嵌 T.TextField），此时
    // control.visualFocus 与 control.activeFocus 都为 false，焦点环会消失。
    // 8 个使用 editable:true 的页面（PreferenceControl.qml:146、
    // InterfacePage.qml:181 等）都依赖这一判断。contentItem 可能为空，先判空。
    readonly property bool focused: control.visualFocus
                                    || control.activeFocus
                                    || (control.contentItem && control.contentItem.activeFocus)

    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSizeBody

    delegate: ItemDelegate {
        required property var model
        required property int index

        width: ListView.view.width
        text: model[control.textRole]

        // highlighted 用于键盘移动；checked 表示当前选中项（画勾 + 左侧条）。
        highlighted: control.highlightedIndex === index
        checkable: true
        checked: control.currentIndex === index
        hoverEnabled: control.hoverEnabled
    }

    // 用文本字符代替 Basic 的 PNG 箭头：两套主题都能直接跟随字体渲染，
    // 弹层展开时旋转 180°；reduceMotion 下 Theme.motionFast 为 0，动画瞬时落定。
    indicator: Text {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 14
        height: metrics.heightControl
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: "\u25BE"
        color: control.enabled ? Theme.textSecondary : Theme.controlTextDisabled
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontSizeBody
        rotation: control.popup && control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.motionFast } }
    }

    // 保留 Basic 的 editable 分支：contentItem 始终是 T.TextField，editable=false
    // 时由模板显示 displayText 且不可输入。
    contentItem: T.TextField {
        // 外层 left/rightPadding 已包含 12 与指示器 20，内层不再重复留白。
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0

        text: control.editable ? control.editText : control.displayText

        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse

        color: control.enabled ? Theme.textPrimary : Theme.controlTextDisabled
        selectionColor: Theme.brandPrimary
        selectedTextColor: Theme.selectedTextColor
        verticalAlignment: Text.AlignVCenter
        font: control.font
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: metrics.heightControl
        radius: Theme.radiusField

        // flat 组合框仅在展开时显底，与 Basic 语义一致。
        visible: !control.flat || control.down

        color: {
            if (!control.enabled)
                return Theme.controlBackgroundDisabled
            if (control.down)
                return Theme.controlBackgroundPressed
            if (control.hovered)
                return Theme.surfaceHover
            return Theme.bgPrimary
        }

        // 焦点环绘制在边框内侧，控件尺寸不随聚焦变化；focused 覆盖
        // editable 时焦点落在内嵌 TextField 的情况（见上方 focused 注释）。
        border.width: control.focused ? metrics.focusRingWidth : 1
        border.color: {
            if (!control.enabled)
                return Theme.controlBorderDisabled
            if (control.focused)
                return Theme.brandPrimary
            if (control.down || control.hovered)
                return Theme.cardBorder
            return Theme.borderSubtle
        }
    }

    popup: T.Popup {
        id: popup

        y: control.height
        // 宽度不小于控件宽，最小 160（docs/44 §2.3.4）。
        implicitWidth: Math.max(control.width, 160)
        width: implicitWidth
        // 内容高 + 上下 padding 4；上限同时受弹层最大高与窗口可用空间约束。
        // 为什么加窗口约束：仅用 320 时，控件靠近窗口底部弹出的下拉会超出
        // 窗口可视区。窗口高 - 上下 margin - 控件自身高度即剩余空间；再以
        // 一行选项高兜底，避免极小窗口下算出 <=0 的高度。
        // 注意必须写 control.Window.height 而不是裸 Window.height：Popup
        // 不是 Item，附加属性挂在 Popup 上运行时取不到窗口，实测返回 0
        // （会退化成只剩一行高）。挂在 ComboBox（Control/Item）上才是窗口值。
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         metrics.popupMaxHeight,
                         Math.max(metrics.heightOption,
                                  control.Window.height - topMargin - bottomMargin - control.height))
        padding: Theme.spaceXs
        topMargin: 6
        bottomMargin: 6

        // 业务自定义 delegate 若读取 palette（如 palette.text），默认会拿到
        // Basic 的 palette 而非当前主题；显式继承控件 palette 保持主题一致。
        palette: control.palette

        contentItem: ListView {
            clip: true
            implicitHeight: Math.max(contentHeight, metrics.heightOption)
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds

            // 弹层选项超出高度上限时提供垂直滚动条。8px 滑块沿用同目录
            // ScrollBar 样式（policy=AsNeeded 时内容不溢出会自动隐藏）。
            // 必须用 Controls 命名空间：同目录同名类型会遮蔽附加属性解析。
            Controls.ScrollBar.vertical: Controls.ScrollBar {
                policy: Controls.ScrollBar.AsNeeded
            }
        }

        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.cardBackground
            border.width: 1
            border.color: Theme.cardBorder
        }
    }

    NexusMetrics { id: metrics }
}