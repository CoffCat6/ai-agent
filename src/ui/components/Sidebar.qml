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
                onClicked: AgentVM.newChat()
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
                }
            }

            Timer {
                id: searchDebounce
                interval: 200
                onTriggered: AgentVM.searchMessages(searchField.text.trim())
            }
        }

        // ── Session History ──────────────────────────
        Text {
            Layout.topMargin: 16
            Layout.leftMargin: 20
            text: root.searching ? qsTr("搜索结果") : "SESSIONS"
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontMono
            font.weight: Font.DemiBold
        }

        ScrollView {
            visible: !root.searching
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true

            ListView {
                id: sessionList
                model: AgentVM.sessionList
                spacing: 2
                clip: true

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: sessionList.count === 0
                    text: qsTr("暂无历史会话")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }

                delegate: ItemDelegate {
                    id: sessionDelegate
                    width: sessionList.width
                    height: 48

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
                    tooltip: qsTr("模型配置 (Ctrl+,)")
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
