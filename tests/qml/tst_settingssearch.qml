// docs/17 B6: settings search core (settings-search.js) — matching,
// dedup and category-order behaviour. Pure data-in/data-out: the harness
// feeds synthetic registry entries, no NexusAI module resolution needed.
// The dialog wiring (results list / jumpToCategory) is covered by build +
// qmllint; the logic is what deserves pinned assertions.
import QtQuick
import QtTest
import "../../src/ui/components/settings-search.js" as SettingsSearchLib

Item {
    width: 400
    height: 200
    id: probeRoot

    // docs/17 B6 调试捕获：delegate 里的角色可见性记录。
    property var probeCapture: []

    // Synthetic registry entries shaped like SettingsCenterDialog builds
    // them (declarative preferences, category order preserved).
    readonly property var registryEntries: [
        { title: "主题模式", category: "interface", categoryName: "界面设置", keywords: "ui.themeMode" },
        { title: "界面语言", category: "interface", categoryName: "界面设置", keywords: "ui.locale" },
        { title: "关闭时最小化到系统托盘", category: "general", categoryName: "通用设置", keywords: "window.closeToTray" },
        { title: "开机自启动", category: "general", categoryName: "通用设置", keywords: "general.autoStart" }
    ]
    readonly property var categoryOrder: ({ model: 1, interface: 2, general: 3,
                                            security: 4, mcp: 5, tools: 6,
                                            memory: 7, usage: 8, backup: 9 })
    readonly property var categoryNames: ({ model: "模型配置", interface: "界面设置",
                                            general: "通用设置", security: "安全",
                                            mcp: "MCP 服务器", tools: "工具管理",
                                            memory: "记忆管理", usage: "用量统计",
                                            backup: "设置与备份" })

    // docs/17 B6 调试夹具：与 SettingsCenterDialog 相同的 delegate 结构，
    // 探测 JS 数组模型在 delegate 里的角色可见性（model.title vs
    // modelData.title）。窗口不显示（offscreen 下也成立）。
    Window {
        width: 300
        height: 300
        visible: true

        ListView {
            id: probeList
            anchors.fill: parent
            model: SettingsSearchLib.search(
                       "模型",
                       [{ title: "模型配置", category: "model", categoryName: "模型配置", keywords: "" }],
                       categoryOrder, categoryNames)
            delegate: Rectangle {
                width: probeList.width
                height: 52
                property string viaModel: typeof model !== "undefined"
                    ? String(model.title) : "(no model role)"
                property string viaModelData: typeof modelData !== "undefined"
                    ? String(modelData.title) : "(no modelData role)"
                Component.onCompleted: probeRoot.probeCapture.push({
                    viaModel: viaModel, viaModelData: viaModelData })
                Text { objectName: "probeTitle"; text: "probe" }
            }
        }
    }

    TestCase {
        name: "SettingsSearch"

        // docs/17 B6: JS 数组模型的角色在 delegate 里只经 modelData 可见
        // （model 聚合角色不存在——SettingsCenterDialog 的结果 delegate
        // 因此绑定 modelData.*；此断言锁住该行为，防止悄悄回退）。
        function test_delegateRolesProbe() {
            verify(probeRoot.probeCapture.length > 0, "no delegate instantiated")
            const capture = probeRoot.probeCapture[0]
            compare(capture.viaModelData, "思考与上下文")
        }

        function test_emptyQueryYieldsNothing() {
            compare(SettingsSearchLib.search("", [], {}).length, 0)
            compare(SettingsSearchLib.search("   ", [], {}).length, 0)
        }

        function test_registryEntryMatchesByTitle() {
            const results = SettingsSearchLib.search("主题", registryEntries, categoryOrder, categoryNames)
            compare(results.length, 1)
            compare(results[0].title, "主题模式")
            compare(results[0].category, "interface")
            compare(results[0].categoryName, "界面设置")
        }

        function test_registryEntryMatchesByKey() {
            // The physical key is part of the haystack.
            const results = SettingsSearchLib.search("autostart", registryEntries, categoryOrder, categoryNames)
            compare(results.length, 1)
            compare(results[0].title, "开机自启动")
        }

        function test_handIndexMatchesByChineseTitle() {
            const results = SettingsSearchLib.search("配额", registryEntries, categoryOrder, categoryNames)
            compare(results.length, 1)
            compare(results[0].title, "配额设置")
            compare(results[0].category, "usage")
        }

        function test_handIndexMatchesByEnglishKeyword() {
            // 英文关键词命中手写索引：联网搜索后端从 Tavily/SearXNG 换成
            // DuckDuckGo/智谱/DeepSeek 后，关键词表必须同步，否则设置中心
            // 里搜「zhipu」会搜不到联网搜索页。
            const results = SettingsSearchLib.search("zhipu", registryEntries, categoryOrder, categoryNames)
            compare(results.length, 1)
            compare(results[0].title, "联网搜索")
            compare(results[0].category, "tools")
        }

        function test_categoryNameParticipatesInMatch() {
            // "安全" only appears in the categoryName of security entries.
            const results = SettingsSearchLib.search("安全", registryEntries, categoryOrder, categoryNames)
            verify(results.length >= 2)
            for (let i = 0; i < results.length; ++i)
                compare(results[i].category, "security")
        }

        function test_resultsSortedByCategoryOrder() {
            // "超时" hits general (模型行为) and "token" hits memory + usage —
            // craft a query hitting two categories and assert the order.
            const results = SettingsSearchLib.search("备份", registryEntries, categoryOrder, categoryNames)
            // 设置导出与导入 + 数据库备份, both backup — plus nothing else.
            compare(results.length, 2)
            compare(results[0].category, "backup")
            compare(results[1].category, "backup")
        }

        function test_noMatchIsEmpty() {
            compare(SettingsSearchLib.search("不存在的关键词xyz", registryEntries, categoryOrder, categoryNames).length, 0)
        }

        function test_dedupesRegistryAndHandOverlap() {
            // A registry entry with the same title+category as a hand entry
            // must not appear twice.
            const entries = registryEntries.concat([
                { title: "长期记忆", category: "memory", categoryName: "记忆管理", keywords: "" }
            ])
            const results = SettingsSearchLib.search("记忆", entries, categoryOrder, categoryNames)
            let hits = 0
            for (let i = 0; i < results.length; ++i)
                if (results[i].title === "长期记忆") ++hits
            compare(hits, 1)
        }
    }
}
