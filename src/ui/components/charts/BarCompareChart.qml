import QtQuick
import QtQuick.Controls
import NexusAI.ViewModels

// 批次六 T2（docs/16 §8）：按模型用量对比柱状图。QML Canvas 自绘，
// 不引入 Qt Charts。
//
// 数据契约（与 T1 tokenStatsDetailed.byModel 口径对齐，由调用方注入）：
//   series: [{ label: 模型名, value: 数值 }, ...]
//   mode:   "tokens"（默认）| "calls" —— 数值口径开关，切换时调用方
//           重新注入对应 series；组件用 mode 决定单位文案与无障碍描述。
// value = 0 是真实数据（画 0 长度柱 + 柱尾「0」），空序列显示空态占位。
//
// 横向（而非纵向）柱状图：模型名是长 ID（deepseek-v4-flash-0731、
// gemini-3.6-flash-high），纵向柱图的 x 轴标签只能分到「绘图区宽度 /
// 模型数」——设置面板 ~560px 内容区里 7 个模型每个槽仅 ~70px，名字
// 必然被省略成「deepseek-v4-f…」。改为横向后，模型名独占左侧一列，
// 列宽按最长名字实测（上限 42% 宽度），名字完整可读，柱长表示数值。
//
// 交互：悬停行 → 行底色 + 柱描边高亮 + tooltip；键盘左右键在
// 「概览 ↔ 各柱」间移动高亮。
Item {
    id: root

    required property var series
    property string mode: "tokens"

    // ── 派生数据 ─────────────────────────────────────
    readonly property var bars: {
        var out = []
        var list = root.series || []
        for (var i = 0; i < list.length; ++i) {
            var item = list[i]
            var v = Number(item && item.value ? item.value : 0)
            if (!Number.isFinite(v)) v = 0
            var l = item && item.label !== undefined ? String(item.label) : ""
            out.push({ label: l, value: v })
        }
        return out
    }
    readonly property real maxValue: {
        var m = 0
        for (var i = 0; i < root.bars.length; ++i)
            m = Math.max(m, root.bars[i].value)
        return m
    }
    readonly property bool hasData: root.bars.length > 0

    // mode → 单位文案（仅影响展示层，不改变数值）
    readonly property string unitText: root.mode === "calls" ? qsTr("次") : qsTr("Token")

    // ── y 轴自动刻度（nice number 取整，目标 4 段）────
    function niceStep(rough) {
        if (rough <= 0) return 1
        var exp = Math.floor(Math.log(rough) / Math.LN10)
        var frac = rough / Math.pow(10, exp)
        var nice = frac <= 1 ? 1 : frac <= 2 ? 2 : frac <= 5 ? 5 : 10
        return nice * Math.pow(10, exp)
    }
    readonly property real axisStep: {
        var s = root.niceStep(root.maxValue / 4)
        return Number.isFinite(s) && s > 0 ? s : 1
    }
    readonly property real axisMax: {
        var m = Math.max(root.axisStep,
                         Math.ceil(root.maxValue / root.axisStep) * root.axisStep)
        return Number.isFinite(m) && m > 0 ? m : Math.max(1, root.maxValue)
    }

    // ── 交互状态（悬停优先于键盘选中）────────────────
    property int hoverIndex: -1
    property int keyIndex: -1
    readonly property int highlightIndex: root.hoverIndex >= 0 ? root.hoverIndex : root.keyIndex

    // ── 色板：Theme 既有语义色循环，与 DonutChart 图例呼应 ──
    readonly property var paletteColors: [Theme.brandPrimary, Theme.statusSuccess,
                                          Theme.statusWarning, Theme.statusDanger]
    function seriesColor(i) {
        var n = root.paletteColors.length
        return root.paletteColors[((i % n) + n) % n]
    }
    function fmtNumber(v) {
        return Number(v || 0).toLocaleString(Qt.locale(), 'f', 0)
    }
    // 批次六安全审查 S.XSS.02：模型名来自 LLM 网关响应（远端可控），
    // QQuickToolTip 无 textFormat 属性（Qt 6.9 仅 text/delay/timeout），
    // 样式 contentItem 的 Text 默认 AutoText 会把以标签开头的模型名按
    // 富文本渲染（如 <img src> 信标）。拼 tooltip 前打断 '<'，确保按
    // 纯文本显示。
    function plainLabel(s) {
        return String(s).replace(/</g, "\\<")
    }
    function compactNumber(v) {
        var x = Number(v || 0)
        var trim = function(n) { return String(Math.round(n * 10) / 10) }
        if (x >= 1000000) return trim(x / 1000000) + "M"
        if (x >= 10000) return trim(x / 1000) + "k"
        return String(Math.round(x))
    }

    readonly property string accessSummary: {
        if (!root.hasData) return qsTr("暂无数据")
        var hi = root.highlightIndex
        if (hi >= 0 && hi < root.bars.length)
            return qsTr("%1: %2 %3").arg(root.bars[hi].label)
                .arg(root.fmtNumber(root.bars[hi].value)).arg(root.unitText)
        return qsTr("共 %1 个模型，最高 %2 %3").arg(root.bars.length)
            .arg(root.fmtNumber(root.maxValue)).arg(root.unitText)
    }

    onBarsChanged: canvas.requestPaint()
    onHighlightIndexChanged: canvas.requestPaint()
    onModeChanged: canvas.requestPaint()          // 单位口径切换（配合新 series）
    onPaletteColorsChanged: canvas.requestPaint() // 明暗主题切换时重取色
    onAxisMaxChanged: canvas.requestPaint()
    onPadLeftChanged: canvas.requestPaint()
    onVisibleChanged: if (visible) canvas.requestPaint()

    implicitWidth: 400
    implicitHeight: 220
    focusPolicy: Qt.StrongFocus

    Keys.onLeftPressed: (event) => {
        root.keyIndex = root.stepHighlight(-1)
        event.accepted = true
    }
    Keys.onRightPressed: (event) => {
        root.keyIndex = root.stepHighlight(1)
        event.accepted = true
    }
    // 键盘步进：-1 为整体概览，越界钳制在首/末柱
    function stepHighlight(dir) {
        var n = root.bars.length
        if (n === 0) return -1
        var next = root.keyIndex + dir
        if (next < 0) return -1
        if (next >= n) return n - 1
        return next
    }

    // ── 模型名列宽：按最长模型名实测 ─────────────────
    readonly property string longestLabel: {
        var s = ""
        for (var i = 0; i < root.bars.length; ++i)
            if (root.bars[i].label.length > s.length) s = root.bars[i].label
        return s
    }
    // 隐藏的量尺：implicitWidth 即该字符串在当前字号下的自然宽度
    Text {
        id: labelMeasure
        visible: false
        text: root.longestLabel
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontSans
    }

    // ── 绘图区内边距 ─────────────────────────────────
    // 左：模型名列（实测宽度 + 间距，上限 42% 防长名字吃掉绘图区）
    readonly property real padLeft: Math.max(56, Math.min(root.width * 0.42,
                                                          labelMeasure.implicitWidth + 16))
    // 右：柱尾数值标签（803.9k 约 40px）
    readonly property real padRight: 54
    readonly property real padTop: 8
    readonly property real padBottom: 24

    // 悬停命中：y → 行序号
    function indexAtY(y) {
        var n = root.bars.length
        if (n === 0) return -1
        var plotH = canvas.height - root.padTop - root.padBottom
        if (plotH <= 0) return -1
        var idx = Math.floor((y - root.padTop) * n / plotH)
        return Math.max(0, Math.min(n - 1, idx))
    }

    // Canvas 侧文本省略（尾部截断加省略号）——模型名列宽度已按最长名
    // 实测，正常不触发；仅当 42% 上限被撑满（超长模型名）时兜底。
    function elidedText(ctx, text, maxW) {
        var t = String(text)
        if (maxW <= 0) return ""
        if (ctx.measureText(t).width <= maxW) return t
        while (t.length > 1 && ctx.measureText(t + "…").width > maxW)
            t = t.substring(0, t.length - 1)
        return t + "…"
    }

    // 横向柱体路径：右端圆角、左端直角（左端贴在数值轴基线上）
    function roundedBarPath(ctx, x, y, w, h, r) {
        var rr = Math.max(0, Math.min(r, w, h / 2))
        ctx.beginPath()
        ctx.moveTo(x, y)
        ctx.lineTo(x + w - rr, y)
        ctx.quadraticCurveTo(x + w, y, x + w, y + rr)
        ctx.lineTo(x + w, y + h - rr)
        ctx.quadraticCurveTo(x + w, y + h, x + w - rr, y + h)
        ctx.lineTo(x, y + h)
        ctx.closePath()
    }

    // ── 空态 ─────────────────────────────────────────
    Text {
        visible: !root.hasData
        anchors.centerIn: parent
        text: qsTr("该范围内暂无数据")
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        opacity: 0.7
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            // 可观测性：onPaint 内 JS 异常否则完全静默（clearRect 已执行
            // → 纯空白）
            try {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (!root.hasData) return
                var n = root.bars.length
                var plotL = root.padLeft
                var plotR = width - root.padRight
                var plotT = root.padTop
                var plotB = height - root.padBottom
                var plotW = Math.max(1, plotR - plotL)
                var plotH = Math.max(1, plotB - plotT)
                var rowH = plotH / n
                var barH = Math.max(3, Math.min(rowH * 0.62, 22))

                // 纵向网格线 + x 轴刻度（nice number 步进）
                ctx.strokeStyle = Theme.borderSubtle
                ctx.lineWidth = 1
                ctx.fillStyle = Theme.textSecondary
                ctx.font = Theme.fontSizeCaption + "px \"" + Theme.fontSans + "\""
                ctx.textAlign = "center"
                ctx.textBaseline = "top"
                for (var v = 0; v <= root.axisMax + 0.5; v += root.axisStep) {
                    var gx = plotL + plotW * (v / root.axisMax)
                    ctx.beginPath()
                    ctx.moveTo(gx, plotT)
                    ctx.lineTo(gx, plotB)
                    ctx.stroke()
                    ctx.fillText(root.compactNumber(v), gx, plotB + 6)
                }

                for (var i = 0; i < n; ++i) {
                    var value = root.bars[i].value
                    var cy = plotT + rowH * i + rowH / 2
                    var y = cy - barH / 2
                    var w = plotW * Math.max(0, value) / root.axisMax
                    var highlighted = i === root.highlightIndex

                    // 悬停/选中行：整行底色（横向柱图里行就是「数据项」，
                    // 行级高亮比只描柱更容易扫读）
                    if (highlighted) {
                        ctx.fillStyle = Theme.surfaceHover
                        ctx.fillRect(0, plotT + rowH * i, width, rowH)
                    }

                    // 柱体
                    if (w > 0) {
                        root.roundedBarPath(ctx, plotL, y, Math.max(2, w), barH, 3)
                        ctx.fillStyle = root.seriesColor(i)
                        ctx.fill()
                        if (highlighted) {
                            ctx.strokeStyle = Theme.brandPrimary
                            ctx.lineWidth = 2
                            ctx.stroke()
                        }
                    }

                    // 模型名（右对齐到柱基线左侧）
                    ctx.textAlign = "right"
                    ctx.textBaseline = "middle"
                    ctx.fillStyle = highlighted ? Theme.textPrimary : Theme.textSecondary
                    ctx.fillText(root.elidedText(ctx, root.bars[i].label, plotL - 12),
                                 plotL - 8, cy)

                    // 数值（柱尾外，紧凑格式；超出右侧留白时省略兜底）
                    ctx.textAlign = "left"
                    ctx.fillStyle = highlighted ? Theme.textPrimary : Theme.textSecondary
                    ctx.fillText(root.elidedText(ctx, root.compactNumber(value),
                                                 root.padRight - 10),
                                 plotL + w + 6, cy)
                }
            } catch (e) {
                console.warn("[BarCompareChart] paint failed:", e, "w=", width,
                             "h=", height, "bars=", root.bars.length,
                             "axisMax=", root.axisMax)
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: (mouse) => {
            // 仅在绘图区纵向范围内命中（上下留白外不提示）
            if (mouse.y < root.padTop - 4 || mouse.y > height - root.padBottom + 4) {
                root.hoverIndex = -1
            } else {
                root.hoverIndex = root.indexAtY(mouse.y)
            }
        }
        onExited: root.hoverIndex = -1
        ToolTip.visible: containsMouse && root.highlightIndex >= 0
                         && root.highlightIndex < root.bars.length
        ToolTip.delay: 150
        ToolTip.text: root.highlightIndex >= 0 && root.highlightIndex < root.bars.length
            ? qsTr("%1: %2 %3").arg(root.plainLabel(root.bars[root.highlightIndex].label))
                .arg(root.fmtNumber(root.bars[root.highlightIndex].value))
                .arg(root.unitText)
            : ""
    }

    // 键盘焦点指示环
    Rectangle {
        anchors.fill: parent
        visible: root.activeFocus
        color: "transparent"
        border.width: 1
        border.color: Theme.brandPrimary
        radius: Theme.radiusSm
    }

    Accessible.role: Accessible.Chart
    Accessible.name: qsTr("模型用量对比柱状图")
    Accessible.description: root.accessSummary
}
