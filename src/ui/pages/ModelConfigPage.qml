import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

ScrollView {
    id: page
    clip: true

    Accessible.name: qsTr("模型配置页")

    property string editingId: Settings.activeProfileId
    property var editing: {
        const list = Settings.profiles
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === editingId) return list[i]
        return list.length > 0 ? list[0] : null
    }
    // 主流长度档位。上下文按 1024 进制取整（1M 与 AIAgent::MaxContextTokens
    // 的天花板对齐），最大输出同理；标签给人看的写法，值给机器算的写法。
    readonly property var contextPresets: [
        { v: 131072,  label: "128K" },
        { v: 262144,  label: "256K" },
        { v: 307200,  label: "300K" },
        { v: 1048576, label: "1M" }
    ]
    readonly property var outputPresets: [
        { v: 32768,  label: "32K" },
        { v: 65536,  label: "64K" },
        { v: 131072, label: "128K" },
        { v: 262144, label: "256K" }
    ]

    // 当前编辑 profile 所属厂商可选的线协议（各自带默认端点）。同一个厂商可以
    // 有多条协议（OpenAI 兼容 = Chat Completions / Responses），协议决定报文
    // 格式与请求路径，厂商只决定默认值。
    readonly property var protocolOptions: editing
        ? Settings.endpointOptions(editing.provider) : []

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
        contextLengthField.text = p && p.contextLength !== undefined
            ? String(p.contextLength) : ""
    }

    function submitField(key, value) {
        if (!editing) return
        Settings.updateProfile(editing.id, { [key]: value })
        // A newly typed model name joins the profile's model list too, so
        // the list below (and the chat-side model picker) stays complete.
        if (key === "model" && value.trim() !== "")
            Settings.addModelToProfile(editing.id, value.trim())
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
                            // 同一厂商的两条协议（Chat Completions / Responses）光看
                            // 厂商名分不出来，所以在列表里带上协议标签。
                            text: {
                                const name = modelData.displayName || modelData.provider
                                const opts = Settings.endpointOptions(modelData.provider)
                                if (opts.length < 2) return name
                                for (let i = 0; i < opts.length; ++i)
                                    if (opts[i].apiProtocol === modelData.apiProtocol)
                                        return name + " · " + opts[i].label
                                return name
                            }
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
                    text: qsTr("+ 添加")
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

            // 接口协议：厂商确定默认值，协议才确定报文格式与请求路径。只有一个
            // 协议的厂商不显示这一行，避免无意义的单选。
            ColumnLayout {
                visible: page.protocolOptions.length > 1
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("接口协议"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                RowLayout {
                    spacing: 8
                    Repeater {
                        model: page.protocolOptions
                        AppButton {
                            text: modelData.label
                            highlighted: page.editing && page.editing.apiProtocol === modelData.apiProtocol
                            onClicked: page.submitFields({"apiProtocol": modelData.apiProtocol,
                                                          "baseUrl": modelData.baseUrl,
                                                          "model": modelData.model})
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    visible: page.editing && page.editing.apiProtocol === "openai-responses"
                    Layout.fillWidth: true
                    text: qsTr("Responses 需要端点显式实现 /responses；多数“OpenAI 兼容”网关只提供 Chat Completions。")
                    color: Theme.textSecondary
                    font.pixelSize: 10; font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
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
                    placeholderText: page.editing && page.editing.provider === "anthropic" ? "sk-ant-..." : qsTr("sk-...（或设置环境变量 OPENAI_API_KEY）")
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

            // ── Model list of this profile ─────────────────────
            // The chat-side model picker lists profile.models; this section
            // surfaces the same list in the editor (click = make current,
            // × = remove, type + Enter = add).
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("模型列表"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: page.editing ? (page.editing.models || []) : []

                        delegate: Rectangle {
                            width: modelChipRow.implicitWidth + 22
                            height: 26
                            radius: 4
                            readonly property bool isCurrent: page.editing && modelData === page.editing.model
                            color: isCurrent ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g, Theme.brandPrimary.b, 0.15) : Theme.bgPrimary
                            border.width: 1
                            border.color: isCurrent ? Theme.brandPrimary : Theme.borderSubtle

                            MouseArea {
                                id: chipMA
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                // Click anywhere on the chip to make this the
                                // current model; the × zone removes it.
                                onClicked: Settings.selectModelForProfile(page.editingId, modelData)
                            }

                            Row {
                                id: modelChipRow
                                anchors.centerIn: parent
                                spacing: 6

                                Text {
                                    text: modelData
                                    color: isCurrent ? Theme.brandPrimary : Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: {
                                        const cw = Settings.modelContextWindow(
                                            page.editing.apiProtocol, modelData)
                                        return cw > 0 ? qsTr("%1K").arg(Math.round(cw / 1000))
                                                      : qsTr("未知")
                                    }
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                    font.family: Theme.fontMono
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Item {
                                    width: 14
                                    height: parent.height

                                    Text {
                                        anchors.centerIn: parent
                                        text: "×"
                                        color: removeMA.containsMouse ? Theme.statusDanger : Theme.textSecondary
                                        font.pixelSize: 12
                                    }

                                    MouseArea {
                                        id: removeMA
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Settings.removeModelFromProfile(page.editingId, modelData)
                                    }
                                }
                            }
                        }
                    }

                    TextField {
                        id: addModelField
                        width: 150
                        height: 26
                        placeholderText: qsTr("添加模型…")
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                        font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono
                        background: Rectangle { radius: 4; color: Theme.bgPrimary; border.width: addModelField.activeFocus ? 2 : 1; border.color: addModelField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                        onAccepted: {
                            if (text.trim() !== "" && page.editing) {
                                Settings.addModelToProfile(page.editingId, text.trim())
                                text = ""
                            }
                        }
                    }
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
                        SizePresetRow {
                            Layout.fillWidth: true
                            entries: page.outputPresets
                            currentValue: parseInt(maxTokensField.text) || 0
                            onChosen: (value) => {
                                maxTokensField.text = value === "" ? "" : String(value)
                                page.submitField("maxOutputTokens", maxTokensField.text)
                            }
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
                        // Ollama 的"上下文长度"就是 num_ctx，用同一批档位
                        SizePresetRow {
                            Layout.fillWidth: true
                            entries: page.contextPresets
                            currentValue: parseInt(numCtxField.text) || 0
                            onChosen: (value) => {
                                numCtxField.text = value === "" ? "" : String(value)
                                page.submitField("numCtx", numCtxField.text)
                            }
                        }
                    }
                }

                // ── 思考强度 ─────────────────────────────────────
                // 与聊天输入区的快捷选择框读写同一个 profile 字段，不存在
                // 第二份真相。档位由各 Provider 折算：OpenAI → reasoning_effort，
                // Anthropic → thinking.budget_tokens，Ollama → 布尔 think。
                ColumnLayout {
                    id: effortSection
                    Layout.fillWidth: true
                    spacing: 4
                    readonly property string effort:
                        page.editing ? (page.editing.reasoningEffort || "") : ""

                    Text {
                        text: qsTr("思考强度（留默认 = 不随请求发送思考参数）")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    RowLayout {
                        spacing: 8
                        AppButton {
                            text: qsTr("默认")
                            highlighted: effortSection.effort === ""
                            onClicked: page.submitField("reasoningEffort", "")
                        }
                        AppButton {
                            text: qsTr("关闭")
                            highlighted: effortSection.effort === "off"
                            onClicked: page.submitField("reasoningEffort", "off")
                        }
                        AppButton {
                            text: qsTr("低")
                            highlighted: effortSection.effort === "low"
                            onClicked: page.submitField("reasoningEffort", "low")
                        }
                        AppButton {
                            text: qsTr("中")
                            highlighted: effortSection.effort === "medium"
                            onClicked: page.submitField("reasoningEffort", "medium")
                        }
                        AppButton {
                            text: qsTr("高")
                            highlighted: effortSection.effort === "high"
                            onClicked: page.submitField("reasoningEffort", "high")
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("OpenAI 兼容按档位发送 reasoning_effort；Claude 折算为思考预算（需输出上限留得住预算，否则自动退回不思考）；Ollama 只有开/关，低中高均按开处理。")
                        color: Theme.textSecondary
                        opacity: 0.8
                        font.pixelSize: 10
                        font.family: Theme.fontSans
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("上下文预算覆盖（留空 = 按模型能力表自动）")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                    }
                    TextField {
                        id: contextLengthField
                        Layout.fillWidth: true
                        placeholderText: qsTr("留空 = 自动")
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontMono
                        validator: IntValidator { bottom: 2048; top: 1048576 }
                        background: Rectangle {
                            radius: 6
                            color: Theme.bgPrimary
                            border.width: contextLengthField.activeFocus ? 2 : 1
                            border.color: contextLengthField.activeFocus
                                ? Theme.brandPrimary : Theme.borderSubtle
                        }
                        onEditingFinished: page.submitField("contextLength", text.trim())
                    }
                    SizePresetRow {
                        Layout.fillWidth: true
                        entries: page.contextPresets
                        currentValue: parseInt(contextLengthField.text) || 0
                        onChosen: (value) => {
                            contextLengthField.text = value === "" ? "" : String(value)
                            page.submitField("contextLength", contextLengthField.text)
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
                        ConnectionTester.test(page.editing.apiProtocol, page.editing.baseUrl, page.editing.apiKey, page.editing.model)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
