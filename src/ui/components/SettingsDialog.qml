import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Model configuration dialog: manages multiple model profiles (any mix of
// providers) and switches the active one instantly. The profile selected in
// the chip row is the one being edited; "启用此配置" makes it the live one.
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    // Fixed size: a ColumnLayout contentItem cannot derive a usable implicit
    // size from fillWidth/fillHeight children, so size the popup here.
    width: 560
    height: 680

    // ── Editor state ──────────────────────────────────
    property string editingId: Settings.activeProfileId
    property var editing: {
        const list = Settings.profiles
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === editingId) return list[i]
        return list.length > 0 ? list[0] : null
    }
    onEditingChanged: syncFields()
    Component.onCompleted: syncFields()
    Connections {
        target: Settings
        function onProfilesChanged() { dialog.syncFields() }
    }

    function syncFields() {
        const p = editing
        nameField.text = p ? (p.displayName || "") : ""
        apiKeyField.text = p ? (p.apiKey || "") : ""
        baseUrlField.text = p ? (p.baseUrl || "") : ""
        modelField.text = p ? (p.model || "") : ""
    }

    background: Rectangle {
        color: Theme.bgSecondary
        radius: 8
        border.width: 1
        border.color: Theme.borderSubtle
    }

    contentItem: ColumnLayout {
        spacing: 12

        // ── Title ─────────────────────────────────────
        Text {
            Layout.leftMargin: 24
            Layout.topMargin: 24
            Layout.rightMargin: 24
            text: qsTr("模型服务配置")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
        }

        Text {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: qsTr("可保存多个模型配置，点击芯片切换编辑，「启用」后立即生效")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
        }

        // ── Profile chips ─────────────────────────────
        Flow {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 8

            Repeater {
                model: Settings.profiles

                delegate: Rectangle {
                    width: chipRow.implicitWidth + 24
                    height: 30
                    radius: 15

                    readonly property bool isActive: modelData.id === Settings.activeProfileId
                    readonly property bool isEditing: modelData.id === dialog.editingId

                    color: isActive ? Qt.rgba(
                        Theme.brandPrimary.r, Theme.brandPrimary.g,
                        Theme.brandPrimary.b, 0.15) : Theme.bgPrimary
                    border.width: isEditing ? 2 : 1
                    border.color: isEditing ? Theme.brandPrimary : Theme.borderSubtle

                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6

                        Rectangle {
                            visible: parent.parent.isActive
                            width: 6
                            height: 6
                            radius: 3
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.brandPrimary
                        }

                        Text {
                            text: modelData.displayName || modelData.provider
                            color: isActive ? Theme.brandPrimary : Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dialog.editingId = modelData.id
                    }
                }
            }

            // Add-profile chip
            Rectangle {
                width: addChipText.implicitWidth + 24
                height: 30
                radius: 15
                color: addChipMA.containsMouse ? Theme.surfaceHover : "transparent"
                border.width: 1
                border.color: addChipMA.containsMouse ? Theme.brandPrimary : Theme.borderSubtle

                Text {
                    id: addChipText
                    anchors.centerIn: parent
                    text: "+ 添加"
                    color: addChipMA.containsMouse ? Theme.brandPrimary : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }

                MouseArea {
                    id: addChipMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: dialog.editingId = Settings.addProfile()
                }
            }
        }

        // ── Editor fields ─────────────────────────────
        ColumnLayout {
            visible: dialog.editing !== null
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.fillWidth: true
            spacing: 10

            // Display name
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("显示名称")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("例如：工作用 GPT、本地 qwen…")
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    background: Rectangle {
                        radius: 6
                        color: Theme.bgPrimary
                        border.width: nameField.activeFocus ? 2 : 1
                        border.color: nameField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                    }
                    onEditingFinished: dialog.submitField("displayName", text)
                }
            }

            // Provider type
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("提供商")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                RowLayout {
                    spacing: 8

                    AppButton {
                        text: qsTr("OpenAI 兼容")
                        highlighted: dialog.editing && dialog.editing.provider === "openai"
                        onClicked: {
                            const d = Settings.defaultEndpoint("openai")
                            dialog.submitFields({"provider": "openai", "baseUrl": d.baseUrl, "model": d.model})
                        }
                    }
                    AppButton {
                        text: qsTr("Claude")
                        highlighted: dialog.editing && dialog.editing.provider === "anthropic"
                        onClicked: {
                            const d = Settings.defaultEndpoint("anthropic")
                            dialog.submitFields({"provider": "anthropic", "baseUrl": d.baseUrl, "model": d.model})
                        }
                    }
                    AppButton {
                        text: qsTr("Ollama 本地")
                        highlighted: dialog.editing && dialog.editing.provider === "ollama"
                        onClicked: {
                            const d = Settings.defaultEndpoint("ollama")
                            dialog.submitFields({"provider": "ollama", "baseUrl": d.baseUrl, "model": d.model})
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // API key (cloud providers only)
            ColumnLayout {
                visible: dialog.editing && dialog.editing.provider !== "ollama"
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("API Key")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: dialog.editing && dialog.editing.provider === "anthropic"
                        ? "sk-ant-..." : "sk-...（或设置环境变量 OPENAI_API_KEY）"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    background: Rectangle {
                        radius: 6
                        color: Theme.bgPrimary
                        border.width: apiKeyField.activeFocus ? 2 : 1
                        border.color: apiKeyField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                    }
                    onEditingFinished: dialog.submitField("apiKey", text)
                }
                Text {
                    visible: dialog.editing && (dialog.editing.apiKey || "").length === 0
                        && dialog.editing.provider !== "ollama"
                    text: dialog.editing && dialog.editing.provider === "anthropic"
                        ? qsTr("未配置 Key 时将回退读取 ANTHROPIC_API_KEY 环境变量")
                        : qsTr("未配置 Key 时将回退读取 OPENAI_API_KEY 环境变量")
                    color: Theme.statusWarning
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                }
            }

            // Base URL
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("服务地址 Base URL")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                TextField {
                    id: baseUrlField
                    Layout.fillWidth: true
                    placeholderText: "https://api.openai.com/v1"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    background: Rectangle {
                        radius: 6
                        color: Theme.bgPrimary
                        border.width: baseUrlField.activeFocus ? 2 : 1
                        border.color: baseUrlField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                    }
                    onEditingFinished: dialog.submitField("baseUrl", text)
                }
            }

            // Model
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("模型")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                TextField {
                    id: modelField
                    Layout.fillWidth: true
                    placeholderText: "gpt-4o-mini"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    background: Rectangle {
                        radius: 6
                        color: Theme.bgPrimary
                        border.width: modelField.activeFocus ? 2 : 1
                        border.color: modelField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                    }
                    onEditingFinished: dialog.submitField("model", text)
                }
            }

            // Profile actions
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AppButton {
                    text: dialog.editing && dialog.editing.id === Settings.activeProfileId
                        ? qsTr("✓ 使用中") : qsTr("启用此配置")
                    highlighted: dialog.editing && dialog.editing.id !== Settings.activeProfileId
                    enabled: dialog.editing && dialog.editing.id !== Settings.activeProfileId
                    onClicked: Settings.setActiveProfile(dialog.editing.id)
                }

                Text {
                    visible: dialog.editing && dialog.editing.id !== Settings.activeProfileId
                    text: qsTr("编辑保存后需「启用」才会用于对话")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("删除")
                    buttonColor: "transparent"
                    textColor: Theme.statusDanger
                    border.color: Theme.borderSubtle
                    enabled: Settings.profiles.length > 1
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        Settings.removeProfile(dialog.editing.id)
                        dialog.editingId = Settings.activeProfileId
                    }
                }
            }
        }

        // ── Divider ───────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            height: 1
            color: Theme.borderSubtle
        }

        // ── Global settings ───────────────────────────
        ColumnLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: qsTr("通用")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTr("上下文窗口 (Tokens)")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                TextField {
                    id: contextField
                    Layout.fillWidth: true
                    text: Settings.contextTokens
                    placeholderText: "8192"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    validator: IntValidator { bottom: 2048; top: 1048576 }
                    background: Rectangle {
                        radius: 6
                        color: Theme.bgPrimary
                        border.width: contextField.activeFocus ? 2 : 1
                        border.color: contextField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                    }
                    onEditingFinished: {
                        const parsed = parseInt(text)
                        Settings.contextTokens = isNaN(parsed) ? 8192 : parsed
                        text = Settings.contextTokens
                    }
                }
                Text {
                    text: qsTr("超过该预算时会自动压缩历史对话（保留系统提示与最近消息）")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                }
            }

            CheckBox {
                checked: Settings.simulationMode
                onToggled: Settings.simulationMode = checked
                text: qsTr("仿真演示模式（不调用真实 LLM）")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
            }
        }

        // ── Divider ───────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            height: 1
            color: Theme.borderSubtle
        }

        // ── About ─────────────────────────────────────
        ColumnLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 8
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "NexusAI"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }

                Rectangle {
                    width: versionBadge.implicitWidth + 14
                    height: 18
                    radius: 9
                    color: Theme.brandPrimary
                    opacity: 0.15

                    Text {
                        id: versionBadge
                        anchors.centerIn: parent
                        text: "v" + Settings.appVersion
                        color: Theme.brandPrimary
                        font.pixelSize: 10
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                }

                Item { Layout.fillWidth: true }
            }

            Text {
                text: qsTr("本地智能体桌面应用 · 多模型提供商 · 工具调用 · 本地知识库")
                color: Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        // ── Footer ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 24
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: dialog.testing ? qsTr("正在连接…") : dialog.testResult
                color: {
                    if (dialog.testing) return Theme.textSecondary
                    return dialog.testOk ? Theme.statusSuccess : Theme.statusDanger
                }
                font.pixelSize: 10
                font.family: Theme.fontSans
                elide: Text.ElideRight
                visible: dialog.testing || dialog.testResult !== ""
            }

            AppButton {
                text: dialog.testing ? qsTr("测试中…") : qsTr("测试连接")
                enabled: !dialog.testing && dialog.editing !== null
                onClicked: {
                    dialog.testing = true
                    dialog.testResult = ""
                    ConnectionTester.test(dialog.editing.provider, dialog.editing.baseUrl,
                                          dialog.editing.apiKey, dialog.editing.model)
                }
            }

            AppButton {
                text: qsTr("完成")
                highlighted: true
                onClicked: dialog.close()
            }
        }
    }

    // ── Connection test state ─────────────────────────
    property bool testing: false
    property string testResult: ""
    property bool testOk: false

    Connections {
        target: ConnectionTester
        function onTestFinished(ok, message) {
            dialog.testing = false
            dialog.testOk = ok
            dialog.testResult = message
        }
    }

    // ── Helpers ───────────────────────────────────────
    function submitField(key, value) {
        if (editing)
            Settings.updateProfile(editing.id, { [key]: value })
    }

    function submitFields(map) {
        if (editing)
            Settings.updateProfile(editing.id, map)
    }
}
