import QtQuick
import QtQuick.Controls
import NexusAI

// 批次六 T2（docs/16 §8）：按模型用量对比柱状图。QML Canvas 自绘，
// 不引入 Qt Charts。
//
// 数据契约（与 T1 tokenStatsDetailed.byModel 口径对齐，由调用方注入）：
//   series: [{ label: 模型名, value: 数值 }, ...]
//   mode:   "tokens"（默认）| "calls" —— 数值口径开关，切换时调用方
//           重新注入对应 series；组件用 mode 决定单位文案与无障碍描述。
// value = 0 是真实数据（画 0 高度柱 + 柱顶「0」），空序列显示空态占位。
//
// 交互：悬停柱 → 品牌色描边高亮 + tooltip；键盘左右键在
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
    readonly property real axisStep: root.niceStep(root.maxValue / 4)
    readonly property real axisMax: Math.max(root.axisStep,
                                             Math.ceil(root.maxValue / root.axisStep) * root.axisStep)

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

    // ── 绘图区内边距 ─────────────────────────────────
    readonly property real padLeft: 48
    readonly property real padRight: 16
    readonly property real padTop: 14
    readonly property real padBottom: 26

    // 悬停命中：x → 柱槽序号
    function indexAtX(x) {
        var n = root.bars.length
        if (n === 0) return -1
        var plotW = canvas.width - root.padLeft - root.padRight
        if (plotW <= 0) return -1
        var idx = Math.floor((x - root.padLeft) * n / plotW)
        return Math.max(0, Math.min(n - 1, idx))
    }

    // Canvas 侧文本省略（模型名过长时截断加省略号）
    function elidedText(ctx, text, maxW) {
        var t = String(text)
        if (maxW <= 0) return ""
        if (ctx.measureText(t).width <= maxW) return t
        while (t.length > 1 && ctx.measureText(t + "…").width > maxW)
            t = t.substring(0, t.length - 1)
        return t + "…"
    }

    // 顶部圆角、底部直角的柱体路径
    function roundedBarPath(ctx, x, y, w, h, r) {
        var rr = Math.min(r, w / 2, h)
        ctx.beginPath()
        ctx.moveTo(x + rr, y)
        ctx.lineTo(x + w - rr, y)
        ctx.quadraticCurveTo(x + w, y, x + w, y + rr)
        ctx.lineTo(x + w, y + h)
        ctx.lineTo(x, y + h)
        ctx.lineTo(x, y + rr)
        ctx.quadraticCurveTo(x, y, x + rr, y)
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
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.hasData) return
            var n = root.bars.length
            var plotL = root.padLeft
            var plotR = width - root.padRight
            var plotT = root.padTop
            var plotB = height - root.padBottom
            var plotW = plotR - plotL
            var plotH = plotB - plotT

            // 网格线 + y 轴刻度（nice number 步进）
            ctx.strokeStyle = Theme.borderSubtle
            ctx.lineWidth = 1
            ctx.fillStyle = Theme.textSecondary
            ctx.font = Theme.fontSizeCaption + "px \"" + Theme.fontSans + "\""
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (var v = 0; v <= root.axisMax + 0.5; v += root.axisStep) {
                var gy = plotB - plotH * (v / root.axisMax)
                ctx.beginPath()
                ctx.moveTo(plotL, gy)
                ctx.lineTo(plotR, gy)
                ctx.stroke()
                ctx.fillText(root.compactNumber(v), plotL - 6, gy)
            }

            var slot = plotW / n
            var barW = Math.min(slot * 0.6, 64)
            for (var i = 0; i < n; ++i) {
                var value = root.bars[i].value
                var h = plotH * (value / root.axisMax)
                var x = plotL + slot * i + (slot - barW) / 2
                var y = plotB - h
                var highlighted = i === root.highlightIndex

                if (h > 0) {
                    root.roundedBarPath(ctx, x, y, barW, h, 3)
                    ctx.fillStyle = root.seriesColor(i)
                    ctx.fill()
                    if (highlighted) {
                        ctx.strokeStyle = Theme.brandPrimary
                        ctx.lineWidth = 2
                        ctx.stroke()
                    }
                }

                // 柱顶数值（紧凑格式；高亮时主色强调）
                ctx.textAlign = "center"
                ctx.textBaseline = "bottom"
                ctx.fillStyle = highlighted ? Theme.textPrimary : Theme.textSecondary
                ctx.fillText(root.compactNumber(value), x + barW / 2, y - 3)

                // x 轴模型名（槽宽内省略截断）
                ctx.textBaseline = "top"
                ctx.fillStyle = highlighted ? Theme.textPrimary : Theme.textSecondary
                ctx.fillText(root.elidedText(ctx, root.bars[i].label, slot - 4),
                             plotL + slot * i + slot / 2, plotB + 6)
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: (mouse) => {
            // 仅在绘图区横向范围内命中
            if (mouse.x < root.padLeft - 8 || mouse.x > width - root.padRight + 8) {
                root.hoverIndex = -1
            } else {
                root.hoverIndex = root.indexAtX(mouse.x)
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