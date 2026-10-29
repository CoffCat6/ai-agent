import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

SettingsPage {
    id: page

    Accessible.name: qsTr("工具管理页")

    title: qsTr("工具管理")
    description: qsTr("控制哪些工具可被 AI 调用。禁用的工具不会暴露给模型。")

    // 联网搜索后端的本地化显示名。「值集合」单一来源是
    // Settings.webSearchProviders（WebSearchProviderRegistry，与 schema
    // options、main.cpp 的工厂同源），这里只做展示层 label 映射。
    readonly property var webSearchProviderLabels: ({
        "duckduckgo": qsTr("DuckDuckGo（免密钥）"),
        "zhipu": qsTr("智谱 Web Search"),
        "deepseek": qsTr("DeepSeek Search")
    })

    Repeater {
        model: AgentVM.toolRegistryModel

        delegate: Rectangle {
            Layout.fillWidth: true
            implicitHeight: row.implicitHeight + Theme.spaceLg
            color: Theme.bgSecondary
            radius: Theme.radiusMd
            border.width: 1
            border.color: Theme.borderSubtle

            RowLayout {
                id: row
                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                spacing: Theme.spaceMd

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.spaceXs

                    RowLayout {
                        spacing: Theme.spaceSm
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
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                        Text {
                            text: "· " + (modelData.level === "safe" ? qsTr("自动")
                                        : modelData.level === "requires_confirm" ? qsTr("需确认")
                                        : qsTr("危险"))
                            // docs/25 C6: 危险级别色归 Theme token
                            color: modelData.level === "dangerous" ? Theme.statusDanger : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                        // docs/17 B1: 策略拦截徽标——Switch 只反映用户层，
                        // 被仅本地模式等策略拦截时在此可见。
                        Text {
                            visible: modelData.blockedByPolicy === true
                            text: "· " + qsTr("仅本地模式拦截")
                            color: Theme.statusWarning
                            font.pixelSize: Theme.fontSizeMicro
                            font.bold: true
                            font.family: Theme.fontSans
                        }
                    }

                    Text {
                        text: modelData.description
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                Switch {
                    id: toolEnableSwitch
                    // docs/25 D3: 读屏可区分是哪个工具的开关
                    Accessible.name: qsTr("启用工具 %1").arg(modelData.name)
                    checked: modelData.enabled
                    onToggled: AgentVM.setToolEnabled(modelData.name, checked)
                }
            }
        }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

    // ── 联网搜索 ──────────────────────────────
    SettingsSection {
        title: qsTr("联网搜索")
        description: qsTr("启用后模型可调用 web_search 获取实时信息。搜索查询会发送至所配置的搜索服务商，请勿在查询中包含敏感信息。")

        SettingRow {
            objectName: "webSearch/provider"
            label: qsTr("搜索服务")

            ComboBox {
                id: providerCombo
                Layout.preferredWidth: Theme.controlWidthLg
                // 值单一来源 Settings.webSearchProviders（后端注册表，
                // 与 schema options、main.cpp 的工厂同源）；仅 label
                // 在此本地化映射。新增后端时这里零改动。
                model: Settings.webSearchProviders.map(p => ({
                    value: p,
                    label: page.webSearchProviderLabels[p] || p
                }))
                textRole: "label"
                currentIndex: {
                    const i = Settings.webSearchProviders.indexOf(Settings.webSearchProvider)
                    return i >= 0 ? i : 0
                }
                onActivated: (index) => Settings.webSearchProvider =
                    Settings.webSearchProviders[index]
                Accessible.name: qsTr("搜索服务")
            }
        }

        // 是否需要密钥由后端注册表回答（requiresApiKey），UI 不再
        // 硬编码 provider 字符串——新增免密钥后端时这里自动跟上。
        // 注意：必须绑定属性而不是调用函数，否则切换 provider 后
        // QML 不会重算，输入框会一直藏着直到重开设置页。
        RowLayout {
            objectName: "webSearch/apiKey"
            Layout.fillWidth: true
            visible: Settings.webSearchProviderNeedsApiKey
            spacing: Theme.spaceSm

            SettingsField {
                id: apiKeyField
                label: qsTr("搜索 API Key")
                secret: true
                // 空值 = 不修改；写入成功后清空输入框，避免明文
                // 一直留在掩码后面。
                ignoreEmptySubmit: true
                clearOnSubmit: true
                commitOnDestroy: true
                statusText: Settings.webSearchApiKeySet
                    ? qsTr("已配置") : qsTr("未配置")
                statusOk: Settings.webSearchApiKeySet
                placeholderText: Settings.webSearchApiKeySet
                                 ? qsTr("输入新值可覆盖，留空不修改")
                                 : qsTr("输入搜索服务 API Key")
                onSubmitted: (value) => Settings.webSearchApiKey = value
            }

            AppButton {
                text: qsTr("清除密钥")
                enabled: Settings.webSearchApiKeySet
                onClicked: Settings.webSearchApiKey = ""
                Accessible.name: qsTr("清除搜索 API Key")
            }
        }

        // 连通性测试：用当前选中的服务 + 当前密钥真实检索一次。
        // 与代理测试同一套交互（按钮 + 内联结论文本），结果三态由
        // webSearchTesting / webSearchTestOk / webSearchTestResult 驱动。
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            AppButton {
                text: Settings.webSearchTesting ? qsTr("测试中…") : qsTr("测试连接")
                // 需要密钥的后端在没填 key 时测试必然失败，直接禁用
                // 比让用户点出一句「请先填写 API Key」更省事。
                enabled: !Settings.webSearchTesting
                         && (!Settings.webSearchProviderNeedsApiKey
                             || Settings.webSearchApiKeySet)
                onClicked: Settings.testWebSearch()
                Accessible.name: qsTr("测试搜索服务连接")
            }

            StatusLine {
                text: Settings.webSearchTestResult
                state: Settings.webSearchTesting ? "busy"
                     : (Settings.webSearchTestOk ? "success" : "warning")
            }
        }
    }

    // ── 会话权限模式 ──────────────────────────
    SettingsSection {
        Layout.bottomMargin: Theme.spaceXs
        title: qsTr("会话权限模式")
        description: qsTr("新会话默认使用的工具审批级别。可在会话内随时通过输入框旁的模式选择器更改，仅对当前会话生效。")

        SettingRow {
            objectName: "agent/defaultPermissionMode"
            label: qsTr("默认模式")

            ComboBox {
                id: permissionCombo
                Layout.preferredWidth: Theme.controlWidthLg
                textRole: "label"
                readonly property var modes: [
                    { key: "confirm",     label: qsTr("变更确认") },
                    { key: "plan_only",   label: qsTr("只读（计划模式）") },
                    { key: "auto_edit",   label: qsTr("自动编辑") },
                    { key: "full_access", label: qsTr("完全访问") }
                ]
                model: modes
                currentIndex: {
                    for (var i = 0; i < modes.length; ++i)
                        if (modes[i].key === Settings.defaultPermissionMode)
                            return i
                    return 0
                }
                onActivated: Settings.defaultPermissionMode = modes[currentIndex].key
                Accessible.name: qsTr("默认权限模式")
            }
        }
    }
}
