import QtQuick
import QtQuick.Controls
import NexusAI

// 批次六 T2（docs/16 §8）：按天 Token 走势折线图。QML Canvas 自绘，
// 不引入 Qt Charts。
//
// 数据契约（与 T1 byDay 口径对齐，由调用方注入并保证按天升序）：
//   series: [{ label: "MM-dd", value: 当日 token }, ...]
// 全 0 序列视为真实数据（画 0 基线平线），空序列显示空态占位。
//
// 交互：悬停数据点 → 竖参考线 + 放大高亮点 + tooltip；
// 键盘左右键在「概览 ↔ 各数据点」间移动高亮。
Item {
    id: root

    required property var series

    // tooltip 单位口径（默认 Token；调用方可换成其他单位）
    property string unitLabel: qsTr("Token")

    // ── 派生数据 ─────────────────────────────────────
    readonly property var points: {
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
        for (var i = 0; i < root.points.length; ++i)
            m = Math.max(m, root.points[i].value)
        return m
    }
    readonly property bool hasData: root.points.length > 0

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

    // ── 色板：单序列取品牌主色（Theme 既有语义色）────
    readonly property var paletteColors: [Theme.brandPrimary, Theme.statusSuccess,
                                          Theme.statusWarning, Theme.statusDanger]
    function seriesColor(i) {
        var n = root.paletteColors.length
        return root.paletteColors[((i % n) + n) % n]
    }
    function fmtNumber(v) {
        return Number(v || 0).toLocaleString(Qt.locale(), 'f', 0)
    }
    // 轴标签紧凑格式：大数缩写为 k/M，避免挤占绘图区
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
        if (hi >= 0 && hi < root.points.length)
            return qsTr("%1: %2 %3").arg(root.points[hi].label)
                .arg(root.fmtNumber(root.points[hi].value)).arg(root.unitLabel)
        return qsTr("共 %1 天，峰值 %2 %3").arg(root.points.length)
            .arg(root.fmtNumber(root.maxValue)).arg(root.unitLabel)
    }

    onPointsChanged: canvas.requestPaint()
    onHighlightIndexChanged: canvas.requestPaint()
    onPaletteColorsChanged: canvas.requestPaint()   // 明暗主题切换时重取色
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
    // 键盘步进：-1 为整体概览，越界钳制在首/末数据点
    function stepHighlight(dir) {
        var n = root.points.length
        if (n === 0) return -1
        var next = root.keyIndex + dir
        if (next < 0) return -1
        if (next >= n) return n - 1
        return next
    }

    // ── 绘图区内边距（左容纳 y 轴刻度，底容纳 x 轴日期）──
    readonly property real padLeft: 48
    readonly property real padRight: 16
    readonly property real padTop: 14
    readonly property real padBottom: 26

    // 数据点坐标换算（单点时落在绘图区中线）
    function xAt(i) {
        var n = root.points.length
        var plotW = canvas.width - root.padLeft - root.padRight
        if (n <= 1) return root.padLeft + plotW / 2
        return root.padLeft + plotW * i / (n - 1)
    }
    function yAt(v) {
        var plotH = canvas.height - root.padTop - root.padBottom
        var plotB = canvas.height - root.padBottom
        return plotB - plotH * (v / root.axisMax)
    }
    function indexAtX(x) {
        var n = root.points.length
        if (n === 0) return -1
        if (n === 1) return 0
        var plotW = canvas.width - root.padLeft - root.padRight
        var idx = Math.round((x - root.padLeft) * (n - 1) / plotW)
        return Math.max(0, Math.min(n - 1, idx))
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
            var n = root.points.length
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

            // x 轴日期标签（按可用宽度抽样，避免重叠；尽量补上末点）
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            var stepX = n > 1 ? plotW / (n - 1) : 0
            var maxLabels = Math.max(2, Math.floor(plotW / 56))
            var stride = Math.max(1, Math.ceil(n / maxLabels))
            var lastShown = -1
            for (var i = 0; i < n; i += stride) {
                ctx.fillText(root.points[i].label, root.xAt(i), plotB + 6)
                lastShown = i
            }
            if (lastShown < n - 1 && (n - 1 - lastShown) * stepX >= 30)
                ctx.fillText(root.points[n - 1].label, root.xAt(n - 1), plotB + 6)

            var col = root.seriesColor(0)

            // 折线下方渐变填充（品牌色低透明度，增强走势体感）
            var grad = ctx.createLinearGradient(0, plotT, 0, plotB)
            grad.addColorStop(0, Qt.rgba(col.r, col.g, col.b, 0.22))
            grad.addColorStop(1, Qt.rgba(col.r, col.g, col.b, 0.02))
            ctx.beginPath()
            ctx.moveTo(root.xAt(0), plotB)
            for (var j = 0; j < n; ++j)
                ctx.lineTo(root.xAt(j), root.yAt(root.points[j].value))
            ctx.lineTo(root.xAt(n - 1), plotB)
            ctx.closePath()
            ctx.fillStyle = grad
            ctx.fill()

            // 折线
            ctx.beginPath()
            ctx.moveTo(root.xAt(0), root.yAt(root.points[0].value))
            for (var k = 1; k < n; ++k)
                ctx.lineTo(root.xAt(k), root.yAt(root.points[k].value))
            ctx.strokeStyle = col
            ctx.lineWidth = 2
            ctx.stroke()

            // 数据点
            for (var m = 0; m < n; ++m) {
                ctx.beginPath()
                ctx.arc(root.xAt(m), root.yAt(root.points[m].value), 3, 0, Math.PI * 2)
                ctx.fillStyle = col
                ctx.fill()
                ctx.strokeStyle = Theme.bgSecondary
                ctx.lineWidth = 1
                ctx.stroke()
            }

            // 高亮：竖虚线参考线 + 放大描边点
            if (root.highlightIndex >= 0 && root.highlightIndex < n) {
                var hi = root.highlightIndex
                var hx = root.xAt(hi)
                var hy = root.yAt(root.points[hi].value)
                ctx.strokeStyle = Theme.borderSubtle
                ctx.lineWidth = 1
                ctx.setLineDash([3, 3])
                ctx.beginPath()
                ctx.moveTo(hx, plotT)
                ctx.lineTo(hx, plotB)
                ctx.stroke()
                ctx.setLineDash([])
                ctx.beginPath()
                ctx.arc(hx, hy, 5, 0, Math.PI * 2)
                ctx.fillStyle = col
                ctx.fill()
                ctx.strokeStyle = Theme.textPrimary
                ctx.lineWidth = 2
                ctx.stroke()
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: (mouse) => {
            // 仅在绘图区横向范围内命中（左右 padding 外不提示）
            if (mouse.x < root.padLeft - 8 || mouse.x > width - root.padRight + 8) {
                root.hoverIndex = -1
            } else {
                root.hoverIndex = root.indexAtX(mouse.x)
            }
        }
        onExited: root.hoverIndex = -1
        ToolTip.visible: containsMouse && root.highlightIndex >= 0
                         && root.highlightIndex < root.points.length
        ToolTip.delay: 150
        ToolTip.text: root.highlightIndex >= 0 && root.highlightIndex < root.points.length
            ? qsTr("%1: %2 %3").arg(root.points[root.highlightIndex].label)
                .arg(root.fmtNumber(root.points[root.highlightIndex].value))
                .arg(root.unitLabel)
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
    Accessible.name: qsTr("每日 Token 走势折线图")
    Accessible.description: root.accessSummary
}