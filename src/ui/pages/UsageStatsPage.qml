import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// M1 (stats) + docs/16 §8: token / cost usage panel. Aggregates token_usage
// rows by model, day, source and direction; shows cost breakdown and quota
// status. The detailed aggregates come from SqliteMemory::tokenStatsDetailed
// / costStats / quotaStatus / tokenStatsBySource.
//
// 页面信息层级（自上而下，逐层下钻，避免同一份数据用图和列表各说一遍）：
//   统计范围 → 概览（4 个 KPI）→ 用量图表（3 张，一行一张）→ Token 构成
//   → 按来源拆分 → 费用明细 → 请求明细（表格）→ 配额状态 / 配额设置
// 原先的「按模型」「按天」两个列表已删除：前者与环形图 / 模型对比图 /
// 费用明细完全同源，后者与按天走势折线图同源，列表只重复图的结论。
Item {
    id: page

    Accessible.name: qsTr("用量统计页")
    Accessible.description: qsTr("查看模型调用次数、Token 消耗、费用与配额统计")

    property var stats: ({})
    property var costStatsData: ({})
    property var quotaStatusData: ({})
    property var bySourceData: []
    property real sourcesTotal: 0   // 各来源 token 之和（占比分母，避免估算值 > totalTokens 导致 > 100%）

    // ── Batch 6 T3: 时间段选择 ─────────────────────
    // 预设 + 自定义起止日期（本地自然日）。所有预设统一换算为
    // [fromTs, toTs) 范围模式调用：聚合接口与明细 tokenUsageRecords
    // 共用同一口径（明细 (0,0) = 全部记录，而聚合 (0,0) 会回退
    // recentDays 30 天——「全部」用 fromTs=1 规避该口径差）。
    property string rangeMode: "d30"   // today | d7 | d30 | all | custom
    property real customFromTs: 0      // 最近一次合法解析的自定义起点（epoch ms）
    property real customToTs: 0        // 自定义终点（截止日次日 0 点，含当天）
    property string customError: ""    // 非空 = 输入非法：提示且不刷新（保持上一有效范围）
    property string barMode: "tokens"  // BarCompareChart 口径：tokens | calls

    // ── Batch 6 T3: 请求明细分页状态 ────────────────
    readonly property int detailPageSize: 50
    property var detailRows: []
    property int detailTotal: 0
    property int detailOffset: 0
    // 请求明细导出的结果提示（成功/失败共用一行，切范围时清空）。
    property string exportMessage: ""
    property bool exportIsError: false

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

    // 预设 → 范围换算（Batch 6 T3）：
    //   today [今天0点, 明天0点)；d7/d30 [now-N天, ∞)（滚动窗口，与既有
    //   recentDays=N 的 addDays(-N) 语义等价）；all [1, ∞)（1970 至今，
    //   对齐明细 (1,0) = 全部记录）；custom [customFromTs, customToTs)。
    readonly property var rangeParams: {
        switch (page.rangeMode) {
        case "today":  return { from: new Date().setHours(0, 0, 0, 0),
                                to: new Date().setHours(24, 0, 0, 0) }
        case "d7":     return { from: Date.now() - 7 * 86400000, to: 0 }
        case "d30":    return { from: Date.now() - 30 * 86400000, to: 0 }
        case "all":    return { from: 1, to: 0 }
        case "custom": return { from: page.customFromTs, to: page.customToTs }
        }
        return { from: 0, to: 0 }
    }

    // ── Batch 6 T3: 图表数据映射 ───────────────────
    // byModel → DonutChart（token 占比）/ BarCompareChart（barMode 切换
    // token / 调用次数口径，组件侧只认 {label, value}）
    readonly property var modelSeries: {
        var out = []
        var list = page.stats.byModel || []
        for (var i = 0; i < list.length; ++i) {
            var m = list[i]
            out.push({
                label: m.model && m.model.length > 0 ? m.model : qsTr("(未知)"),
                tokens: Number(m.inputTokens || 0) + Number(m.outputTokens || 0),
                calls: Number(m.calls || 0)
            })
        }
        return out
    }
    readonly property var donutSeries: {
        var out = []
        for (var i = 0; i < page.modelSeries.length; ++i)
            out.push({ label: page.modelSeries[i].label,
                       value: page.modelSeries[i].tokens })
        return out
    }
    readonly property var barSeries: {
        var out = []
        for (var i = 0; i < page.modelSeries.length; ++i)
            out.push({ label: page.modelSeries[i].label,
                       value: page.barMode === "calls"
                              ? page.modelSeries[i].calls
                              : page.modelSeries[i].tokens })
        return out
    }
    // byDay 按天 DESC（最新在前）返回，折线图需时间升序 → reverse，
    // label 取 "yyyy-MM-dd" 的 MM-dd 段
    readonly property var dayTrendSeries: {
        var list = (page.stats.byDay || []).slice().reverse()
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var d = list[i]
            out.push({ label: String(d.day || "").substring(5),
                       value: Number(d.inputTokens || 0) + Number(d.outputTokens || 0) })
        }
        return out
    }

    // 模型对比图卡片高度随模型数量自适应（横向柱状图每模型一行）：
    // 模型少时不留大片空白，模型多时封顶 380 由图表内部压缩行高。
    readonly property int barCardHeight: Math.max(180, Math.min(380,
                                          page.modelSeries.length * 30 + 72))

    function refresh() {
        var p = page.rangeParams
        page.exportMessage = ""
        // 范围模式下 from 恒 > 0，recentDays 被忽略（30 仅为防御性回退值）
        stats = AgentVM.tokenStatsDetailed(30, p.from, p.to)
        costStatsData = AgentVM.costStats(30, p.from, p.to)
        quotaStatusData = AgentVM.quotaStatus()
        bySourceData = AgentVM.tokenStatsBySource(30, p.from, p.to)
        // 计算各来源 token 之和作为占比分母。stats.totalTokens 是 API 真实值
        //（不含 tools schema），而来源拆分的 inputToolCall 含 tools schema，
        // 两者口径不一致会导致占比 > 100%（如工具调用 117%）。
        // 用各来源之和作分母确保占比之和 = 100%。
        sourcesTotal = 0
        for (var i = 0; i < bySourceData.length; ++i) {
            var s = bySourceData[i]
            sourcesTotal += (s.inputTokens || 0) + (s.outputTokens || 0)
        }
        loadDetailPage(true)   // 时间段变化 → 明细重置回首页
    }

    // ── Batch 6 T3: 明细分页 ──────────────────────
    // reset=true 拉首页（时间段切换后），false 追加下一页。
    function loadDetailPage(reset) {
        var p = page.rangeParams
        var resp = AgentVM.tokenUsageRecords(p.from, p.to, page.detailPageSize,
                                             reset ? 0 : page.detailOffset)
        var rows = (resp && resp.rows) ? resp.rows : []
        page.detailTotal = Number(resp && resp.total ? resp.total : 0)
        page.detailRows = reset ? rows : page.detailRows.concat(rows)
        page.detailOffset = reset ? rows.length : page.detailOffset + rows.length
    }

    // 导出的是表格当前视图（筛选 + 排序后已加载的行），所见即所得；
    // 文件写入与 CSV 转义都在 AgentVM 里（便于单测/复用）。
    function exportDetailCsv() {
        if (page.detailRows.length === 0) {
            page.exportIsError = true
            page.exportMessage = qsTr("没有可导出的明细。")
            return
        }
        usageExportDialog.open()
    }

    // ── Batch 6 T3: 自定义日期解析（yyyy-MM-dd，本地自然日）──
    // 非法格式 / 越界日期（如 02-31）返回 null：new Date 会把 02-31 规整
    // 成 03-03，回验三段才能拦住自动进位。
    function parseYmd(s) {
        var str = String(s || "")
        if (!/^\d{4}-\d{2}-\d{2}$/.test(str)) return null
        var p = str.split("-")
        var y = parseInt(p[0], 10), mo = parseInt(p[1], 10), d = parseInt(p[2], 10)
        var dt = new Date(y, mo - 1, d, 0, 0, 0, 0)
        if (dt.getFullYear() !== y || dt.getMonth() !== mo - 1 || dt.getDate() !== d)
            return null
        return dt
    }
    function ymdText(d) {
        var mo = d.getMonth() + 1, day = d.getDate()
        return d.getFullYear() + "-" + (mo < 10 ? "0" : "") + mo
               + "-" + (day < 10 ? "0" : "") + day
    }
    // 应用输入框中的自定义范围；非法输入仅提示、不刷新（沿用上一有效数据）
    function applyCustomRange() {
        var f = parseYmd(fromField.text)
        var t = parseYmd(toField.text)
        if (!f || !t) {
            page.customError = qsTr("日期格式应为 YYYY-MM-DD")
            return
        }
        if (f.getTime() > t.getTime()) {
            page.customError = qsTr("起始日期不能晚于截止日期")
            return
        }
        page.customError = ""
        page.customFromTs = f.getTime()
        // 截止日期含当天：上界取次日 0 点（与「今日」预设同构的左闭右开）
        page.customToTs = new Date(t.getFullYear(), t.getMonth(), t.getDate() + 1).getTime()
        refresh()
    }
    // 快捷按钮：写入输入框并立即应用
    function setCustomRange(from, to) {
        fromField.text = ymdText(from)
        toField.text = ymdText(to)
        applyCustomRange()
    }
    function selectRange(mode) {
        if (page.rangeMode === mode) {
            refresh()   // 重复点击同一预设 = 强制刷新当前范围
            return
        }
        page.rangeMode = mode
        if (mode === "custom") {
            if (page.customToTs <= 0) {
                // 首次进入自定义：默认「今天」单日
                var now = new Date()
                setCustomRange(now, now)
            } else {
                refresh()   // 沿用上次合法范围
            }
        } else {
            refresh()
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
            Layout.preferredHeight: header.implicitHeight + Theme.spaceLg
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl

            SettingsPageHeader {
                id: header
                objectName: "usage/overview"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Theme.spaceXs
                title: qsTr("用量统计")
                description: qsTr("统计已完成任务的模型调用次数、Token 消耗、费用与配额状态。")
            }
        }

        // ── 明细 ────────────────────────────────────────
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceMd
            clip: true
            contentWidth: width
            contentHeight: detailCol.implicitHeight

            ScrollBar.vertical: AppScrollBar {}

            ColumnLayout {
                id: detailCol
                width: flick.width
                spacing: Theme.spaceLg

        // ── 时间段选择器（Batch 6 T3：预设按钮 + 自定义输入框）──
        // 批次六审查 m4 窄窗口适配：单行（标签 + 5 预设 + 刷新）在内容区
        // ~560px（同页配额区 2×2 适配口径）下会溢出，刷新按钮被挤出可视
        // 区。拆两行：标签与刷新一行（刷新保持右端），预设按钮 Flow 一行
        // ——Flow 按可用宽度自动换行，更窄时按钮换行而非被裁剪/挤压，
        // 全部按钮始终可达，不引入新文案。
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Text {
                    text: qsTr("统计范围")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    // 防御性兜底：该行仅标签+刷新，~560px 下不触发；
                    // 极窄（<250px）或超长翻译时省略号保护。
                    elide: Text.ElideRight
                    Layout.maximumWidth: 120
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("刷新")
                    onClicked: page.refresh()
                    Accessible.name: qsTr("刷新用量统计")
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Repeater {
                    model: [
                        { key: "today",  label: qsTr("今日") },
                        { key: "d7",     label: qsTr("7天内") },
                        { key: "d30",    label: qsTr("30天内") },
                        { key: "all",    label: qsTr("全部") },
                        { key: "custom", label: qsTr("自定义") }
                    ]

                    delegate: AppButton {
                        required property var modelData
                        text: modelData.label
                        highlighted: page.rangeMode === modelData.key
                        onClicked: page.selectRange(modelData.key)
                        Accessible.name: modelData.label
                    }
                }
            }
        }

        // 自定义起止日期（已拍板决策：输入框 + 快捷按钮，不用日历弹出）
        ColumnLayout {
            visible: page.rangeMode === "custom"
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Text {
                    text: qsTr("起始日期")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
                TextField {
                    id: fromField
                    Layout.preferredWidth: Theme.controlWidthXs
                    placeholderText: "YYYY-MM-DD"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: Theme.selectedTextColor
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontMono
                    background: Rectangle {
                        radius: Theme.radiusField
                        color: Theme.surfaceHover
                        border.width: fromField.activeFocus ? 2 : 1
                        border.color: fromField.activeFocus ? Theme.brandPrimary
                                                            : Theme.borderSubtle
                    }
                    Accessible.name: qsTr("起始日期")
                    // 回车 / 失焦共用 editingFinished，避免双触发
                    onEditingFinished: page.applyCustomRange()
                }
                Text {
                    text: qsTr("截止日期（含当天）")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
                TextField {
                    id: toField
                    Layout.preferredWidth: Theme.controlWidthXs
                    placeholderText: "YYYY-MM-DD"
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: Theme.selectedTextColor
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontMono
                    background: Rectangle {
                        radius: Theme.radiusField
                        color: Theme.surfaceHover
                        border.width: toField.activeFocus ? 2 : 1
                        border.color: toField.activeFocus ? Theme.brandPrimary
                                                          : Theme.borderSubtle
                    }
                    Accessible.name: qsTr("截止日期（含当天）")
                    onEditingFinished: page.applyCustomRange()
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                AppButton {
                    text: qsTr("今天")
                    onClicked: {
                        var d = new Date()
                        page.setCustomRange(d, d)
                    }
                    Accessible.name: qsTr("快捷选择今天")
                }
                AppButton {
                    text: qsTr("昨天")
                    onClicked: {
                        var d = new Date()
                        d.setDate(d.getDate() - 1)
                        page.setCustomRange(d, d)
                    }
                    Accessible.name: qsTr("快捷选择昨天")
                }
                AppButton {
                    text: qsTr("本月")
                    onClicked: {
                        var now = new Date()
                        // day=0 被 Date 规整为上月末日：本月 1 日 ~ 本月末
                        page.setCustomRange(new Date(now.getFullYear(), now.getMonth(), 1),
                                            new Date(now.getFullYear(), now.getMonth() + 1, 0))
                    }
                    Accessible.name: qsTr("快捷选择本月")
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: page.customError !== ""
                    text: page.customError
                    color: Theme.statusDanger
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
            }
        }

        // ── 概览卡片（§8.1.1：任务次数 / 累计 Token / 累计费用 / 平均延迟）──
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

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
                    radius: Theme.radiusMd
                    color: Theme.bgSecondary
                    border.width: 1
                    border.color: Theme.borderSubtle

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Theme.spaceXs

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

        // ── 用量图表：一张图独占一行 ────────────────────
        // 内容区宽度只有 ~560px（设置对话框 860 减去导航与边距），双图
        // 并排时每张只剩 ~270px：环形图图例被压到 200px 上限、长模型名
        // 一律省略成「step-5-prev…」，柱状图 x 轴标签也挤成省略号。改为
        // 一行一张后，图例、轴标签、绘图区都拿到整宽，长模型名完整可读。
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            Text {
                text: qsTr("用量图表")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }

            // 图一：模型 Token 占比（环形图 + 图例）
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 236
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    Text {
                        text: qsTr("模型 Token 分布")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                    DonutChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        series: page.donutSeries
                        centerLabel: qsTr("总 Token")
                    }
                }
            }

            // 图二：按天走势（折线图）
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 212
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    Text {
                        text: qsTr("按天走势")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                    LineTrendChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        series: page.dayTrendSeries
                    }
                }
            }

            // 图三：模型对比（横向柱状图，模型名占独立列不截断）
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: page.barCardHeight
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Text {
                            text: qsTr("模型对比")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: qsTr("Token")
                            highlighted: page.barMode === "tokens"
                            onClicked: page.barMode = "tokens"
                            Accessible.name: qsTr("按 Token 对比")
                        }
                        AppButton {
                            text: qsTr("调用次数")
                            highlighted: page.barMode === "calls"
                            onClicked: page.barMode = "calls"
                            Accessible.name: qsTr("按调用次数对比")
                        }
                    }
                    BarCompareChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        series: page.barSeries
                        mode: page.barMode
                    }
                }
            }
        }

        // ── 方向维度卡片（§8.1.2：Token 构成）──────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            Text {
                text: qsTr("Token 构成")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: dirCol.implicitHeight + Theme.spaceLg
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: dirCol
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceSm

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
                            spacing: Theme.spaceSm

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
                                color: Theme.bgPrimary
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
            spacing: Theme.spaceXs

            Text {
                text: qsTr("按来源拆分")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: srcCol.implicitHeight + Theme.spaceLg
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: srcCol
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    // 表头（docs/25 C5: 空态时隐藏，与 byModel/byDay 空态形态一致）
                    RowLayout {
                        visible: page.bySourceData.length > 0
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm
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
                    }

                    Repeater {
                        model: page.bySourceData

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

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
            spacing: Theme.spaceXs

            Text {
                text: qsTr("费用明细")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: costCol.implicitHeight + Theme.spaceLg
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: costCol
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    // 表头（docs/25 C5: 空态时隐藏）
                    RowLayout {
                        visible: !page.costRowsEmpty
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm
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
                    }

                    Repeater {
                        model: page.costStatsData.byModel || []

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            Text {
                                text: modelData.model && modelData.model.length > 0
                                      ? modelData.model : qsTr("(未知)")
                                textFormat: Text.PlainText   // 模型名（远端可控）防富文本渲染
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
                        spacing: Theme.spaceSm
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
                        Layout.topMargin: Theme.spaceXs
                    }
                }
            }
        }

        // ── 请求明细（Batch 6 T3 + 表格化：每次 LLM 请求一行）──
        // 表格能力（筛选 / 排序 / 拖拽列宽 / 表体内部滚动）收在
        // UsageDetailTable 里；本页只负责注入已加载的行与分页回调。
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Text {
                    text: qsTr("请求明细")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("点击表头排序 · 拖动列边界调整列宽")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    elide: Text.ElideRight
                    Layout.maximumWidth: 240
                }
                Text {
                    visible: page.exportMessage !== ""
                    text: page.exportMessage
                    color: page.exportIsError ? Theme.statusDanger : Theme.statusSuccess
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    elide: Text.ElideRight
                    Layout.maximumWidth: 240
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                AppButton {
                    objectName: "usageExportCsv"
                    text: qsTr("导出 CSV")
                    enabled: page.detailRows.length > 0
                    onClicked: page.exportDetailCsv()
                    Accessible.name: qsTr("将请求明细导出为 CSV 文件")
                    // 导出的是「已加载」的行：先点「加载更多」再导出才能拿到全部。
                    HoverHandler { id: exportHover }
                    ToolTip.visible: exportHover.hovered
                    ToolTip.text: qsTr("导出表格当前视图（已加载 %1 / 共 %2 行）")
                        .arg(page.detailRows.length).arg(page.detailTotal)
                }
            }

            UsageDetailTable {
                id: detailTable
                Layout.fillWidth: true
                rows: page.detailRows
                total: page.detailTotal
                onLoadMore: page.loadDetailPage(false)
            }
        }

        // ── 配额状态（§8.1.5）──────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            Text {
                text: qsTr("配额状态")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: quotaCol.implicitHeight + Theme.spaceLg
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: quotaCol
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceSm

                    // docs/25 C5: 配额区空态补全（对齐 byModel/byDay）——
                    // 无任何用量且未配置任何配额时不渲染 0/不限 四行
                    Text {
                        visible: page.quotaEmpty
                        text: qsTr("该范围内暂无数据")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
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
                            spacing: Theme.spaceSm

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
                                color: Theme.bgPrimary
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

        // ── 配额设置（docs/17 B4：P1-3——配额键此前无任何编辑入口）──
        // 2×2 网格 + 完整标签：整页滚动后垂直空间不再稀缺，宽度紧张时
        // 仍能完整显示（单行四列在内容区 ~560px 下必然截断）。
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: quotaSettingsCol.implicitHeight + Theme.spaceLg
            radius: Theme.radiusMd
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: quotaSettingsCol
                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                spacing: Theme.spaceSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

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
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                                elide: Text.ElideRight
                        Layout.fillWidth: true
                        HoverHandler { id: quotaTipHover }
                        ToolTip.visible: quotaTipHover.hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("费用按已计费请求累计。勾选“超限拒绝”后达到上限直接拒绝请求，默认仅警告。")
                    }
                    Switch {
                        objectName: "strictQuotaMode"
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
                    columnSpacing: Theme.spaceLg
                    rowSpacing: Theme.spaceXs

                    // External quota changes (e.g. a backup restore done from
                    // the 备份 page in the same dialog session) must reach
                    // these four SpinBoxes: the Component.onCompleted seed is
                    // one-shot, and a stale spin would WRITE the stale value
                    // back on the next onValueModified. Focus guard keeps a
                    // typed-but-uncommitted edit from being stomped (same
                    // pattern as ModelConfigPage's profilesChanged rebind).
                    Connections {
                        target: Settings
                        function onDailyTokenQuotaChanged() {
                            if (!dailyTokenSpin.activeFocus)
                                dailyTokenSpin.value = Settings.dailyTokenQuota
                        }
                        function onMonthlyTokenQuotaChanged() {
                            if (!monthlyTokenSpin.activeFocus)
                                monthlyTokenSpin.value = Settings.monthlyTokenQuota
                        }
                        function onDailyCostQuotaCentsChanged() {
                            if (!dailyCostSpin.activeFocus)
                                dailyCostSpin.value = Math.round(Settings.dailyCostQuotaCents / 100)
                        }
                        function onMonthlyCostQuotaCentsChanged() {
                            if (!monthlyCostSpin.activeFocus)
                                monthlyCostSpin.value = Math.round(Settings.monthlyCostQuotaCents / 100)
                        }
                    }

                    Text { text: qsTr("每日 Token 上限"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: dailyTokenSpin
                        objectName: "dailyTokenQuota"
                        Layout.fillWidth: true
                        from: 0; to: 100000000; stepSize: 1000; editable: true
                        Component.onCompleted: value = Settings.dailyTokenQuota
                        onValueModified: Settings.dailyTokenQuota = value
                        Accessible.name: qsTr("每日 Token 上限")
                    }
                    Text { text: qsTr("每月 Token 上限"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: monthlyTokenSpin
                        objectName: "monthlyTokenQuota"
                        Layout.fillWidth: true
                        from: 0; to: 1000000000; stepSize: 10000; editable: true
                        Component.onCompleted: value = Settings.monthlyTokenQuota
                        onValueModified: Settings.monthlyTokenQuota = value
                        Accessible.name: qsTr("每月 Token 上限")
                    }
                    Text { text: qsTr("每日费用上限（元）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: dailyCostSpin
                        objectName: "dailyCostQuotaCents"
                        Layout.fillWidth: true
                        from: 0; to: 1000000; stepSize: 1; editable: true
                        Component.onCompleted: value = Math.round(Settings.dailyCostQuotaCents / 100)
                        onValueModified: Settings.dailyCostQuotaCents = value * 100
                        Accessible.name: qsTr("每日费用上限 元")
                    }
                    Text { text: qsTr("每月费用上限（元）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans; Layout.preferredWidth: 104 }
                    SpinBox {
                        id: monthlyCostSpin
                        objectName: "monthlyCostQuotaCents"
                        Layout.fillWidth: true
                        from: 0; to: 10000000; stepSize: 1; editable: true
                        Component.onCompleted: value = Math.round(Settings.monthlyCostQuotaCents / 100)
                        onValueModified: Settings.monthlyCostQuotaCents = value * 100
                        Accessible.name: qsTr("每月费用上限 元")
                    }
                }
            }
        }

                Item { Layout.preferredHeight: 8 }
            }
        }
    }

    FileDialog {
        id: usageExportDialog
        title: qsTr("导出请求明细")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV 文件 (*.csv)")]
        onAccepted: {
            // viewRows = 表格当前视图（筛选 + 排序），不是整库。
            const rows = detailTable.viewRows
            const ok = AgentVM.exportUsageCsv(selectedFile.toString(), rows)
            page.exportIsError = !ok
            page.exportMessage = ok
                ? qsTr("已导出 %1 行明细。").arg(rows.length)
                : qsTr("导出失败，请检查目标路径是否可写。")
        }
    }
}
