import QtQuick
import QtQuick.Controls
import QtQuick.Window
import NexusAI.ViewModels

// NexusStyle 样式回归夹具（tests/test_style_qml.cpp 专用）。
//
// 只放 Qt Quick Controls 原生控件，不放业务组件：断言对象是样式层本身，
// 而不是某个页面的局部覆盖。控件刻意不写 width/height，让 implicit 尺寸
// 参与断言；Column 只负责摆位，不会拉伸子项。
//
// 不用像素/截图基线：offscreen 平台下字体度量与抗锯齿不稳定，颜色/几何
// 属性才是可复现的契约。
Window {
    id: root
    objectName: "styleWindow"
    visible: true
    width: 1000
    height: 900
    color: Theme.bgPrimary

    // ── ComboBox 弹层滚动条探针（任务 #13 / docs/44 §2.3.3）──
    // ComboBox.qml 在 popup.contentItem（ListView）上通过附加属性挂载
    // Controls.ScrollBar.vertical。弹层 contentItem 在 open() 前可能为 null，
    // 因此这里做防御性判空；调用方在 open() 之后再读该属性。
    readonly property bool comboHasScrollBar: {
        var popup = comboBox.popup
        if (!popup || !popup.contentItem)
            return false
        return !!popup.contentItem.ScrollBar.vertical
    }

    // 长模型（15 项 > 10）时内容高度超过 320 上限，AsNeeded 滚动条应可见。
    readonly property bool longComboScrollBarVisible: {
        var popup = longComboBox.popup
        if (!popup || !popup.contentItem)
            return false
        var bar = popup.contentItem.ScrollBar.vertical
        return !!bar && bar.visible
    }

    // 递归检查某个对象树下是否有 running 状态的动画由 C++ 侧完成
    // （QObject::children() 递归）：QML 的 resources 属性是 non-bindable，
    // 用 readonly property 绑定会在动画对象创建前求值并永久为 false。

    Item {
        id: content
        anchors.fill: parent

        Column {
            x: 12
            y: 12
            spacing: 6

            Button {
                objectName: "button"
                text: "Button"
            }
            Button {
                objectName: "highlightedButton"
                text: "Primary"
                highlighted: true
            }
            ToolButton {
                objectName: "toolButton"
                text: "T"
            }
            TextField {
                objectName: "textField"
                placeholderText: "field"
            }
            TextArea {
                objectName: "textArea"
                placeholderText: "area"
            }
            ComboBox {
                id: comboBox
                objectName: "comboBox"
                // 显式 ListModel + textRole：delegate 的 text 可稳定断言
                // （裸 JS 字符串数组在 delegate 里取 textRole 依赖默认角色名）。
                model: ListModel {
                    ListElement { text: "One" }
                    ListElement { text: "Two" }
                    ListElement { text: "Three" }
                }
                textRole: "text"
            }
            Switch {
                objectName: "switch"
            }
            CheckBox {
                objectName: "checkBox"
                text: "check"
            }
            RadioButton {
                objectName: "radioButton"
                text: "radio"
            }
            SpinBox {
                objectName: "spinBox"
                from: 0
                to: 100
                value: 5
            }
            Slider {
                objectName: "slider"
            }
            ItemDelegate {
                objectName: "itemDelegate"
                text: "row"
            }
            ProgressBar {
                objectName: "progressBar"
                value: 0.5
            }
            ScrollBar {
                objectName: "scrollBar"
                orientation: Qt.Vertical
                size: 0.5
            }

            // ── 局部 background 覆盖回归（任务 #13 / docs/44 §2.3.7）──
            // 业务方（29 处）用 `background: Rectangle` 覆盖输入框外观，且不设
            // implicitHeight。此时 T.TextField 的 implicitHeight 退化为
            // contentHeight + topPadding + bottomPadding，样式必须靠上下 6px
            // padding 保住 ~29px 高度，不能再出现 17-20px 的压塌。
            TextField {
                objectName: "textFieldCustomBackground"
                placeholderText: "custom bg"
                background: Rectangle {

                    color: Theme.bgPrimary
                    radius: Theme.radiusField
                }
            }
            TextArea {
                objectName: "textAreaCustomBackground"
                placeholderText: "custom bg"
                background: Rectangle {
                    color: Theme.bgPrimary
                    radius: Theme.radiusField
                }
            }
        }

        Column {
            x: 520
            y: 12
            spacing: 6

            // ── 可编辑 ComboBox 焦点探针（任务 #13 / docs/44 §2.3.3）──
            // editable 时 Tab 焦点落在 contentItem（内嵌 T.TextField），
            // control.activeFocus/visualFocus 均为 false，样式必须靠
            // focused = visualFocus || activeFocus || contentItem.activeFocus 兜底。
            ComboBox {
                objectName: "editableComboBox"
                editable: true
                model: ListModel {
                    ListElement { text: "Alpha" }
                    ListElement { text: "Beta" }
                    ListElement { text: "Gamma" }
                }
                textRole: "text"
            }

            // ── 长模型弹层高度上限探针（任务 #13 / docs/44 §2.3.4）──
            // 15 项 × 30px = 450px，必须被 metrics.popupMaxHeight(320) 截断。
            ComboBox {
                id: longComboBox
                objectName: "longComboBox"
                model: ListModel {
                    ListElement { text: "Item 01" }
                    ListElement { text: "Item 02" }
                    ListElement { text: "Item 03" }
                    ListElement { text: "Item 04" }
                    ListElement { text: "Item 05" }
                    ListElement { text: "Item 06" }
                    ListElement { text: "Item 07" }
                    ListElement { text: "Item 08" }
                    ListElement { text: "Item 09" }
                    ListElement { text: "Item 10" }
                    ListElement { text: "Item 11" }
                    ListElement { text: "Item 12" }
                    ListElement { text: "Item 13" }
                    ListElement { text: "Item 14" }
                    ListElement { text: "Item 15" }
                }
                textRole: "text"
            }

            // ── ScrollBar 可见性矩阵（任务 #13 / 样式 ScrollBar.qml:28-31）──
            // size == 1.0 表示内容不溢出，AsNeeded 下必须隐藏（否则会出现
            // 一根撑满全高的假滚动条并截获点击）；AlwaysOn 仍常显。
            ScrollBar {
                objectName: "scrollBarFullSize"
                orientation: Qt.Vertical
                size: 1.0
                policy: ScrollBar.AsNeeded
            }
            ScrollBar {
                objectName: "scrollBarHalfSize"
                orientation: Qt.Vertical
                size: 0.5
                policy: ScrollBar.AsNeeded
            }
            ScrollBar {
                objectName: "scrollBarAlwaysOnFullSize"
                orientation: Qt.Vertical
                size: 1.0
                policy: ScrollBar.AlwaysOn
            }

            // ── reduceMotion 下 indeterminate 动画应停止（任务 #13）──
            ProgressBar {
                id: indeterminateProgressBar
                objectName: "indeterminateProgressBar"
                indeterminate: true
            }
        }

        // 弹层：Menu 由 C++ 侧 open() 后采样背景/边框；ToolTip 只需隐式尺寸。
        Menu {
            objectName: "menu"
            MenuItem { objectName: "menuItem"; text: "first" }
            MenuSeparator { objectName: "menuSeparator" }
            MenuItem { text: "second" }
        }

        ToolTip {
            objectName: "toolTip"
            text: "tip"
        }
    }
}