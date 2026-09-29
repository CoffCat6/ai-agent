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

    color: Theme.bgSecondary
    border.width: 0
    clip: true

    // Batch 17: while the search field has text the sidebar shows the
    // cross-session search results instead of the session list.
    readonly property bool searching: searchField.text.trim().length > 0
    // Batch 18: session picked for export + chosen format ("md" / "json")
    property string exportSessionId: ""
    property string exportFormat: "md"

    // docs/17 树状共存: 项目节点展开状态（id → bool）。对象键变更不触发
    // 绑定重算，expansionEpoch 作为失效标记强制刷新。
    property var expandedProjects: ({})
    property int expansionEpoch: 0

    // Fade the content out while collapsing so text never gets squeezed
    readonly property real contentFade: AgentVM.isSidebarExpanded ? 1.0 : 0.0

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
                    width: 24
                    height: 24
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
                    text: qsTr("新会话")
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
                onClicked: AgentVM.openTab()
            }

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("新会话")
            Accessible.onPressAction: AgentVM.openTab()
        }

        // ── Tree sidebar（docs/17 树状共存）──────────────
        // 项目树与对话树共存：所有会话始终可见。点击项目行 = 激活
        // 该项目（沙箱 + 新会话归属）并展开它；点击「对话」头 = 回到
        // 本地对话模式。旧"切换视图互相隐藏"的交互废弃。
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
                        text: "PROJECTS"
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontMono
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }

                    Rectangle {
                        width: 22; height: 22; radius: 4
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
                        // 展开与激活解耦：只由点击状态表决定（点击取反，
                        // epoch 作绑定失效标记）。激活中的项目同样可收起。
                        readonly property bool expanded: {
                            root.expansionEpoch
                            return root.expandedProjects[modelData.id] === true
                        }
                        readonly property var projectSessions: {
                            root.expansionEpoch   // 失效标记：强制重算
                            return AgentVM.sessionList.filter(function(s) {
                                return s.projectId === projectNode.modelData.id
                            })
                        }
                        Layout.fillWidth: true
                        spacing: 2

                        // 项目头行：点击 = 激活 + 展开（不再 toggle 退出）
                        Rectangle {
                            id: projectHeader
                            property bool confirmDelete: false
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            radius: 6
                            color: projectMA.containsMouse || projectNode.isActive
                                ? Theme.surfaceHover : "transparent"

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 32
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
                                    text: projectNode.modelData.name
                                    color: projectNode.isActive
                                        ? Theme.brandPrimary : Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                }
                            }

                            // 删除项目（两步确认；会话保留为本地会话）
                            Rectangle {
                                width: 20; height: 20; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                visible: projectMA.containsMouse
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

                            MouseArea {
                                id: projectMA
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    // 激活该项目（沙箱/归属跟随）并展开；
                                    // 已激活时点击 = 切换折叠（退出走「对话」头）。
                                    if (!projectNode.isActive) {
                                        AgentVM.setActiveProjectId(
                                            projectNode.modelData.id)
                                        root.expandedProjects[
                                            projectNode.modelData.id] = true
                                    } else {
                                        root.expandedProjects[
                                            projectNode.modelData.id]
                                            = !projectNode.expanded
                                    }
                                    root.expansionEpoch++
                                }
                            }
                        }

                        // 展开的会话（缩进显示）
                        ColumnLayout {
                            visible: projectNode.expanded
                            Layout.fillWidth: true
                            Layout.leftMargin: 14
                            spacing: 2

                            Repeater {
                                model: projectNode.projectSessions
                                delegate: sessionRow
                            }
                        }
                    }
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
                    color: localMA.containsMouse || localHeader.isActive
                        ? Theme.surfaceHover : "transparent"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 36
                        spacing: 6

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
                            onClicked: {
                                AgentVM.setActiveProjectId("")
                                AgentVM.openTab()
                            }
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("新建本地对话")
                    }

                    MouseArea {
                        id: localMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: AgentVM.setActiveProjectId("")
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("本地对话")
                }

                // 本地会话（缩进）
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    spacing: 2

                    Repeater {
                        model: {
                            root.expansionEpoch
                            return AgentVM.sessionList.filter(function(s) {
                                return !s.projectId
                            })
                        }
                        delegate: sessionRow
                    }
                }

                Text {
                    visible: {
                        root.expansionEpoch
                        return AgentVM.sessionList.filter(function(s) {
                            return !s.projectId
                        }).length === 0
                    }
                    text: qsTr("暂无历史会话")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    Layout.leftMargin: 24
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
                onTriggered: AgentVM.searchMessages(searchField.text.trim())
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

                background: Rectangle {
                    color: hovered ? Theme.surfaceHover : "transparent"
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
                        height: 20

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
                            onAccepted: {
                                if (text.trimmed().length > 0)
                                    AgentVM.renameSession(modelData.id, text)
                                sessionDelegate.renaming = false
                            }
                            onFocusChanged: if (!focus && sessionDelegate.renaming)
                                sessionDelegate.renaming = false

                            Component.onCompleted: if (sessionDelegate.renaming) forceActiveFocus()
                        }

                        Text {
                            visible: !sessionDelegate.renaming
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title && modelData.title.length > 0
                                ? modelData.title : qsTr("新会话")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                            width: parent.width - (sessionDelegate.hovered ? 66 : 0)
                        }

                        Row {
                            visible: sessionDelegate.hovered && !sessionDelegate.renaming
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            spacing: 2

                            // Export action (Batch 18)
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                color: exportMA.containsMouse ? Theme.surfaceHover : "transparent"

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
                            }

                            // Rename action
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                color: renameMA.containsMouse ? Theme.surfaceHover : "transparent"

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
                            }

                            // Delete action (two-step confirm)
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                color: deleteMA.containsMouse || sessionDelegate.confirmDelete
                                    ? Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                                              Theme.statusDanger.b, 0.15) : "transparent"

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
                                    onClicked: {
                                        if (!sessionDelegate.confirmDelete) {
                                            sessionDelegate.confirmDelete = true
                                            confirmResetTimer.restart()
                                        } else {
                                            confirmResetTimer.stop()
                                            sessionDelegate.confirmDelete = false
                                            AgentVM.deleteSession(modelData.id)
                                        }
                                    }
                                }
                            }
                        }

                        Timer {
                            id: confirmResetTimer
                            interval: 2500
                            onTriggered: sessionDelegate.confirmDelete = false
                        }
                    }

                    Text {
                        text: modelData.timeText ? modelData.timeText : ""
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
                            width: parent.width
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
                    width: 48
                    height: 24
                    radius: 12
                    color: ThemeManager.isDark ? Theme.brandPrimary : Theme.surfaceHover
                    border.width: 1
                    border.color: Theme.borderSubtle

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
                        onClicked: ThemeManager.isDark = !ThemeManager.isDark
                    }
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
