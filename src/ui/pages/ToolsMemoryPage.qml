import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// L1: settings page for web search (T6) and episodic long-term memory (T8).
// Both bind directly to SettingsService properties and save in real time -
// the bottom "save / cancel" bar does not apply here (same convention as
// GeneralPage).
Item {
    id: page

    Accessible.name: qsTr("工具与记忆设置页")
    Accessible.description: qsTr("配置联网搜索与长期记忆")

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

            Text {
                text: qsTr("工具与记忆")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: 24
                Layout.rightMargin: 24
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

            // ── 长期记忆 (T8) ──────────────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("长期记忆")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Text {
                    text: qsTr("开启后，每次任务结束会生成会话摘要并在新会话中注入相关历史，帮助模型记住此前聊过的内容。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("启用长期记忆")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    Switch {
                        checked: Settings.episodicMemoryEnabled
                        onToggled: Settings.episodicMemoryEnabled = checked
                        Accessible.name: qsTr("启用长期记忆")
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

            // ── 联网搜索 (T6) ──────────────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 16
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("联网搜索")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Text {
                    text: qsTr("启用后模型可调用 web_search 获取实时信息。搜索查询会发送至所配置的搜索服务商，请勿在查询中包含敏感信息。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("搜索服务")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        id: providerCombo
                        Layout.preferredWidth: 200
                        model: Settings.webSearchProviders
                        currentIndex: {
                            const i = Settings.webSearchProviders.indexOf(Settings.webSearchProvider)
                            return i >= 0 ? i : 0
                        }
                        onActivated: Settings.webSearchProvider = currentText
                        Accessible.name: qsTr("搜索服务")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: qsTr("搜索 API Key")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: apiKeyField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: Settings.webSearchApiKeySet
                                             ? qsTr("已配置（输入新值可覆盖，留空不修改）")
                                             : qsTr("未配置")
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
                            Accessible.name: qsTr("搜索 API Key")
                            // Non-empty input overwrites the stored key; an empty
                            // commit is a no-op (clearing is an explicit action).
                            onEditingFinished: {
                                if (text.length > 0) {
                                    Settings.webSearchApiKey = text
                                    text = ""
                                }
                            }
                        }

                        AppButton {
                            text: qsTr("清除密钥")
                            enabled: Settings.webSearchApiKeySet
                            opacity: enabled ? 1.0 : 0.4
                            onClicked: Settings.webSearchApiKey = ""
                            Accessible.name: qsTr("清除搜索 API Key")
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
