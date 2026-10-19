pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 用量统计页「请求明细」表格：筛选 + 排序 + 拖拽调列宽。
//
// 数据契约（由调用方注入，组件自身不查库、不翻页）：
//   rows:  [{ timestamp, model, inputTokens, outputTokens, cacheHitTokens,
//             latencyMs, costCents }, ...]  —— 已加载的一页或多页明细
//   total: 该范围内服务端命中的总条数（分页脚注用）
//   调用方响应 loadMore 信号继续追加（组件只发信号，不知道分页口径）。
//
// 为什么用 ListView 而不是 Repeater（docs/25 §9 s2）：明细「加载更多」
// 会累积数百行，Repeater 每次追加都全量重建 delegate；ListView 只创建
// 可见行，滚动区高度固定，页面本身不会被明细撑到几屏长。
//
// 布局：表头固定在滚动区外（纵向滚动时吸附），横向滚动由外层 Flickable
// 承载；表头与行共用同一份 columnWidths，因此任何列宽下都严格对齐。
ColumnLayout {
    id: root

    // ── 注入数据 ────────────────────────────────────
    property var rows: []
    property int total: 0
    // 表体最大高度（px）：超出后表体内部滚动，页面不再变长
    property int maxBodyHeight: 360

    signal loadMore()

    spacing: 8

    // ── 列定义（表头 / 数据行 / 拖拽手柄共用这一份口径）──
    // width 只是初始值：实际列宽在 columnWidths 里维护，可拖拽改。
    readonly property var columns: [
        { key: "timestamp", title: qsTr("时间"),     width: 132, align: Text.AlignLeft },
        { key: "model",     title: qsTr("模型"),     width: 200, align: Text.AlignLeft },
        { key: "total",     title: qsTr("总 Token"), width: 92,  align: Text.AlignRight },
        { key: "input",     title: qsTr("输入"),     width: 84,  align: Text.AlignRight },
        { key: "output",    title: qsTr("输出"),     width: 84,  align: Text.AlignRight },
        { key: "cache",     title: qsTr("缓存占比"), width: 86,  align: Text.AlignRight },
        { key: "latency",   title: qsTr("延迟"),     width: 76,  align: Text.AlignRight },
        { key: "cost",      title: qsTr("费用"),     width: 82,  align: Text.AlignRight }
    ]
    readonly property var defaultWidths: {
        var out = []
        for (var i = 0; i < root.columns.length; ++i)
            out.push(root.columns[i].width)
        return out
    }
    // 整体重新赋值（而非原地改元素）才能触发绑定刷新——QML 的 var 数组
    // 元素赋值不发通知。
    property var columnWidths: root.defaultWidths

    readonly property real tableWidth: {
        var sum = 0
        for (var i = 0; i < root.columnWidths.length; ++i)
            sum += Number(root.columnWidths[i] || 0)
        return sum
    }
    readonly property int rowHeight: 30
    readonly property int headerHeight: 32
    readonly property int minColumnWidth: 56
    readonly property int maxColumnWidth: 420
    // 横向滚动条预留带：滚动条吸附在视口底部，预留后不压住最后一行
    readonly property int hbarSpace: 16

    // ── 筛选 / 排序状态 ─────────────────────────────
    property string keyword: ""
    property string modelFilter: ""          // 空 = 全部模型
    property string sortKey: "timestamp"     // 默认按时间倒序（最新请求在前）
    property bool sortAsc: false

    // 下拉选项：按出现顺序（= 时间倒序）去重，不重排——重排会让已选中的
    // 索引指向别的模型（追加数据时列表会变长）。
    readonly property var modelOptions: {
        var seen = ({})
        var out = []
        for (var i = 0; i < root.rows.length; ++i) {
            var name = root.modelText(root.rows[i])
            if (!seen[name]) {
                seen[name] = true
                out.push(name)
            }
        }
        return out
    }
    readonly property var modelChoices: [qsTr("全部模型")].concat(root.modelOptions)

    // 筛选 + 排序后的行（只对已加载的行生效：数据在 DB 侧分页，
    // 筛选是本地视图层的即时过滤，脚注会同时给出「筛选后」条数）
    readonly property var viewRows: {
        var kw = root.keyword.trim().toLowerCase()
        var mf = root.modelFilter
        var out = []
        for (var i = 0; i < root.rows.length; ++i) {
            var r = root.rows[i]
            if (!r) continue
            var name = root.modelText(r)
            if (mf !== "" && name !== mf) continue
            if (kw !== "") {
                var hay = (name + " " + root.timeText(r)).toLowerCase()
                if (hay.indexOf(kw) < 0) continue
            }
            out.push(r)
        }
        if (out.length > 1) {
            var key = root.sortKey
            var asc = root.sortAsc
            out.sort(function (a, b) {
                var va = root.sortValue(a, key)
                var vb = root.sortValue(b, key)
                if (va < vb) return asc ? -1 : 1
                if (va > vb) return asc ? 1 : -1
                return 0
            })
        }
        return out
    }

    readonly property int bodyHeight: root.viewRows.length === 0
                                      ? 56
                                      : Math.min(root.viewRows.length * root.rowHeight,
                                                 root.maxBodyHeight)

    readonly property bool columnsAdjusted: {
        for (var i = 0; i < root.defaultWidths.length; ++i) {
            if (Number(root.columnWidths[i]) !== Number(root.defaultWidths[i]))
                return true
        }
        return false
    }

    readonly property string summaryText: {
        var shown = root.rows.length
        var filtered = root.viewRows.length
        var text = qsTr("已加载 %1 / 共 %2 条")
                .arg(root.fmtNumber(shown)).arg(root.fmtNumber(root.total))
        if (filtered !== shown)
            text += qsTr(" · 筛选后 %1 条").arg(root.fmtNumber(filtered))
        return text
    }

    // ── 单元格取值 ──────────────────────────────────
    function fmtNumber(n) {
        return Number(n || 0).toLocaleString(Qt.locale(), 'f', 0)
    }
    function fmtCost(costCents) {
        return qsTr("¥%1").arg((Number(costCents || 0) / 100.0).toFixed(2))
    }
    function fmtLatency(ms) {
        if (!ms || ms <= 0) return qsTr("—")
        if (ms < 1000) return qsTr("%1 ms").arg(ms)
        return qsTr("%1 s").arg((ms / 1000).toFixed(1))
    }
    function fmtCacheRatio(hit, input) {
        if (Number(input || 0) <= 0) return qsTr("—")
        return qsTr("%1%").arg((Number(hit || 0) / Number(input) * 100).toFixed(1))
    }
    function timeText(r) {
        return r && r.timestamp
            ? Theme.formatDateTime(new Date(Number(r.timestamp)))
            : qsTr("—")
    }
    function modelText(r) {
        var m = r ? r.model : ""
        return (m && String(m).length > 0) ? String(m) : qsTr("(未知)")
    }
    function cellText(r, key) {
        if (!r) return qsTr("—")
        switch (key) {
        case "timestamp": return root.timeText(r)
        case "model":     return root.modelText(r)
        case "total":     return root.fmtNumber(Number(r.inputTokens || 0)
                                                + Number(r.outputTokens || 0))
        case "input":     return root.fmtNumber(r.inputTokens)
        case "output":    return root.fmtNumber(r.outputTokens)
        case "cache":     return root.fmtCacheRatio(r.cacheHitTokens, r.inputTokens)
        case "latency":   return root.fmtLatency(r.latencyMs)
        case "cost":      return root.fmtCost(r.costCents)
        }
        return qsTr("—")
    }
    function sortValue(r, key) {
        switch (key) {
        case "model":   return root.modelText(r).toLowerCase()
        case "total":   return Number(r.inputTokens || 0) + Number(r.outputTokens || 0)
        case "input":   return Number(r.inputTokens || 0)
        case "output":  return Number(r.outputTokens || 0)
        case "cache":   return Number(r.inputTokens || 0) > 0
                              ? Number(r.cacheHitTokens || 0) / Number(r.inputTokens) : 0
        case "latency": return Number(r.latencyMs || 0)
        case "cost":    return Number(r.costCents || 0)
        }
        return Number(r.timestamp || 0)
    }

    // ── 排序 / 列宽 ─────────────────────────────────
    function toggleSort(key) {
        if (root.sortKey === key) {
            root.sortAsc = !root.sortAsc
            return
        }
        root.sortKey = key
        // 模型名按 A→Z 起手，数值/时间按「从大到新」起手更符合查看习惯
        root.sortAsc = (key === "model")
    }
    function setColumnWidth(index, width) {
        if (index < 0 || index >= root.columnWidths.length) return
        var w = Math.round(Math.max(root.minColumnWidth,
                                    Math.min(root.maxColumnWidth, width)))
        if (w === Number(root.columnWidths[index])) return
        var next = root.columnWidths.slice()
        next[index] = w
        root.columnWidths = next
    }
    function resetColumns() {
        root.columnWidths = root.defaultWidths.slice()
    }
    function resetFilters() {
        root.keyword = ""
        root.modelFilter = ""
        root.sortKey = "timestamp"
        root.sortAsc = false
        keywordField.text = ""
        modelCombo.currentIndex = 0
    }

    // ── 筛选栏 ──────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        TextField {
            id: keywordField
            Layout.fillWidth: true
            Layout.minimumWidth: 110
            placeholderText: qsTr("搜索模型 / 时间…")
            placeholderTextColor: Theme.textSecondary
            color: Theme.textPrimary
            selectionColor: Theme.brandPrimary
            selectedTextColor: Theme.selectedTextColor
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            background: Rectangle {
                radius: 6
                color: Theme.bgPrimary
                border.width: keywordField.activeFocus ? 2 : 1
                border.color: keywordField.activeFocus ? Theme.brandPrimary
                                                       : Theme.borderSubtle
            }
            onTextChanged: root.keyword = text
            Accessible.name: qsTr("搜索请求明细（模型或时间）")
        }

        ComboBox {
            id: modelCombo
            Layout.preferredWidth: 146
            model: root.modelChoices
            // 与 modelFilter 单向同步：用户选择后绑定失效，由 resetFilters
            // 显式回写（否则「重置」后下拉还停在旧选项上）
            currentIndex: Math.max(0, root.modelChoices.indexOf(
                root.modelFilter === "" ? qsTr("全部模型") : root.modelFilter))
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            onActivated: (index) => {
                root.modelFilter = index <= 0 ? "" : String(root.modelChoices[index])
            }
            Accessible.name: qsTr("按模型筛选请求明细")
        }

        AppButton {
            text: qsTr("重置")
            enabled: root.keyword !== "" || root.modelFilter !== ""
                     || root.sortKey !== "timestamp" || root.sortAsc
            onClicked: root.resetFilters()
            Accessible.name: qsTr("重置筛选与排序")
        }
    }

    // ── 表格 ────────────────────────────────────────
    // 结构：外层横向 Flickable（contentWidth = 列宽总和）只负责横向滚动；
    // 表头与表体都保持「视口宽」，由内部内容整体左移 -contentX 完成平移。
    // 这样纵向滚动条始终钉在视口右缘——若让表体宽度等于列宽总和，它的
    // 滚动条会被横向滚动推出屏幕，长表格就看不到纵向位置。
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: root.headerHeight + root.bodyHeight + root.hbarSpace + 12
        radius: 8
        color: Theme.bgPrimary
        border.width: 1
        border.color: Theme.borderSubtle

        Flickable {
            id: hscroll
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            contentWidth: Math.max(width, root.tableWidth)
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

            Column {
                id: tableCol
                width: hscroll.width
                spacing: 0

                // 表头：点击排序，拖动列边界改列宽，双击边界恢复该列默认宽
                Item {
                    width: tableCol.width
                    height: root.headerHeight

                    Row {
                        // 表头行按列宽总和排布，随横向滚动整体平移
                        width: root.tableWidth
                        height: parent.height
                        x: -hscroll.contentX

                        Repeater {
                            model: root.columns

                            delegate: Item {
                                id: headCell
                                required property var modelData
                                required property int index
                                readonly property bool sorted: root.sortKey === headCell.modelData.key

                                width: root.columnWidths[index]
                                height: root.headerHeight

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: headCell.modelData.title
                                    horizontalAlignment: headCell.modelData.align
                                    elide: Text.ElideRight
                                    color: headCell.sorted ? Theme.textPrimary
                                                           : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.bold: true
                                    font.family: Theme.fontSans
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: headCell.sorted ? (root.sortAsc ? "▲" : "▼") : ""
                                    color: Theme.brandPrimary
                                    font.pixelSize: 9
                                    font.family: Theme.fontSans
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.rightMargin: 8
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.toggleSort(headCell.modelData.key)
                                }
                                // 列宽拖拽手柄（压在排序点击区之上，8px 宽，
                                // 命中面比视觉宽，便于鼠标抓取）
                                Rectangle {
                                    id: grip
                                    anchors.right: parent.right
                                    width: 8
                                    height: parent.height
                                    color: gripArea.pressed || gripArea.containsMouse
                                           ? Theme.brandPrimary : "transparent"
                                    opacity: 0.45

                                    MouseArea {
                                        id: gripArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.SplitHCursor
                                        property real pressSceneX: 0
                                        property real pressWidth: 0

                                        onPressed: (mouse) => {
                                            gripArea.pressSceneX =
                                                gripArea.mapToItem(null, mouse.x, mouse.y).x
                                            gripArea.pressWidth =
                                                Number(root.columnWidths[headCell.index])
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!gripArea.pressed) return
                                            var dx = gripArea.mapToItem(null, mouse.x, mouse.y).x
                                                     - gripArea.pressSceneX
                                            root.setColumnWidth(headCell.index,
                                                                gripArea.pressWidth + dx)
                                        }
                                        onDoubleClicked: root.setColumnWidth(
                                            headCell.index,
                                            Number(root.defaultWidths[headCell.index]))
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.borderSubtle
                    }
                }

                ListView {
                    id: body
                    width: tableCol.width
                    height: root.viewRows.length === 0 ? 0 : root.bodyHeight
                    clip: true
                    model: root.viewRows
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Accessible.role: Accessible.Table
                    Accessible.name: qsTr("请求明细表格")

                    delegate: Item {
                        id: rowItem
                        required property var modelData
                        required property int index

                        width: body.width
                        height: root.rowHeight

                        Rectangle {
                            anchors.fill: parent
                            color: rowHover.containsMouse
                                   ? Theme.surfaceHover
                                   : (rowItem.index % 2 === 1 ? Theme.bgSecondary
                                                              : "transparent")
                        }

                        Row {
                            width: root.tableWidth
                            height: parent.height
                            x: -hscroll.contentX

                            Repeater {
                                model: root.columns

                                delegate: Item {
                                    id: dataCell
                                    required property var modelData
                                    required property int index

                                    width: root.columnWidths[index]
                                    height: rowItem.height

                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: dataCell.modelData.align
                                        text: root.cellText(rowItem.modelData,
                                                            dataCell.modelData.key)
                                        // 模型名来自网关响应（远端可控），强制纯文本
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        color: dataCell.modelData.key === "model"
                                               ? Theme.textPrimary : Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: dataCell.modelData.key === "model"
                                                     ? Theme.fontSans : Theme.fontMono
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.borderSubtle
                            opacity: 0.5
                        }

                        MouseArea {
                            id: rowHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton   // 只取 hover，不拦截点击
                        }
                    }
                }

                // 空态：整页无数据 vs 筛选后无匹配，文案区分（避免误判数据丢失）
                Text {
                    width: tableCol.width
                    height: root.viewRows.length === 0 ? 56 : 0
                    visible: root.viewRows.length === 0
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                    text: root.rows.length === 0 ? qsTr("该范围内暂无数据")
                                                 : qsTr("没有匹配的请求")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    opacity: 0.7
                }
            }
        }
    }

    // ── 脚注：加载进度 + 列宽复位 + 加载更多 ─────────
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: root.summaryText
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            elide: Text.ElideRight
        }
        AppButton {
            visible: root.columnsAdjusted
            text: qsTr("重置列宽")
            onClicked: root.resetColumns()
            Accessible.name: qsTr("恢复默认列宽")
        }
        AppButton {
            visible: root.rows.length < root.total
            text: qsTr("加载更多")
            onClicked: root.loadMore()
            Accessible.name: qsTr("加载更多请求明细")
        }
    }
}
