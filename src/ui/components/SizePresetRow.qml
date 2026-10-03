pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 长度类字段的预设芯片行（上下文窗口 / 最大输出 tokens）。
//
// 存在的理由只有一个：这些值是 131072、262144 这种没人能心算出来的数字，
// 手敲既容易少个 0 也需要反复查模型规格。旁边保留输入框，自定义值仍然可填。
//
// 用 Flow 而不是 RowLayout：这些芯片挂在三列并排的窄栏下方，装不下时必须
// 换行，否则会互相挤压或溢出到相邻列。
Flow {
    id: root

    // [{ v: 数值, label: 展示文案 }]，单位与所服务的字段一致
    property var entries: []
    // 当前值；0 表示字段为空（走"自动"档）
    property int currentValue: 0
    // 是否给一个「自动」芯片：点击写入空值 = 不发该参数 / 退回能力表默认
    property bool allowAuto: true

    signal chosen(var value)

    spacing: 6
    Layout.preferredHeight: implicitHeight

    Repeater {
        model: root.entries

        delegate: AppButton {
            required property var modelData
            text: modelData.label
            highlighted: root.currentValue !== 0 && root.currentValue === modelData.v
            onClicked: root.chosen(modelData.v)
        }
    }

    AppButton {
        visible: root.allowAuto
        text: qsTr("自动")
        highlighted: root.currentValue === 0
        onClicked: root.chosen("")
    }
}
