import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// M1: long-term memory management page. Browses / searches / forgets the
// episodic summaries the agent stores (T8). Operations apply immediately -
// there is no edit buffer here (same convention as ToolsMemoryPage).
Item {
    id: page

    Accessible.name: qsTr("记忆管理页")
    Accessible.description: qsTr("浏览、搜索和删除长期记忆摘要")

    // [{id, sessionId, summary, tags, timestamp}]
    property var episodes: []
    readonly property int episodeCount: episodes.length

    function refresh() {
        episodes = AgentVM.listEpisodic(500)
    }

    function applySearch() {
        const kw = searchField.text.trim()
        episodes = kw.length > 0 ? AgentVM.searchEpisodic(kw, 500)
                                 : AgentVM.listEpisodic(500)
    }

    // 设置中心底栏「恢复默认」按当前分类分派；本页只有记忆相关的
    // SettingsService 键（没有声明式偏好），必须自己提供入口。
    function requestResetDefaults() {
        Settings.resetMemoryToDefaults()
    }

    function forgetOne(id) {
        if (AgentVM.removeEpisodic(id))
            applySearch()
    }

    Component.onCompleted: refresh()

    Connections {
        target: AgentVM
        function onEpisodicChanged() { page.applySearch() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 标题 ────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: header.implicitHeight + Theme.spaceLg
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl

            SettingsPageHeader {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Theme.spaceXs
                title: qsTr("记忆管理")
                description: qsTr("长期记忆摘要由会话自动生成，可在新会话中检索注入；删除后不可恢复。")
            }
        }

        // ── 长期记忆与保留（docs/23 G3）──────────────────
        SettingsSection {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceSm
            title: qsTr("记忆与保留")

            SettingRow {
                objectName: "memory/episodicMemoryEnabled"
                label: qsTr("启用长期记忆")
                description: qsTr("开启后，每次任务结束会生成会话摘要并在新会话中注入相关历史，帮助模型记住此前聊过的内容。")

                Switch {
                    checked: Settings.episodicMemoryEnabled
                    onToggled: Settings.episodicMemoryEnabled = checked
                    Accessible.name: qsTr("启用长期记忆")
                }
            }

            SettingRow {
                objectName: "memory/auditRetentionDays"
                label: qsTr("审计日志保留天数")
                description: qsTr("工具执行审计链的保留天数，0 表示永久保留。到期记录在启动与每 6 小时的例行清理中删除。")

                SpinBox {
                    from: 0
                    to: 3650
                    value: Settings.auditRetentionDays
                    onValueModified: Settings.auditRetentionDays = value
                    Accessible.name: qsTr("审计日志保留天数")
                }
            }

            SettingRow {
                objectName: "memory/tokenRetentionDays"
                label: qsTr("Token 用量保留天数")
                description: qsTr("每次模型请求的用量明细（token 与费用统计的数据源）的保留天数，0 表示永久保留。")

                SpinBox {
                    from: 0
                    to: 3650
                    value: Settings.tokenRetentionDays
                    onValueModified: Settings.tokenRetentionDays = value
                    Accessible.name: qsTr("Token 用量保留天数")
                }
            }
        }

        // ── 搜索 + 清空 ─────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceMd
            spacing: Theme.spaceSm

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("搜索记忆内容…")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                background: Rectangle {
                    radius: Theme.radiusField
                    color: Theme.surfaceHover
                    border.width: searchField.activeFocus ? 2 : 1
                    border.color: searchField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                }
                Accessible.name: qsTr("搜索记忆内容")
                onTextChanged: page.applySearch()
            }

            AppButton {
                text: qsTr("清空全部")
                enabled: page.episodeCount > 0
                onClicked: confirmClearDialog.open()
                Accessible.name: qsTr("清空全部长期记忆")
            }
        }

        // ── 计数 ────────────────────────────────────────
        Text {
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceSm
            text: qsTr("共 %1 条记忆").arg(page.episodeCount)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
        }

        // ── 列表 ────────────────────────────────────────
        Rectangle {
            objectName: "memory/episodes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceMd
            radius: Theme.radiusMd
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.borderSubtle
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Theme.spaceXs
                clip: true
                model: page.episodes
                spacing: 2
                ScrollBar.vertical: AppScrollBar {}

                delegate: Rectangle {
                    id: episodeDelegate
                    required property var modelData
                    // docs/25 C5: 摘要 3 行截断的展开/收起状态（delegate 局部，
                    // 切换条目后随 delegate 重建复位）
                    property bool summaryExpanded: false
                    width: list.width - 8
                    implicitHeight: itemCol.implicitHeight + Theme.spaceLg
                    radius: Theme.radiusField
                    color: itemHover.hovered ? Theme.bgSecondary : "transparent"

                    HoverHandler { id: itemHover }

                    ColumnLayout {
                        id: itemCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.spaceMd
                        anchors.rightMargin: Theme.spaceMd
                        spacing: Theme.spaceXs

                        Text {
                            id: summaryText
                            text: modelData.summary && modelData.summary.length > 0
                                  ? modelData.summary : qsTr("(空摘要)")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            wrapMode: Text.Wrap
                            // docs/25 C5: 默认 3 行截断，可展开查看全文
                            maximumLineCount: episodeDelegate.summaryExpanded ? 99999 : 3
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        // docs/25 C5: 摘要展开/收起入口——仅在发生截断或
                        // 已展开时可见，避免短摘要挂一个无意义操作。
                        Text {
                            visible: summaryText.truncated || episodeDelegate.summaryExpanded
                            text: episodeDelegate.summaryExpanded ? qsTr("收起") : qsTr("展开全文")
                            color: Theme.brandPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: episodeDelegate.summaryExpanded =
                                           !episodeDelegate.summaryExpanded
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

                            Text {
                                text: Theme.formatDateTime(new Date(modelData.timestamp))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                            }
                            Text {
                                visible: modelData.tags && modelData.tags.length > 0
                                text: modelData.tags
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("遗忘")
                                onClicked: page.forgetOne(modelData.id)
                                Accessible.name: qsTr("遗忘这条记忆")
                            }
                        }
                    }
                }
            }

            // ── 空态 ────────────────────────────────────
            Text {
                anchors.centerIn: parent
                visible: page.episodeCount === 0
                text: searchField.text.trim().length > 0
                      ? qsTr("没有匹配的记忆")
                      : qsTr("暂无长期记忆。开启上方的长期记忆开关后，完成对话会自动生成摘要。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                width: parent.width - Theme.spaceXl * 2
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: confirmClearDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 360
        height: 160

        background: DialogBackground {}
        contentItem: ColumnLayout {
            spacing: Theme.spaceMd
            Text {
                text: qsTr("确认清空全部长期记忆？此操作不可恢复。")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                Layout.leftMargin: Theme.spaceXl
                Layout.topMargin: Theme.spaceXl
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
                    onClicked: confirmClearDialog.close()
                }
                AppButton {
                    text: qsTr("清空")
                    highlighted: true
                    onClicked: {
                        AgentVM.clearEpisodic()
                        searchField.text = ""
                        page.refresh()
                        confirmClearDialog.close()
                    }
                }
            }
        }
    }
}
