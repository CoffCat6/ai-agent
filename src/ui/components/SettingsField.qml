import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 设置页的字段行：定宽标签 + 输入框 + 可选的值切换。
//
// 标签定宽是为了让各输入框的左边界对齐——原来标签散在输入框上方，视线要来回跳；
// 也让「这个框填什么」的提示不必硬编在页面里。
RowLayout {
    id: root

    property string label: ""
    property string placeholderText: ""
    // 密文字段默认遮蔽，并自带一个显示/隐藏切换（API Key 填完看不到内容是最常见的困惑点）
    property bool secret: false
    property bool revealed: false
    // 外部通过 input 读写文本与焦点（syncFields 要跳过正在输入的字段）
    property alias input: fieldInput
    property alias validator: fieldInput.validator

    signal submitted(string value)

    Layout.fillWidth: true
    spacing: 10

    Text {
        Layout.preferredWidth: 128
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
        selectedTextColor: "#ffffff"
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        background: Rectangle {
            radius: 6
            color: Theme.bgPrimary
            border.width: fieldInput.activeFocus ? 2 : 1
            border.color: fieldInput.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
        }
        Accessible.name: root.label
        onEditingFinished: root.submitted(text)
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
