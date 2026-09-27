import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// M1 (stats): token / cost usage panel. Aggregates audit_log task_complete
// rows (the only place real token counts land) by model and local day.
Item {
    id: page

    Accessible.name: qsTr("用量统计页")
    Accessible.description: qsTr("查看模型调用次数、Token 消耗与延迟统计")

    property int windowDays: 30
    property var stats: ({})

    function refresh() {
        stats = AgentVM.tokenStats(windowDays)
    }

    function fmtNumber(n) {
        return Number(n || 0).toLocaleString(Qt.locale(), 'f', 0)
    }

    function fmtLatency(ms) {
        if (!ms || ms <= 0) return qsTr("—")
        if (ms < 1000) return qsTr("%1 ms").arg(ms)
        return qsTr("%1 s").arg((ms / 1000).toFixed(1))
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
                    text: qsTr("用量统计")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("统计已完成任务的模型调用次数、Token 消耗与平均延迟。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
        }

        // ── 时间窗 ──────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 12
            spacing: 8

            Text {
                text: qsTr("统计范围")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
            }
            ComboBox {
                id: windowCombo
                Layout.preferredWidth: 140
                model: [qsTr("近 7 天"), qsTr("近 30 天"), qsTr("近 90 天")]
                currentIndex: 1
                onActivated: {
                    page.windowDays = [7, 30, 90][currentIndex]
                    page.refresh()
                }
                Accessible.name: qsTr("统计范围")
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("刷新")
                onClicked: page.refresh()
                Accessible.name: qsTr("刷新用量统计")
            }
        }

        // ── 概览卡片 ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 16
            spacing: 12

            Repeater {
                model: [
                    { label: qsTr("任务次数"), value: page.fmtNumber(page.stats.totalCalls) },
                    { label: qsTr("累计 Token"), value: page.fmtNumber(page.stats.totalTokens) },
                    { label: qsTr("平均延迟"), value: page.fmtLatency(page.stats.avgLatencyMs) }
                ]

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    radius: 8
                    color: Theme.bgPrimary
                    border.width: 1
                    border.color: Theme.borderSubtle

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        Text {
                            text: modelData.value
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeH2
                            font.bold: true
                            font.family: Theme.fontSans
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: modelData.label
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }
        }

        // ── 明细 ────────────────────────────────────────
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 12
            clip: true
            contentWidth: width
            contentHeight: detailCol.implicitHeight

            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: detailCol
                width: flick.width
                spacing: 16

                // 按模型
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("按模型")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    Text {
                        visible: (page.stats.byModel || []).length === 0
                        text: qsTr("该范围内暂无数据")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        opacity: 0.7
                    }

                    Repeater {
                        model: page.stats.byModel || []

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 40
                            radius: 6
                            color: Theme.bgPrimary
                            border.width: 1
                            border.color: Theme.borderSubtle

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Text {
                                    text: modelData.model && modelData.model.length > 0
                                          ? modelData.model : qsTr("(未知)")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: qsTr("%1 次").arg(page.fmtNumber(modelData.calls))
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                }
                                Text {
                                    text: qsTr("%1 tokens").arg(page.fmtNumber(modelData.tokens))
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                }
                                Text {
                                    text: page.fmtLatency(modelData.avgLatencyMs)
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                }
                            }
                        }
                    }
                }

                // 按天
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("按天")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    Text {
                        visible: (page.stats.byDay || []).length === 0
                        text: qsTr("该范围内暂无数据")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        opacity: 0.7
                    }

                    Repeater {
                        model: page.stats.byDay || []

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 32
                            radius: 6
                            color: Theme.bgPrimary
                            border.width: 1
                            border.color: Theme.borderSubtle

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Text {
                                    text: modelData.day
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: qsTr("%1 次").arg(page.fmtNumber(modelData.calls))
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                }
                                Text {
                                    text: qsTr("%1 tokens").arg(page.fmtNumber(modelData.tokens))
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 8 }
            }
        }
    }
}
