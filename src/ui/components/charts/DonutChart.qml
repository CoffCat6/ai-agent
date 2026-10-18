import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// 批次六 T2（docs/16 §8）：模型 Token 占比环形图。QML Canvas 自绘，
// 不引入 Qt Charts（find_package 保持核心组件 + Pdf）。
//
// 数据契约（与 T1 tokenStatsDetailed.byModel 口径对齐，由调用方注入，
// 组件自身不拉取数据）：
//   series: [{ label: 模型名, value: token 数 }, ...]
// value <= 0 的项画不出扇区，过滤后不参与占比，也不会导致异常。
//
// 交互：悬停扇区/图例项 → 外扩高亮 + 中心切换为该项数值 + tooltip；
// 键盘左右键在「概览 ↔ 各扇区」间移动高亮（配合读屏 description）。
Item {
    id: root

    required property var series

    // 环心第二行标签（默认「总量」；调用方可按口径换成「调用次数」等）
    property string centerLabel: qsTr("总量")

    // ── 派生数据 ─────────────────────────────────────
    readonly property var slices: {
        var out = []
        var list = root.series || []
        for (var i = 0; i < list.length; ++i) {
            var item = list[i]
            var v = Number(item && item.value ? item.value : 0)
            if (v > 0) {
                var l = item && item.label !== undefined ? String(item.label) : ""
                out.push({ label: l, value: v })
            }
        }
        return out
    }
    readonly property real totalValue: {
        var sum = 0
        for (var i = 0; i < root.slices.length; ++i) sum += root.slices[i].value
        return sum
    }
    readonly property bool hasData: root.slices.length > 0

    // ── 交互状态（悬停优先于键盘选中）────────────────
    property int hoverIndex: -1
    property int keyIndex: -1
    readonly property int highlightIndex: root.hoverIndex >= 0 ? root.hoverIndex : root.keyIndex

    // ── 色板：Theme 既有语义色循环，超出数量回绕复用 ──
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
    function percentText(i) {
        if (root.totalValue <= 0 || i < 0 || i >= root.slices.length) return "0.0%"
        return (root.slices[i].value / root.totalValue * 100).toFixed(1) + "%"
    }

    // ── 环心文本：默认总量，悬停/键盘选中时切换为该项 ──
    readonly property string centerValueText:
        root.highlightIndex >= 0 && root.highlightIndex < root.slices.length
            ? root.fmtNumber(root.slices[root.highlightIndex].value)
            : root.fmtNumber(root.totalValue)
    readonly property string centerCaptionText:
        root.highlightIndex >= 0 && root.highlightIndex < root.slices.length
            ? root.slices[root.highlightIndex].label
            : root.centerLabel

    // 无障碍摘要：选中某项时读出该项，否则读整体概览（均为注入数据的
    // 真实聚合，不伪造）
    readonly property string accessSummary: {
        if (!root.hasData) return qsTr("暂无数据")
        var hi = root.highlightIndex
        if (hi >= 0 && hi < root.slices.length)
            return qsTr("%1: %2（%3）").arg(root.slices[hi].label)
                .arg(root.fmtNumber(root.slices[hi].value)).arg(root.percentText(hi))
        return qsTr("共 %1 个模型，总计 %2").arg(root.slices.length)
            .arg(root.fmtNumber(root.totalValue))
    }

    onSlicesChanged: canvas.requestPaint()
    onHighlightIndexChanged: canvas.requestPaint()
    onPaletteColorsChanged: canvas.requestPaint()   // 明暗主题切换时重取色

    implicitWidth: 360
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
    // 键盘步进：-1 为整体概览，越界钳制在首/末扇区
    function stepHighlight(dir) {
        var n = root.slices.length
        if (n === 0) return -1
        var next = root.keyIndex + dir
        if (next < 0) return -1
        if (next >= n) return n - 1
        return next
    }

    // ── 空态（勿伪造数据，显示占位提示）──────────────
    Text {
        visible: !root.hasData
        anchors.centerIn: parent
        text: qsTr("该范围内暂无数据")
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        opacity: 0.7
    }

    RowLayout {
        visible: root.hasData
        anchors.fill: parent
        spacing: Theme.spaceLg

        // 左：环形主体（保持正方形，直径随高度收缩）
        Item {
            id: chartArea
            Layout.fillHeight: true
            Layout.preferredWidth: height

            Canvas {
                id: canvas
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!root.hasData) return
                    var geo = root.sectorGeometry()
                    var acc = -Math.PI / 2   // 12 点方向起，顺时针
                    for (var i = 0; i < root.slices.length; ++i) {
                        var sweep = root.slices[i].value / root.totalValue * Math.PI * 2
                        // 高亮扇区半径外扩 4px
                        var r = i === root.highlightIndex ? geo.outerR + 4 : geo.outerR
                        ctx.beginPath()
                        ctx.arc(geo.cx, geo.cy, r, acc, acc + sweep)
                        ctx.arc(geo.cx, geo.cy, geo.innerR, acc + sweep, acc, true)
                        ctx.closePath()
                        ctx.fillStyle = root.seriesColor(i)
                        ctx.fill()
                        // 扇区间用背景色细描边制造分隔
                        ctx.strokeStyle = Theme.bgSecondary
                        ctx.lineWidth = 1
                        ctx.stroke()
                        acc += sweep
                    }
                }
            }

            MouseArea {
                id: hoverArea
                anchors.fill: parent
                hoverEnabled: true
                onPositionChanged: (mouse) => {
                    root.hoverIndex = root.hitSector(mouse.x, mouse.y)
                }
                onExited: root.hoverIndex = -1
                ToolTip.visible: containsMouse && root.highlightIndex >= 0
                                 && root.highlightIndex < root.slices.length
                ToolTip.delay: 150
                ToolTip.text: root.highlightIndex >= 0 && root.highlightIndex < root.slices.length
                    ? qsTr("%1: %2（%3）").arg(root.plainLabel(root.slices[root.highlightIndex].label))
                        .arg(root.fmtNumber(root.slices[root.highlightIndex].value))
                        .arg(root.percentText(root.highlightIndex))
                    : ""
            }

            // 环心：总量 / 选中项数值
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.52, 150)
                spacing: 2
                Text {
                    width: parent.width
                    text: root.centerValueText
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                }
                Text {
                    width: parent.width
                    text: root.centerCaptionText
                    textFormat: Text.PlainText   // 模型名（远端可控）防富文本渲染
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                }
            }
        }

        // 右：图例（模型名 + 占比；悬停联动高亮扇区）
        Column {
            spacing: Theme.spaceSm
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: 200

            Repeater {
                model: root.slices

                delegate: Item {
                    id: legendRow
                    required property var modelData
                    required property int index
                    readonly property bool highlighted: legendRow.index === root.highlightIndex
                    implicitWidth: legendRowLayout.implicitWidth
                    implicitHeight: legendRowLayout.implicitHeight

                    RowLayout {
                        id: legendRowLayout
                        anchors.fill: parent
                        spacing: Theme.spaceSm

                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 2
                            color: root.seriesColor(legendRow.index)
                        }
                        Text {
                            Layout.preferredWidth: 100
                            text: legendRow.modelData.label
                            textFormat: Text.PlainText   // 模型名（远端可控）防富文本渲染
                            color: legendRow.highlighted ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: root.percentText(legendRow.index)
                            color: legendRow.highlighted ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: root.hoverIndex = legendRow.index
                        onExited: root.hoverIndex = -1
                    }
                }
            }
        }
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

    // ── 命中测试：鼠标位置 → 扇区序号 ───────────────
    function sectorGeometry() {
        var outerR = Math.min(chartArea.width, chartArea.height) / 2 - 8
        return { cx: chartArea.width / 2, cy: chartArea.height / 2,
                 outerR: outerR, innerR: outerR * 0.62 }
    }
    function hitSector(x, y) {
        if (!root.hasData) return -1
        var geo = root.sectorGeometry()
        var dx = x - geo.cx
        var dy = y - geo.cy
        var dist = Math.sqrt(dx * dx + dy * dy)
        if (dist < geo.innerR || dist > geo.outerR + 6) return -1
        // atan2 基准 0 = 3 点方向；+PI/2 换算为从 12 点起顺时针，与绘制一致
        var a = Math.atan2(dy, dx) + Math.PI / 2
        if (a < 0) a += Math.PI * 2
        var acc = 0
        for (var i = 0; i < root.slices.length; ++i) {
            var sweep = root.slices[i].value / root.totalValue * Math.PI * 2
            if (a >= acc && a < acc + sweep) return i
            acc += sweep
        }
        return root.slices.length - 1
    }

    Accessible.role: Accessible.Chart
    Accessible.name: qsTr("模型 Token 分布环形图")
    Accessible.description: root.accessSummary
}