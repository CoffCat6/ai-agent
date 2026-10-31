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
    // docs/31 S4.1 O8：记住上次尺寸（隐藏偏好键 ui.settingsDialogWidth /
    // ui.settingsDialogHeight，0 = 从未保存过）。preferred* 是"用户想要
    // 的尺寸"，实际 width/height 再按父容器 clamp——窄窗口下永远不出可视区。
    // 只在关闭时落盘，拖动过程不写设置。
    // 默认 900×680：分类从 12 个扩到 16 个并增加分组标题后，左导航需要
    // 滚动；同时拆页后的字段更长（日志/代理/嵌入各成一页），620 高会把
    // 底部主操作挤出视口。
    readonly property int defaultDialogWidth: 900
    readonly property int defaultDialogHeight: 680
    readonly property int minDialogWidth: 640
    readonly property int minDialogHeight: 480
    property int preferredWidth: defaultDialogWidth
    property int preferredHeight: defaultDialogHeight

    width: Math.min(preferredWidth, parent ? parent.width - 24 : preferredWidth)
    height: Math.min(preferredHeight, parent ? parent.height - 24 : preferredHeight)

    property string currentCategory: "model"
    property bool explicitCategory: false

    // docs/31 N2：录制快捷键时全局 Shortcut 集体让位；切页必须把让位状态
    // 收回，否则整个应用的快捷键会一直失效（关闭路径见下方 onClosed）。
    onCurrentCategoryChanged: if (ShortcutMap.recording) ShortcutMap.recording = false
    // 非空 = 搜索态——内容区由分类页切换为搜索结果列表。
    readonly property bool searching: searchField.text.trim().length > 0
    // 窄窗口（默认 900 宽的对话框降到 900 以下）把左侧导航收成图标条，
    // 把 144px 让给内容区；窗口最小 900 时对话框为 876，正好命中。
    readonly property bool compactNav: width < 900
    // 搜索结果跳转后要定位的字段（SettingsCatalog 条目 id == 目标 objectName）。
    property string highlightTargetId: ""
    property int highlightAttempts: 0

    // docs/31 O7：切页不再销毁已访问过的分类页，滚动位置与输入状态得以保留。
    // 只在本次打开期间缓存——关闭设置中心时全部释放，重开仍是新鲜状态。
    // usage / activity / projects / prompts 不参与缓存（见各自 Loader 的注释）。
    property var visitedCategories: ({})
    readonly property int maxCachedPages: 5
    // 底栏「恢复全部设置」只在确实存在可复位分类时出现。
    readonly property bool anyReset: {
        const cats = PreferenceRegistry.categories()
        for (let i = 0; i < cats.length; ++i)
            if (SettingsCatalog.resetMode(cats[i]) !== "none") return true
        return false
    }

    // 底栏即时反馈：成功保存显示短暂"已保存"，持久化失败显示红色原因。
    property string saveStatusText: ""
    property bool saveStatusIsError: false
    // docs/31 S4.3 N3：最近一次写穿的偏好键可撤销。撤销入口跟回执同寿命
    // （可撤销时 5 秒，普通回执 1.8 秒），过时即消失，只作用于一次改动。
    property string undoKey: ""
    property bool undoAvailable: false
    // 复位期间抑制逐键的"已保存"，最后只显示一条"已恢复本页默认值"。
    property bool suppressSaveStatus: false
    property bool lastSaveFailed: false

    // 持久化失败且对话框未打开时仍需要全局提示（设置也可能被其它入口改写）。
    signal settingsError(string message)

    // 恢复默认的可见性与分派由 SettingsCatalog 的条目元数据决定：
    // "page" = 页面自己的 requestResetDefaults()，"preference" =
    // EditBuffer.resetToDefault，其余分类没有可复位的键。
    function categoryHasReset(categoryId) {
        return SettingsCatalog.resetMode(categoryId) !== "none"
    }

    function categoryDisplayName(categoryId) {
        // 语言切换后重新求值；categoryName 在查询时解析已存的源文案。
        I18n.currentLocale
        return PreferenceRegistry.categoryName(categoryId)
    }

    function focusSearch() {
        if (!opened) open()
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    function showSaveStatus(message, isError, duration) {
        saveStatusText = message
        saveStatusIsError = isError
        lastSaveFailed = isError
        // 每条新回执先收回撤销入口；noteSaved() 在确认这次改动真的可撤销
        // 后会重新打开（复位/错误/普通提示都不能沿用上一次的撤销点）。
        undoAvailable = false
        undoKey = ""
        saveStatusTimer.interval = (duration && duration > 0)
            ? duration : (isError ? 8000 : 1800)
        saveStatusTimer.restart()
    }

    // 设置键是否属于当前分类。SettingsCatalog 对偏好键和 M1 键都用 settings
    // 键本身当条目 id（modelProfiles/activeProfileId 亦然），因此能统一映射。
    // settings.lastCategory 是导航自身的写入，不属于任何用户可见字段。
    function keyBelongsToCurrentCategory(key) {
        if (key === "settings.lastCategory") return false
        return SettingsCatalog.entry(key).category === currentCategory
    }

    // SettingsService 的 settingSaved(key) 与 PreferenceStore 的 saved(keys)
    // 共用：只对当前分类的写入给回执，避免后台/其他分类的持久化闪出"已保存"。
    // docs/31 S4.3 N3：偏好键的写穿由 EditBuffer 记录撤销点，回执里给「撤销」。
    // 只认"这次 saved 正好是 EditBuffer 记下的那个键"——M1 键、导航/尺寸等
    // 隐藏键、以及密钥类都不会出现撤销入口。
    function noteSaved(keys) {
        if (!opened || suppressSaveStatus) return
        let relevantKey = ""
        if (typeof keys === "string") {
            if (keyBelongsToCurrentCategory(keys)) relevantKey = keys
        } else if (keys) {
            for (let i = 0; i < keys.length; ++i) {
                if (keyBelongsToCurrentCategory(keys[i])) {
                    relevantKey = keys[i]
                    break
                }
            }
        }
        if (relevantKey.length === 0) return
        const canUndo = EditBuffer.canUndo && EditBuffer.undoKey === relevantKey
            && SettingsCatalog.entry(relevantKey).secret !== true
        showSaveStatus(qsTr("已保存"), false, canUndo ? 5000 : 1800)
        if (canUndo) {
            undoKey = relevantKey
            undoAvailable = true
        }
    }

    // docs/31 S4.3 N3：撤销最近一次写穿。成功/失败都给回执；ClearUndo 已在
    // C++ 侧完成，这里只同步底部提示。
    function performUndo() {
        const key = undoKey
        const ok = EditBuffer.undoLast()
        undoAvailable = false
        undoKey = ""
        if (ok) {
            showSaveStatus(qsTr("已撤销对「%1」的更改")
                               .arg(SettingsCatalog.entry(key).title), false, 2600)
        } else {
            showSaveStatus(qsTr("无法撤销上次更改"), true, 8000)
        }
    }

    function noteSaveError(message) {
        if (opened) showSaveStatus(message, true, 8000)
        else settingsError(message)
    }

    function performReset() {
        const name = categoryDisplayName(currentCategory)
        lastSaveFailed = false
        suppressSaveStatus = true
        const didReset = resetPage(currentCategory)
        suppressSaveStatus = false
        if (didReset && !lastSaveFailed)
            showSaveStatus(qsTr("已恢复「%1」的默认值").arg(name), false, 2600)
    }

    // 先交给页面自己的复位函数（M1 键）；页面没有实现时回落到声明式偏好
    // 复位（M2 键）；两者都没有、但分类里仍有可复位的页面级条目时，逐条
    // 走 SettingsCatalog.resetValue（安全页、用量页、工具页只在自绘控件上
    // 写 SettingsService，没有 requestResetDefaults）。返回是否真的复位了内容。
    function resetPage(categoryId) {
        const pageLoaders = {
            "agent": agentPageLoader,
            "network": networkPageLoader,
            "knowledge": knowledgePageLoader,
            "memory": memoryPageLoader,
            "logs": logsPageLoader,
            "backup": backupPageLoader
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
        return resetCatalogEntries(categoryId)
    }

    // 复位某个分类里登记为 "page" 的目录条目。密钥与代理凭据的
    // resetValue 恒返回 false，因此红线 1 不受影响。
    function resetCatalogEntries(categoryId) {
        let didReset = false
        const entries = SettingsCatalog.entries()
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (entry.category !== categoryId || entry.resetMode !== "page")
                continue
            if (SettingsCatalog.resetValue(entry.id)) didReset = true
        }
        return didReset
    }


    // 记录访问过的分类，供 Loader 保持实例。缓存条数有上限：ModelConfig /
    // PromptAssets 这类重页留太多会明显涨内存。JS 对象的键顺序即插入顺序，
    // 因此"最早访问的"就是 Object.keys() 的第一个。
    function markVisited(categoryId) {
        if (categoryId.length === 0) return
        if (visitedCategories[categoryId] === true) return
        const next = {}
        for (const key in visitedCategories) next[key] = true
        next[categoryId] = true
        const keys = Object.keys(next)
        while (keys.length > maxCachedPages) {
            const oldest = keys.shift()
            if (oldest === categoryId || oldest === currentCategory) break
            delete next[oldest]
        }
        visitedCategories = next
    }

    // 分类页是否应保持实例：当前分类或本次打开期间访问过。
    function pageActive(categoryId) {
        return currentCategory === categoryId || visitedCategories[categoryId] === true
    }

    // docs/31 O1：全局复位。偏好键一次批量落盘（EditBuffer.resetAllToDefault），
    // M1 键逐条走 SettingsCatalog.resetValue——它只接受有真实 setter 的键，
    // 加密密钥与代理凭据一律拒绝，因此不会被清掉（红线 1）。
    function performResetAll() {
        lastSaveFailed = false
        suppressSaveStatus = true
        EditBuffer.resetAllToDefault()
        const entries = SettingsCatalog.entries()
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].resetMode === "page")
                SettingsCatalog.resetValue(entries[i].id)
        }
        suppressSaveStatus = false
        if (!lastSaveFailed)
            showSaveStatus(qsTr("已恢复全部设置（密钥与代理凭据保持不变）"), false, 3000)
    }

    // docs/31 S4.1 O8：打开时读取上次尺寸；关闭时落盘当前目标尺寸。
    function applyRememberedSize() {
        const savedW = Number(PreferenceStore.load("ui.settingsDialogWidth"))
        const savedH = Number(PreferenceStore.load("ui.settingsDialogHeight"))
        if (isFinite(savedW) && savedW > 0)
            preferredWidth = Math.max(minDialogWidth, Math.round(savedW))
        if (isFinite(savedH) && savedH > 0)
            preferredHeight = Math.max(minDialogHeight, Math.round(savedH))
    }

    function persistSize() {
        const w = Math.round(preferredWidth)
        const h = Math.round(preferredHeight)
        if (Number(PreferenceStore.load("ui.settingsDialogWidth")) !== w)
            PreferenceStore.save("ui.settingsDialogWidth", w)
        if (Number(PreferenceStore.load("ui.settingsDialogHeight")) !== h)
            PreferenceStore.save("ui.settingsDialogHeight", h)
    }

    function openCategory(id) {
        explicitCategory = true
        currentCategory = id
        // 定向入口（模型选择器、空态「配置模型」、侧栏分类直达）落到的
        // 页面也算"用户上次所在分类"，同样记录——否则从这类入口进入后
        // 没点过导航就关闭，下次普通打开会回到更早的分类。
        PreferenceStore.save("settings.lastCategory", id)
        // 编程入口（模型选择器、空态「配置模型」等）与导航点击一样计入
        // 缓存，否则从这类入口进入的页面切走即销毁，滚动位置全丢。
        markVisited(id)
        open()
    }

    // 点击搜索结果：打开分类；有条目锚点时滚动到该字段并短暂高亮。
    // 页面挂在 Loader 上，切换分类即重建，所以定位要等一帧再试。
    function jumpToSetting(categoryId, settingId) {
        searchField.clear()
        currentCategory = categoryId
        markVisited(categoryId)
        PreferenceStore.save("settings.lastCategory", categoryId)
        clearHighlight()
        // 搜索面板随文本清空而隐藏；把焦点交还给页面宿主，避免键盘焦点
        // 留在不可见的 ListView 上。
        pageHost.forceActiveFocus()
        if (!settingId) return
        highlightTargetId = settingId
        highlightAttempts = 0
        highlightTimer.restart()
    }

    function jumpToCategory(categoryId) {
        jumpToSetting(categoryId, "")
    }

    // 键盘 Enter 的入口：按当前查询重新取结果列表，不依赖 ListView 已经
    // 完成布局（delegate 可能还没创建）。
    function jumpToSearchIndex(index) {
        const results = SettingsCatalog.search(searchField.text)
        if (index < 0 || index >= results.length) return
        const result = results[index]
        jumpToSetting(result.category, result.anchor)
    }

    // immediate=true 用于关闭对话框等场景，直接隐藏；默认走淡出，等动画
    // 结束后再摘掉 visible，避免高亮框"啪"地消失。
    function clearHighlight(immediate) {
        highlightTargetId = ""
        highlightHideTimer.stop()
        highlightOverlay.opacity = 0
        if (immediate) {
            highlightClearTimer.stop()
            highlightOverlay.visible = false
        } else {
            highlightClearTimer.restart()
        }
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
                const margin = Theme.spaceXl + Theme.spaceSm
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
        highlightClearTimer.stop()
        highlightOverlay.visible = true
        // opacity 从 0 起跳，Behavior 负责淡入；重复跳转时先由 clearHighlight()
        // 把旧框淡出，再在这里淡入到新位置。
        highlightOverlay.opacity = 1
        highlightHideTimer.restart()
    }

    background: DialogBackground {}

    onOpened: {
        applyRememberedSize()
        const last = PreferenceStore.load("settings.lastCategory")
        if (!explicitCategory && last && PreferenceRegistry.categories().indexOf(last) >= 0)
            currentCategory = last
        explicitCategory = false
        saveStatusText = ""
        undoAvailable = false
        undoKey = ""
        lastSaveFailed = false
        // 「数据与诊断」默认收起；若上次停在其中的分类，重新打开时展开，
        // 否则选中项会藏在折叠标题下面。
        categoryNav.ensureActiveGroupExpanded()
        markVisited(currentCategory)
    }

    onClosed: {
        // docs/31 S4.1 O8：尺寸只在关闭时落盘（拖动过程不写设置）。
        persistSize()
        // docs/31 N2：录制中关闭设置中心时必须恢复全局快捷键。
        if (ShortcutMap.recording) ShortcutMap.recording = false
        clearHighlight(true)
        saveStatusTimer.stop()
        saveStatusText = ""
        undoAvailable = false
        undoKey = ""
        suppressSaveStatus = false
        lastSaveFailed = false
        // 关闭时释放本次打开期间缓存的分类页：下次打开重新构建，避免把
        // 其它入口改过的设置显示成旧值（O7 只在一次打开期间保状态）。
        visitedCategories = ({})
        if (EditBuffer.dirty) EditBuffer.rollback()
    }

    Connections {
        target: PreferenceStore
        function onPersistenceError(message) { dialog.noteSaveError(message) }
        function onSaved(keys) { dialog.noteSaved(keys) }
    }

    Connections {
        target: Settings
        function onPersistenceError(message) { dialog.noteSaveError(message) }
        function onSettingSaved(key) { dialog.noteSaved(key) }
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
                anchors.leftMargin: Theme.spaceXl
                anchors.rightMargin: Theme.spaceLg

                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceSm

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
                        objectName: "settingsSearchField"
                        Layout.preferredWidth: Math.max(160, Math.min(280, dialog.width * 0.32))
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
                            radius: Theme.radiusField
                            color: Theme.surfaceHover
                            border.width: searchField.activeFocus ? 2 : 1
                            border.color: searchField.activeFocus
                                ? Theme.brandPrimary : Theme.borderSubtle
                        }
                        Accessible.name: qsTr("搜索设置")
                        Accessible.description: qsTr("输入关键词筛选设置；Ctrl+F 可随时聚焦搜索框")
                        onTextChanged: {
                            dialog.clearHighlight()
                            if (searchField.text.length === 0)
                                searchResults.currentIndex = -1
                        }
                        onAccepted: dialog.jumpToSearchIndex(
                                        searchResults.currentIndex >= 0
                                            ? searchResults.currentIndex : 0)
                        Keys.onEscapePressed: (event) => {
                            if (searchField.text.length > 0) {
                                searchField.clear()
                                event.accepted = true
                            }
                        }
                        Keys.onDownPressed: (event) => {
                            if (!dialog.searching || searchResults.count === 0) return
                            searchResults.currentIndex = 0
                            searchResults.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                    IconButton {
                        visible: searchField.text.length > 0
                        icon: "x"
                        iconSize: 12
                        Accessible.name: qsTr("清除搜索")
                        onClicked: {
                            searchField.clear()
                            searchField.forceActiveFocus()
                        }
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
                Layout.preferredWidth: categoryNav.compact ? 56 : 200
                Layout.fillHeight: true
                compact: dialog.compactNav
                currentCategory: dialog.currentCategory
                onCategorySelected: (id) => {
                    dialog.clearHighlight()
                    dialog.currentCategory = id
                    dialog.markVisited(id)
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
                Item {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    visible: dialog.searching

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.spaceSm

                        // 结果计数 + 键盘提示：搜索不再只有鼠标可达。
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            Text {
                                visible: searchResults.count > 0
                                text: qsTr("找到 %1 个设置").arg(searchResults.count)
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: searchResults.count > 0
                                text: qsTr("↑↓ 选择 · Enter 跳转 · Esc 清除")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                            }
                        }

                        ListView {
                            id: searchResults
                            objectName: "settingsSearchResults"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            activeFocusOnTab: true
                            // 语言切换后重新求值（条目标题在 C++ 侧按当前语言翻译）。
                            model: {
                                I18n.currentLocale
                                return SettingsCatalog.search(searchField.text)
                            }
                            spacing: Theme.spaceXs
                            ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

                            onCountChanged: {
                                if (!dialog.searching) {
                                    currentIndex = -1
                                    return
                                }
                                if (count > 0 && currentIndex < 0) currentIndex = 0
                                else if (count === 0) currentIndex = -1
                            }

                            Keys.onUpPressed: (event) => {
                                if (count === 0) return
                                if (currentIndex > 0) currentIndex -= 1
                                else searchField.forceActiveFocus()
                                event.accepted = true
                            }
                            Keys.onDownPressed: (event) => {
                                if (count === 0) return
                                if (currentIndex < count - 1) currentIndex += 1
                                event.accepted = true
                            }
                            Keys.onReturnPressed: (event) => {
                                dialog.jumpToSearchIndex(currentIndex)
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                dialog.jumpToSearchIndex(currentIndex)
                                event.accepted = true
                            }
                            Keys.onEscapePressed: (event) => {
                                searchField.clear()
                                searchField.forceActiveFocus()
                                event.accepted = true
                            }

                            delegate: Rectangle {
                                width: searchResults.width
                                height: 52
                                radius: Theme.radiusMd
                                readonly property bool selected: ListView.isCurrentItem
                                color: selected
                                    ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                              Theme.brandPrimary.b, 0.10)
                                    : (resultMa.containsMouse ? Theme.surfaceHover
                                                              : Theme.bgPrimary)
                                border.width: selected ? 2 : 1
                                border.color: selected ? Theme.brandPrimary
                                                       : Theme.borderSubtle

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spaceMd
                                    anchors.rightMargin: Theme.spaceMd
                                    spacing: Theme.spaceSm

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        // titleHtml 由 SettingsCatalog 预转义并在命中
                                        // 区间加 <b>——用 StyledText 而不是在 QML
                                        // 里拼标签，避免搜索词被当成标记执行。
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.titleHtml || modelData.title
                                            textFormat: Text.StyledText
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontSizeBody
                                            font.family: Theme.fontSans
                                            elide: Text.ElideRight
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.spaceXs

                                            // 与行级圆点同一口径：值 != 默认值。
                                            Rectangle {
                                                visible: modelData.modified === true
                                                width: 6
                                                height: 6
                                                radius: width / 2
                                                color: Theme.brandPrimary
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                // 当前值摘要：密钥类只会是"已配置/
                                                // 未配置"，永远不含明文。
                                                text: {
                                                    const value = modelData.currentDisplay
                                                    if (value === undefined || value === null
                                                            || value.length === 0)
                                                        return modelData.categoryName
                                                    return modelData.categoryName + " · " + value
                                                }
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSizeCaption
                                                font.family: Theme.fontSans
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    Text {
                                        text: selected ? qsTr("Enter 跳转") : qsTr("跳转")
                                        color: selected ? Theme.brandPrimary
                                                        : Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        visible: selected || resultMa.containsMouse
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
                                Accessible.name: modelData.title + " (" + modelData.categoryName
                                    + (modelData.currentDisplay
                                       ? "：" + modelData.currentDisplay : "") + ")"
                                Accessible.onPressAction: dialog.jumpToSetting(modelData.category,
                                                                               modelData.anchor)
                            }

                            // 空态：无匹配设置。
                            ColumnLayout {
                                visible: searchResults.count === 0
                                anchors.centerIn: parent
                                width: Math.max(0, Math.min(360,
                                    searchResults.width - Theme.spaceXl * 2))
                                spacing: Theme.spaceSm

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("没有匹配「%1」的设置").arg(searchField.text.trim())
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontSans
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("换个关键词，或清除搜索返回分类页。")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                }
                                AppButton {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("清除搜索")
                                    onClicked: {
                                        searchField.clear()
                                        searchField.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    anchors.fill: parent
                    visible: !dialog.searching

                    // active = 是否保留实例（本次打开期间访问过），
                    // visible = 是否当前分类。两者必须分开绑定：只写
                    // `visible: active` 会让所有访问过的页面同时可见，
                    // 各自 anchors.fill 互相重叠（S1 真实回归）。
                    Loader {
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("model")
                        visible: dialog.currentCategory === "model"
                        objectName: "settingsPageLoader.model"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/ModelConfigPage.qml"
                    }

                    Loader {
                        id: agentPageLoader
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("agent")
                        visible: dialog.currentCategory === "agent"
                        objectName: "settingsPageLoader.agent"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/AgentBehaviorPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("interface")
                        visible: dialog.currentCategory === "interface"
                        objectName: "settingsPageLoader.interface"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/InterfacePage.qml"
                    }

                    Loader {
                        id: generalPageLoader
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("general")
                        visible: dialog.currentCategory === "general"
                        objectName: "settingsPageLoader.general"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/GeneralPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("shortcuts")
                        visible: dialog.currentCategory === "shortcuts"
                        objectName: "settingsPageLoader.shortcuts"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/ShortcutsPage.qml"
                    }

                    Loader {
                        id: networkPageLoader
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("network")
                        visible: dialog.currentCategory === "network"
                        objectName: "settingsPageLoader.network"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/NetworkPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("security")
                        visible: dialog.currentCategory === "security"
                        objectName: "settingsPageLoader.security"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/SecurityPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("mcp")
                        visible: dialog.currentCategory === "mcp"
                        objectName: "settingsPageLoader.mcp"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/McpPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("tools")
                        visible: dialog.currentCategory === "tools"
                        objectName: "settingsPageLoader.tools"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/ToolsPage.qml"
                    }

                    Loader {
                        id: memoryPageLoader
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("memory")
                        visible: dialog.currentCategory === "memory"
                        objectName: "settingsPageLoader.memory"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/MemoryPage.qml"
                    }

                    Loader {
                        id: knowledgePageLoader
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("knowledge")
                        visible: dialog.currentCategory === "knowledge"
                        objectName: "settingsPageLoader.knowledge"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/KnowledgePage.qml"
                    }

                    Loader {
                        id: backupPageLoader
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("backup")
                        visible: dialog.currentCategory === "backup"
                        objectName: "settingsPageLoader.backup"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/SettingsBackupPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        // 绑定 opened：关闭即销毁，下次打开重建并重新查询，
                        // 避免 stats 停留在首次加载时的旧值（统计不涨的假象）。
                        active: dialog.currentCategory === "usage" && dialog.opened
                        visible: dialog.currentCategory === "usage"
                        objectName: "settingsPageLoader.usage"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/UsageStatsPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        // 同 usage：绑定 opened，关闭即销毁，重开重新拉取
                        // usage_log，时间线不停留在旧值。
                        active: dialog.currentCategory === "activity" && dialog.opened
                        visible: dialog.currentCategory === "activity"
                        objectName: "settingsPageLoader.activity"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/ActivityLogPage.qml"
                    }

                    Loader {
                        id: logsPageLoader
                        anchors.fill: parent
                        active: dialog.opened && dialog.pageActive("logs")
                        visible: dialog.currentCategory === "logs"
                        objectName: "settingsPageLoader.logs"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/LogsPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        // G3: per-project AGENTS.md authoring. Rebuilt on open so
                        // the project list and rule states are fresh.
                        active: dialog.currentCategory === "projects" && dialog.opened
                        visible: dialog.currentCategory === "projects"
                        objectName: "settingsPageLoader.projects"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/ProjectRulesPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        // 提示词与指令：三组用户资产（预设/斜杠指令/会话模板）。
                        active: dialog.currentCategory === "prompts" && dialog.opened
                        visible: dialog.currentCategory === "prompts"
                        objectName: "settingsPageLoader.prompts"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/PromptAssetsPage.qml"
                    }

                    Loader {
                        anchors.fill: parent
                        // docs/31 S4.5 N5：数据管理。绑定 opened（同 usage）：
                        // 关闭即销毁，重开时重新拉统计，避免停在旧值。
                        active: dialog.currentCategory === "storage" && dialog.opened
                        visible: dialog.currentCategory === "storage"
                        objectName: "settingsPageLoader.storage"
                        source: "qrc:/qt/qml/NexusAI/src/ui/pages/DataPage.qml"
                    }
                }   // ── 分类页包裹层（搜索时隐藏）──

                // 搜索结果定位后的短暂高亮框。位置由 applyPendingHighlight()
                // 用目标 item.mapToItem(pageHost) 计算，不侵入各页控件。
                Rectangle {
                    id: highlightOverlay
                    visible: false
                    z: 100
                    opacity: 0
                    color: Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                   Theme.brandPrimary.b, 0.10)
                    radius: Theme.radiusMd
                    border.width: 2
                    border.color: Theme.brandPrimary
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.motionBase
                            easing.type: Easing.OutCubic
                        }
                    }
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
                anchors.leftMargin: Theme.spaceXl
                anchors.rightMargin: Theme.spaceXl

                // docs/17（设置统一最终决策）: 所有分类均为即时保存，底栏
                // 只保留"恢复默认"（仅声明式偏好分类可用）、保存回执与"关闭"。
                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceSm

                    AppButton {
                        objectName: "settingsResetCategoryButton"
                        text: qsTr("恢复本页默认值")
                        // 分类没有可复位的键（模型配置通过 SettingsService
                        // 保存整条 profile、MCP/项目/提示词是独立资产）时不显示。
                        visible: dialog.categoryHasReset(dialog.currentCategory)
                        Accessible.description: qsTr("将「%1」的设置恢复为默认值")
                            .arg(dialog.categoryDisplayName(dialog.currentCategory))
                        onClicked: confirmResetDialog.open()
                    }

                    // docs/31 O1：全局复位。密钥与代理凭据不在复位范围内，
                    // 弹窗里必须写明，否则用户会以为"全部"包含它们。
                    AppButton {
                        objectName: "settingsResetAllButton"
                        text: qsTr("恢复全部设置")
                        visible: dialog.anyReset
                        Accessible.description: qsTr("将所有分类的设置恢复为默认值")
                        onClicked: confirmResetAllDialog.open()
                    }

                    // 即时保存没有"保存"按钮，回执必须自己出现；错误与成功共用
                    // 这一行，空闲时不占视觉注意力。
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        RowLayout {
                            id: saveStatusRow
                            anchors.fill: parent
                            visible: dialog.saveStatusText.length > 0
                            spacing: Theme.spaceXs

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                width: 6
                                height: 6
                                radius: width / 2
                                color: dialog.saveStatusIsError
                                    ? Theme.statusDanger : Theme.statusSuccess
                            }

                            Text {
                                id: saveStatusLabel
                                objectName: "settingsSaveStatus"
                                Layout.fillWidth: true
                                text: dialog.saveStatusText
                                color: dialog.saveStatusIsError
                                    ? Theme.statusDanger : Theme.statusSuccess
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                elide: Text.ElideRight

                                HoverHandler { id: saveStatusHover }
                                ToolTip.visible: saveStatusHover.hovered
                                                 && saveStatusLabel.truncated
                                ToolTip.text: dialog.saveStatusText
                            }

                            // docs/31 S4.3 N3：可撤销的这次写穿在回执里给一个
                            // 显式入口；5 秒后随回执一起消失。只对偏好键出现
                            // （M1 键 / 密钥类 / 隐藏键的 saved 不建立撤销点）。
                            AppButton {
                                objectName: "settingsUndoSaveButton"
                                Layout.alignment: Qt.AlignVCenter
                                visible: dialog.undoAvailable && dialog.undoKey.length > 0
                                         && EditBuffer.canUndo
                                         && EditBuffer.undoKey === dialog.undoKey
                                text: qsTr("撤销")
                                Accessible.description: qsTr("撤销刚才对「%1」的更改")
                                    .arg(SettingsCatalog.entry(dialog.undoKey).title)
                                onClicked: dialog.performUndo()
                            }
                        }
                    }

                    AppButton {
                        text: qsTr("关闭")
                        highlighted: true
                        onClicked: dialog.close()
                    }
                }
            }

            // docs/31 S4.1 O8：右下角拖拽调整对话框尺寸。挂在底栏 Rectangle
            // 上（不随左右 24px 内边距内缩），贴着真正的右下角；底栏右侧留白
            // 比 16px 把手宽，拖拽不会盖住「关闭」按钮。
            Rectangle {
                id: resizeHandle
                objectName: "settingsResizeHandle"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 16
                height: 16
                color: "transparent"
                z: 2

                Text {
                    anchors.centerIn: parent
                    text: "◢" // ◢：右下角方向提示
                    color: Theme.textSecondary
                    opacity: 0.55
                    font.pixelSize: Theme.fontSizeCaption
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeFDiagCursor
                    property real startPointerX: 0
                    property real startPointerY: 0
                    property int startWidth: 0
                    property int startHeight: 0
                    onPressed: function (mouse) {
                        const p = mapToItem(null, mouse.x, mouse.y)
                        startPointerX = p.x
                        startPointerY = p.y
                        startWidth = dialog.preferredWidth
                        startHeight = dialog.preferredHeight
                    }
                    onPositionChanged: function (mouse) {
                        if (!pressed) return
                        const p = mapToItem(null, mouse.x, mouse.y)
                        const maxW = dialog.parent ? dialog.parent.width - 24 : 4000
                        const maxH = dialog.parent ? dialog.parent.height - 24 : 4000
                        dialog.preferredWidth = Math.max(
                            dialog.minDialogWidth,
                            Math.min(maxW, startWidth + (p.x - startPointerX)))
                        dialog.preferredHeight = Math.max(
                            dialog.minDialogHeight,
                            Math.min(maxH, startHeight + (p.y - startPointerY)))
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

    // 淡出结束后才真正隐藏，避免高亮框在动画中途被 visible=false 截断。
    Timer {
        id: highlightClearTimer
        interval: Theme.motionBase + 40
        repeat: false
        onTriggered: {
            if (highlightOverlay.opacity === 0)
                highlightOverlay.visible = false
        }
    }

    Timer {
        id: saveStatusTimer
        interval: 1800
        repeat: false
        onTriggered: {
            dialog.saveStatusText = ""
            // 回执消失后撤销入口也一并收回（N3：5 秒后不可再撤销）。
            dialog.undoAvailable = false
            dialog.undoKey = ""
        }
    }

    // docs/17: 即时保存后不再有"放弃修改"路径；confirmCancelDialog 已移除，
    // onClosed 的 rollback 仅兜底丢弃校验失败遗留的 staged 残值。
    Dialog {
        id: confirmResetDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 420
        height: 280

        background: DialogBackground {}
        contentItem: ColumnLayout {
            spacing: Theme.spaceSm

            Text {
                text: qsTr("将「%1」恢复为默认值？")
                    .arg(dialog.categoryDisplayName(dialog.currentCategory))
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.topMargin: Theme.spaceXl
                Layout.rightMargin: Theme.spaceXl
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("本页设置会立即写入默认值，操作不可撤销。")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.rightMargin: Theme.spaceXl
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("已保存的模型、搜索与向量 API 密钥不会被清除；如需删除密钥，请使用字段旁的「清除密钥」。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.rightMargin: Theme.spaceXl
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Text {
                // 网络页的复位会清空代理认证（见 resetNetworkToDefaults），
                // 这与其他页"密钥不随复位清除"的约定不同，必须在此说明。
                visible: dialog.currentCategory === "network"
                text: qsTr("注意：代理用户名与密码会一并恢复为默认（清空）。")
                color: Theme.statusWarning
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.rightMargin: Theme.spaceXl
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.rightMargin: Theme.spaceXl
                Layout.bottomMargin: Theme.spaceLg
                spacing: Theme.spaceSm
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: confirmResetDialog.close()
                }
                AppButton {
                    text: qsTr("恢复默认值")
                    highlighted: true
                    onClicked: {
                        dialog.performReset()
                        confirmResetDialog.close()
                    }
                }
            }
        }
    }

    // docs/31 O1：全局确认。与「恢复本页默认值」的区别（凭据不被清除）
    // 必须写在这里，而不是让用户从一次复位的结果里自己发现。
    Dialog {
        id: confirmResetAllDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 440
        height: 300

        background: DialogBackground {}
        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: qsTr("将所有设置恢复为默认值？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.topMargin: Theme.spaceXl
                Layout.rightMargin: Theme.spaceXl
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            Text {
                text: qsTr("模型、界面、智能体、网络、安全、工具、记忆、知识库、用量、日志等分类的设置都会立即写入默认值。该操作不可撤销。")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.rightMargin: Theme.spaceXl
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            Text {
                text: qsTr("已保存的模型、搜索与向量 API 密钥，以及代理用户名与密码不会被清除；清除密钥只能在各页面的字段旁操作。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.rightMargin: Theme.spaceXl
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.rightMargin: Theme.spaceXl
                Layout.bottomMargin: Theme.spaceLg
                spacing: Theme.spaceSm
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: confirmResetAllDialog.close()
                }
                AppButton {
                    text: qsTr("恢复全部设置")
                    highlighted: true
                    onClicked: {
                        dialog.performResetAll()
                        confirmResetAllDialog.close()
                    }
                }
            }
        }
    }
}
