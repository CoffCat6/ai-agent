import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

SettingsPage {
    id: page

    Accessible.name: qsTr("工具管理页")

    title: qsTr("工具管理")
    description: qsTr("控制哪些工具可被 AI 调用。禁用的工具不会暴露给模型；内置工具默认折叠，MCP 工具单独成组。")

    // 工具列表按来源拆成两组：内置工具默认折叠，MCP 工具单独成组，
    // 让「本地随附」与「服务器提供」不再混在同一串里。两个名字都以
    // AgentVM.toolRegistryModel 的 source 字段为准（MCP 工具名带 mcp__ 前缀）。
    //
    // 搜索：内置工具随版本增长到十几项、MCP 工具上限 64 项后，逐行翻找
    // 不再可行。名称与说明都参与子串匹配（不区分大小写）；命中时自动
    // 展开内置组，否则用户在折叠状态下搜索会以为没结果。
    property string searchQuery: ""

    function matchesSearch(tool) {
        const q = page.searchQuery.trim().toLowerCase()
        if (q.length === 0) return true
        return String(tool.name || "").toLowerCase().indexOf(q) >= 0
            || String(tool.description || "").toLowerCase().indexOf(q) >= 0
    }

    readonly property var builtinTools:
        AgentVM.toolRegistryModel.filter(t => t.source !== "MCP" && page.matchesSearch(t))
    readonly property var mcpTools:
        AgentVM.toolRegistryModel.filter(t => t.source === "MCP" && page.matchesSearch(t))
    readonly property int builtinEnabledCount:
        page.builtinTools.filter(t => t.enabled).length
    readonly property int mcpEnabledCount:
        page.mcpTools.filter(t => t.enabled).length
    readonly property bool searching: page.searchQuery.trim().length > 0

    // 联网搜索后端的本地化显示名。「值集合」单一来源是
    // Settings.webSearchProviders（WebSearchProviderRegistry，与 schema
    // options、main.cpp 的工厂同源），这里只做展示层 label 映射。
    readonly property var webSearchProviderLabels: ({
        "duckduckgo": qsTr("DuckDuckGo（免密钥）"),
        "zhipu": qsTr("智谱 Web Search"),
        "deepseek": qsTr("DeepSeek Search")
    })

    // ── 搜索 ─────────────────────────────────
    TextField {
        id: searchField
        objectName: "toolsSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("搜索工具名称或说明…")
        placeholderTextColor: Theme.textSecondary
        color: Theme.textPrimary
        selectionColor: Theme.brandPrimary
        selectedTextColor: Theme.selectedTextColor
        selectByMouse: true
        font.pixelSize: Theme.fontSizeBody
        Accessible.name: qsTr("搜索工具")
        background: Rectangle {
            radius: Theme.radiusField
            color: Theme.surfaceHover
            border.width: searchField.activeFocus ? 2 : 1
            border.color: searchField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
        }
        onTextChanged: {
            page.searchQuery = text
            // 命中时展开内置组；折叠状态下的搜索等于没搜。
            if (text.trim().length > 0 && builtinSection.collapsed)
                builtinSection.toggle()
        }
    }

    // ── 内置工具（默认折叠，避免一进页面就是十几行）──
    SettingsSection {
        id: builtinSection
        objectName: "toolsBuiltinSection"
        title: qsTr("内置工具")
        description: qsTr("随应用提供的工具。展开后可逐个启停；禁用的工具不会暴露给模型。子代理专属工具（propose_edit）只在子代理运行时可用，不在此列表。")
        collapsible: true
        collapsed: true
        badge: page.builtinTools.length === 0
               ? (page.searching ? qsTr("无匹配") : qsTr("暂无"))
               : qsTr("%1 个 · 已启用 %2").arg(page.builtinTools.length)
                     .arg(page.builtinEnabledCount)

        Repeater {
            model: page.builtinTools
            delegate: ToolRow {}
        }
    }

    // ── MCP 工具（由服务器提供，与内置工具分开管理）──
    SettingsSection {
        id: mcpSection
        objectName: "toolsMcpSection"
        title: qsTr("MCP 工具")
        description: qsTr("由已连接的 MCP 服务器提供，与内置工具分开管理。")
        collapsible: true
        badge: page.mcpTools.length === 0
               ? (page.searching ? qsTr("无匹配") : "")
               : qsTr("%1 个 · 已启用 %2").arg(page.mcpTools.length)
                     .arg(page.mcpEnabledCount)

        Repeater {
            model: page.mcpTools
            delegate: ToolRow {}
        }

        Text {
            objectName: "toolsMcpEmptyHint"
            visible: page.mcpTools.length === 0 && !page.searching
            Layout.fillWidth: true
            text: qsTr("暂无 MCP 工具。先到「MCP 服务器」页连接服务器，服务器提供的工具会出现在这里。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }
    }

    Text {
        objectName: "toolsSearchEmpty"
        visible: page.searching
                 && page.builtinTools.length === 0
                 && page.mcpTools.length === 0
        Layout.fillWidth: true
        text: qsTr("没有匹配「%1」的工具。换个关键词，或清空搜索查看全部。").arg(page.searchQuery.trim())
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
        wrapMode: Text.Wrap
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
