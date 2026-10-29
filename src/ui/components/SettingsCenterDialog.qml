import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    // 900×680：分类从 12 个扩到 16 个并增加分组标题后，左导航需要滚动；
    // 同时拆页后的字段更长（日志/代理/嵌入各成一页），620 高会把底部
    // 主操作挤出视口。
    width: Math.min(900, parent ? parent.width - 24 : 900)
    height: Math.min(680, parent ? parent.height - 24 : 680)

    property string currentCategory: "model"
    property bool explicitCategory: false
    // docs/17 B6: 非空 = 搜索态——内容区由分类页切换为搜索结果列表。
    readonly property bool searching: searchField.text.trim().length > 0
    // 搜索结果跳转后要定位的字段（SettingsCatalog 条目 id == 目标 objectName）。
    property string highlightTargetId: ""
    property int highlightAttempts: 0

    signal settingsSaved()
    signal settingsError(string message)

    // 恢复默认的可见性与分派由 SettingsCatalog 的条目元数据决定：
    // "page" = 页面自己的 requestResetDefaults()，"preference" =
    // EditBuffer.resetToDefault，其余分类没有可复位的键。
    function categoryHasReset(categoryId) {
        return SettingsCatalog.resetMode(categoryId) !== "none"
    }

    // 先交给页面自己的复位函数（M1 键）；页面没有实现时回落到声明式偏好
    // 复位（M2 键）。返回是否真的复位了内容。
    function resetPage(categoryId) {
        const pageLoaders = {
            "agent": agentPageLoader,
            "network": networkPageLoader,
            "knowledge": knowledgePageLoader,
            "memory": memoryPageLoader,
            "logs": logsPageLoader
        }
        const loader = pageLoaders[categoryId]
        if (loader && loader.item
                && typeof loader.item.requestResetDefaults === "function") {
            loader.item.requestResetDefaults()
            return true
        }
        const keys = PreferenceRegistry.preferencesInCategory(categoryId)
        if (keys.length > 0) {
            EditBuffer.resetToDefault(keys)
            return true
        }
        return false
    }


    function openCategory(id) {
        explicitCategory = true
        currentCategory = id
        open()
    }

    // 点击搜索结果：打开分类；有条目锚点时滚动到该字段并短暂高亮。
    // 页面挂在 Loader 上，切换分类即重建，所以定位要等一帧再试。
    function jumpToSetting(categoryId, settingId) {
        searchField.clear()
        currentCategory = categoryId
        PreferenceStore.save("settings.lastCategory", categoryId)
        clearHighlight()
        if (!settingId) return
        highlightTargetId = settingId
        highlightAttempts = 0
        highlightTimer.restart()
    }

    function jumpToCategory(categoryId) {
        jumpToSetting(categoryId, "")
    }

    function clearHighlight() {
        highlightTargetId = ""
        highlightHideTimer.stop()
        highlightOverlay.visible = false
    }

    // QML 没有 findChild；Loader.item / ScrollView.contentItem 都不是普通的
    // children，所以两条路径都要递归。
    function findSettingItem(node, name) {
        if (!node || name.length === 0) return null
        if (node.objectName === name) return node
        if (node.item) {
            const viaLoader = findSettingItem(node.item, name)
            if (viaLoader) return viaLoader
        }
        const kids = node.children || []
        for (let i = 0; i < kids.length; ++i) {
            const hit = findSettingItem(kids[i], name)
            if (hit) return hit
        }
        return null
    }

    // 沿父链找可滚动容器（Flickable，或 ScrollView 内部的 Flickable），把
    // 目标滚进视口。页面根部的 Flickable、模型页与提示词页的 ScrollView 都命中。
    function scrollSettingIntoView(item) {
        let node = item
        while (node && node !== pageHost) {
            let scroller = null
            if (node.contentY !== undefined && node.contentHeight !== undefined
                    && node.contentItem !== undefined)
                scroller = node
            else if (node.contentItem && node.contentItem.contentY !== undefined
                     && node.contentItem.contentHeight !== undefined)
                scroller = node.contentItem
            if (scroller && scroller.height > 0) {
                const pos = item.mapToItem(scroller.contentItem, 0, 0)
                const margin = 32
                let target = scroller.contentY
                if (pos.y < scroller.contentY + margin)
                    target = pos.y - margin
                else if (pos.y + item.height > scroller.contentY + scroller.height - margin)
                    target = pos.y + item.height - scroller.height + margin
                const maxY = Math.max(0, scroller.contentHeight - scroller.height)
                scroller.contentY = Math.min(Math.max(0, target), maxY)
            }
            node = node.parent
        }
    }

    function applyPendingHighlight() {
        if (highlightTargetId.length === 0) return
        const item = findSettingItem(pageHost, highlightTargetId)
        if (!item) {
            // Loader 可能还没完成布局；重试几次后放弃（仍停留在分类页顶部）。
            if (++highlightAttempts < 6) highlightTimer.restart()
            return
        }
        scrollSettingIntoView(item)
        const pos = item.mapToItem(pageHost, 0, 0)
        highlightOverlay.x = pos.x - 4
        highlightOverlay.y = pos.y - 4
        highlightOverlay.width = Math.min(item.width + 8, pageHost.width - pos.x)
        highlightOverlay.height = Math.min(item.height + 8, pageHost.height - pos.y)
        highlightOverlay.visible = true
        highlightHideTimer.restart()
    }

    background: DialogBackground {}

    onOpened: {
        const last = PreferenceStore.load("settings.lastCategory")
        if (!explicitCategory && last && PreferenceRegistry.categories().indexOf(last) >= 0)
            currentCategory = last
        explicitCategory = false
    }

    onClosed: {
        clearHighlight()
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
                        onTextChanged: dialog.clearHighlight()
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
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                currentCategory: dialog.currentCategory
                onCategorySelected: (id) => {
                    dialog.clearHighlight()
                    dialog.currentCategory = id
                    PreferenceStore.save("settings.lastCategory", id)
                }
            }

            Rectangle {
                id: pageHost
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
                    // 语言切换后重新求值（条目标题在 C++ 侧按当前语言翻译）。
                    model: {
                        I18n.currentLocale
                        return SettingsCatalog.search(searchField.text)
                    }
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
                            onClicked: dialog.jumpToSetting(modelData.category,
                                                            modelData.anchor)
                        }

                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.title + " (" + modelData.categoryName + ")"
                        Accessible.onPressAction: dialog.jumpToSetting(modelData.category,
                                                                       modelData.anchor)
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
                    id: agentPageLoader
                    anchors.fill: parent
                    active: dialog.currentCategory === "agent"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/AgentBehaviorPage.qml"
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
                    id: networkPageLoader
                    anchors.fill: parent
                    active: dialog.currentCategory === "network"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/NetworkPage.qml"
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
                    id: memoryPageLoader
                    anchors.fill: parent
                    active: dialog.currentCategory === "memory"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/MemoryPage.qml"
                }

                Loader {
                    id: knowledgePageLoader
                    anchors.fill: parent
                    active: dialog.currentCategory === "knowledge"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/KnowledgePage.qml"
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
                    id: logsPageLoader
                    anchors.fill: parent
                    active: dialog.currentCategory === "logs"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/LogsPage.qml"
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

                // 搜索结果定位后的短暂高亮框。位置由 applyPendingHighlight()
                // 用目标 item.mapToItem(pageHost) 计算，不侵入各页控件。
                Rectangle {
                    id: highlightOverlay
                    visible: false
                    z: 100
                    color: "transparent"
                    radius: 8
                    border.width: 2
                    border.color: Theme.brandPrimary
                }
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
                        // 分类没有可复位的键（模型配置通过 SettingsService
                        // 保存整条 profile、MCP/项目/提示词是独立资产）时不显示。
                        visible: dialog.categoryHasReset(dialog.currentCategory)
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

    // 搜索跳转的定位重试与高亮自动消失（不依赖具体页面的加载完成信号）。
    Timer {
        id: highlightTimer
        interval: 60
        repeat: false
        onTriggered: dialog.applyPendingHighlight()
    }

    Timer {
        id: highlightHideTimer
        interval: 2200
        repeat: false
        onTriggered: dialog.clearHighlight()
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
                        dialog.resetPage(dialog.currentCategory)
                        confirmResetDialog.close()
                    }
                }
            }
        }
    }
}
