import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 设置中心左侧导航：一级分组标题 + 组内分类。
//
// 分组来自 PreferenceRegistry.categoryGroups()（SettingsSchema 的
// CategoryGroupMeta），分类归属由 CategoryMeta::groupId 决定。分组只影响
// 展示层级，不改变任何存储键——settings.lastCategory 仍是分类 id。
//
// 用 Flickable 承载：分类从 12 个扩到 16 个后，固定 Column 会直接溢出
// 可视区且无法滚动。
//
// 两个密度相关问题在这里处理：
//   * 分组可折叠。「数据与诊断」默认收起，避免新用户一上来就被诊断项淹没；
//     当前分类所在组会在切换/搜索跳转时自动展开，不会出现"选中项不可见"。
//   * 窗口很窄时由宿主把 compact 设为 true：导航收成 56px 图标条，文字与
//     分组标题让位给内容区，分类名走 tooltip 与读屏名。
Rectangle {
    id: nav

    objectName: "settingsCategoryNav"

    property string currentCategory: "model"
    property bool compact: false
    // 按分组 id 记录折叠状态。改值必须整体重新赋值，QML 的 var 属性才能
    // 通知绑定重新求值；直接改成员不会触发。
    property var collapsedGroups: ({ "data": true })

    signal categorySelected(string id)

    // 折叠状态跨会话保留：否则每次打开设置都回到「数据与诊断收起」的
    // 出厂状态，用户刚展开的组下次又没了。自动展开（当前分类所在组）
    // 只改内存，不回写存储，避免"打开一次设置就把折叠偏好洗掉"。
    readonly property string collapsedGroupsKey: "settings.collapsedGroups"

    function loadCollapsedGroups() {
        if (!PreferenceStore.hasStoredValue(collapsedGroupsKey)) return
        const stored = PreferenceStore.load(collapsedGroupsKey)
        const next = {}
        const text = typeof stored === "string" ? stored : ""
        for (const id of text.split(","))
            if (id.length > 0) next[id] = true
        collapsedGroups = next
    }

    function persistCollapsedGroups() {
        PreferenceStore.save(collapsedGroupsKey,
                             Object.keys(collapsedGroups).join(","))
    }

    readonly property var categoryIcons: ({
        "model": "layers",
        "agent": "preset",
        "tools": "terminal",
        "mcp": "branch",
        "interface": "sun",
        "general": "settings",
        "network": "globe",
        "security": "shield",
        "memory": "archive",
        "knowledge": "book",
        "prompts": "edit",
        "projects": "folder",
        "usage": "chart",
        "activity": "pulse",
        "logs": "list",
        "backup": "database"
    })

    function categoryIcon(categoryId) {
        return categoryIcons[categoryId] || "settings"
    }

    function groupCollapsed(groupId) {
        return !compact && collapsedGroups[groupId] === true
    }

    function setGroupCollapsed(groupId, collapsed, persist) {
        const next = {}
        for (const key in collapsedGroups)
            next[key] = collapsedGroups[key]
        if (collapsed) next[groupId] = true
        else delete next[groupId]
        collapsedGroups = next
        if (persist) persistCollapsedGroups()
    }

    function toggleGroup(groupId) {
        if (compact) return
        setGroupCollapsed(groupId, !groupCollapsed(groupId), true)
    }

    // 当前分类必须可见：上次分类、搜索结果跳转、compact 切换都可能落在
    // 一个已收起的组里。
    function ensureActiveGroupExpanded() {
        if (compact) return
        const groupId = PreferenceRegistry.categoryGroup(currentCategory)
        if (groupId.length > 0 && collapsedGroups[groupId] === true)
            setGroupCollapsed(groupId, false)
    }

    onCurrentCategoryChanged: ensureActiveGroupExpanded()
    onCompactChanged: ensureActiveGroupExpanded()
    Component.onCompleted: {
        loadCollapsedGroups()
        ensureActiveGroupExpanded()
    }

    color: Theme.bgSecondary

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSubtle
    }

    Flickable {
        id: navFlick
        anchors.fill: parent
        contentWidth: width
        contentHeight: navColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {
            policy: navColumn.implicitHeight > navFlick.height
                ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: navColumn
            width: navFlick.width
            spacing: 0

            Repeater {
                model: PreferenceRegistry.categoryGroups()

                delegate: Column {
                    id: groupColumn
                    required property string modelData
                    required property int index

                    readonly property bool collapsed: nav.groupCollapsed(groupColumn.modelData)
                    readonly property var groupCategories:
                        PreferenceRegistry.categoriesInGroup(groupColumn.modelData)
                    readonly property bool containsActive:
                        groupColumn.groupCategories.indexOf(nav.currentCategory) >= 0

                    width: navColumn.width
                    spacing: 0

                    Item {
                        id: groupHeader
                        width: groupColumn.width
                        height: nav.compact ? 14 : 32
                        activeFocusOnTab: !nav.compact
                        Accessible.role: Accessible.Button
                        Accessible.name: {
                            I18n.currentLocale
                            return PreferenceRegistry.categoryGroupName(groupColumn.modelData)
                        }
                        Accessible.onPressAction:
                            nav.toggleGroup(groupColumn.modelData)

                        Keys.onReturnPressed: (event) => {
                            nav.toggleGroup(groupColumn.modelData)
                            event.accepted = true
                        }
                        Keys.onEnterPressed: (event) => {
                            nav.toggleGroup(groupColumn.modelData)
                            event.accepted = true
                        }
                        Keys.onSpacePressed: (event) => {
                            nav.toggleGroup(groupColumn.modelData)
                            event.accepted = true
                        }

                        // 组间分隔线；compact 时它是唯一的分组提示。
                        Rectangle {
                            visible: groupColumn.index > 0
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: nav.compact ? 10 : 14
                            anchors.rightMargin: nav.compact ? 10 : 14
                            height: 1
                            color: Theme.borderSubtle
                        }

                        RowLayout {
                            visible: !nav.compact
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 10
                            spacing: Theme.spaceXs

                            Text {
                                Layout.fillWidth: true
                                // Reading currentLocale re-evaluates this
                                // binding on a language switch; groupName
                                // resolves the stored QT_TRANSLATE_NOOP
                                // source at query time.
                                text: {
                                    I18n.currentLocale
                                    return PreferenceRegistry.categoryGroupName(
                                        groupColumn.modelData)
                                }
                                color: groupMA.containsMouse
                                    ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeMicro
                                font.bold: true
                                font.family: Theme.fontMono
                                font.letterSpacing: 0.4
                                elide: Text.ElideRight
                            }

                            AppIcon {
                                icon: groupColumn.collapsed
                                    ? "chevron-right" : "chevron-down"
                                size: 12
                                iconColor: groupMA.containsMouse
                                    ? Theme.textPrimary : Theme.textSecondary
                            }

                            // 用户手动收起当前分类所在的组时，用品牌点提示
                            // "选中项藏在这里"，而不是让选中态凭空消失。
                            Rectangle {
                                visible: groupColumn.collapsed
                                    && groupColumn.containsActive
                                width: 6
                                height: 6
                                radius: width / 2
                                color: Theme.brandPrimary
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: "transparent"
                            border.width: groupHeader.activeFocus ? 1 : 0
                            border.color: Theme.brandPrimary
                        }

                        MouseArea {
                            id: groupMA
                            anchors.fill: parent
                            enabled: !nav.compact
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                groupHeader.forceActiveFocus()
                                nav.toggleGroup(groupColumn.modelData)
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        visible: !groupColumn.collapsed
                        spacing: 2

                        Repeater {
                            model: groupColumn.groupCategories

                            delegate: Rectangle {
                                id: navItem
                                required property string modelData

                                readonly property bool isActive:
                                    navItem.modelData === nav.currentCategory
                                readonly property string categoryName: {
                                    I18n.currentLocale
                                    // Reading currentLocale re-evaluates this
                                    // binding on a language switch;
                                    // categoryName resolves the stored
                                    // QT_TRANSLATE_NOOP source at query time.
                                    return PreferenceRegistry.categoryName(
                                        navItem.modelData)
                                }

                                x: Theme.spaceXs
                                width: groupColumn.width - Theme.spaceMd
                                height: 36
                                radius: Theme.radiusMd
                                activeFocusOnTab: true

                                color: isActive
                                    ? Qt.rgba(Theme.brandPrimary.r,
                                              Theme.brandPrimary.g,
                                              Theme.brandPrimary.b, 0.10)
                                    : (itemMA.containsMouse ? Theme.surfaceHover
                                                            : "transparent")

                                AppIcon {
                                    icon: nav.categoryIcon(navItem.modelData)
                                    size: 16
                                    iconColor: navItem.isActive
                                        ? Theme.brandPrimary
                                        : (itemMA.containsMouse ? Theme.textPrimary
                                                                : Theme.textSecondary)
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: nav.compact
                                        ? (navItem.width - width) / 2 : 10
                                }

                                Text {
                                    visible: !nav.compact
                                    anchors.left: parent.left
                                    anchors.leftMargin: 34
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: navItem.categoryName
                                    color: navItem.isActive
                                        ? Theme.brandPrimary : Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontSans
                                    font.bold: navItem.isActive
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: "transparent"
                                    border.width: navItem.activeFocus ? 1 : 0
                                    border.color: Theme.brandPrimary
                                }

                                // 图标条模式下文字不可见，名称靠 tooltip 补足。
                                ToolTip.visible: nav.compact && itemMA.containsMouse
                                ToolTip.text: navItem.categoryName
                                ToolTip.delay: 450
                                ToolTip.timeout: 4000

                                Keys.onReturnPressed: (event) => {
                                    nav.categorySelected(navItem.modelData)
                                    event.accepted = true
                                }
                                Keys.onEnterPressed: (event) => {
                                    nav.categorySelected(navItem.modelData)
                                    event.accepted = true
                                }
                                Keys.onSpacePressed: (event) => {
                                    nav.categorySelected(navItem.modelData)
                                    event.accepted = true
                                }

                                MouseArea {
                                    id: itemMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        navItem.forceActiveFocus()
                                        nav.categorySelected(navItem.modelData)
                                    }
                                }

                                Accessible.role: Accessible.ListItem
                                Accessible.name: {
                                    I18n.currentLocale
                                    return PreferenceRegistry.categoryName(
                                        navItem.modelData)
                                }
                                Accessible.description: {
                                    I18n.currentLocale
                                    return navItem.isActive
                                        ? qsTr("当前选中")
                                        : PreferenceRegistry.categoryGroupName(
                                              groupColumn.modelData)
                                }
                                Accessible.onPressAction:
                                    nav.categorySelected(navItem.modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
