import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    id: page

    Accessible.name: qsTr("通用设置页")

    // Future "general" preferences registered in PreferenceRegistry render
    // here; today the category is empty and the section stays hidden.
    property var preferenceKeys: PreferenceRegistry.preferencesInCategory("general")

    // docs/17 B2: 恢复默认会重置安全开关（全盘读取/仅本地模式/内网访问），
    // 必须二次确认后才执行。
    function requestResetDefaults() {
        if (Settings.allowFullDiskRead || Settings.localOnlyMode
                || Settings.allowLocalNetworkFetch)
            resetConfirmDialog.open()
        else
            Settings.resetGeneralToDefaults()
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

            Item { Layout.preferredHeight: 4 }

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

            // ── 安全与隐私（docs/17 B5）────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 8

                Text { text: qsTr("安全与隐私"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    CheckBox { checked: Settings.allowFullDiskRead; onToggled: Settings.allowFullDiskRead = checked; text: qsTr("允许读取整个磁盘"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
                    Text {
                        text: qsTr("关闭时文件读取仅限文档/下载/桌面/应用目录；开启后除系统目录外全部可读，模型读到的内容可能发送给云端模型。")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        opacity: 0.8
                        Layout.leftMargin: 32
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    CheckBox { checked: Settings.localOnlyMode; onToggled: Settings.localOnlyMode = checked; text: qsTr("仅本地模式"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
                    Text {
                        text: qsTr("使用云端模型时拦截知识库搜索、联网搜索与全部 MCP 工具（工具页会显示拦截徽标）；本地模型不受影响。")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        opacity: 0.8
                        Layout.leftMargin: 32
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    CheckBox { checked: Settings.allowLocalNetworkFetch; onToggled: Settings.allowLocalNetworkFetch = checked; text: qsTr("允许 http_fetch 访问本机与内网地址"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
                    Text {
                        text: qsTr("关闭时 http_fetch 拒绝本机/内网目标（防 SSRF）；仅在确有访问内网服务的需求时开启。")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        opacity: 0.8
                        Layout.leftMargin: 32
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

                CheckBox { checked: Settings.simulationMode; onToggled: Settings.simulationMode = checked; text: qsTr("仿真演示模式（不调用真实 LLM）"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }

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
                        Accessible.name: qsTr("上下文窗口 Tokens")
                    }
                    Text {
                        text: qsTr("默认值。按模型能力表自动适配时，此项作为未知模型的 fallback。")
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
                    selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                    wrapMode: TextArea.Wrap
                    font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontMono
                    background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: promptField.activeFocus ? 2 : 1; border.color: promptField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    onEditingFinished: Settings.systemPromptOverride = text
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

            // ── 诊断 ──────────────────────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 4

                Text { text: qsTr("诊断"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                ComboBox {
                    id: logLevelCombo
                    Layout.preferredWidth: 220
                    property var levelNames: ["debug", "info", "warning", "critical"]
                    model: levelNames
                    onActivated: Settings.logLevel = currentText
                    Component.onCompleted: { const i = levelNames.indexOf(Settings.logLevel); currentIndex = i >= 0 ? i : 1 }
                    Accessible.name: qsTr("日志级别")
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

                Text { text: qsTr("嵌入模型"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                ComboBox {
                    Layout.fillWidth: true
                    editable: true
                    model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
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
                    Layout.fillWidth: true
                    text: Settings.embeddingBaseUrl
                    placeholderText: qsTr("http://localhost:11434")
                    onEditingFinished: Settings.embeddingBaseUrl = text
                    selectByMouse: true
                    Accessible.name: qsTr("嵌入服务地址")
                }
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

    // docs/17 B2: 恢复默认的二次确认——安全开关一并回落。
    Dialog {
        id: resetConfirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        modal: true
        closePolicy: Popup.CloseOnEscape

        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("恢复通用设置默认值？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("将重置上下文窗口、系统提示词、日志级别、嵌入配置，")
                      + qsTr("并关闭「允许读取整个磁盘」「仅本地模式」「允许内网访问」三个安全开关。确定继续吗？")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                AppButton {
                    text: qsTr("取消")
                    onClicked: resetConfirmDialog.close()
                }
                AppButton {
                    text: qsTr("恢复默认")
                    highlighted: true
                    onClicked: {
                        resetConfirmDialog.close()
                        Settings.resetGeneralToDefaults()
                    }
                }
            }
        }
    }
}
