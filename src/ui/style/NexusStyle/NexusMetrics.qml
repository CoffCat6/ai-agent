import QtQuick

// NexusStyle 控件几何常量。
//
// 这些数值与主题无关（亮/暗两套共用），且被十余个样式文件引用：集中在这里
// 避免每个控件各写 32/6/30 造成漂移。颜色不在此处——颜色一律走 Theme token。
//
// 用法（同目录隐式导入，无需 import）：`NexusMetrics { id: metrics }`。
QtObject {
    // 控件高度：按钮 / 输入框 / 下拉收起态 / SpinBox 共用一档。
    readonly property int heightControl: 32
    // 下拉选项与 MenuItem 的行高。
    readonly property int heightOption: 30
    // 多行输入框的最小高度（TextArea）。
    readonly property int heightTextAreaMin: 64

    // Switch：轨道 36×20、滑块 16、轨道圆角为高度一半。
    readonly property int widthSwitchTrack: 36
    readonly property int heightSwitchTrack: 20
    readonly property int sizeSwitchThumb: 16
    readonly property int radiusSwitchTrack: 10

    // CheckBox / RadioButton 指示器。
    readonly property int sizeCheckIndicator: 16

    // Slider：细轨道 + 圆滑块。
    readonly property int sizeSliderTrack: 4
    readonly property int sizeSliderThumb: 14

    // ScrollBar 滑块厚度（与 AppScrollBar 对齐）。
    readonly property int sizeScrollBar: 8

    // 控件内图标。
    readonly property int sizeIcon: 16

    // SpinBox 上下按钮列：每列宽 20、每个按钮高 16。
    readonly property int widthSpinButtons: 20
    readonly property int heightSpinButton: 16

    // 焦点环宽度与下拉弹层最大高度。
    readonly property int focusRingWidth: 2
    readonly property int popupMaxHeight: 320

    // 圆角（与 Theme 圆角 token 取值一致；样式文件仍优先直接引 Theme）。
    readonly property int radiusControl: 6
    readonly property int radiusPopup: 8
    readonly property int radiusOption: 4

    // 下拉选项左侧选中标记宽度。
    readonly property int widthSelectionBar: 3
}