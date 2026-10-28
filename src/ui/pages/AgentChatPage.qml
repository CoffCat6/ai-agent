import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Agent chat page - main content area with trace view and input
Item {
    id: root

    signal settingsRequested()
    // TopBar 预设菜单的「管理…」：带目标分类打开设置中心。
    signal settingsCategoryRequested(string category)

    // A1 (hit-locate): 搜索命中定位高亮行索引，-1 表示不高亮。
    // TraceNodeRouter.highlighted 绑定到 index === revealHighlightIndex。
    property int revealHighlightIndex: -1
    // 高亮强度：搜索命中 0.10（"这是你要找的"），导轨跳转 0.06
    // （只是"确认落点"，不该和搜索结果混淆）。
    property real revealHighlightStrength: 0.10


    // Sending can synchronously insert the user row before requestFollow().
    // Forward the intent immediately instead of waiting for another count change.
    QtObject {
        id: traceJumpImpl
        signal followRequested()
        function requestFollow() {
            followRequested()
        }
    }
    readonly property alias traceJump: traceJumpImpl

    Accessible.name: qsTr("AI 对话页面")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            // 顶栏的模板入口「管理模板…」与空态入口同一条路径。
            onTemplatesRequested: root.settingsCategoryRequested("prompts")
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TranscriptViewport {
                id: transcript
                anchors.fill: parent
                model: AgentVM.traceModel
                scrollBarColor: Theme.textSecondary

                // Keep detail state outside delegates so resets/session changes
                // do not discard the user's expand/collapse choices.
                //
                // docs/30 §5-3: 按会话分桶。折叠 key 对流式中间节点是
                // `type:index:内容前缀`（TraceNodeRouter.qml），index 在两条
                // 会话里必然撞车——单窗口模型下切换会话是高频路径，共用一个
                // 字典会让 A 会话的卡片在 B 会话里被错误折叠/展开。
                // 外层键是 sessionId，空会话（还没发消息）用空串。
                property var detailStateBySession: ({})
                readonly property string detailSessionKey: AgentVM.currentSessionId
                readonly property var detailState: {
                    const all = detailStateBySession
                    const key = detailSessionKey
                    return key in all ? all[key] : ({})
                }
                property bool showProcess: true
                readonly property int intermediateCount: AgentVM.intermediateCount

                function detailExpanded(key, defaultExpanded) {
                    const state = detailState
                    return key in state ? state[key] : defaultExpanded
                }

                function toggleDetail(key, defaultExpanded) {
                    const all = Object.assign({}, detailStateBySession)
                    const session = Object.assign({}, detailState)
                    session[key] = !(key in session ? session[key] : defaultExpanded)
                    all[detailSessionKey] = session
                    detailStateBySession = all
                }

                function toggleProcess() {
                    showProcess = !showProcess
                    requestScrollToBottom()
                }

                delegate: TraceNodeRouter {
                    width: transcript.availableWidth
                    showProcess: transcript.showProcess
                    detailExpandedFn: function(key, def) {
                        return transcript.detailExpanded(key, def)
                    }
                    toggleDetailFn: function(key, def) {
                        transcript.toggleDetail(key, def)
                    }
                    // A1 (hit-locate): 搜索命中定位——索引匹配时短暂高亮。
                    highlighted: index === root.revealHighlightIndex
                    highlightStrength: root.revealHighlightStrength
                }
            }

            // 左侧用户消息导航导轨。数据来自 AgentVM.userMessageIndex
            // （trace 变化时随之更新），当前条由 viewport 的像素位置算出。
            MessageNavigator {
                id: navigator
                anchors.left: parent.left
                anchors.leftMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                viewportHeight: transcript.height
                entries: AgentVM.userMessageIndex
                // rows 只随 trace 变化，activeIndex 随滚动变化；把两者分开
                // 算，避免每次滚动都重建一份 rows 数组。
                readonly property var rows: {
                    const list = AgentVM.userMessageIndex
                    const out = []
                    for (let i = 0; i < list.length; ++i) out.push(list[i].row)
                    return out
                }
                activeIndex: {
                    // 依赖 scrollOffset 才会随滚动重算（QML 只跟踪读到的属性）。
                    const ignored = transcript.scrollOffset
                    return transcript.activeIndexIn(rows)
                }
                onJumpRequested: function(row) {
                    transcript.scrollToIndex(row)
                    // 复用搜索定位的高亮链路，但底色更弱：导轨跳转是
                    // "确认落点"，不是"搜索结果"。
                    root.revealHighlightStrength = 0.06
                    root.revealHighlightIndex = row
                    highlightTimer.start()
                }
            }

            Column {
                id: emptyState
                anchors.centerIn: parent
                width: 260
                spacing: Theme.spaceMd
                visible: transcript.count === 0

                Rectangle {
                    x: (emptyState.width - width) / 2
                    width: 48
                    height: 48
                    radius: Theme.radiusMd
                    color: Theme.brandPrimary
                    opacity: 0.92

                    Text {
                        anchors.centerIn: parent
                        text: "N"
                        color: Theme.bgPrimary
                        font.pixelSize: 26
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                }

                Text {
                    id: emptyText
                    width: parent.width
                    text: AgentVM.modelConfigured
                        ? qsTr("发送一条消息，开始与 NexusAI 对话")
                        : qsTr("先连接一个模型，即可开始对话。你的输入会保留在这里。")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !AgentVM.modelConfigured
                    text: qsTr("配置模型")
                    highlighted: true
                    onClicked: root.settingsRequested()
                }

                // 模板入口的空态副本：侧栏折叠后原本没有任何可达路径，
                // 这里补一条（只在有模板时出现，避免空菜单多一个死按钮）。
                Item {
                    id: emptyTemplateEntry
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: Settings.sessionTemplates.length > 0
                    width: emptyTemplateRow.implicitWidth + 20
                    height: 28

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: emptyTemplateMA.containsMouse || emptyTemplateMenu.popped
                            ? Theme.surfaceHover : "transparent"
                        border.width: 1
                        border.color: emptyTemplateMenu.popped
                            ? Theme.brandPrimary : Theme.borderSubtle

                        Row {
                            id: emptyTemplateRow
                            anchors.centerIn: parent
                            spacing: 6

                            AppIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                icon: "preset"
                                size: 12
                                iconColor: emptyTemplateMA.containsMouse
                                    || emptyTemplateMenu.popped
                                    ? Theme.brandPrimary : Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("从模板开始")
                                color: emptyTemplateMA.containsMouse
                                    || emptyTemplateMenu.popped
                                    ? Theme.brandPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                            }
                            AppIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                icon: "chevron-down"
                                size: 10
                                iconColor: Theme.textSecondary
                            }
                        }

                        MouseArea {
                            id: emptyTemplateMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: emptyTemplateMenu.toggleBelow(emptyTemplateEntry)
                        }
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("从会话模板开始")
                    Accessible.onPressAction: emptyTemplateMenu.openBelow(emptyTemplateEntry)
                }
            }

            SessionTemplateMenu {
                id: emptyTemplateMenu
                onManageRequested: root.settingsCategoryRequested("prompts")
            }

            Rectangle {
                visible: transcript.intermediateCount > 0
                anchors.top: parent.top
                anchors.topMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                z: 5
                width: processRow.implicitWidth + 20
                height: 26
                radius: height / 2
                color: processMA.containsMouse ? Theme.surfaceHover : Theme.cardBackground
                border.width: 1
                border.color: Theme.cardBorder
                opacity: 0.94

                Row {
                    id: processRow
                    anchors.centerIn: parent
                    spacing: 6

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        icon: transcript.showProcess ? "chevron-up" : "chevron-down"
                        size: 11
                        iconColor: processMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: transcript.showProcess
                            ? qsTr("折叠过程")
                            : qsTr("思考过程 · %1").arg(transcript.intermediateCount)
                        color: processMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontSans
                    }
                }

                MouseArea {
                    id: processMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: transcript.toggleProcess()
                }

                ToolTip.visible: processMA.containsMouse
                ToolTip.delay: 600
                ToolTip.text: transcript.showProcess
                    ? qsTr("隐藏思考与工具调用过程") : qsTr("显示思考与工具调用过程")
            }

            Rectangle {
                visible: AgentVM.pendingConfirmation.count > 0 && !transcript.pinnedToBottom
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 52
                anchors.right: parent.right
                anchors.rightMargin: 16
                z: 5
                width: confirmRow.implicitWidth + 20
                height: 32
                radius: height / 2
                color: confirmBadgeMA.containsMouse ? Theme.surfaceHover : Theme.cardBackground
                border.width: 1
                border.color: Theme.statusWarning
                opacity: 0.94

                Row {
                    id: confirmRow
                    anchors.centerIn: parent
                    spacing: 6

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        icon: "warning"
                        size: 12
                        iconColor: Theme.statusWarning
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("有待确认 · %1").arg(AgentVM.pendingConfirmation.count)
                        color: Theme.statusWarning
                        font.pixelSize: 10
                        font.family: Theme.fontSans
                        font.bold: true
                    }
                }

                MouseArea {
                    id: confirmBadgeMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: transcript.jumpToBottom()
                }

                ToolTip.visible: confirmBadgeMA.containsMouse
                ToolTip.delay: 600
                ToolTip.text: qsTr("有待确认的工具操作，点击查看")
            }

            Rectangle {
                visible: !transcript.pinnedToBottom && transcript.maximumOffset > 0
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 16
                z: 5
                width: 32
                height: 32
                radius: width / 2
                color: Theme.cardBackground
                border.width: 1
                border.color: Theme.cardBorder
                opacity: 0.94

                AppIcon {
                    anchors.centerIn: parent
                    icon: "chevron-down"
                    size: 15
                    iconColor: jumpMA.containsMouse ? Theme.brandPrimary : Theme.textPrimary
                }

                MouseArea {
                    id: jumpMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: transcript.jumpToBottom()
                }

                ToolTip.visible: jumpMA.containsMouse
                ToolTip.delay: 600
                ToolTip.text: qsTr("回到底部")
            }
        }

        Connections {
            target: root.traceJump
            function onFollowRequested() {
                transcript.jumpToBottom()
            }
        }

        Connections {
            target: AgentVM
            function onCurrentSessionIdChanged() {
                transcript.jumpToBottom()
            }
        }

        // A1 (hit-locate): 监听 ChangeBus 上的搜索命中定位意图。
        // revealMessage() 先 loadSession 同步填充 traceModel，再 notify
        // search.revealMessage；此处 Qt.callLater 延迟一帧，等 Repeater
        // 完成新会话的布局后再读 rowOfMessageId 和 scrollToIndex。
        Connections {
            target: ChangeBus
            function onChanged(key, value) {
                if (key !== "search.revealMessage")
                    return
                const messageId = value.toLongLong()
                if (messageId <= 0) {
                    transcript.jumpToBottom()
                    return
                }
                Qt.callLater(function() {
                    const row = AgentVM.traceModel.rowOfMessageId(messageId)
                    if (row >= 0) {
                        transcript.scrollToIndex(row)
                        root.revealHighlightStrength = 0.10
                        root.revealHighlightIndex = row
                        highlightTimer.start()
                    } else {
                        // assistant 命中或 messageId 未映射到当前 trace 行 → 回退跳到底部
                        transcript.jumpToBottom()
                    }
                }, 0)
            }
        }

        // A1 (hit-locate): 2s 清除高亮的本地 Timer（root 级，避免与页面级 Timer 重复）。
        Timer {
            id: highlightTimer
            interval: 2000
            onTriggered: root.revealHighlightIndex = -1
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: inputConsole.height + modelOptions.implicitHeight + 24

            InputConsole {
                id: inputConsole
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 14
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                traceJump: root.traceJump
            }

            Flow {
                id: modelOptions
                anchors.top: inputConsole.bottom
                anchors.topMargin: 4
                anchors.left: inputConsole.left
                anchors.right: inputConsole.right
                anchors.leftMargin: 4
                spacing: 4
                ModelSelector {
                    onSettingsRequested: root.settingsRequested()
                }
                ThinkingEffortSelector {}
                PermissionModeSelector {}
                // 提示词预设与另三个会话级选择器同排：回显当前套用的预设，
                // 向上弹出带摘要的列表（顶栏只有 40px，装不下预设名）。
                PromptPresetSelector {
                    onManageRequested: root.settingsCategoryRequested("prompts")
                }
            }
        }
    }

    // docs/20 §3.4（A4 拖拽附件）：窗口根级 DropArea（main.qml）解析完成后经
    // attachmentsDropped 信号把附件描述符推给输入区预览。
    Connections {
        target: AgentVM
        function onAttachmentsDropped(attachments) {
            inputConsole.appendAttachments(attachments)
        }
    }
}
