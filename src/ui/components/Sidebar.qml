import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import QtCore
import NexusAI
import NexusAI.ViewModels

// Left sidebar - session history and navigation.
// Collapse/expand is driven by AgentVM.isSidebarExpanded: main.qml animates
// this item's width to 0 (content fades via contentFade) and hosts the
// floating toggle handle, which stays reachable while collapsed.
Rectangle {
    id: root

    signal settingsRequested()
    signal auditRequested()
    signal logsRequested()
    // docs/23 G7: a session was (soft-)deleted — main.qml shows the undo
    // toast carrying the id (toasts live at the overlay level, and the
    // no-cross-file-id convention routes this through a signal).
    signal sessionDeletedUndoable(string sessionId, string title)

    color: Theme.bgSecondary
    border.width: 0
    clip: true

    // Batch 17: while the search field has text the sidebar shows the
    // cross-session search results instead of the session list.
    readonly property bool searching: searchField.text.trim().length > 0
    // Batch 18: session picked for export + chosen format ("md" / "json")
    property string exportSessionId: ""
    property string exportFormat: "md"
    property bool includeArchived: false

    // docs/17 树状共存: 项目节点展开状态（id → bool）。对象键变更不触发
    // 绑定重算，expansionEpoch 作为失效标记强制刷新。
    property var expandedProjects: ({})
    property int expansionEpoch: 0
    // 本地对话树展开状态。与项目树对称，默认展开——它是主视图。
    property bool localExpanded: true

    // 会话按项目分组（键 = projectId；"__local__" = 无归属的本地会话）。
    // 一次 O(会话数) 遍历，供两棵树共用。原先每个项目节点各自 filter 全量
    // 列表，复杂度是 O(项目数 × 会话数)，且展开/收起都会触发重算。
    // 只依赖 AgentVM.sessionList / projects 的通知重算——不再挂
    // expansionEpoch，避免每次折叠都把所有 Repeater 的 delegate 重建一遍。
    readonly property var sessionGroups: {
        const map = ({})
        map["__local__"] = []
        const projects = AgentVM.projects
        for (let i = 0; i < projects.length; ++i)
            map[projects[i].id] = []
        const list = AgentVM.sessionList
        for (let j = 0; j < list.length; ++j) {
            const session = list[j]
            if (session.archived && !root.includeArchived) continue
            const pid = session.projectId
            if (pid && map[pid]) map[pid].push(session)
            else if (!pid) map["__local__"].push(session)
        }
        return map
    }
    // 本地会话（无 projectId）——列表与空态两处共用，避免重复过滤。
    readonly property var localSessions: root.sessionGroups["__local__"]
    // 共享的空数组：引用稳定，避免 Repeater 因「新数组身份」反复重建。
    readonly property var noSessions: []

    // 启动时恢复"上次浏览位置"：展开上次所在的项目节点，重启后能直接看到
    // 自己项目的会话。这里**只恢复浏览态**——归属按标签持有，启动标签默认
    // 本地（普通对话），不再把任何项目套到它上面（docs/18 期 3）。
    // projects/activeId 由 AgentViewModel 在归属变化时经 SettingsService
    // 写入（docs/25 A5：内存镜像与 QSettings 保持一致）。
    Component.onCompleted: {
        const last = Settings.activeProjectId
        if (last.length > 0) {
            root.expandedProjects[last] = true
            root.expansionEpoch++
        }
    }

    // Fade the content out while collapsing so text never gets squeezed
    readonly property real contentFade: AgentVM.isSidebarExpanded ? 1.0 : 0.0

    // docs/17 P0-3: 项目被程序化激活（如点击一条项目会话自动联动）时，
    // 与点击项目行一样自动展开该节点。
    Connections {
        target: AgentVM
        function onActiveProjectIdChanged() {
            if (AgentVM.activeProjectId.length > 0
                    && root.expandedProjects[AgentVM.activeProjectId] !== true) {
                root.expandedProjects[AgentVM.activeProjectId] = true
                root.expansionEpoch++
            }
        }
    }

    // Right border separator
    Rectangle {
        width: 1
        height: parent.height
        anchors.right: parent.right
        color: Theme.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0
        opacity: root.contentFade

        Behavior on opacity { NumberAnimation { duration: 150 } }

        // ── Logo / Title ───────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8

                // Logo mark
                Rectangle {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: 4
                    color: Theme.brandPrimary

                    Text {
                        anchors.centerIn: parent
                        text: "N"
                        color: Theme.bgPrimary
                        font.pixelSize: 14
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                }

                Text {
                    text: "NexusAI"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.weight: Font.Bold
                    font.family: Theme.fontSans
                }
            }
        }

        // ── New Chat Button ───────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.preferredHeight: 36
            radius: 6
            color: newChatMA.containsMouse ? Theme.surfaceHover : "transparent"
            border.width: 1
            border.color: Theme.borderSubtle

            Row {
                anchors.centerIn: parent
                spacing: 6

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "add"
                    size: 14
                    iconColor: Theme.textPrimary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    // docs/18: 这是"新建普通对话"的入口（项目会话从项目行的
                    // 「+」建），文案与会话行里"未命名会话"的兜底标题区分开。
                    text: qsTr("新对话")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                }
            }

            MouseArea {
                id: newChatMA
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // Batch 29 (F7): a new session now opens as a new parallel
                // tab, so starting one never stops a task running elsewhere.
                // docs/17: 默认新建"普通对话"——项目会话只从项目行的「+」建。
                onClicked: AgentVM.newLocalChat()
            }

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("新对话")
            Accessible.onPressAction: AgentVM.newLocalChat()
        }

        // ── Tree sidebar（docs/17 树状共存 / docs/18 归属按标签）────
        // 项目树与对话树共存：所有会话始终可见。点击项目行 = **纯浏览**
        // （展开/收起该项目的会话列表），不改写归属；点击「对话」头 =
        // 回到本地对话模式。旧"切换视图互相隐藏"的交互废弃。
        //
        // docs/18：旧实现的"点击项目行 = 激活该项目"来自"全局单活动项目"
        // 模型，该模型已被取代。归属按标签持有，改写它的合法入口只有：
        // 顶栏徽标（仅空标签可点选）、项目行的「+」、点击「对话」头，
        // 以及打开一条旧会话时的同步。侧栏不再承担"激活项目"。
        ScrollView {
            id: treeScroll
            visible: !root.searching
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true

            ColumnLayout {
                id: treeCol
                width: treeScroll.width
                spacing: 2

                // ── 项目树头 ──────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 12
                    Layout.topMargin: 8

                    Text {
                        // docs/25 C3: 区块标题进翻译流程（中文「项目」，
                        // 与下方「对话」标题风格一致）
                        text: qsTr("PROJECTS")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontMono
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }

                    Rectangle {
                        // Layout.preferred* 而非 width/height：本行是
                        // RowLayout，直接写尺寸属未定义行为，且行高会按
                        // 文本(10px)塌陷、按钮 22px 溢出。
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        radius: 4
                        color: importMA.containsMouse
                            ? Theme.surfaceHover : "transparent"
                        AppIcon {
                            anchors.centerIn: parent
                            icon: "add"
                            size: 12
                            iconColor: importMA.containsMouse
                                ? Theme.brandPrimary : Theme.textSecondary
                        }
                        MouseArea {
                            id: importMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: importProjectDialog.open()
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("导入项目")
                    }
                }

                Repeater {
                    model: AgentVM.projects

                    delegate: ColumnLayout {
                        id: projectNode
                        required property var modelData
                        readonly property bool isActive:
                            AgentVM.activeProjectId === modelData.id
                        property bool renaming: false
                        // 展开与激活解耦：只由点击状态表决定（点击取反，
                        // epoch 作绑定失效标记）。激活中的项目同样可收起。
                        readonly property bool expanded: {
                            root.expansionEpoch
                            return root.expandedProjects[modelData.id] === true
                        }
                        // 直接取分组结果（见 root.sessionGroups），不再各自
                        // filter 全量列表；分组对象未覆盖时回落共享空数组。
                        readonly property var projectSessions: {
                            const grouped = root.sessionGroups[projectNode.modelData.id]
                            return grouped !== undefined ? grouped : root.noSessions
                        }
                        Layout.fillWidth: true
                        spacing: 2

                        // 项目头行：点击 = 展开/收起（纯浏览，不改归属）
                        Rectangle {
                            id: projectHeader
                            property bool confirmDelete: false
                            Layout.fillWidth: true
                            // 与「新会话」按钮 / 搜索框 / 「对话」头对齐的内缩。
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.preferredHeight: 32
                            radius: 6
                            color: headerHover.hovered || projectNode.isActive
                                ? Theme.surfaceHover : "transparent"

                            // 整行悬停判定用 HoverHandler 而非铺满整行的
                            // MouseArea：Handler 不是 Item、不参与命中测试，
                            // 因此不会遮挡右侧操作按钮。已实测确认父级
                            // HoverHandler 在子项 MouseArea 被悬停时仍上报
                            // hovered，故按钮自身高亮与整行悬停可共存。
                            HoverHandler { id: headerHover }

                            // 点击层必须声明在按钮之前（= 位于下层）。此前它
                            // 铺满整行且声明在最后，把新建/重命名/删除三个按钮
                            // 的点击全部吞掉（childAt 命中测试证实）。
                            MouseArea {
                                id: projectMA
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (projectNode.renaming) return
                                    // docs/18 §2.2/§3.6：点击项目行是**纯浏览**，
                                    // 只切换该项目会话列表的展开态，不改写归属。
                                    // 旧实现此处调 setActiveProjectId，会在活动
                                    // 标签已有会话时把它静默改写到该项目上：
                                    // 顶栏徽标随之切换，且该标签的沙箱/系统提示
                                    // 与其正在显示的会话脱钩。
                                    root.expandedProjects[
                                        projectNode.modelData.id]
                                        = !projectNode.expanded
                                    root.expansionEpoch++
                                }
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                // 右侧留三个悬浮按钮的宽度（新建/重命名/删除）。
                                anchors.rightMargin: 68
                                spacing: 6

                                AppIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    icon: projectNode.expanded
                                        ? "chevron-down" : "chevron-right"
                                    size: 11
                                    iconColor: Theme.textSecondary
                                }
                                AppIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    icon: "folder"
                                    size: 13
                                    iconColor: projectNode.isActive
                                        ? Theme.brandPrimary : Theme.textSecondary
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - parent.spacing * 2 - 24
                                    visible: !projectNode.renaming
                                    text: projectNode.modelData.name
                                    color: projectNode.isActive
                                        ? Theme.brandPrimary : Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                }
                                // P2: 项目行内重命名（与会话行同交互：回车
                                // 提交，失焦取消）。
                                TextInput {
                                    id: projectRenameInput
                                    visible: projectNode.renaming
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - parent.spacing * 2 - 24
                                    text: projectNode.modelData.name
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    selectByMouse: true
                                    clip: true
                                    // 必须用 JS 的 trim()：QML 字符串没有
                                    // QString.trimmed()，调用会抛 TypeError 并
                                    // 中断 onAccepted —— 重命名静默失效。
                                    onAccepted: {
                                        if (text.trim().length > 0)
                                            AgentVM.renameProject(
                                                projectNode.modelData.id, text)
                                        projectNode.renaming = false
                                    }
                                    onFocusChanged: if (!focus && projectNode.renaming)
                                        projectNode.renaming = false
                                    // 聚焦必须在「可见」时触发：Component.
                                    // onCompleted 只在创建时执行一次，之后
                                    // renaming 变 true 不会重放，输入框会显示
                                    // 但不聚焦。
                                    onVisibleChanged: if (visible) {
                                        forceActiveFocus()
                                        selectAll()
                                    }
                                }
                            }

                            // P2: 在该项目下新建会话（先激活再开新 tab，
                            // 新会话归属与沙箱随激活生效）。
                            Rectangle {
                                width: 20; height: 20; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 56
                                visible: headerHover.hovered
                                color: projectNewMA.containsMouse
                                    ? Theme.surfaceHover : "transparent"
                                AppIcon {
                                    anchors.centerIn: parent
                                    icon: "add"
                                    size: 11
                                    iconColor: projectNewMA.containsMouse
                                        ? Theme.brandPrimary : Theme.textSecondary
                                }
                                MouseArea {
                                    id: projectNewMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // 项目会话的唯一入口：项目行的「+」。
                                    onClicked: AgentVM.newProjectChat(
                                        projectNode.modelData.id)
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("在此项目下新建会话")
                            }

                            // P2: 项目重命名入口（行内编辑）。
                            Rectangle {
                                width: 20; height: 20; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 32
                                visible: headerHover.hovered
                                color: projectRenameMA.containsMouse
                                    ? Theme.surfaceHover : "transparent"
                                AppIcon {
                                    anchors.centerIn: parent
                                    icon: "edit"
                                    size: 11
                                    iconColor: projectRenameMA.containsMouse
                                        ? Theme.brandPrimary : Theme.textSecondary
                                }
                                MouseArea {
                                    id: projectRenameMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        projectHeader.confirmDelete = false
                                        projectNode.renaming = true
                                    }
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("重命名项目")
                            }

                            // 删除项目（两步确认；会话保留为本地会话）
                            Rectangle {
                                width: 20; height: 20; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                visible: headerHover.hovered
                                color: projectDeleteMA.containsMouse
                                    || projectHeader.confirmDelete
                                    ? Qt.rgba(Theme.statusDanger.r,
                                              Theme.statusDanger.g,
                                              Theme.statusDanger.b, 0.15)
                                    : "transparent"
                                AppIcon {
                                    anchors.centerIn: parent
                                    visible: !projectHeader.confirmDelete
                                    icon: "close"
                                    size: 11
                                    iconColor: projectDeleteMA.containsMouse
                                        ? Theme.statusDanger : Theme.textSecondary
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: projectHeader.confirmDelete
                                    text: qsTr("确认?")
                                    color: Theme.statusDanger
                                    font.pixelSize: 8
                                    font.bold: true
                                }
                                MouseArea {
                                    id: projectDeleteMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!projectHeader.confirmDelete) {
                                            projectHeader.confirmDelete = true
                                            projectConfirmReset.restart()
                                        } else {
                                            projectConfirmReset.stop()
                                            projectHeader.confirmDelete = false
                                            AgentVM.removeProject(
                                                projectNode.modelData.id)
                                        }
                                    }
                                }
                            }

                            Timer {
                                id: projectConfirmReset
                                interval: 2500
                                onTriggered: projectHeader.confirmDelete = false
                            }

                        }

                        // 展开的会话（缩进显示，与项目行形成可见层级）
                        ColumnLayout {
                            visible: projectNode.expanded
                            Layout.fillWidth: true
                            Layout.leftMargin: 26
                            Layout.rightMargin: 12
                            spacing: 2

                            Repeater {
                                model: projectNode.projectSessions
                                delegate: sessionRow
                            }

                            // 项目下暂无会话的空态（缩进与会话标题对齐）
                            Text {
                                visible: projectNode.projectSessions.length === 0
                                text: qsTr("暂无会话")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                Layout.leftMargin: 20
                                Layout.topMargin: 2
                            }
                        }
                    }
                }

                // 项目列表为空时的空态与导入指引
                Text {
                    visible: AgentVM.projects.length === 0
                    text: qsTr("暂无项目。点上方 + 选择文件夹导入")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 12
                    Layout.topMargin: 2
                }

                // ── 对话树头（本地模式入口）────────────────
                Rectangle {
                    id: localHeader
                    readonly property bool isActive:
                        AgentVM.activeProjectId === ""
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 10
                    Layout.preferredHeight: 32
                    radius: 6
                    color: localHover.hovered || localHeader.isActive
                        ? Theme.surfaceHover : "transparent"

                    // 与项目头同构：Handler 负责整行悬停，点击层下沉到按钮
                    // 之下，否则右侧「新建本地对话」按钮会被整行点击层吞掉。
                    HoverHandler { id: localHover }

                    MouseArea {
                        id: localMA
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // 与项目行同语义：未激活 = 激活并展开；已激活 =
                            // 切换折叠（与项目树对称）。
                            if (!localHeader.isActive) {
                                AgentVM.setActiveProjectId("")
                                root.localExpanded = true
                            } else {
                                root.localExpanded = !root.localExpanded
                            }
                        }
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 36
                        spacing: 6

                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: root.localExpanded
                                ? "chevron-down" : "chevron-right"
                            size: 11
                            iconColor: Theme.textSecondary
                        }
                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "message"
                            size: 13
                            iconColor: localHeader.isActive
                                ? Theme.brandPrimary : Theme.textSecondary
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("对话")
                            color: localHeader.isActive
                                ? Theme.brandPrimary : Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                        }
                    }

                    // 本地归属下新建会话
                    Rectangle {
                        width: 22; height: 22; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        color: localNewMA.containsMouse
                            ? Theme.surfaceHover : "transparent"
                        AppIcon {
                            anchors.centerIn: parent
                            icon: "add"
                            size: 12
                            iconColor: localNewMA.containsMouse
                                ? Theme.brandPrimary : Theme.textSecondary
                        }
                        MouseArea {
                            id: localNewMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: AgentVM.newLocalChat()
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("新建本地对话")
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("本地对话")
                }

                // 本地会话（缩进，随「对话」头折叠）
                ColumnLayout {
                    visible: root.localExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                    Layout.rightMargin: 12
                    spacing: 2

                    Repeater {
                        model: root.localSessions
                        delegate: sessionRow
                    }
                }

                Text {
                    visible: root.localExpanded && root.localSessions.length === 0
                    text: qsTr("暂无历史会话")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    Layout.leftMargin: 46
                    Layout.topMargin: 4
                }
            }
        }

        // ── Search (Batch 17 / F1) ───────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            Layout.preferredHeight: 32
            radius: 6
            color: Theme.bgPrimary
            border.width: searchField.activeFocus ? 2 : 1
            border.color: searchField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "search"
                    size: 14
                    iconColor: Theme.textSecondary
                }

                TextField {
                    id: searchField
                    width: parent.width - parent.spacing - 14 - 16
                    anchors.verticalCenter: parent.verticalCenter
                    placeholderText: qsTr("搜索会话历史…")
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    background: null
                    selectByMouse: true
                    onTextChanged: searchDebounce.restart()

                    Accessible.name: qsTr("搜索会话历史")
                }
            }

            Timer {
                id: searchDebounce
                interval: 200
                onTriggered: AgentVM.searchMessages(searchField.text.trim(), root.includeArchived)
            }
        }

        CheckBox {
            id: archivedToggle
            text: qsTr("包含已归档")
            Layout.leftMargin: 12
            checked: root.includeArchived
            indicator: Rectangle {
                x: 6
                y: (archivedToggle.height - height) / 2
                width: 16
                height: 16
                radius: 3
                color: archivedToggle.checked ? Theme.brandPrimary : Theme.bgPrimary
                border.color: archivedToggle.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                Text {
                    anchors.centerIn: parent
                    text: archivedToggle.checked ? "✓" : ""
                    color: Theme.bgPrimary
                    font.pixelSize: 12
                }
            }
            contentItem: Text {
                leftPadding: 30
                text: archivedToggle.text
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                verticalAlignment: Text.AlignVCenter
            }
            onToggled: {
                root.includeArchived = checked
                searchDebounce.restart()
            }
        }

        FolderDialog {
            id: importProjectDialog
            title: qsTr("选择项目文件夹")
            onAccepted: AgentVM.importProject(selectedFolder)
        }

        // 共用的会话行 delegate（项目树 / 本地树两处复用）
        Component {
            id: sessionRow
            ItemDelegate {
                id: sessionDelegate
                // docs/17: 布局子项（ColumnLayout 内）必须用附加属性——
                // 显式 width/height 会被布局忽略，热区缩成 implicitWidth
                // 窄条（点击"无法切换"的根因）。
                Layout.fillWidth: true
                Layout.preferredHeight: 48

                Accessible.name: modelData.title && modelData.title.length > 0
                    ? modelData.title : qsTr("新会话")
                Accessible.description: modelData.timeText || ""

                readonly property bool isActive: AgentVM.currentSessionId === modelData.id
                property bool renaming: false
                property bool confirmDelete: false

                // 右键菜单：同一组操作（与 hover 按钮等价，供习惯右键的用户）。
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: sessionMenu.popup()
                }

                Menu {
                    id: sessionMenu
                    parent: Overlay.overlay
                    MenuItem {
                        text: modelData.pinned ? qsTr("取消置顶") : qsTr("置顶会话")
                        onTriggered: AgentVM.setSessionPinned(modelData.id, !modelData.pinned)
                    }
                    MenuItem {
                        text: modelData.archived ? qsTr("恢复会话") : qsTr("归档会话")
                        onTriggered: AgentVM.setSessionArchived(modelData.id, !modelData.archived)
                    }
                    // docs/25 D2: 导出/重命名/删除此前只有 hover 操作条一条入口，
                    // 键盘与读屏用户不可达——菜单扩展出等价路径（Menu 自带
                    // 键盘导航）。删除有全局撤销通知兜底，菜单内一步执行。
                    MenuItem {
                        text: qsTr("导出会话")
                        onTriggered: {
                            root.exportSessionId = modelData.id
                            exportDialog.open()
                        }
                    }
                    MenuItem {
                        text: qsTr("重命名会话")
                        onTriggered: {
                            sessionDelegate.confirmDelete = false
                            sessionDelegate.renaming = true
                        }
                    }
                    MenuItem {
                        text: qsTr("删除会话")
                        onTriggered: {
                            const deletedId = modelData.id
                            root.sessionDeletedUndoable(deletedId,
                                                        modelData.title || "")
                            AgentVM.deleteSession(deletedId)
                        }
                    }
                }

                background: Rectangle {
                    // docs/25 D2: activeFocus 高亮让键盘用户可见「行已聚焦」，
                    // 同时操作条依据 activeFocus 显示（见下方 Row.visible）。
                    color: hovered || sessionDelegate.activeFocus
                        ? Theme.surfaceHover : "transparent"
                    Rectangle {
                        width: 3
                        height: parent.height
                        color: Theme.brandPrimary
                        visible: isActive
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 12
                    spacing: 2

                    // Title row: inline editor / label / hover actions
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 20

                        TextInput {
                            id: renameInput
                            visible: sessionDelegate.renaming
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 8
                            text: modelData.title
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            selectByMouse: true
                            clip: true
                            // 用 JS 的 trim()：QML 字符串没有 QString.trimmed()，
                            // 调用会抛 TypeError 并中断 onAccepted。
                            // 自动保存：内容变更即提交（去抖 600ms），Enter 立即
                            // 提交并退出编辑，失焦也保存——不等按 Enter。
                            onTextChanged: {
                                renameCommitTimer.restart()
                            }
                            onAccepted: {
                                renameCommitTimer.stop()
                                if (text.trim().length > 0)
                                    AgentVM.renameSession(modelData.id, text)
                                sessionDelegate.renaming = false
                            }
                            onFocusChanged: if (!focus && sessionDelegate.renaming) {
                                renameCommitTimer.stop()
                                if (text.trim().length > 0
                                        && text !== modelData.title)
                                    AgentVM.renameSession(modelData.id, text)
                                sessionDelegate.renaming = false
                            }
                            // 聚焦改由「可见」触发（Component.onCompleted
                            // 只在创建时跑一次，不会在 renaming 变 true 时重放）。
                            onVisibleChanged: if (visible) {
                                forceActiveFocus()
                                selectAll()
                            }

                            Timer {
                                id: renameCommitTimer
                                interval: 600
                                onTriggered: {
                                    if (!sessionDelegate.renaming) return
                                    const trimmed = renameInput.text.trim()
                                    // 空串或与原标题相同不提交，避免清空标题/空写库。
                                    if (trimmed.length > 0 && trimmed !== modelData.title)
                                        AgentVM.renameSession(modelData.id, trimmed)
                                }
                            }
                        }

                        Text {
                            id: sessionTitle
                            visible: !sessionDelegate.renaming
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title && modelData.title.length > 0
                                ? modelData.title : qsTr("新会话")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                            width: parent.width
                                   - ((sessionDelegate.hovered || sessionDelegate.activeFocus) ? 132 : 0)

                            // docs/25 C5: elide 截断时悬停可看完整标题
                            HoverHandler { id: sessionTitleHover }
                            ToolTip.visible: sessionTitleHover.hovered && sessionTitle.truncated
                            ToolTip.text: sessionTitle.text
                        }

                        // Hover 操作行：置顶 / 归档 / 导出 / 重命名 / 删除。
                        // 全部用直连 MouseArea 的 Rectangle，不再用 Popup Menu——
                        // Menu 在 Repeater delegate 内弹出会被焦点抢占秒关，
                        // 置顶/归档因此从未生效（bug 修复：改为直连按钮）。
                        // docs/25 D2: 键盘可达——行聚焦（ItemDelegate 在 Tab 链
                        // 内）时同样显示，按钮可继续 Tab 进入并回车激活。
                        Row {
                            visible: (sessionDelegate.hovered || sessionDelegate.activeFocus)
                                     && !sessionDelegate.renaming
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            spacing: 2

                            // 置顶 / 取消置顶
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                color: pinMA.containsMouse ? Theme.surfaceHover : "transparent"
                                // docs/25 D1/D2: 焦点环 + 键盘激活
                                border.width: activeFocus ? 2 : 0
                                border.color: Theme.brandPrimary

                                AppIcon {
                                    anchors.centerIn: parent
                                    icon: "pin"
                                    size: 12
                                    rotation: modelData.pinned ? 45 : 0
                                    iconColor: modelData.pinned || pinMA.containsMouse
                                        ? Theme.brandPrimary : Theme.textSecondary
                                }

                                MouseArea {
                                    id: pinMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: AgentVM.setSessionPinned(modelData.id, !modelData.pinned)
                                }
                                activeFocusOnTab: true
                                Keys.onReturnPressed:
                                    AgentVM.setSessionPinned(modelData.id, !modelData.pinned)
                                Keys.onSpacePressed:
                                    AgentVM.setSessionPinned(modelData.id, !modelData.pinned)
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.pinned ? qsTr("取消置顶") : qsTr("置顶会话")
                                Accessible.onPressAction: AgentVM.setSessionPinned(modelData.id, !modelData.pinned)
                            }

                            // 归档 / 恢复
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                color: archiveMA.containsMouse ? Theme.surfaceHover : "transparent"
                                border.width: activeFocus ? 2 : 0
                                border.color: Theme.brandPrimary

                                AppIcon {
                                    anchors.centerIn: parent
                                    icon: "archive"
                                    size: 12
                                    iconColor: modelData.archived || archiveMA.containsMouse
                                        ? Theme.statusWarning : Theme.textSecondary
                                }

                                MouseArea {
                                    id: archiveMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: AgentVM.setSessionArchived(modelData.id, !modelData.archived)
                                }
                                activeFocusOnTab: true
                                Keys.onReturnPressed:
                                    AgentVM.setSessionArchived(modelData.id, !modelData.archived)
                                Keys.onSpacePressed:
                                    AgentVM.setSessionArchived(modelData.id, !modelData.archived)
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.archived ? qsTr("恢复会话") : qsTr("归档会话")
                                Accessible.onPressAction: AgentVM.setSessionArchived(modelData.id, !modelData.archived)
                            }

                            // Export action (Batch 18)
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                color: exportMA.containsMouse ? Theme.surfaceHover : "transparent"
                                border.width: activeFocus ? 2 : 0
                                border.color: Theme.brandPrimary

                                AppIcon {
                                    anchors.centerIn: parent
                                    icon: "export"
                                    size: 12
                                    iconColor: exportMA.containsMouse ? Theme.brandPrimary : Theme.textSecondary
                                }

                                MouseArea {
                                    id: exportMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.exportSessionId = modelData.id
                                        exportDialog.open()
                                    }
                                }
                                // docs/25 D2: 此前缺 Accessible——补 role/name/onPressAction
                                activeFocusOnTab: true
                                Keys.onReturnPressed: {
                                    root.exportSessionId = modelData.id
                                    exportDialog.open()
                                }
                                Keys.onSpacePressed: {
                                    root.exportSessionId = modelData.id
                                    exportDialog.open()
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("导出会话")
                                Accessible.onPressAction: {
                                    root.exportSessionId = modelData.id
                                    exportDialog.open()
                                }
                            }

                            // Rename action
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                color: renameMA.containsMouse ? Theme.surfaceHover : "transparent"
                                border.width: activeFocus ? 2 : 0
                                border.color: Theme.brandPrimary

                                AppIcon {
                                    anchors.centerIn: parent
                                    icon: "edit"
                                    size: 12
                                    iconColor: renameMA.containsMouse ? Theme.brandPrimary : Theme.textSecondary
                                }

                                MouseArea {
                                    id: renameMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        sessionDelegate.confirmDelete = false
                                        sessionDelegate.renaming = true
                                    }
                                }
                                activeFocusOnTab: true
                                Keys.onReturnPressed: {
                                    sessionDelegate.confirmDelete = false
                                    sessionDelegate.renaming = true
                                }
                                Keys.onSpacePressed: {
                                    sessionDelegate.confirmDelete = false
                                    sessionDelegate.renaming = true
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("重命名会话")
                                Accessible.onPressAction: {
                                    sessionDelegate.confirmDelete = false
                                    sessionDelegate.renaming = true
                                }
                            }

                            // Delete action (two-step confirm)
                            Rectangle {
                                id: deleteBtn
                                width: 20
                                height: 20
                                radius: 4
                                color: deleteMA.containsMouse || sessionDelegate.confirmDelete
                                    ? Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                                              Theme.statusDanger.b, 0.15) : "transparent"
                                border.width: activeFocus ? 2 : 0
                                border.color: Theme.statusDanger

                                AppIcon {
                                    anchors.centerIn: parent
                                    visible: !sessionDelegate.confirmDelete
                                    icon: "close"
                                    size: 12
                                    iconColor: deleteMA.containsMouse ? Theme.statusDanger : Theme.textSecondary
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: sessionDelegate.confirmDelete
                                    text: qsTr("确认?")
                                    color: Theme.statusDanger
                                    font.pixelSize: 9
                                    font.bold: true
                                }

                                MouseArea {
                                    id: deleteMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: deleteBtn.confirmOrDelete()
                                }
                                activeFocusOnTab: true
                                Keys.onReturnPressed: deleteBtn.confirmOrDelete()
                                Keys.onSpacePressed: deleteBtn.confirmOrDelete()
                                // 两步确认逻辑提为函数：鼠标/键盘/读屏共用。
                                function confirmOrDelete() {
                                    if (!sessionDelegate.confirmDelete) {
                                        sessionDelegate.confirmDelete = true
                                        confirmResetTimer.restart()
                                    } else {
                                        confirmResetTimer.stop()
                                        sessionDelegate.confirmDelete = false
                                        // 顺序不能反：deleteSession 收缩 Repeater 模型时会同步销毁
                                        // 本 delegate，之后再取任何 id 都抛 "is not defined"，
                                        // 撤销通知从来没送达过。
                                        const deletedId = modelData.id
                                        root.sessionDeletedUndoable(deletedId,
                                                                    modelData.title || "")
                                        AgentVM.deleteSession(deletedId)
                                    }
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("删除会话")
                                Accessible.description: qsTr("需要点击两次确认")
                                Accessible.onPressAction: deleteBtn.confirmOrDelete()
                            }
                        }

                        Timer {
                            id: confirmResetTimer
                            interval: 2500
                            onTriggered: sessionDelegate.confirmDelete = false
                        }
                    }

                    Text {
                        text: (modelData.pinned ? qsTr("已置顶 · ") : "")
                            + (modelData.archived ? qsTr("已归档 · ") : "")
                            + (modelData.timeText || "")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                }

                onClicked: {
                    if (sessionDelegate.renaming) return
                    sessionDelegate.confirmDelete = false
                    AgentVM.loadSession(modelData.id)
                }
            }
        }

        // ── Search Results (Batch 17 / F1) ───────────
        // Grouped by session: the title caption renders on the first row of
        // each group (previousSection != section). Click restores the whole
        // session; in-message scroll positioning is a declared enhancement.
        ScrollView {
            visible: root.searching
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true

            ListView {
                id: searchResultsList
                model: AgentVM.searchResults
                spacing: 2
                clip: true
                section.property: "sessionId"

                Text {
                    anchors.centerIn: parent
                    visible: searchResultsList.count === 0
                    text: qsTr("无匹配结果")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }

                delegate: ItemDelegate {
                    id: resultDelegate
                    width: searchResultsList.width
                    // Batch 21 (B4): a backend notice row (too many messages
                    // for the LIKE scan) renders as a single wrapped caption.
                    readonly property bool isNotice:
                        modelData.sessionId === "__search_notice__"
                    height: isNotice ? 48
                        : (ListView.previousSection !== ListView.section ? 62 : 44)

                    background: Rectangle {
                        color: resultDelegate.hovered && !resultDelegate.isNotice
                            ? Theme.surfaceHover : "transparent"
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 12
                        spacing: 2

                        Text {
                            visible: !resultDelegate.isNotice
                                && ListView.previousSection !== ListView.section
                            text: modelData.sessionTitle && modelData.sessionTitle.length > 0
                                ? modelData.sessionTitle : qsTr("新会话")
                            color: Theme.brandPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.weight: Font.DemiBold
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: resultDelegate.isNotice ? modelData.notice : modelData.snippet
                            color: resultDelegate.isNotice
                                ? Theme.statusWarning : Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            elide: resultDelegate.isNotice ? Text.ElideNone : Text.ElideRight
                            wrapMode: resultDelegate.isNotice ? Text.WordWrap : Text.NoWrap
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: !resultDelegate.isNotice
                            text: modelData.role === "user" ? qsTr("用户")
                                : modelData.role === "assistant" ? qsTr("助手") : modelData.role
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.family: Theme.fontMono
                        }
                    }

                    onClicked: {
                        if (resultDelegate.isNotice) return   // informational, not a session
                        searchField.text = ""          // back to the session list
                        AgentVM.loadSession(modelData.sessionId)
                    }
                }
            }
        }

        // ── Bottom Bar: Audit + Settings + Theme ─────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            Rectangle {
                width: parent.width
                height: 1
                anchors.top: parent.top
                color: Theme.borderSubtle
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 4

                // Runtime log panel (日志开发 doc 6): leftmost entry
                IconButton {
                    icon: "terminal"
                    iconSize: 17
                    tooltip: qsTr("日志")
                    onClicked: root.logsRequested()
                }

                // Audit log button
                IconButton {
                    icon: "shield"
                    iconSize: 17
                    tooltip: qsTr("审计日志")
                    onClicked: root.auditRequested()
                }

                // Settings button
                IconButton {
                    icon: "settings"
                    iconSize: 17
                    tooltip: qsTr("设置 (Ctrl+,)")
                    onClicked: root.settingsRequested()
                }

                Item { Layout.fillWidth: true }

                // Theme toggle switch: knob carries the mode icon
                Rectangle {
                    id: themeToggle
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 24
                    radius: 12
                    color: ThemeManager.isDark ? Theme.brandPrimary : Theme.surfaceHover
                    border.width: themeToggle.activeFocus ? 2 : 1
                    border.color: themeToggle.activeFocus
                        ? Theme.brandPrimary : Theme.borderSubtle

                    // 切换动作提为函数：鼠标/键盘/读屏共用。
                    function toggleTheme() {
                        ThemeManager.isDark = !ThemeManager.isDark
                    }

                    Rectangle {
                        width: 18
                        height: 18
                        radius: 9
                        anchors.verticalCenter: parent.verticalCenter
                        x: ThemeManager.isDark ? 27 : 3
                        color: Theme.bgSecondary
                        border.width: 1
                        border.color: Theme.borderSubtle

                        AppIcon {
                            anchors.centerIn: parent
                            icon: ThemeManager.isDark ? "moon" : "sun"
                            size: 11
                            iconColor: Theme.textPrimary
                        }

                        Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: themeToggle.toggleTheme()
                    }

                    // docs/25 D5: 纯 MouseArea 开关补键盘与读屏可达
                    activeFocusOnTab: true
                    Keys.onReturnPressed: themeToggle.toggleTheme()
                    Keys.onSpacePressed: themeToggle.toggleTheme()
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: qsTr("深色模式")
                    Accessible.description: ThemeManager.isDark
                        ? qsTr("当前为深色主题，按回车切换到浅色")
                        : qsTr("当前为浅色主题，按回车切换到深色")
                    Accessible.checkable: true
                    Accessible.checked: ThemeManager.isDark
                    Accessible.onPressAction: themeToggle.toggleTheme()
                }
            }
        }
    }

    // ── Export dialog (Batch 18 / F2) ─────────────────
    // SaveFile with both format filters; the selected filter row decides the
    // export format. main.qml surfaces the VM's noticeOccurred as a toast.
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        nameFilters: [qsTr("Markdown (*.md)"), qsTr("JSON (*.json)")]
        onAccepted: AgentVM.exportSession(
            root.exportSessionId, selectedFile,
            selectedNameFilter.index === 0 ? "md" : "json")
    }
}
