import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 知识库嵌入：原「通用设置」的知识库嵌入区块独立成页。
//
// 独立于「记忆管理」：记忆页主体是摘要列表，设置项挤在列表上方会持续压缩
// 浏览区；嵌入本质是"知识库使用哪个向量服务"，与文档索引/检索同域。
Item {
    id: page

    Accessible.name: qsTr("知识库嵌入页")

    // 嵌入 provider 的本地化显示名。provider 的「值集合」单一来源是
    // Settings.embeddingProviders（schema options，与 main.cpp 后端工厂共用），
    // 此处只做展示层 label 映射。
    readonly property var embeddingProviderLabels: ({
        "ollama": qsTr("本地 Ollama"),
        "openai": qsTr("OpenAI 兼容服务")
    })

    // 嵌入模型预设：本地 Ollama 与外部 OpenAI 兼容服务的模型生态不同，
    // 下拉候选随当前 provider 联动。
    readonly property var embeddingModelPresets: ({
        "ollama": ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"],
        "openai": ["text-embedding-3-small", "text-embedding-3-large", "text-embedding-ada-002"]
    })

    function requestResetDefaults() {
        Settings.resetEmbeddingToDefaults()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 32

        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: flick.width
            spacing: 16

            Item { Layout.preferredHeight: 4 }

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("知识库嵌入")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("文档导入知识库时用这里的向量服务生成索引；修改模型或地址后需要重新索引已有文档。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            SettingCard {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.bottomMargin: 16
                title: qsTr("向量服务")
                subtitle: qsTr("本地 Ollama 数据不出本机；外部服务会把文档内容发送到所配置的端点。")

                SettingRow {
                    objectName: "embedding/provider"
                    label: qsTr("嵌入服务提供方")

                    ComboBox {
                        id: embeddingProviderCombo
                        Layout.preferredWidth: 220
                        // 值单一来源 Settings.embeddingProviders（schema options，
                        // 与后端工厂共用），仅 label 在此本地化映射。
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
                }

                // provider 切换后当前 model 若不在新预设列表中，自动重置为
                // 新预设第一项（持久化值与显示脱节防御）。
                Connections {
                    target: Settings
                    function onEmbeddingProviderChanged() {
                        const presets = page.embeddingModelPresets[Settings.embeddingProvider] || []
                        if (!presets.includes(Settings.embeddingModel) && presets.length > 0) {
                            Settings.embeddingModel = presets[0]
                        }
                    }
                }

                SettingRow {
                    objectName: "embedding/model"
                    label: qsTr("嵌入模型")

                    ComboBox {
                        id: embeddingModelCombo
                        Layout.preferredWidth: 260
                        editable: true
                        // 模型预设随 provider 切换联动：候选列表不再硬编码
                        // Ollama 一组，切到外部服务后跟随其模型生态。
                        model: page.embeddingModelPresets[Settings.embeddingProvider] || []
                        // QQuickComboBox::componentComplete() resets an editable
                        // combo's editText to currentText (the first item), which
                        // wiped the binding below on every page build — the saved
                        // model showed up as the default again. Selecting the
                        // persisted index keeps currentText in sync with the saved
                        // value; setEditText() no-ops when the text already matches.
                        currentIndex: model.indexOf(Settings.embeddingModel)
                        editText: Settings.embeddingModel
                        onActivated: Settings.embeddingModel = currentText
                        onAccepted: Settings.embeddingModel = editText
                        // Editable combos only fire accepted() on Enter; commit on
                        // focus loss too so a freely typed model name survives
                        // clicking elsewhere.
                        onFocusChanged: if (!focus) Settings.embeddingModel = editText
                        Component.onCompleted: if (editText !== Settings.embeddingModel)
                            editText = Settings.embeddingModel
                        Accessible.name: qsTr("知识库嵌入模型")
                    }
                }

                SettingsField {
                    id: embeddingUrlField
                    objectName: "embedding/baseUrl"
                    label: qsTr("嵌入服务地址")
                    mono: true
                    text: Settings.embeddingBaseUrl
                    lastKnownValue: Settings.embeddingBaseUrl
                    // 外部服务两种粘贴形态（带/不带 /v1）都可用，后端拼接层
                    // 已做容错，提示里写明即可。
                    placeholderText: Settings.embeddingProvider === "openai"
                        ? qsTr("https://api.openai.com 或 https://api.siliconflow.cn，可带或不带 /v1")
                        : qsTr("http://localhost:11434")
                    // Loader 销毁不触发 editingFinished。
                    commitDebounce: 400
                    commitOnDestroy: true
                    onSubmitted: (value) => Settings.embeddingBaseUrl = value
                }

                // API key 密码框，仅外部 provider 可见（本地 Ollama 无需密钥；
                // ColumnLayout 中 visible: false 即不占位）。密钥不回显明文，
                // 只以 placeholder 呈现"已配置/未配置"，输入新值覆盖、留空
                // 不修改。
                RowLayout {
                    Layout.fillWidth: true
                    visible: Settings.embeddingProvider === "openai"
                    spacing: 8

                    SettingsField {
                        id: embeddingApiKeyField
                        objectName: "embedding/apiKey"
                        label: qsTr("向量模型 API Key")
                        labelWidth: 140
                        secret: true
                        // 密文字段读 text（不是 displayText），空值不提交；
                        // 写入成功后清空输入框。
                        ignoreEmptySubmit: true
                        clearOnSubmit: true
                        commitOnDestroy: true
                        placeholderText: Settings.embeddingApiKeySet
                            ? qsTr("已配置（输入新值可覆盖，留空不修改）")
                            : qsTr("未配置")
                        onSubmitted: (value) => Settings.embeddingApiKey = value
                    }

                    AppButton {
                        text: qsTr("清除密钥")
                        enabled: Settings.embeddingApiKeySet
                        opacity: enabled ? 1.0 : 0.4
                        onClicked: Settings.embeddingApiKey = ""
                        Accessible.name: qsTr("清除向量模型 API Key")
                    }
                }

                // 隐私外发提示：仅外部 provider 时文档内容会离开本机生成向量。
                Text {
                    visible: Settings.embeddingProvider === "openai"
                    text: qsTr("文档内容将发送到所配置的外部服务生成向量，请勿导入敏感资料。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    opacity: 0.8
                }
                // 纯本地模式开启时外部请求会被装配层拦截并回退本地 Ollama，
                // 提示用户当前选择实际不会生效。
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
            }

            Item { Layout.fillHeight: true }
        }
    }
}
