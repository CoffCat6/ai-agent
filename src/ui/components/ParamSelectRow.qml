pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 参数下拉行：定宽标签 + 下拉框（+ 按需露出的自定义输入框）。
//
// 长度类参数（最大输出 tokens、上下文预算）的真实取值是 131072 这种没人能心算
// 出来的数字，手敲容易少数一位；枚举类参数（思考强度）本来就是有限档位。两者
// 统一收进下拉：选项文案只带单位，解释留在字段自身的灰色 placeholder 上。
//
// 选项表 = [自动] + entries + [自定义…]。选中预设/自动立即发 submitted；选中
// 「自定义…」露出输入框，由用户敲值后回车或失焦提交。
RowLayout {
    id: root

    property string label: ""
    // [{ v: 值, label: "128K" }]
    property var entries: []
    // 当前生效值（profile 里存的那个）；空字符串表示"自动"
    property var currentValue: ""
    property bool allowAuto: true
    property bool allowCustom: true
    // 「自动」项的文案：思考强度那类场合写「默认」更贴切
    property string autoLabel: qsTr("自动")
    property string placeholderText: ""
    // 外部通过 input 读写文本与焦点（隐藏时 input.text 仍持有原始值）
    property alias input: field
    property alias validator: field.validator
    // 选中「自定义…」后为 true：输入框露出
    property bool customMode: false

    signal submitted(var value)

    readonly property string customLabel: qsTr("自定义…")
    readonly property var optionLabels: {
        const out = []
        if (root.allowAuto) out.push(root.autoLabel)
        for (let i = 0; i < root.entries.length; ++i) out.push(root.entries[i].label)
        if (root.allowCustom) out.push(root.customLabel)
        return out
    }
    readonly property int customIndex:
        root.allowCustom ? root.optionLabels.length - 1 : -1

    // 值 → 下拉索引：空走「自动」，命中预设走该预设，其余（手填值）落到「自定义…」；
    // 不允许自定义的枚举下拉（思考强度）遇到表外的旧值则退回「自动」，不留空选项。
    function indexOfValue(value) {
        const s = (value === undefined || value === null) ? "" : String(value)
        if (s === "") return root.allowAuto ? 0 : -1
        for (let i = 0; i < root.entries.length; ++i)
            if (String(root.entries[i].v) === s)
                return i + (root.allowAuto ? 1 : 0)
        if (root.allowCustom) return root.customIndex
        return root.allowAuto ? 0 : -1
    }

    // 输入框 text 始终持有原始值，哪怕当前选中的是预设（此时框是隐藏的）：页面里
    // 「当前生效值」「输出上限是否不小于窗口」等判断都从 input.text 派生，隐藏时
    // 也必须读得到。
    function refresh() {
        const s = (root.currentValue === undefined || root.currentValue === null)
            ? "" : String(root.currentValue)
        // 下拉选中项总能安全重算（不会碰用户的键盘输入）
        combo.currentIndex = root.indexOfValue(s)
        // 用户正在敲自定义值：不要覆盖输入框内容，也不要收起它
        if (root.customMode && field.activeFocus) return
        field.text = s
        // 只在"存的就是自定义值"时自动露出输入框：用户点了「自定义…」但还没敲完
        // 时 currentValue 仍是旧值（不是 customIndex），customMode 保持不动。
        root.customMode = root.allowCustom
            && combo.currentIndex === root.customIndex
    }

    function applyOption(index) {
        if (root.allowAuto && index === 0) {
            root.customMode = false
            // 输入框是隐藏的，但 input.text 仍被页面读（manualContext / requestedOutput）：
            // 不清掉的话"选自动"之后那些派生值会停在旧预设上。
            field.text = ""
            root.submitted("")
            return
        }
        if (root.allowCustom && index === root.customIndex) {
            root.customMode = true
            field.forceActiveFocus()
            return
        }
        const entry = root.entries[index - (root.allowAuto ? 1 : 0)]
        if (entry === undefined) return
        root.customMode = false
        field.text = String(entry.v)
        root.submitted(entry.v)
    }

    Layout.fillWidth: true
    spacing: 10

    onCurrentValueChanged: root.refresh()
    // 选项表变化（首次绑定 entries）会让 ComboBox 把 currentIndex 重置回 0，重算一次。
    onOptionLabelsChanged: root.refresh()
    Component.onCompleted: root.refresh()

    Text {
        Layout.preferredWidth: 128
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.bold: true
        font.family: Theme.fontMono
        elide: Text.ElideRight
    }

    ComboBox {
        id: combo
        Layout.preferredWidth: 200
        model: root.optionLabels
        Accessible.name: root.label
        onActivated: (index) => root.applyOption(index)
    }

    TextField {
        id: field
        visible: root.customMode
        Layout.fillWidth: true
        placeholderText: root.placeholderText
        placeholderTextColor: Theme.textSecondary
        color: Theme.textPrimary
        selectionColor: Theme.brandPrimary
        selectedTextColor: "#ffffff"
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        background: Rectangle {
            radius: 6
            color: Theme.bgPrimary
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
        }
        Accessible.name: root.label
        onEditingFinished: {
            const value = text.trim()
            if (value === "") {
                // 自定义框空着就离开 = 放弃这次修改，回到原值（不能当成"清空成自动"，
                // 那会把用户之前选好的预设默默抹掉）。
                root.refresh()
                return
            }
            root.submitted(value)
        }
    }
}
