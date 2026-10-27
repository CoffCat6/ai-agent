import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1200
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    title: "NexusAI"
    color: Theme.bgPrimary

    // Injected by main.cpp via engine.setInitialProperties: whether the OS
    // really has a tray area. Close-to-tray is only safe when it does.
    property bool trayAvailable: true

    // ── Window geometry persistence (启动时恢复窗口) ─────
    // Restore only when the preference allows it; AgentVM.loadWindowGeometry
    // already drops positions that are off every screen.
    Component.onCompleted: {
        if (!AgentVM.restoreWindowOnStartup) return
        const geo = AgentVM.loadWindowGeometry()
        if (!geo.valid) return
        if (geo.maximized) {
            mainWindow.visibility = Window.Maximized
        } else {
            x = geo.x
            y = geo.y
            width = geo.width
            height = geo.height
        }
    }
    onClosing: (close) => {
        AgentVM.saveWindowGeometry(x, y, width, height,
                                   visibility === Window.Maximized)
        // Qt6 的 QQuickCloseEvent 只有 accepted 布尔属性（默认 true），没有
        // Qt5 QCloseEvent 的 accept() 方法——调用它会抛 TypeError。
        // docs/20 §4.1（D10）：开启"关闭时最小化到托盘"后不再真正关窗，
        // 只把窗口藏起来（托盘图标仍在，双击召回）；真正的退出走托盘菜单。
        // 托盘不可用时绝不能藏：否则窗口消失又没有任何召回入口，只能去
        // 任务管理器结束进程（trayAvailable 由 main.cpp 注入）。
        const closeToTray = PreferenceStore.load("window.closeToTray") === true
                           && mainWindow.trayAvailable
        if (closeToTray) {
            close.accepted = false
            mainWindow.hide()
            return
        }
        close.accepted = true
    }

    // Width always kept free of the two side panels when resizing them
    readonly property int chatMinWidth: 320

    // ── Main Layout ────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Left: Sidebar ──────────────────────────────
        // Collapse animates the width to 0 (content fades inside the
        // Sidebar); the floating handle below stays reachable while hidden.
        // Expanded width comes from AgentVM.sidebarWidth, dragged via
        // leftSplitter and persisted across runs.
        Sidebar {
            id: sidebarPanel
            Layout.preferredWidth: AgentVM.isSidebarExpanded ? AgentVM.sidebarWidth : 0
            Layout.fillHeight: true
            // Stay visible through the whole collapse animation; RowLayout
            // skips invisible items entirely, which would freeze the width.
            visible: AgentVM.isSidebarExpanded || width > 0.5

            Behavior on Layout.preferredWidth {
                // While the splitter is dragged the width must track the
                // cursor exactly instead of easing behind it
                enabled: !leftSplitter.dragActive
                NumberAnimation { duration: 220; easing.type: Easing.OutQuad }
            }

            onSettingsRequested: settingsCenterDialog.openCategory("model")
            onAuditRequested: auditDialog.open()
            onLogsRequested: logPanel.open()
            onKnowledgeRequested: knowledgeDialog.open()
            onSettingsCategoryRequested: (category) => settingsCenterDialog.openCategory(category)
        }

        // ── Sidebar / Chat Splitter ────────────────────
        SplitBar {
            id: leftSplitter
            // Sits before AgentChatPage in the RowLayout, so lift it above
            // the chat's ScrollView to keep the full hit area usable
            z: 5
            Layout.fillHeight: true
            visible: sidebarPanel.width > 0.5
            Layout.preferredWidth: visible ? implicitWidth : 0

            onDragged: (delta) => {
                const reserved = (AgentVM.isPanelVisible ? AgentVM.panelWidth : 0)
                    + chatMinWidth + rightSplitter.width
                AgentVM.sidebarWidth = Math.max(180, Math.min(
                    AgentVM.sidebarWidth + delta,
                    Math.min(560, mainWindow.width - reserved)))
            }
            onResetRequested: AgentVM.sidebarWidth = 260
        }

        // ── Center: Chat Area ──────────────────────────
        AgentChatPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            onSettingsRequested: settingsCenterDialog.openCategory("model")
            onSettingsCategoryRequested: (category) => settingsCenterDialog.openCategory(category)
        }

        // ── Chat / Context Panel Splitter ──────────────
        SplitBar {
            id: rightSplitter
            z: 5
            Layout.fillHeight: true
            visible: contextPanel.width > 0.5
            Layout.preferredWidth: visible ? implicitWidth : 0

            // Dragging left (negative delta) widens the panel
            onDragged: (delta) => {
                const reserved = (AgentVM.isSidebarExpanded ? AgentVM.sidebarWidth : 0)
                    + chatMinWidth + leftSplitter.width
                AgentVM.panelWidth = Math.max(220, Math.min(
                    AgentVM.panelWidth - delta,
                    Math.min(640, mainWindow.width - reserved)))
            }
            onResetRequested: AgentVM.panelWidth = 300
        }

        // ── Right: Context Panel ──────────────────────
        ContextPanel {
            id: contextPanel
            Layout.preferredWidth: AgentVM.isPanelVisible ? AgentVM.panelWidth : 0
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            visible: AgentVM.isPanelVisible || width > 0.5

            Behavior on Layout.preferredWidth {
                enabled: !rightSplitter.dragActive
                NumberAnimation { duration: 220; easing.type: Easing.OutQuad }
            }

        }

        // docs/20 §5.1 (CITE-801): 引用徽标 → 文档详情。AssistantMessage 不
        // 直接引用对话框 id（拿不到），改为走 AgentVM 的显式信号链，这里统一
        // 接到同一个 KnowledgeDocDialog 实例。
        Connections {
            target: AgentVM
            function onKnowledgeDocumentOpenRequested(name, chunk) {
                knowledgeDocDialog.openFor(name, chunk)
            }
        }
    }

    // ── Sidebar Toggle Handle ──────────────────────────
    // Floats on the sidebar's right edge; docks to the window edge while
    // collapsed so the sidebar can always be re-opened from here.
    Item {
        id: sidebarHandle
        width: 24
        height: 56
        z: 20
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(sidebarPanel.width - 12, 6)

        Rectangle {
            anchors.fill: parent
            radius: height / 2

            color: {
                if (sidebarHandleMA.containsMouse && AgentVM.isSidebarExpanded) return Theme.brandPrimary
                if (sidebarHandleMA.containsMouse) return Theme.surfaceHover
                return Theme.bgSecondary
            }
            border.width: 1
            border.color: sidebarHandleMA.containsMouse && AgentVM.isSidebarExpanded
                ? Theme.brandPrimary : Theme.borderSubtle

            Behavior on color { ColorAnimation { duration: 150 } }

            AppIcon {
                anchors.centerIn: parent
                icon: AgentVM.isSidebarExpanded ? "chevron-left" : "chevron-right"
                size: 14
                iconColor: sidebarHandleMA.containsMouse && AgentVM.isSidebarExpanded
                    ? Theme.bgPrimary : Theme.textSecondary
            }

            MouseArea {
                id: sidebarHandleMA
                anchors.fill: parent
                anchors.margins: -4   // Slightly larger hit area
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: AgentVM.toggleSidebar()
            }

            ToolTip.visible: sidebarHandleMA.containsMouse
            ToolTip.delay: 600
            ToolTip.text: AgentVM.isSidebarExpanded
                ? qsTr("收起侧边栏 (Ctrl+[)") : qsTr("展开侧边栏 (Ctrl+[)")
        }
    }

    // ── Context Panel Toggle Handle ────────────────────
    // Mirrors the sidebar handle on the right edge.
    Item {
        id: panelHandle
        width: 24
        height: 56
        z: 20
        anchors.verticalCenter: parent.verticalCenter
        x: parent.width - width - Math.max(contextPanel.width - 12, 6)

        Rectangle {
            anchors.fill: parent
            radius: height / 2

            color: {
                if (panelHandleMA.containsMouse && AgentVM.isPanelVisible) return Theme.brandPrimary
                if (panelHandleMA.containsMouse) return Theme.surfaceHover
                return Theme.bgSecondary
            }
            border.width: 1
            border.color: panelHandleMA.containsMouse && AgentVM.isPanelVisible
                ? Theme.brandPrimary : Theme.borderSubtle

            Behavior on color { ColorAnimation { duration: 150 } }

            AppIcon {
                anchors.centerIn: parent
                icon: AgentVM.isPanelVisible ? "chevron-right" : "chevron-left"
                size: 14
                iconColor: panelHandleMA.containsMouse && AgentVM.isPanelVisible
                    ? Theme.bgPrimary : Theme.textSecondary
            }

            MouseArea {
                id: panelHandleMA
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: AgentVM.togglePanel()
            }

            ToolTip.visible: panelHandleMA.containsMouse
            ToolTip.delay: 600
            ToolTip.text: AgentVM.isPanelVisible
                ? qsTr("收起上下文面板 (Ctrl+B)") : qsTr("展开上下文面板 (Ctrl+B)")
        }
    }

    // ── Settings Center Dialog (General Settings) ─────
    SettingsCenterDialog {
        id: settingsCenterDialog
        objectName: "settingsCenterDialog"
        parent: Overlay.overlay
        onSettingsSaved: noticeToast.show(qsTr("设置已保存"))
        onSettingsError: (message) => errorToast.show(message)
    }

    // ── Audit Log Dialog ──────────────────────────────
    AuditDialog {
        id: auditDialog
        parent: Overlay.overlay
    }

    // ── Runtime Log Panel (日志开发 doc 6) ─────────────
    LogPanel {
        id: logPanel
        parent: Overlay.overlay
    }

    // docs/20 §3.4（A4 拖拽附件）：窗口级文件拖放。
    //
    // 为什么是 QML 根级 DropArea：QQuickWindow 的拖放事件经 delivery agent
    // 分派给带 ItemAcceptsDrops 的 item，而只有 DropArea 会设置该 flag；
    // 纯 C++ 事件过滤器无法让平台“接受”（光标始终是禁止形状）。
    // 为什么放在最后声明：DropArea 是透明命中层，声明在内容之后即为
    // contentItem 最上层子项，铺满整窗且不受 RowLayout 锚定约束。
    //
    // 关键契约：`drop.urls` 里的 url 元素是 QML 的 URL 包装对象，只有
    // toString()/valueOf()，**没有 toLocalFile()**（QML 侧调用会抛
    // TypeError，导致 paths 恒为空、drop 静默失效）。因此这里必须把
    // 原始 urls 交给 C++，由 AgentViewModel::handleDroppedFiles 用
    // QUrl 转成真实本地路径（含百分号解码）。不要在 QML 里解析路径。
    DropArea {
        id: windowDrop
        anchors.fill: parent
        onEntered: function(drag) {
            // 显式接受含文件 URL 的拖拽（光标变“允许”）；否则不干预默认行为。
            if (drag.hasUrls)
                drag.acceptProposedAction()
        }
        onDropped: function(drop) {
            if (!drop.hasUrls) return
            AgentVM.handleDroppedFiles(drop.urls)
            drop.acceptProposedAction()
        }
    }

    // ── Knowledge Document Detail Dialog ──────────────
    // Knowledge base management. Global like the log/audit dialogs beside it in
    // the sidebar tool strip - it is not scoped to a project or a session.
    KnowledgeDialog {
        id: knowledgeDialog
        parent: Overlay.overlay
        onDocumentDetailsRequested: (name) => knowledgeDocDialog.openFor(name)
    }

    KnowledgeDocDialog {
        id: knowledgeDocDialog
        parent: Overlay.overlay
    }

    // ── Task List Dialog ──────────────────────────────
    // The todo list moved out of the narrow sidebar: a task list and a diff
    // both need width the 220..640px column cannot give. `todos` is bound to
    // the live VM list, so a task written while the dialog is open shows up
    // without any plumbing; the TopBar button just calls open().
    TodoDialog {
        id: todoDialog
        parent: Overlay.overlay
        todos: AgentVM.todoList
    }

    // ── File Review Dialog ────────────────────────────
    // Same reason as above: the diff pane was unreadable in the sidebar.
    // Bound to the live checkpoint list, so opening needs no data hand-off.
    ReviewDialog {
        id: reviewDialog
        parent: Overlay.overlay
    }


    // ── Error / Notice Toasts ───────────────────────────
    AppToast {
        id: errorToast
        accentColor: Theme.statusDanger
        duration: 5000
    }

    Connections {
        target: AgentVM
        function onErrorOccurred(message) {
            errorToast.show(message)
        }
        function onModelSettingsRequested() {
            settingsCenterDialog.openCategory("model")
        }
    }

    AppToast {
        id: noticeToast
        accentColor: Theme.statusSuccess
        duration: 3500
        // Slide below the error toast while both are on screen
        anchors.topMargin: errorToast.visible ? 64 : 24
        // Animate the margin so the reflow doesn't jump when the error
        // toast appears or disappears underneath.
        Behavior on anchors.topMargin { NumberAnimation { duration: 150 } }
    }

    Connections {
        target: AgentVM
        function onNoticeOccurred(message) {
            noticeToast.show(message)
        }
    }

    // docs/23 G7: session-delete undo toast — the 30-day soft-delete window
    // is the safety net, this toast is the affordance.
    AppToast {
        id: deleteUndoToast
        property string pendingSessionId: ""
        accentColor: Theme.textSecondary
        duration: 10000
        // Slide below the notice toast while both are on screen
        anchors.topMargin: noticeToast.visible ? 104 : 24
        onActionTriggered: {
            AgentVM.undoDeleteSession(pendingSessionId)
            pendingSessionId = ""
        }
    }

    Connections {
        target: sidebarPanel
        function onSessionDeletedUndoable(sessionId, title) {
            deleteUndoToast.pendingSessionId = sessionId
            deleteUndoToast.showWithAction(
                qsTr("已删除会话「%1」，可撤销").arg(
                    title.length > 0 ? title : qsTr("会话")),
                qsTr("撤销"))
        }
    }

    // ── Keyboard Shortcuts ─────────────────────────────
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsCenterDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+L"
        onActivated: auditDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+B"
        onActivated: AgentVM.togglePanel()
    }

    // Review and the task list are dialogs now (the sidebar was too narrow
    // for either), so they get keys next to the panel toggle they replaced.
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: reviewDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+T"
        onActivated: todoDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+["
        onActivated: AgentVM.toggleSidebar()
    }

    Shortcut {
        sequence: "Ctrl+D"
        onActivated: ThemeManager.isDark = !ThemeManager.isDark
    }

    // docs/20 §4.4（D9 快捷键速查表）：Ctrl+/（与 Ctrl+, 同键位的常见布局）
    // 打开速查表，让新用户能发现功能。
    Shortcut {
        sequence: "Ctrl+/"
        onActivated: shortcutsDialog.open()
    }

    ShortcutsDialog {
        id: shortcutsDialog
        parent: Overlay.overlay
    }
}
