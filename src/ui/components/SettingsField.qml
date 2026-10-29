import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 设置页的字段行：定宽标签 + 输入框 + 可选的值切换。
//
// 标签定宽是为了让各输入框的左边界对齐——原来标签散在输入框上方，视线要来回跳；
// 也让「这个框填什么」的提示不必硬编在页面里。
//
// 提交时机（docs/20 §3.6 同类缺陷修复）：本组件**只在失焦/回车**发 submitted，
// 销毁时提交由使用方通过 commitOnDestroy 显式开启——不能无条件自动提交：
//   * 宿主页（ModelConfigPage）挂在 SettingsCenterDialog 的 Loader 上，切换分类
//     会直接销毁页面且不触发 editingFinished，编辑中的值会静默丢失（同
//     GeneralPage 的根因）；
//   * 但 "当前模型" 字段的每次提交都会被 SettingsService 并进 models 列表
//     （SettingsService.cpp:473），逐键/销毁自动提交会把 "g"、"gp" 这类半截名字
//     灌进模型列表。所以是否在销毁时提交、以及提交什么，由使用方决定。
RowLayout {
    id: root

    property string label: ""
    // 128px 是大多数设置页标签的宽度；个别更长的标签可单独放宽。
    property int labelWidth: 128
    property string placeholderText: ""
    // 路径/URL/密钥等机器可读值用等宽字体，叙述性文本用正文字体。
    property bool mono: false
    // 密文字段默认遮蔽，并自带一个显示/隐藏切换（API Key 填完看不到内容是最常见的困惑点）
    property bool secret: false
    property bool revealed: false
    // 外部通过 input/text 读写文本与焦点（syncFields 要跳过正在输入的字段）；
    // text 别名让页面可以像普通输入框一样声明初值。
    property alias input: fieldInput
    property alias text: fieldInput.text
    property alias validator: fieldInput.validator

    signal submitted(string value)

    // 长文本（系统提示词、代理地址等）的防抖提交间隔；0 = 只在失焦/回车提交。
    // 防抖只提交与 lastKnownValue 不同的值，避免每次停顿都把原值重写一遍。
    property int commitDebounce: 0
    // 密文字段常用"留空 = 不修改"：空输入不触发 submitted。
    property bool ignoreEmptySubmit: false
    // 密钥类字段提交后清空输入框，避免明文在掩码后面继续存在。
    property bool clearOnSubmit: false

    // 组件被销毁（宿主 Loader 切换分类）时把当前文本发一次 submitted。
    // 默认关：调用方按字段语义决定（见上方说明）。
    property bool commitOnDestroy: false
    // 记下"上次已知的已提交值"（或页面加载时的初值），销毁时只在用户真的改过
    // 才提交——避免页面只是被重建就把原值又写一遍（触发多余的 profilesChanged
    // 与审计噪声）。
    property string lastKnownValue: ""

    // 供使用方在同步字段后调用，刷新"已知值"基线。
    function noteValue(value) {
        root.lastKnownValue = (value === undefined || value === null) ? "" : String(value)
    }

    // 唯一提交入口：所有路径都读 `text`（不是 displayText——Password 字段的
    // displayText 是掩码，写进去等于把密钥毁掉），并统一处理"空值不提交"
    // 与"提交后清空"两种字段语义。
    function submitCurrent() {
        const current = fieldInput.text
        if (root.ignoreEmptySubmit && current.length === 0) return
        root.submitted(current)
        root.lastKnownValue = current
        if (root.clearOnSubmit) {
            fieldInput.text = ""
            root.lastKnownValue = ""
        }
    }

    function commitIfDirty() {
        if (fieldInput.text === root.lastKnownValue) return
        root.submitCurrent()
    }

    Layout.fillWidth: true
    spacing: 10

    Text {
        Layout.preferredWidth: root.labelWidth
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.bold: true
        font.family: Theme.fontMono
        elide: Text.ElideRight
    }

    TextField {
        id: fieldInput
        Layout.fillWidth: true
        placeholderText: root.placeholderText
        placeholderTextColor: Theme.textSecondary
        echoMode: root.secret && !root.revealed ? TextInput.Password : TextInput.Normal
        color: Theme.textPrimary
        selectionColor: Theme.brandPrimary
        selectedTextColor: Theme.selectedTextColor
        font.pixelSize: Theme.fontSizeBody
        font.family: root.mono ? Theme.fontMono : Theme.fontSans
        selectByMouse: true
        background: Rectangle {
            radius: 6
            color: Theme.bgPrimary
            border.width: fieldInput.activeFocus ? 2 : 1
            border.color: fieldInput.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
        }
        Accessible.name: root.label
        // editingFinished covers both Enter and focus loss; adding onAccepted as
        // well would submit twice for a single Enter keypress.
        onEditingFinished: root.submitCurrent()
        // 防抖只在长文本字段启用；Timer 与输入框同生命周期，销毁时由
        // commitOnDestroy 兜底。
        onTextEdited: if (root.commitDebounce > 0) commitTimer.restart()
        Component.onDestruction: if (root.commitOnDestroy) root.commitIfDirty()

        Timer {
            id: commitTimer
            interval: root.commitDebounce
            repeat: false
            onTriggered: root.commitIfDirty()
        }
    }

    Text {
        id: revealText
        visible: root.secret
        text: root.revealed ? qsTr("隐藏") : qsTr("显示")
        padding: 4
        color: revealMA.containsMouse ? Theme.brandPrimary : Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("显示或隐藏密钥")

        MouseArea {
            id: revealMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.revealed = !root.revealed
        }
    }
}
