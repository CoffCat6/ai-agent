import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// M1 (stats) + docs/16 §8: token / cost usage panel. Aggregates token_usage
// rows by model, day, source and direction; shows cost breakdown and quota
// status. The detailed aggregates come from SqliteMemory::tokenStatsDetailed
// / costStats / quotaStatus / tokenStatsBySource.
Item {
    id: page

    Accessible.name: qsTr("用量统计页")
    Accessible.description: qsTr("查看模型调用次数、Token 消耗、费用与配额统计")

    property int windowDays: 30
    property var stats: ({})
    property var costStatsData: ({})
    property var quotaStatusData: ({})
    property var bySourceData: []
    property real sourcesTotal: 0   // 各来源 token 之和（占比分母，避免估算值 > totalTokens 导致 > 100%）

    // docs/25 C5: 费用明细空态判定（表头/合计/预估行共用）
    readonly property bool costRowsEmpty: (costStatsData.byModel || []).length === 0
    // docs/25 C5: 配额区空态判定——无任何用量且未配置任何配额时，
    // 四行 0/不限 没有信息量，统一显示空态（对齐 byModel/byDay）。
    readonly property bool quotaEmpty: {
        var q = quotaStatusData || {}
        return Number(q.dailyTokensUsed || 0) === 0
            && Number(q.dailyCostCentsUsed || 0) === 0
            && Number(q.monthlyTokensUsed || 0) === 0
            && Number(q.monthlyCostCentsUsed || 0) === 0
            && Settings.dailyTokenQuota <= 0 && Settings.dailyCostQuotaCents <= 0
            && Settings.monthlyTokenQuota <= 0 && Settings.monthlyCostQuotaCents <= 0
    }

    function refresh() {
        stats = AgentVM.tokenStatsDetailed(windowDays)
        costStatsData = AgentVM.costStats(windowDays)
        quotaStatusData = AgentVM.quotaStatus()
        bySourceData = AgentVM.tokenStatsBySource(windowDays)
        // 计算各来源 token 之和作为占比分母。stats.totalTokens 是 API 真实值
        //（不含 tools schema），而来源拆分的 inputToolCall 含 tools schema，
        // 两者口径不一致会导致占比 > 100%（如工具调用 117%）。
        // 用各来源之和作分母确保占比之和 = 100%。
        sourcesTotal = 0
        for (var i = 0; i < bySourceData.length; ++i) {
            var s = bySourceData[i]
            sourcesTotal += (s.inputTokens || 0) + (s.outputTokens || 0)
        }
    }

    function fmtNumber(n) {
        return Number(n || 0).toLocaleString(Qt.locale(), 'f', 0)
    }

    function fmtLatency(ms) {
        if (!ms || ms <= 0) return qsTr("—")
        if (ms < 1000) return qsTr("%1 ms").arg(ms)
        return qsTr("%1 s").arg((ms / 1000).toFixed(1))
    }

    // docs/16 §8: 费用格式化（分 → 元，保留两位）
    function fmtCost(costCents) {
        return qsTr("¥%1").arg((Number(costCents || 0) / 100.0).toFixed(2))
    }

    function fmtPercent(ratio) {
        return qsTr("%1%").arg((Number(ratio || 0) * 100).toFixed(1))
    }

    // 来源标识 → 中文标签
    function sourceLabel(source) {
        const map = {
            "system_prompt": qsTr("系统提示词"),
            "conversation":  qsTr("对话消息"),
            "tool_call":     qsTr("工具调用"),
            "mcp":           qsTr("MCP"),
            "skill":         qsTr("技能"),
            "image":         qsTr("图片"),
            "other":         qsTr("其他")
        }
        return map[source] || source || qsTr("未知")
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
                    text: qsTr("统计已完成任务的模型调用次数、Token 消耗、费用与配额状态。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
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

        // ── 时间窗 ──────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
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

        // ── 配额设置（docs/17 B4：P1-3——配额键此前无任何编辑入口）──
        // 2×2 网格 + 完整标签：整页滚动后垂直空间不再稀缺，宽度紧张时
        // 仍能完整显示（单行四列在内容区 ~560px 下必然截断）。
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: quotaSettingsCol.implicitHeight + 20
            radius: 8
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: quotaSettingsCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("配额设置")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                    Text {
                        text: qsTr("0 = 不限制 · 本地模型不计费")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.family: Theme.fontSans
                        opacity: 0.8
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        HoverHandler { id: quotaTipHover }
                        ToolTip.visible: quotaTipHover.hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("费用按已计费请求累计。勾选“超限拒绝”后达到上限直接拒绝请求，默认仅警告。")
                    }
                    Switch {
                        checked: Settings.strictQuotaMode
                        onToggled: Settings.strictQuotaMode = checked
                        text: qsTr("超限拒绝请求")
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        Accessible.name: qsTr("超限时直接拒绝请求")
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 6

                    Text { text: qsTr("每日 Token 上限"); color: Theme.textSecondary; font.pixelSize: 10; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: dailyTokenSpin
                        Layout.fillWidth: true
                        from: 0; to: 100000000; stepSize: 1000; editable: true
                        Component.onCompleted: value = Settings.dailyTokenQuota
                        onValueModified: Settings.dailyTokenQuota = value
                        Accessible.name: qsTr("每日 Token 上限")
                    }
                    Text { text: qsTr("每月 Token 上限"); color: Theme.textSecondary; font.pixelSize: 10; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: monthlyTokenSpin
                        Layout.fillWidth: true
                        from: 0; to: 1000000000; stepSize: 10000; editable: true
                        Component.onCompleted: value = Settings.monthlyTokenQuota
                        onValueModified: Settings.monthlyTokenQuota = value
                        Accessible.name: qsTr("每月 Token 上限")
                    }
                    Text { text: qsTr("每日费用上限（元）"); color: Theme.textSecondary; font.pixelSize: 10; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: dailyCostSpin
                        Layout.fillWidth: true
                        from: 0; to: 1000000; stepSize: 1; editable: true
                        Component.onCompleted: value = Math.round(Settings.dailyCostQuotaCents / 100)
                        onValueModified: Settings.dailyCostQuotaCents = value * 100
                        Accessible.name: qsTr("每日费用上限 元")
                    }
                    Text { text: qsTr("每月费用上限（元）"); color: Theme.textSecondary; font.pixelSize: 10; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: monthlyCostSpin
                        Layout.fillWidth: true
                        from: 0; to: 10000000; stepSize: 1; editable: true
                        Component.onCompleted: value = Math.round(Settings.monthlyCostQuotaCents / 100)
                        onValueModified: Settings.monthlyCostQuotaCents = value * 100
                        Accessible.name: qsTr("每月费用上限 元")
                    }
                }
            }
        }

        // ── 概览卡片（§8.1.1：任务次数 / 累计 Token / 累计费用 / 平均延迟）──
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: [
                    { label: qsTr("任务次数"), value: page.fmtNumber(page.stats.totalCalls) },
                    { label: qsTr("累计 Token"), value: page.fmtNumber(page.stats.totalTokens) },
                    { label: qsTr("累计费用"), value: page.fmtCost(page.stats.totalCostCents) },
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

                // ── 方向维度卡片（§8.1.2：Token 构成）──────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("Token 构成")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: dirCol.implicitHeight + 24
                        radius: 8
                        color: Theme.bgPrimary
                        border.width: 1
                        border.color: Theme.borderSubtle

                        ColumnLayout {
                            id: dirCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            // 输入 / 输出 / 缓存命中 三行进度条
                            Repeater {
                                model: [
                                    { label: qsTr("输入(未命中)"), tokens: Math.max(0, page.stats.totalInputTokens - page.stats.totalCacheHitTokens), color: Theme.brandPrimary },
                                    { label: qsTr("缓存命中"), tokens: page.stats.totalCacheHitTokens, color: Theme.statusWarning },
                                    { label: qsTr("输出"), tokens: page.stats.totalOutputTokens, color: Theme.statusSuccess }
                                ]

                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: modelData.label
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        Layout.preferredWidth: 84
                                    }
                                    Text {
                                        text: page.fmtNumber(modelData.tokens)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 80
                                    }
                                    Text {
                                        text: page.fmtPercent(page.stats.totalTokens > 0
                                                              ? modelData.tokens / page.stats.totalTokens : 0)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 50
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 6
                                        radius: 3
                                        color: Theme.bgSecondary
                                        Rectangle {
                                            width: Math.max(0, Math.min(parent.width,
                                                parent.width * (page.stats.totalTokens > 0
                                                    ? modelData.tokens / page.stats.totalTokens : 0)))
                                            height: parent.height
                                            radius: parent.radius
                                            color: modelData.color
                                        }
                                    }
                                }
                            }

                            Text {
                                text: qsTr("缓存命中率: %1").arg(page.fmtPercent(page.stats.cacheHitRate))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                            }
                        }
                    }
                }

                // ── 来源维度卡片（§8.1.3：按来源拆分）──────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("按来源拆分")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: srcCol.implicitHeight + 24
                        radius: 8
                        color: Theme.bgPrimary
                        border.width: 1
                        border.color: Theme.borderSubtle

                        ColumnLayout {
                            id: srcCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4

                            // 表头（docs/25 C5: 空态时隐藏，与 byModel/byDay 空态形态一致）
                            RowLayout {
                                visible: page.bySourceData.length > 0
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: qsTr("来源")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontSans
                                    Layout.preferredWidth: 100
                                }
                                Text {
                                    text: qsTr("输入")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 80
                                }
                                Text {
                                    text: qsTr("输出")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 80
                                }
                                Text {
                                    text: qsTr("占比")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontMono
                                    Layout.fillWidth: true
                                }
                            }

                            // docs/25 C5: 按来源空态补全（对齐 byModel/byDay）
                            Text {
                                visible: page.bySourceData.length === 0
                                text: qsTr("该范围内暂无数据")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                opacity: 0.7
                            }

                            Repeater {
                                model: page.bySourceData

                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: page.sourceLabel(modelData.source)
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        Layout.preferredWidth: 100
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: page.fmtNumber(modelData.inputTokens)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 80
                                    }
                                    Text {
                                        text: page.fmtNumber(modelData.outputTokens)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 80
                                    }
                                    Text {
                                        text: page.fmtPercent(page.sourcesTotal > 0
                                            ? (modelData.inputTokens + modelData.outputTokens) / page.sourcesTotal : 0)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }

                // ── 费用明细（§8.1.4）──────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("费用明细")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: costCol.implicitHeight + 24
                        radius: 8
                        color: Theme.bgPrimary
                        border.width: 1
                        border.color: Theme.borderSubtle

                        ColumnLayout {
                            id: costCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4

                            // 表头（docs/25 C5: 空态时隐藏）
                            RowLayout {
                                visible: !page.costRowsEmpty
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: qsTr("模型")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontSans
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: qsTr("调用")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 60
                                }
                                Text {
                                    text: qsTr("Token")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 80
                                }
                                Text {
                                    text: qsTr("费用")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 70
                                }
                            }

                            // docs/25 C5: 费用明细空态补全（对齐 byModel/byDay）
                            Text {
                                visible: page.costRowsEmpty
                                text: qsTr("该范围内暂无数据")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                opacity: 0.7
                            }

                            Repeater {
                                model: page.costStatsData.byModel || []

                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: modelData.model && modelData.model.length > 0
                                              ? modelData.model : qsTr("(未知)")
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: page.fmtNumber(modelData.calls)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 60
                                    }
                                    Text {
                                        text: page.fmtNumber(modelData.tokens)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 80
                                    }
                                    Text {
                                        text: page.fmtCost(modelData.costCents)
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 70
                                    }
                                }
                            }

                            // 合计行（docs/25 C5: 费用明细空态时一并隐藏）
                            Rectangle {
                                visible: !page.costRowsEmpty
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.borderSubtle
                            }
                            RowLayout {
                                visible: !page.costRowsEmpty
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: qsTr("合计")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontSans
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: page.fmtNumber(page.stats.totalCalls)
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 60
                                }
                                Text {
                                    text: page.fmtNumber(page.stats.totalTokens)
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 80
                                }
                                Text {
                                    text: page.fmtCost(page.costStatsData.totalCostCents)
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontMono
                                    Layout.preferredWidth: 70
                                }
                            }

                            Text {
                                visible: !page.costRowsEmpty
                                text: qsTr("预估月费用: %1").arg(page.fmtCost(page.costStatsData.projectedMonthlyCents))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                Layout.topMargin: 4
                            }
                        }
                    }
                }

                // ── 配额状态（§8.1.5）──────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("配额状态")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: quotaCol.implicitHeight + 24
                        radius: 8
                        color: Theme.bgPrimary
                        border.width: 1
                        border.color: Theme.borderSubtle

                        ColumnLayout {
                            id: quotaCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            // docs/25 C5: 配额区空态补全（对齐 byModel/byDay）——
                            // 无任何用量且未配置任何配额时不渲染 0/不限 四行
                            Text {
                                visible: page.quotaEmpty
                                text: qsTr("该范围内暂无数据")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                opacity: 0.7
                            }

                            // 日 Token / 日费用 / 月 Token / 月费用
                            Repeater {
                                visible: !page.quotaEmpty
                                model: [
                                    { label: qsTr("日 Token"), used: page.quotaStatusData.dailyTokensUsed, quota: Settings.dailyTokenQuota, isCost: false },
                                    { label: qsTr("日费用"), used: page.quotaStatusData.dailyCostCentsUsed, quota: Settings.dailyCostQuotaCents, isCost: true },
                                    { label: qsTr("月 Token"), used: page.quotaStatusData.monthlyTokensUsed, quota: Settings.monthlyTokenQuota, isCost: false },
                                    { label: qsTr("月费用"), used: page.quotaStatusData.monthlyCostCentsUsed, quota: Settings.monthlyCostQuotaCents, isCost: true }
                                ]

                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: modelData.label
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        Layout.preferredWidth: 60
                                    }
                                    Text {
                                        text: modelData.isCost
                                            ? qsTr("%1 / %2").arg(page.fmtCost(modelData.used)).arg(
                                                modelData.quota > 0 ? page.fmtCost(modelData.quota) : qsTr("不限"))
                                            : qsTr("%1 / %2").arg(page.fmtNumber(modelData.used)).arg(
                                                modelData.quota > 0 ? page.fmtNumber(modelData.quota) : qsTr("不限"))
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 160
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 6
                                        radius: 3
                                        color: Theme.bgSecondary
                                        Rectangle {
                                            width: Math.max(0, Math.min(parent.width,
                                                modelData.quota > 0
                                                    ? parent.width * Math.min(1.0, modelData.used / modelData.quota)
                                                    : 0))
                                            height: parent.height
                                            radius: parent.radius
                                            color: modelData.quota > 0 && modelData.used >= modelData.quota
                                                ? Theme.statusDanger : Theme.brandPrimary
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── 按模型（保留现有，字段适配新 API）──────────
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
                                    text: qsTr("%1 tokens").arg(page.fmtNumber(
                                        (modelData.inputTokens || 0) + (modelData.outputTokens || 0)))
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

                // ── 按天（保留现有，字段适配新 API）────────────
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
                                    text: qsTr("%1 tokens").arg(page.fmtNumber(
                                        (modelData.inputTokens || 0) + (modelData.outputTokens || 0)))
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
