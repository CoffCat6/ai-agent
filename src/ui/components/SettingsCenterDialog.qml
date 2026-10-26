import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels
import "settings-search.js" as SettingsSearch

Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    // 860×620：模型配置页改成卡片分组后，560 高的视口里主操作（启用/测试连接）会
    // 被挤出屏幕；580 宽的内容区又放不下"标签 + 输入框 + 一排档位芯片"。
    width: Math.min(860, parent ? parent.width - 24 : 860)
    height: Math.min(620, parent ? parent.height - 24 : 620)

    property string currentCategory: "model"
    property bool explicitCategory: false
    // docs/17 B6: 非空 = 搜索态——内容区由分类页切换为搜索结果列表。
    readonly property bool searching: searchField.text.trim().length > 0

    signal settingsSaved()
    signal settingsError(string message)


    function openCategory(id) {
        explicitCategory = true
        currentCategory = id
        open()
    }

    // docs/17 B6: 点击结果跳转对应分类并退出搜索态。
    function jumpToCategory(categoryId) {
        searchField.clear()
        currentCategory = categoryId
        PreferenceStore.save("settings.lastCategory", categoryId)
    }

    // docs/17 B6: 合并数据源——声明式偏好（PreferenceRegistry 自带
    // displayName/category）+ 手工索引表（settings-search.js）。分类名与
    // 顺序在此解析，JS 侧保持纯数据进出（tests/qml 可直接驱动）。
    function searchSettings(query) {
        const registryEntries = []
        const categoryOrder = {}
        const categoryNames = {}
        const cats = PreferenceRegistry.categories()
        for (let i = 0; i < cats.length; ++i) {
            const id = cats[i]
            categoryOrder[id] = PreferenceRegistry.categoryOrder(id)
            const categoryName = PreferenceRegistry.categoryName(id)
            categoryNames[id] = categoryName
            const keys = PreferenceRegistry.preferencesInCategory(id)
            for (let k = 0; k < keys.length; ++k) {
                const name = PreferenceRegistry.displayName(keys[k])
                if (name.length === 0) continue
                registryEntries.push({
                    title: name,
                    category: id,
                    categoryName: categoryName,
                    keywords: keys[k]
                })
            }
        }
        // docs/25 C3: 手工索引标题在 JS 侧经 QT_TRANSLATE_NOOP 标记，此处
        // 按 SettingsSearch 上下文运行时查询翻译；声明式偏好的 displayName
        // 走同一查询（.ts 无对应条目时原样返回），英文界面搜索结果不再
        // 中英混杂。
        const results = SettingsSearch.search(query, registryEntries, categoryOrder,
                                              categoryNames)
        for (let i = 0; i < results.length; ++i)
            results[i].title = qsTranslate("SettingsSearch", results[i].title)
        return results
    }

    background: DialogBackground {}

    onOpened: {
        const last = PreferenceStore.load("settings.lastCategory")
        if (!explicitCategory && last && PreferenceRegistry.categories().indexOf(last) >= 0)
            currentCategory = last
        explicitCategory = false
    }

    onClosed: {
        if (EditBuffer.dirty) EditBuffer.rollback()
    }

    Connections {
        target: PreferenceStore
        function onPersistenceError(message) { dialog.settingsError(message) }
    }

    Connections {
        target: Settings
        function onPersistenceError(message) { dialog.settingsError(message) }
    }

    contentItem: ColumnLayout {
        Accessible.name: qsTr("设置中心")
        Accessible.description: qsTr("配置模型、界面、工具、MCP 服务器等应用选项")
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.bgSecondary

            Item {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 16

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        text: qsTr("设置")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeH2
                        font.bold: true
                        font.family: Theme.fontSans
                    }
                    // docs/17 B6: 顶部搜索框——输入即切换到结果列表。
                    TextField {
                        id: searchField
                        Layout.preferredWidth: 280
                        Layout.alignment: Qt.AlignVCenter
                        placeholderText: qsTr("搜索设置…")
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary
                        selectedTextColor: Theme.selectedTextColor
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        selectByMouse: true
                        background: Rectangle {
                            radius: 6
                            color: Theme.bgPrimary
                            border.width: searchField.activeFocus ? 2 : 1
                            border.color: searchField.activeFocus
                                ? Theme.brandPrimary : Theme.borderSubtle
                        }
                        Accessible.name: qsTr("搜索设置")
                    }
                    IconButton {
                        visible: searchField.text.length > 0
                        icon: "x"
                        iconSize: 12
                        Accessible.name: qsTr("清除搜索")
                        onClicked: searchField.clear()
                    }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        icon: "x"
                        iconSize: 16
                        // docs/25 D5: 无 tooltip 时读屏名回落为图标名 "x"
                        tooltip: qsTr("关闭")
                        onClicked: dialog.close()
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            CategoryNav {
                id: categoryNav
                Layout.preferredWidth: 180
                Layout.fillHeight: true
                currentCategory: dialog.currentCategory
                onCategorySelected: (id) => {
                    dialog.currentCategory = id
                    PreferenceStore.save("settings.lastCategory", id)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPrimary

                // docs/17 B6: 搜索态把内容区切换为结果列表；分类页只是
                // 隐藏（Loader 状态保留），跳转后原位恢复。
                ListView {
                    id: searchResults
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: dialog.searching
                    clip: true
                    model: dialog.searchSettings(searchField.text)
                    spacing: 4

                    delegate: Rectangle {
                        width: searchResults.width
                        height: 52
                        radius: 6
                        color: resultMa.containsMouse
                            ? Theme.bgSecondary : Theme.bgPrimary
                        border.width: 1
                        border.color: Theme.borderSubtle

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: modelData.title
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: modelData.categoryName
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                }
                            }
                            Text {
                                text: qsTr("跳转")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                visible: resultMa.containsMouse
                            }
                        }

                        MouseArea {
                            id: resultMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.jumpToCategory(modelData.category)
                        }

                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.title + " (" + modelData.categoryName + ")"
                        Accessible.onPressAction: dialog.jumpToCategory(modelData.category)
                    }

                    // 空态：无匹配设置。
                    Text {
                        anchors.centerIn: parent
                        visible: searchResults.count === 0
                        text: qsTr("没有匹配「%1」的设置").arg(searchField.text.trim())
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                    }
                }

                Item {
                    anchors.fill: parent
                    visible: !dialog.searching

                    Loader {
                        anchors.fill: parent
                        active: dialog.currentCategory === "model"
                        visible: active
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/ModelConfigPage.qml"
                    }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "interface"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/InterfacePage.qml"
                }

                Loader {
                    id: generalPageLoader
                    anchors.fill: parent
                    active: dialog.currentCategory === "general"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/GeneralPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "security"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/SecurityPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "mcp"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/McpPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "tools"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/ToolsPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "memory"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/MemoryPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "backup"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/SettingsBackupPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    // 绑定 opened：关闭即销毁，下次打开重建并重新查询，
                    // 避免 stats 停留在首次加载时的旧值（统计不涨的假象）。
                    active: dialog.currentCategory === "usage" && dialog.opened
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/UsageStatsPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    // 同 usage：绑定 opened，关闭即销毁，重开重新拉取
                    // usage_log，时间线不停留在旧值。
                    active: dialog.currentCategory === "activity" && dialog.opened
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/ActivityLogPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    // G3: per-project AGENTS.md authoring. Rebuilt on open so
                    // the project list and rule states are fresh.
                    active: dialog.currentCategory === "projects" && dialog.opened
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/ProjectRulesPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    // 提示词与指令：三组用户资产（预设/斜杠指令/会话模板）。
                    active: dialog.currentCategory === "prompts" && dialog.opened
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/PromptAssetsPage.qml"
                }
                }   // ── 分类页包裹层（搜索时隐藏）──
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.bgSecondary

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }

            Item {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20

                // docs/17（设置统一最终决策）: 所有分类均为即时保存，底栏
                // 只保留"恢复默认"（仅声明式偏好分类可用）与"关闭"。
                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    AppButton {
                        text: qsTr("恢复默认")
                        // Categories without registered preferences (e.g. 模型配置,
                        // which saves through SettingsService instead) have
                        // nothing this button could reset.
                        visible: dialog.currentCategory === "general"
                                 || PreferenceRegistry.preferencesInCategory(
                                        dialog.currentCategory).length > 0
                        onClicked: confirmResetDialog.open()
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        text: qsTr("关闭")
                        highlighted: true
                        onClicked: dialog.close()
                    }
                }
            }
        }
    }

    // docs/17: 即时保存后不再有"放弃修改"路径；confirmCancelDialog 已移除，
    // onClosed 的 rollback 仅兜底丢弃校验失败遗留的 staged 残值。
    Dialog {
        id: confirmResetDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 360
        height: 160

        background: DialogBackground {}
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: qsTr("确认将当前分类的配置项恢复为默认值？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                Layout.leftMargin: 20
                Layout.topMargin: 20
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.rightMargin: 20
                Layout.bottomMargin: 16
                spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: confirmResetDialog.close()
                }
                AppButton {
                    text: qsTr("恢复默认")
                    highlighted: true
                    onClicked: {
                        if (dialog.currentCategory === "general") {
                            if (generalPageLoader.item)
                                generalPageLoader.item.requestResetDefaults()
                        } else {
                            EditBuffer.resetToDefault(
                                PreferenceRegistry.preferencesInCategory(dialog.currentCategory))
                        }
                        confirmResetDialog.close()
                    }
                }
            }
        }
    }
}
