import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

ScrollView {
    id: page
    clip: true

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
        function onProfilesChanged() { page.syncFields() }
    }

    function syncFields() {
        const p = editing
        nameField.text = p ? (p.displayName || "") : ""
        apiKeyField.text = p ? (p.apiKey || "") : ""
        baseUrlField.text = p ? (p.baseUrl || "") : ""
        modelField.text = p ? (p.model || "") : ""
        temperatureField.text = p && p.temperature !== undefined ? String(p.temperature) : ""
        maxTokensField.text = p && p.maxOutputTokens !== undefined ? String(p.maxOutputTokens) : ""
        numCtxField.text = p && p.numCtx !== undefined ? String(p.numCtx) : ""
    }

    function submitField(key, value) {
        if (editing) Settings.updateProfile(editing.id, { [key]: value })
    }
    function submitFields(map) {
        if (editing) Settings.updateProfile(editing.id, map)
    }

    property bool testing: false
    property string testResult: ""
    property bool testOk: false
    Connections {
        target: ConnectionTester
        function onTestFinished(ok, message) {
            page.testing = false
            page.testOk = ok
            page.testResult = message
        }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: 12

        Item { Layout.preferredHeight: 4 }

        Text {
            text: qsTr("模型服务配置")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
            Layout.leftMargin: 24
            Layout.rightMargin: 24
        }

        Text {
            text: qsTr("可保存多个模型配置，点击芯片切换编辑，「启用」后立即生效")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            Layout.leftMargin: 24
            Layout.rightMargin: 24
        }

        // The model page persists through SettingsService immediately and is
        // NOT covered by the dialog's 保存/取消 buttons (those only roll back
        // the staged preferences of the other pages).
        Text {
            text: qsTr("本页修改实时保存，不受底部「保存 / 取消」影响")
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontSans
            opacity: 0.8
            Layout.leftMargin: 24
            Layout.rightMargin: 24
        }

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
                    readonly property bool isEditing: modelData.id === page.editingId
                    color: isActive ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g, Theme.brandPrimary.b, 0.15) : Theme.bgPrimary
                    border.width: isEditing ? 2 : 1
                    border.color: isEditing ? Theme.brandPrimary : Theme.borderSubtle
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle {
                            visible: parent.parent.isActive
                            width: 6; height: 6; radius: 3
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
                        onClicked: page.editingId = modelData.id
                    }
                }
            }

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
                    onClicked: page.editingId = Settings.addProfile()
                }
            }
        }

        ColumnLayout {
            visible: page.editing !== null
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("显示名称"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("例如：工作用 GPT、本地 qwen…")
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary; selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans
                    background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: nameField.activeFocus ? 2 : 1; border.color: nameField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    onEditingFinished: page.submitField("displayName", text)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("提供商"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                RowLayout {
                    spacing: 8
                    AppButton { text: qsTr("OpenAI 兼容"); highlighted: page.editing && page.editing.provider === "openai"; onClicked: { const d = Settings.defaultEndpoint("openai"); page.submitFields({"provider": "openai", "baseUrl": d.baseUrl, "model": d.model}) } }
                    AppButton { text: qsTr("Claude"); highlighted: page.editing && page.editing.provider === "anthropic"; onClicked: { const d = Settings.defaultEndpoint("anthropic"); page.submitFields({"provider": "anthropic", "baseUrl": d.baseUrl, "model": d.model}) } }
                    AppButton { text: qsTr("Ollama 本地"); highlighted: page.editing && page.editing.provider === "ollama"; onClicked: { const d = Settings.defaultEndpoint("ollama"); page.submitFields({"provider": "ollama", "baseUrl": d.baseUrl, "model": d.model}) } }
                    Item { Layout.fillWidth: true }
                }
            }

            ColumnLayout {
                visible: page.editing && page.editing.provider !== "ollama"
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("API Key"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: page.editing && page.editing.provider === "anthropic" ? "sk-ant-..." : "sk-...（或设置环境变量 OPENAI_API_KEY）"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary; selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans
                    background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: apiKeyField.activeFocus ? 2 : 1; border.color: apiKeyField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    onEditingFinished: page.submitField("apiKey", text)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("服务地址 Base URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                TextField {
                    id: baseUrlField
                    Layout.fillWidth: true
                    placeholderText: "https://api.openai.com/v1"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary; selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans
                    background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: baseUrlField.activeFocus ? 2 : 1; border.color: baseUrlField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    onEditingFinished: page.submitField("baseUrl", text)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("当前模型"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                TextField {
                    id: modelField
                    Layout.fillWidth: true
                    placeholderText: "gpt-4o-mini"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary; selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans
                    background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: modelField.activeFocus ? 2 : 1; border.color: modelField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    onEditingFinished: page.submitField("model", text)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("生成参数（留空 = 使用服务默认，不随请求发送）")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("temperature (0-2)")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        TextField {
                            id: temperatureField
                            Layout.fillWidth: true
                            placeholderText: qsTr("留空 = 不发送")
                            placeholderTextColor: Theme.textSecondary
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontMono
                            validator: DoubleValidator { locale: "C"; bottom: 0; decimals: 2 }
                            background: Rectangle {
                                radius: 6
                                color: Theme.bgPrimary
                                border.width: temperatureField.activeFocus ? 2 : 1
                                border.color: temperatureField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                            }
                            onEditingFinished: page.submitField("temperature", text.trim())
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("最大输出 tokens")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        TextField {
                            id: maxTokensField
                            Layout.fillWidth: true
                            placeholderText: qsTr("留空 = 服务默认")
                            placeholderTextColor: Theme.textSecondary
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontMono
                            validator: IntValidator { bottom: 1 }
                            background: Rectangle {
                                radius: 6
                                color: Theme.bgPrimary
                                border.width: maxTokensField.activeFocus ? 2 : 1
                                border.color: maxTokensField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                            }
                            onEditingFinished: page.submitField("maxOutputTokens", text.trim())
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        visible: page.editing && page.editing.provider === "ollama"
                        Text {
                            text: qsTr("num_ctx（上下文窗口）")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        TextField {
                            id: numCtxField
                            Layout.fillWidth: true
                            placeholderText: qsTr("留空 = 不发送")
                            placeholderTextColor: Theme.textSecondary
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontMono
                            validator: IntValidator { bottom: 1 }
                            background: Rectangle {
                                radius: 6
                                color: Theme.bgPrimary
                                border.width: numCtxField.activeFocus ? 2 : 1
                                border.color: numCtxField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                            }
                            onEditingFinished: page.submitField("numCtx", text.trim())
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    text: page.editing && page.editing.id === Settings.activeProfileId ? qsTr("✓ 使用中") : qsTr("启用此配置")
                    highlighted: page.editing && page.editing.id !== Settings.activeProfileId
                    enabled: page.editing && page.editing.id !== Settings.activeProfileId
                    onClicked: Settings.setActiveProfile(page.editing.id)
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("删除")
                    buttonColor: "transparent"
                    textColor: Theme.statusDanger
                    border.color: Theme.borderSubtle
                    enabled: Settings.profiles.length > 1
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: { Settings.removeProfile(page.editing.id); page.editingId = Settings.activeProfileId }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: page.testing ? qsTr("正在连接…") : page.testResult
                    color: page.testing ? Theme.textSecondary : (page.testOk ? Theme.statusSuccess : Theme.statusDanger)
                    font.pixelSize: 10; font.family: Theme.fontSans
                    elide: Text.ElideRight
                    visible: page.testing || page.testResult !== ""
                }
                AppButton {
                    text: page.testing ? qsTr("测试中…") : qsTr("测试连接")
                    enabled: !page.testing && page.editing !== null
                    onClicked: {
                        page.testing = true
                        page.testResult = ""
                        ConnectionTester.test(page.editing.provider, page.editing.baseUrl, page.editing.apiKey, page.editing.model)
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

        ColumnLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.fillWidth: true
            spacing: 8

            Text { text: qsTr("通用"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("上下文窗口 (Tokens)"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                TextField {
                    id: contextField
                    Layout.fillWidth: true
                    text: Settings.contextTokens
                    placeholderText: "8192"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary; selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                    font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans
                    validator: IntValidator { bottom: 2048; top: 1048576 }
                    background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: contextField.activeFocus ? 2 : 1; border.color: contextField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    onEditingFinished: { const parsed = parseInt(text); Settings.contextTokens = isNaN(parsed) ? 8192 : parsed; text = Settings.contextTokens }
                }
            }

            CheckBox { checked: Settings.simulationMode; onToggled: Settings.simulationMode = checked; text: qsTr("仿真演示模式（不调用真实 LLM）"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
            CheckBox { checked: Settings.allowFullDiskRead; onToggled: Settings.allowFullDiskRead = checked; text: qsTr("允许读取整个磁盘"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
            CheckBox { checked: Settings.localOnlyMode; onToggled: Settings.localOnlyMode = checked; text: qsTr("仅本地模式"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
            CheckBox { checked: Settings.allowLocalNetworkFetch; onToggled: Settings.allowLocalNetworkFetch = checked; text: qsTr("允许 http_fetch 访问本机与内网地址"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("日志级别"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                ComboBox {
                    Layout.preferredWidth: 220
                    property var levelNames: ["debug", "info", "warning", "critical"]
                    model: levelNames
                    onActivated: Settings.logLevel = currentText
                    Component.onCompleted: { const i = levelNames.indexOf(Settings.logLevel); currentIndex = i >= 0 ? i : 1 }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("知识库嵌入模型"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                ComboBox {
                    Layout.fillWidth: true
                    editable: true
                    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
                    editText: Settings.embeddingModel
                    onActivated: Settings.embeddingModel = currentText
                    onAccepted: Settings.embeddingModel = editText
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

        ColumnLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 8
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "NexusAI"; color: Theme.textPrimary; font.pixelSize: Theme.fontSizeH2; font.bold: true; font.family: Theme.fontSans }
                Rectangle {
                    width: versionBadge.implicitWidth + 14; height: 18; radius: 9
                    color: Theme.brandPrimary; opacity: 0.15
                    Text { id: versionBadge; anchors.centerIn: parent; text: "v" + Settings.appVersion; color: Theme.brandPrimary; font.pixelSize: 10; font.bold: true; font.family: Theme.fontMono }
                }
                Item { Layout.fillWidth: true }
            }
            Text { text: qsTr("本地智能体桌面应用 · 多模型提供商 · 工具调用 · 本地知识库"); color: Theme.textSecondary; font.pixelSize: 10; font.family: Theme.fontSans; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}
