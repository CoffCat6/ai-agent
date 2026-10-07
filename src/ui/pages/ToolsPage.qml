import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    Accessible.name: qsTr("工具管理页")
    Flickable {
        anchors.fill: parent
        clip: true
        contentHeight: contentCol.implicitHeight + 24
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: parent.width
            spacing: 12

            Item { Layout.preferredHeight: 4 }

            Text {
                text: qsTr("工具管理")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Text {
                text: qsTr("控制哪些工具可被 AI 调用。禁用的工具不会暴露给模型。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Repeater {
                model: AgentVM.toolRegistryModel

                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    implicitHeight: row.implicitHeight + 20
                    color: Theme.bgSecondary
                    radius: 8

                    RowLayout {
                        id: row
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                spacing: 6
                                Text {
                                    text: modelData.name
                                    color: Theme.textPrimary
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontSans
                                }
                                Text {
                                    text: modelData.source === "MCP" ? "· MCP" : "· " + qsTr("内置")
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                    font.family: Theme.fontSans
                                }
                                Text {
                                    text: "· " + (modelData.level === "safe" ? qsTr("自动")
                                                : modelData.level === "requires_confirm" ? qsTr("需确认")
                                                : qsTr("危险"))
                                    color: modelData.level === "dangerous" ? "#e85d5d" : Theme.textSecondary
                                    font.pixelSize: 10
                                    font.family: Theme.fontSans
                                }
                                // docs/17 B1: 策略拦截徽标——Switch 只反映用户层，
                                // 被仅本地模式等策略拦截时在此可见。
                                Text {
                                    visible: modelData.blockedByPolicy === true
                                    text: "· " + qsTr("仅本地模式拦截")
                                    color: Theme.statusWarning
                                    font.pixelSize: 10
                                    font.bold: true
                                    font.family: Theme.fontSans
                                }
                            }

                            Text {
                                text: modelData.description
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                opacity: 0.85
                            }
                        }

                        Switch {
                            checked: modelData.enabled
                            onToggled: AgentVM.setToolEnabled(modelData.name, checked)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

            // ── 联网搜索 ──────────────────────────────
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
                    visible: Settings.webSearchProvider !== "duckduckgo"

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
                            // 提交时机：失焦/回车立即写入；本页挂在设置中心的
                            // Loader 上，切换分类会销毁页面且不触发 editingFinished，
                            // 所以补一条销毁兜底——"填了 Key 却直接切走"最难察觉。
                            // 读取 text（不是 displayText）：密文字段的 displayText
                            // 是掩码点，写进去等于把密钥毁掉。
                            function commitKey() {
                                if (text.length === 0) return
                                Settings.webSearchApiKey = text
                                text = ""
                            }
                            onEditingFinished: commitKey()
                            Component.onDestruction: commitKey()
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

            // ── 会话权限模式 ──────────────────────────
            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 16
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("会话权限模式")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

                Text {
                    text: qsTr("新会话默认使用的工具审批级别。可在会话内随时通过输入框旁的模式选择器更改，仅对当前会话生效。")
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
                        text: qsTr("默认模式")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        id: permissionCombo
                        Layout.preferredWidth: 200
                        textRole: "label"
                        model: [
                            { key: "confirm",     label: qsTr("变更确认") },
                            { key: "auto_edit",   label: qsTr("自动编辑") },
                            { key: "full_access", label: qsTr("完全访问") }
                        ]
                        currentIndex: {
                            const keys = ["confirm", "auto_edit", "full_access"]
                            const i = keys.indexOf(Settings.defaultPermissionMode)
                            return i >= 0 ? i : 0
                        }
                        onActivated: Settings.defaultPermissionMode =
                            ["confirm", "auto_edit", "full_access"][currentIndex]
                        Accessible.name: qsTr("默认权限模式")
                    }
                }
            }
        }
    }
}
