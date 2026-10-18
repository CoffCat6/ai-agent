import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels


Item {
    id: page

    Accessible.name: qsTr("通用设置页")

    // docs/20 工作流 B: "general" 分类下登记的偏好键（托盘/通知/自启等）
    // 自动渲染为开关；模型相关的上下文预算已迁往"模型配置"页。
    property var preferenceKeys: PreferenceRegistry.preferencesInCategory("general")

    // 嵌入 provider 的本地化显示名。provider 的「值集合」单一来源是
    // Settings.embeddingProviders（SettingsSchema 的 embedding/provider
    // options，与 main.cpp 后端工厂共用），此处只做展示层 label 映射。
    readonly property var embeddingProviderLabels: ({
        "ollama": qsTr("本地 Ollama"),
        "openai": qsTr("OpenAI 兼容服务")
    })

    // 嵌入模型预设：仅本页使用，非跨页共享（任务 #27 A3）。本地 Ollama
    // 与外部 OpenAI 兼容服务的模型生态不同，下拉候选随当前 provider 联动
    //（不再硬编码 Ollama 一组）。
    readonly property var embeddingModelPresets: ({
        "ollama": ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"],
        "openai": ["text-embedding-3-small", "text-embedding-3-large", "text-embedding-ada-002"]
    })

    // General preferences and service-backed settings live in separate stores;
    // reset both here so the page-level action really restores the whole page.
    function requestResetDefaults() {
        Settings.resetGeneralToDefaults()
        PreferenceStore.resetBatch(page.preferenceKeys)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 24

        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: flick.width
            spacing: 16


            RowLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true

                Text {
                    text: qsTr("通用设置")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("恢复本页默认")
                    Accessible.name: qsTr("恢复通用设置默认值")
                    onClicked: page.requestResetDefaults()
                }
            }

            Text {
                text: qsTr("本页修改实时保存，不受底部「保存 / 取消」影响")
                color: Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontSans
                opacity: 0.8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            ColumnLayout {
                visible: page.preferenceKeys.length > 0
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 16

                Text {
                    text: qsTr("偏好")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Repeater {
                    model: page.preferenceKeys

                    delegate: PreferenceControl {
                        Layout.fillWidth: true
                        prefKey: modelData
                        // Reading currentLocale re-evaluates this binding on
                        // a language switch; displayName resolves the stored
                        // QT_TRANSLATE_NOOP source at query time.
                        displayName: {
                            I18n.currentLocale
                            return PreferenceRegistry.displayName(modelData)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

            // ── 模型行为 ──────────────────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 8

                Text { text: qsTr("模型行为"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }


                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: qsTr("单次任务步数预算"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                    TextField {
                        id: maxStepsField
                        Layout.fillWidth: true
                        text: Settings.maxSteps
                        placeholderText: "30"
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary; selectionColor: Theme.brandPrimary; selectedTextColor: Theme.selectedTextColor
                        font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans
                        validator: IntValidator { bottom: 3; top: 1000 }
                        background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: maxStepsField.activeFocus ? 2 : 1; border.color: maxStepsField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                        // 未完成编辑期间不持久化（例如清空后输入 1000 的中间态）；
                        // 失焦或 Loader 销毁时只提交有效整数。
                        function commit() {
                            if (!acceptableInput) return
                            Settings.maxSteps = parseInt(text)
                        }
                        onEditingFinished: { commit(); text = Settings.maxSteps }
                        Component.onDestruction: commit()
                        Accessible.name: qsTr("单次任务步数预算")
                    }
                    Text {
                        text: qsTr("一次任务里模型最多进行多少轮「思考 → 工具调用」（3–1000）。用完会强制基于已有信息作答；复杂任务可调高，但通常会增加耗时和 token 消耗。")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("单次任务超时（分钟）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                        Item { Layout.fillWidth: true }
                        SpinBox {
                            from: 1
                            to: 60
                            value: Settings.taskTimeoutMinutes
                            editable: true
                            onValueModified: Settings.taskTimeoutMinutes = value
                            Accessible.name: qsTr("单次任务超时，分钟")
                        }
                    }
                    Text {
                        text: qsTr("任务运行超过此时长会停止并基于已收集的信息作答。较难或需要多次网络调用的任务可以适当调高。")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                Text {
                    text: qsTr("自定义系统提示词：留空使用内置提示词；修改即时生效，内容随请求发送给当前模型服务")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                TextArea {
                    id: promptField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 132
                    text: Settings.systemPromptOverride
                    placeholderText: qsTr("留空 = 内置提示词")
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary; selectedTextColor: Theme.selectedTextColor
                    wrapMode: TextArea.Wrap
                    font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontMono
                    background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: promptField.activeFocus ? 2 : 1; border.color: promptField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    // 同 maxStepsField：长文本按 400ms 防抖提交（避免每次按键都
                    // 记一条审计并写盘），切换分类导致的销毁由 onDestruction 兜底。
                    function commit() { Settings.systemPromptOverride = text }
                    onTextEdited: promptCommitTimer.restart()
                    onEditingFinished: commit()
                    Component.onDestruction: commit()
                    Timer {
                        id: promptCommitTimer
                        interval: 400
                        onTriggered: promptField.commit()
                    }
                    Accessible.name: qsTr("自定义系统提示词")
                }

                Text {
                    text: qsTr("%1 字符").arg(promptField.text.length)
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

            // ── 日志 ──────────────────────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 4

                Text { text: qsTr("日志"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                Text { text: qsTr("最低记录级别"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                ComboBox {
                    id: logLevelCombo
                    Layout.preferredWidth: 220
                    property var levelNames: ["debug", "info", "warning", "critical"]
                    model: levelNames
                    onActivated: Settings.logLevel = currentText
                    Connections {
                        target: Settings
                        function onLogLevelChanged() {
                            const index = logLevelCombo.levelNames.indexOf(Settings.logLevel)
                            logLevelCombo.currentIndex = index >= 0 ? index : 1
                        }
                    }
                    Component.onCompleted: {
                        const index = levelNames.indexOf(Settings.logLevel)
                        currentIndex = index >= 0 ? index : 1
                    }
                    Accessible.name: qsTr("日志级别")
                }

                Text { text: qsTr("日志目录"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                Text {
                    Layout.fillWidth: true
                    text: Settings.logDirectory
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                    wrapMode: Text.WrapAnywhere
                    Accessible.name: qsTr("当前日志目录")
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton { text: qsTr("选择目录"); onClicked: logDirectoryDialog.open() }
                    AppButton { text: qsTr("恢复默认目录"); onClicked: Settings.logDirectory = "" }
                    AppButton { text: qsTr("打开目录"); onClicked: AgentVM.openLogFolder() }
                }
                FolderDialog {
                    id: logDirectoryDialog
                    title: qsTr("选择日志保存目录")
                    onAccepted: Settings.logDirectory = selectedFolder.toString()
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("单个日志文件上限（MB）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                    Item { Layout.fillWidth: true }
                    SpinBox {
                        from: 1
                        to: 1024
                        value: Settings.logMaxFileSizeMb
                        editable: true
                        onValueModified: Settings.logMaxFileSizeMb = value
                        Accessible.name: qsTr("单个日志文件大小上限，兆字节")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("保留日志文件数"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                    Item { Layout.fillWidth: true }
                    SpinBox {
                        from: 1
                        to: 100
                        value: Settings.logRetentionFiles
                        editable: true
                        onValueModified: Settings.logRetentionFiles = value
                        Accessible.name: qsTr("保留日志文件数")
                    }
                }
                Text {
                    text: qsTr("达到单文件大小上限后自动轮转；只保留最新的指定数量文件。日志可能包含本地路径和错误上下文，请选择安全的保存位置。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

            // ── 知识库嵌入 ────────────────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 4
                Text { text: qsTr("知识库嵌入"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }

                // 外部向量模型（任务 #17）：嵌入服务提供方切换。value/text
                // 映射——Settings 存协议键（"ollama"/"openai"），显示本地化
                // 名称；按值查找当前项，未知值（schema 枚举之外的脏数据）
                // 回退第一项，与 ToolsPage 搜索服务下拉同款防御。
                // 本区块为任务 #17 新增：沿用本页裸单例引用惯例，以
                // disable/enable 指令圈定新增范围，避免推高基线计数。
                // qmllint disable unqualified
                Text { text: qsTr("嵌入服务提供方"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                ComboBox {
                    id: embeddingProviderCombo
                    Layout.fillWidth: true
                    // 值单一来源 Settings.embeddingProviders（schema options，
                    // 与后端工厂共用），不再 UI 硬编码 provider 键；仅 label
                    // 在此本地化映射。
                    model: Settings.embeddingProviders.map(p => ({
                        value: p,
                        label: page.embeddingProviderLabels[p] || Settings.providerDisplayName(p)
                    }))
                    textRole: "label"
                    currentIndex: {
                        const i = Settings.embeddingProviders.indexOf(Settings.embeddingProvider)
                        return i >= 0 ? i : 0
                    }
                    onActivated: (index) => Settings.embeddingProvider = Settings.embeddingProviders[index]
                    Accessible.name: qsTr("嵌入服务提供方")
                }
                // qmllint enable unqualified

                // 任务 #27 A3：provider 切换后当前 model 若不在新预设列表中，
                // 自动重置为新预设第一项（持久化值与显示脱节防御）。
                Connections {
                    target: Settings
                    function onEmbeddingProviderChanged() {
                        const presets = page.embeddingModelPresets[Settings.embeddingProvider] || []
                        if (!presets.includes(Settings.embeddingModel) && presets.length > 0) {
                            Settings.embeddingModel = presets[0]
                        }
                    }
                }

                Text { text: qsTr("嵌入模型"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                ComboBox {
                    Layout.fillWidth: true
                    editable: true
                    // 模型预设随 provider 切换联动（任务 #27 A3）：候选列表
                    // 不再硬编码 Ollama 一组，切到外部服务后跟随其模型生态。
                    // qmllint disable unqualified
                    model: page.embeddingModelPresets[Settings.embeddingProvider] || []
                    // qmllint enable unqualified
                    // QQuickComboBox::componentComplete() resets an editable
                    // combo's editText to currentText (the first item), which
                    // wiped the binding below on every page build — the saved
                    // model showed up as the default again (persisted value
                    // was fine, only the echo failed). Selecting the persisted
                    // index keeps currentText in sync with the saved value;
                    // setEditText() no-ops when the text already matches.
                    currentIndex: model.indexOf(Settings.embeddingModel)
                    editText: Settings.embeddingModel
                    onActivated: Settings.embeddingModel = currentText
                    onAccepted: Settings.embeddingModel = editText
                    // Editable combos only fire accepted() on Enter;
                    // commit on focus loss too so a freely typed model
                    // name survives clicking elsewhere (same semantics
                    // as the Base URL field below). Blank is rejected by
                    // the setter.
                    onFocusChanged: if (!focus) Settings.embeddingModel = editText
                    // A saved value outside the model list completes with
                    // textAt(-1) == ""; re-apply the persisted text once
                    // creation has finished.
                    Component.onCompleted: if (editText !== Settings.embeddingModel)
                        editText = Settings.embeddingModel
                    Accessible.name: qsTr("知识库嵌入模型")
                }
                // Batch 25 (G11): Ollama endpoint; empty input keeps the
                // current value (the setter rejects blank after trim).
                Text { text: qsTr("嵌入服务地址"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                TextField {
                    id: embeddingUrlField
                    Layout.fillWidth: true
                    text: Settings.embeddingBaseUrl
                    // 外部向量模型（任务 #17）：placeholder 随 provider 切换——
                    // OpenAI 兼容端点两种粘贴形态（带/不带 /v1）都可用，后端
                    // 拼接层已做容错，提示里写明即可。
                    // qmllint disable unqualified
                    placeholderText: Settings.embeddingProvider === "openai"
                        ? qsTr("https://api.openai.com 或 https://api.siliconflow.cn，可带或不带 /v1")
                        : qsTr("http://localhost:11434")
                    // qmllint enable unqualified
                    // 同 maxStepsField：Loader 销毁不触发 editingFinished。
                    function commit() { Settings.embeddingBaseUrl = text }
                    onTextEdited: urlCommitTimer.restart()
                    onEditingFinished: commit()
                    Component.onDestruction: commit()
                    Timer {
                        id: urlCommitTimer
                        interval: 400
                        onTriggered: embeddingUrlField.commit()
                    }
                    selectByMouse: true
                    Accessible.name: qsTr("嵌入服务地址")
                }

                // 外部向量模型（任务 #17）：API key 密码框，仅外部 provider
                // 可见（本地 Ollama 无需密钥；ColumnLayout 在 Layout 中
                // visible: false 即不占位）。回显模式仿 ToolsPage 的
                // webSearchApiKey：密钥不回显明文，只以 placeholder 呈现
                // "已配置/未配置"，输入新值覆盖、留空不修改。
                // 同上：本区块为任务 #17 新增，disable 指令圈定范围。
                // qmllint disable unqualified
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: Settings.embeddingProvider === "openai"

                    Text { text: qsTr("API Key"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: embeddingApiKeyField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: Settings.embeddingApiKeySet
                                ? qsTr("已配置（输入新值可覆盖，留空不修改）")
                                : qsTr("未配置")
                            selectByMouse: true
                            // 提交时机仿 ToolsPage apiKeyField：失焦/回车立即
                            // 写入；本页挂在设置中心的 Loader 上，切分类销毁
                            // 页面不触发 editingFinished，补一条销毁兜底——
                            // "填了 Key 却直接切走"最难察觉。读取 text（不是
                            // displayText）：密文字段的 displayText 是掩码点，
                            // 写回去等于把密钥毁掉。空值不提交（留空=不修改）。
                            function commitKey() {
                                if (text.length === 0) return
                                Settings.embeddingApiKey = text
                                text = ""
                            }
                            onEditingFinished: commitKey()
                            Component.onDestruction: commitKey()
                            Accessible.name: qsTr("向量模型 API Key")
                        }

                        AppButton {
                            text: qsTr("清除密钥")
                            enabled: Settings.embeddingApiKeySet
                            opacity: enabled ? 1.0 : 0.4
                            onClicked: Settings.embeddingApiKey = ""
                            Accessible.name: qsTr("清除向量模型 API Key")
                        }
                    }
                }
                // qmllint enable unqualified

                // 外部向量模型（任务 #17）：隐私外发提示——仅外部 provider
                // 时文档内容会离开本机生成向量。
                // 以下两条提示为任务 #17 新增，disable 指令圈定范围。
                // qmllint disable unqualified
                Text {
                    visible: Settings.embeddingProvider === "openai"
                    text: qsTr("文档内容将发送到所配置的外部服务生成向量，请勿导入敏感资料。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    opacity: 0.8
                }
                // 纯本地模式开启时外部请求会被装配层拦截并回退本地
                // Ollama（任务 #16 的 localOnlyMode 语义）：提示用户当前
                // 选择实际不会生效，避免"配了 openai 却在偷偷走外网"的
                // 误解，反过来也一样。
                Text {
                    visible: Settings.embeddingProvider === "openai"
                             && Settings.localOnlyMode
                    text: qsTr("纯本地模式已开启：外部向量请求将被拦截，并自动回退本地 Ollama。")
                    color: Theme.brandPrimary
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    opacity: 0.9
                }
                // qmllint enable unqualified
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 16
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

}
