pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

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
//
// 批次七修复（真机反馈图例溢出卡片）：图例区重写。旧实现图例容器是
// 普通 Column（不管理子项宽度），delegate 的隐含宽 = 内部文本自然宽
// 之和，模型名 Text 的 Layout.preferredWidth 只是建议值不是上限，
// RowLayout 空间充足时按自然宽分配 → 长模型名（step-5-preview、
// deepseek-v4-flash-0731 等）整行溢出图例区与卡片边界。
// 新实现（与页面 Flickable 同构模式）：
//   - 图例区（Flickable）宽度有确定上限 200（极窄容器收缩至 120）；
//   - 图例行宽 Layout.fillWidth 跟随图例区实际宽度，不再由内容反向
//     撑开，模型名 fillWidth + minimumWidth: 0 允许收缩触发 elide；
//   - 百分比固定宽右对齐，任何模型名长度下始终可见；
//   - 模型数量多、图例内容高度超过可用高度时，图例区垂直滚动。
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
        // 布局不变式（任何模型名长度/数量组合都不溢出的保证）：
        //   图例区宽度 ∈ [120, 340]，高度 ≤ root.height；
        //   图例行宽 = 图例区实际宽度（fillWidth，非内容反向撑开）；
        //   行内 = 色块 10 + 模型名(可收缩至 0 触发 elide) + 百分比 44。
        Flickable {
            id: legendScroll
            // 宽度：占据环形图之外的剩余宽度（上限 340，防止宽窗口下图例
            // 抢走视觉重心）；极窄容器下最多收缩到 120（保住色块 + 最低
            // 模型名 + 百分比的最小可用组合）。
            // 用量统计页已改为「一张图一行」，图例从固定 200 放宽后，
            // deepseek-v4-flash-0731 / gemini-3.6-flash-high 这类长模型名
            // 不再被压成「deepseek-v4-f…」。
            Layout.fillWidth: true
            Layout.preferredWidth: 200
            Layout.maximumWidth: 340
            Layout.minimumWidth: 120
            // 高度：跟随图例内容、以组件可用高度为上限；配合 AlignVCenter
            // 保持「模型少时垂直居中」的原视觉，模型多时占满并滚动
            Layout.preferredHeight: legendColumn.implicitHeight
            Layout.maximumHeight: root.height
            Layout.alignment: Qt.AlignVCenter

            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: legendColumn.width
            contentHeight: legendColumn.height

            ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: legendColumn
                width: legendScroll.width   // 宽度跟随视口，不产生横向滚动
                spacing: Theme.spaceSm

                Repeater {
                    model: root.slices

                    delegate: Item {
                        id: legendRow
                        required property var modelData
                        required property int index
                        readonly property bool highlighted: legendRow.index === root.highlightIndex

                        // 行宽由 ColumnLayout fillWidth 分配（= 图例区
                        // 实际宽度）；高度由行内容隐含值决定
                        Layout.fillWidth: true
                        implicitHeight: legendRowLayout.implicitHeight

                        RowLayout {
                            id: legendRowLayout
                            // 只锚定水平两侧，高度由隐含值决定
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Theme.spaceSm

                            Rectangle {
                                Layout.preferredWidth: 10
                                Layout.preferredHeight: 10
                                radius: 2
                                color: root.seriesColor(legendRow.index)
                            }
                            Text {
                                // 模型名：填满剩余宽度；minimumWidth 0
                                // 允许收缩，宽度小于自然宽时 elide 生效
                                // （ElideRight，与 BarCompareChart 的
                                // x 轴标签截断风格统一）
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: legendRow.modelData.label
                                textFormat: Text.PlainText   // 模型名（远端可控）防富文本渲染
                                color: legendRow.highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }
                            Text {
                                // 百分比：固定宽右对齐，任何模型名长度下
                                // 始终可见；elide 仅为超大字号的兜底
                                Layout.preferredWidth: 44
                                Layout.minimumWidth: 0
                                horizontalAlignment: Text.AlignRight
                                text: root.percentText(legendRow.index)
                                color: legendRow.highlighted ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                elide: Text.ElideRight
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
        // 极窄容器下外半径可能为负（Canvas 负半径行为未定义），钳制到 0
        var outerR = Math.max(0, Math.min(chartArea.width, chartArea.height) / 2 - 8)
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