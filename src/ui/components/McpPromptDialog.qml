import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/46: MCP prompt 填参对话框 —— / 补全选中带 required 参数的提示词
// 模板时弹出，按模板声明的 arguments 动态生成字段；确定后由宿主
// （InputConsole）把参数交给 McpManager.requestPrompt。
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 440
    height: Math.min(480, parent ? parent.height - 24 : 480)

    property string serverName: ""
    property string promptName: ""
    property string promptTitle: ""
    property string promptDescription: ""
    // [{name, description, required}] —— 来自 prompts/list 的参数声明
    property var arguments: []

    // 提交时携带 {参数名: 值}。信号名避开 Dialog 自带的 accepted()。
    signal promptSubmitted(var args)

    // 所有 required 参数都有值才能提交；可选参数留空即不提交该键。
    readonly property bool requiredFilled: {
        const list = dialog.arguments || []
        for (let i = 0; i < list.length; ++i) {
            const a = list[i] || {}
            if (a.required && String(fieldValues[a.name] || "").length === 0)
                return false
        }
        return true
    }
    property var fieldValues: ({})

    function openFor(entry) {
        serverName = String(entry.server || "")
        promptName = String(entry.name || "")
        promptTitle = String(entry.title || "")
        promptDescription = String(entry.description || "")
        arguments = entry.arguments || []
        fieldValues = {}
        if (!visible) open()
    }

    background: DialogBackground {}

    contentItem: ColumnLayout {
        Accessible.name: qsTr("MCP 提示词参数对话框")
        spacing: 10

        // Title
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.topMargin: 18
            Layout.rightMargin: 20
            spacing: 8

            AppIcon {
                icon: "preset"
                size: 18
                iconColor: Theme.brandPrimary
            }
            Text {
                Layout.fillWidth: true
                text: dialog.promptTitle.length > 0
                    ? dialog.promptTitle : dialog.promptName
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                elide: Text.ElideRight
            }
        }

        // 来源服务器 + 描述
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            text: dialog.serverName
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
            elide: Text.ElideMiddle
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: dialog.promptDescription.length > 0
            text: dialog.promptDescription
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        // 动态参数字段
        Repeater {
            model: dialog.arguments
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 4

                Text {
                    text: modelData.name + (modelData.required
                        ? qsTr(" *") : "")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
                TextField {
                    id: argField
                    Layout.fillWidth: true
                    placeholderText: String(modelData.description || "")
                    echoMode: TextInput.Normal
                    onTextEdited: {
                        const next = Object.assign({}, dialog.fieldValues)
                        next[modelData.name] = text
                        dialog.fieldValues = next
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Action buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 18
            spacing: 8

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("取消")
                onClicked: dialog.reject()
            }
            AppButton {
                text: qsTr("获取提示词")
                highlighted: true
                enabled: dialog.requiredFilled
                onClicked: {
                    // 只提交已填的键：可选参数留空 = 不传。
                    const submitted = {}
                    const list = dialog.arguments || []
                    for (let i = 0; i < list.length; ++i) {
                        const a = list[i] || {}
                        const value = String(dialog.fieldValues[a.name] || "")
                        if (value.length > 0) submitted[a.name] = value
                    }
                    dialog.promptSubmitted(submitted)
                    dialog.accept()
                }
            }
        }
    }
}
