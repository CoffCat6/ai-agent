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

    function forgetOne(id) {
        if (AgentVM.removeEpisodic(id))
            applySearch()
    }

    Component.onCompleted: refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 标题 ────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            Layout.leftMargin: 24
            Layout.rightMargin: 24

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                Text {
                    text: qsTr("记忆管理")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("长期记忆摘要由会话自动生成，可在新会话中检索注入；删除后不可恢复。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
        }

        // ── 搜索 + 清空 ─────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 12
            spacing: 8

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("搜索记忆内容…")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: "#ffffff"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                background: Rectangle {
                    radius: 6
                    color: Theme.bgPrimary
                    border.width: searchField.activeFocus ? 2 : 1
                    border.color: searchField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                }
                Accessible.name: qsTr("搜索记忆内容")
                onTextChanged: page.applySearch()
            }

            AppButton {
                text: qsTr("清空全部")
                enabled: page.episodeCount > 0
                opacity: enabled ? 1.0 : 0.4
                onClicked: confirmClearDialog.open()
                Accessible.name: qsTr("清空全部长期记忆")
            }
        }

        // ── 计数 ────────────────────────────────────────
        Text {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 8
            text: qsTr("共 %1 条记忆").arg(page.episodeCount)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontMono
        }

        // ── 列表 ────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 12
            radius: 8
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.borderSubtle
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: page.episodes
                spacing: 2
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    required property var modelData
                    width: list.width - 8
                    implicitHeight: itemCol.implicitHeight + 20
                    radius: 6
                    color: itemHover.hovered ? Theme.bgSecondary : "transparent"

                    HoverHandler { id: itemHover }

                    ColumnLayout {
                        id: itemCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6

                        Text {
                            text: modelData.summary && modelData.summary.length > 0
                                  ? modelData.summary : qsTr("(空摘要)")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontSans
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                text: Qt.formatDateTime(new Date(modelData.timestamp),
                                                        "yyyy-MM-dd hh:mm")
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
                                opacity: 0.8
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
                      : qsTr("暂无长期记忆。开启「工具与记忆」中的长期记忆后，完成对话会自动生成摘要。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                opacity: 0.7
                width: parent.width - 48
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
            spacing: 12
            Text {
                text: qsTr("确认清空全部长期记忆？此操作不可恢复。")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                Layout.leftMargin: 20
                Layout.topMargin: 20
                Layout.rightMargin: 20
                wrapMode: Text.Wrap
                Layout.fillWidth: true
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
