import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Unified-diff renderer for the review views. The diff text is produced at
// write time / by git and already size-capped upstream, so this component only
// parses and presents it:
//   - unified mode: old/new line-number gutters + colored add/del/context rows
//   - split mode:   paired old|new columns (deletions align with the additions
//                   that replaced them; unmatched lines keep one side blank)
// Hunk and file-header rows span the full width in both modes.
Item {
    id: root

    property string diffText: ""
    property bool diffTruncated: false
    property string emptyText: qsTr("此记录没有保存内容差异。")
    // "unified" | "split"
    property string viewMode: "unified"

    readonly property var parsed: root.parseDiff(diffText)
    readonly property var splitRows: root.buildSplitRows(parsed)
    readonly property var renderRows: viewMode === "split" ? splitRows : parsed
    readonly property bool hasDiff: parsed.length > 0
    readonly property alias renderedCount: lineList.count
    readonly property int addedCount: root.countKind(parsed, "add")
    readonly property int removedCount: root.countKind(parsed, "del")
    // Line-number gutters cost 74px in unified mode and 88px in split
    // mode. The review tab hosts this view inline, where the pane can be
    // as narrow as ~180px - below that the gutters would leave more room
    // for nothing than for code, so they fold away and the text takes the
    // full width.
    readonly property bool showGutters: width >= 240

    // ── Split-column geometry ───────────────────────────
    // The old|new divide used to be a hard 50% of the row width, which is
    // wrong whenever the two sides differ: a one-word edit beside a
    // rewritten paragraph gets half the space each, so the long side wraps
    // into a staircase while the short side sits on whitespace.
    //
    // `splitRatio` is the share of the row the OLD (left) column takes. It
    // starts at the content-aware `autoSplitRatio` and the user can drag the
    // seam; once dragged, `splitTouched` latches so a later file does not
    // silently undo the choice. Because it is a ratio (not pixels) the
    // chosen divide survives the dialog being resized.
    property real splitRatio: 0.5
    property bool splitTouched: false

    // Widest text either column must be able to show before it wraps. Keeps
    // a dragged seam from collapsing a column into a sliver.
    readonly property int minColumnText: 110

    // Content-aware default: monospace, so character count IS width. Give
    // each side a share proportional to its longest line, but only when the
    // two together do not already fit — when they do, 50/50 wastes nothing
    // and reads as the neutral default.
    readonly property real autoSplitRatio: {
        if (viewMode !== "split") return 0.5
        let leftNeed = 0
        let rightNeed = 0
        const rows = root.splitRows
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i]
            if (row.kind !== "pair") continue
            const l = row.oldText ? row.oldText.length : 0
            const r = row.newText ? row.newText.length : 0
            if (l > leftNeed) leftNeed = l
            if (r > rightNeed) rightNeed = r
        }
        const total = leftNeed + rightNeed
        if (total <= 0) return 0.5
        // Rough capacity in characters for the current width (caption-size
        // monospace is ~6.2px per char); both gutters included.
        const capacity = Math.max(20, (width - 90) / 6.2)
        if (total <= capacity) return 0.5
        return Math.max(0.25, Math.min(0.75, leftNeed / total))
    }

    // The seam's x, clamped so neither column drops below minColumnText.
    // Single source of truth for every split-mode offset below.
    function splitXFor(rowWidth) {
        const gutters = root.showGutters
        const leftPad = gutters ? 40 : 4
        const rightPad = gutters ? 41 : 4
        const lo = leftPad + root.minColumnText
        const hi = Math.max(lo + 1, rowWidth - rightPad - root.minColumnText - 4)
        const want = leftPad + (rowWidth - leftPad - rightPad - 4) * root.splitRatio
        return Math.max(lo, Math.min(hi, want))
    }

    // Adopt the content-aware divide for a newly shown diff, unless the
    // user has already dragged the seam (their choice outranks the guess).
    function resetSplitToAuto() {
        if (root.splitTouched) return
        root.splitRatio = root.autoSplitRatio
    }

    // Drag handler for the seam. The delta arrives in pixels; converting it
    // to a ratio keeps the divide proportional, so the same relative split
    // survives the dialog (and therefore this view) being resized. Clamped
    // through splitXFor's own limits by construction: the ratio is derived
    // from the same padded span the clamp uses.
    function dragSplit(deltaPx) {
        if (root.viewMode !== "split") return
        const rowWidth = lineList.width
        if (rowWidth <= 0) return
        const gutters = root.showGutters
        const leftPad = gutters ? 40 : 4
        const rightPad = gutters ? 41 : 4
        const span = rowWidth - leftPad - rightPad - 4
        if (span <= 0) return
        root.splitTouched = true
        root.splitRatio = Math.max(0, Math.min(1,
            root.splitRatio + deltaPx / span))
    }

    onAutoSplitRatioChanged: root.resetSplitToAuto()
    onDiffTextChanged: root.resetSplitToAuto()

    // ── Parsing ────────────────────────────────────────
    // Returns rows {kind, text, oldNo, newNo}: kind is meta (---/+++), hunk
    // (@@), note (\ No newline), context, add or del. Line numbers start in
    // each hunk; a 0 number renders as a blank gutter cell.
    function parseDiff(text) {
        const rows = []
        if (!text || text.length === 0) return rows
        const lines = text.split("\n")
        if (lines.length > 0 && lines[lines.length - 1] === "") lines.pop()

        // Constructed with new RegExp on purpose: a regex literal in a function
        // that also contains a loop makes qmllint 6.9.0 grow without bound.
        const hunkRe = new RegExp("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@")
        let oldNo = 0
        let newNo = 0
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i]
            if (line.indexOf("@@") === 0) {
                const match = hunkRe.exec(line)
                if (match) {
                    oldNo = parseInt(match[1], 10)
                    newNo = parseInt(match[3], 10)
                }
                rows.push({ "kind": "hunk", "text": line, "oldNo": 0, "newNo": 0 })
            } else if (line.indexOf("+++") === 0 || line.indexOf("---") === 0) {
                rows.push({ "kind": "meta", "text": line, "oldNo": 0, "newNo": 0 })
            } else if (line.indexOf("+") === 0) {
                rows.push({ "kind": "add", "text": line.substring(1),
                            "oldNo": 0, "newNo": newNo })
                newNo += 1
            } else if (line.indexOf("-") === 0) {
                rows.push({ "kind": "del", "text": line.substring(1),
                            "oldNo": oldNo, "newNo": 0 })
                oldNo += 1
            } else if (line.indexOf("\\") === 0) {
                rows.push({ "kind": "note", "text": line, "oldNo": 0, "newNo": 0 })
            } else {
                const body = line.indexOf(" ") === 0 ? line.substring(1) : line
                rows.push({ "kind": "context", "text": body,
                            "oldNo": oldNo, "newNo": newNo })
                oldNo += 1
                newNo += 1
            }
        }
        return rows
    }

    // Pairs each run of deletions with the run of additions that follows it,
    // so a rewritten block reads side by side instead of two separate blocks.
    function buildSplitRows(parsedRows) {
        const out = []
        let i = 0
        const count = parsedRows.length
        while (i < count) {
            const row = parsedRows[i]
            if (row.kind === "del") {
                const dels = []
                while (i < count && parsedRows[i].kind === "del") {
                    dels.push(parsedRows[i])
                    i += 1
                }
                const adds = []
                while (i < count && parsedRows[i].kind === "add") {
                    adds.push(parsedRows[i])
                    i += 1
                }
                const pairs = Math.max(dels.length, adds.length)
                for (let k = 0; k < pairs; ++k) {
                    out.push({
                        "kind": "pair",
                        "oldNo": k < dels.length ? dels[k].oldNo : 0,
                        "oldText": k < dels.length ? dels[k].text : "",
                        "newNo": k < adds.length ? adds[k].newNo : 0,
                        "newText": k < adds.length ? adds[k].text : "",
                        "del": k < dels.length,
                        "add": k < adds.length
                    })
                }
            } else if (row.kind === "add") {
                while (i < count && parsedRows[i].kind === "add") {
                    out.push({ "kind": "pair", "oldNo": 0, "oldText": "",
                               "newNo": parsedRows[i].newNo,
                               "newText": parsedRows[i].text,
                               "del": false, "add": true })
                    i += 1
                }
            } else if (row.kind === "context") {
                out.push({ "kind": "pair", "oldNo": row.oldNo,
                           "oldText": row.text, "newNo": row.newNo,
                           "newText": row.text, "del": false, "add": false })
                i += 1
            } else {
                out.push(row)
                i += 1
            }
        }
        return out
    }

    function countKind(rows, kind) {
        let n = 0
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].kind === kind) n += 1
        }
        return n
    }

    // ── Colors ─────────────────────────────────────────
    function isPlain(kind) {
        return kind === "hunk" || kind === "meta" || kind === "note"
    }

    function kindColor(kind) {
        if (kind === "add") return Theme.statusSuccess
        if (kind === "del") return Theme.statusDanger
        if (kind === "hunk") return Theme.brandPrimary
        if (kind === "meta" || kind === "note") return Theme.textSecondary
        return Theme.textPrimary
    }

    function kindBackground(kind) {
        const alpha = Theme.isDark ? 0.18 : 0.10
        if (kind === "add")
            return Qt.rgba(Theme.statusSuccess.r, Theme.statusSuccess.g,
                           Theme.statusSuccess.b, alpha)
        if (kind === "del")
            return Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                           Theme.statusDanger.b, alpha)
        return "transparent"
    }

    // Kept for callers/tests that classify raw diff lines.
    function isFileHeader(line) {
        return line.startsWith("+++") || line.startsWith("---")
    }

    function lineColor(line) {
        if (isFileHeader(line)) return Theme.textSecondary
        if (line.startsWith("+")) return Theme.statusSuccess
        if (line.startsWith("-")) return Theme.statusDanger
        if (line.startsWith("@@")) return Theme.brandPrimary
        return Theme.textPrimary
    }

    function lineBackground(line) {
        if (isFileHeader(line) || line.startsWith("@@")) return "transparent"
        const alpha = Theme.isDark ? 0.18 : 0.10
        if (line.startsWith("+"))
            return Qt.rgba(Theme.statusSuccess.r, Theme.statusSuccess.g,
                           Theme.statusSuccess.b, alpha)
        if (line.startsWith("-"))
            return Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                           Theme.statusDanger.b, alpha)
        return "transparent"
    }

    Column {
        anchors.fill: parent
        spacing: 4

        Rectangle {
            width: parent.width
            height: root.hasDiff
                ? parent.height - (root.diffTruncated ? 22 : 0) : 0
            visible: root.hasDiff
            radius: 6
            color: Theme.codeBackground
            border.color: Theme.borderSubtle
            clip: true

            ListView {
                id: lineList
                objectName: "diffView"
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.renderRows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: diffRow
                    required property var modelData

                    readonly property bool pairRow: modelData.kind === "pair"
                    readonly property bool plain: root.isPlain(modelData.kind)
                    // The seam position is shared by every row (and by the
                    // drag handle), so the columns line up down the whole
                    // diff instead of drifting per row.
                    readonly property real seamX: root.splitXFor(diffRow.width)
                    // Gutter-aware column geometry (see root.showGutters).
                    // The right column starts past its own line-number
                    // gutter, which sits at seamX+5 and is 32 wide — so the
                    // body begins at seamX+41, mirroring the left column's
                    // 40px gutter. Getting this wrong overlaps the numbers.
                    readonly property bool gutters: root.showGutters
                    readonly property int bodyX: gutters ? 78 : 4
                    readonly property int leftX: gutters ? 40 : 4
                    readonly property int rightX: diffRow.seamX + (gutters ? 41 : 4)

                    objectName: pairRow ? "diffSplitRow" : "diffLine"
                    width: ListView.view ? ListView.view.width : 0
                    height: pairRow
                        ? Math.max(16, Math.max(leftBody.implicitHeight,
                                                rightBody.implicitHeight) + 4)
                        : Math.max(16, fullBody.implicitHeight + 2)

                    // Row backgrounds: unified uses one band; split tints the
                    // old/new halves independently.
                    Rectangle {
                        anchors.fill: parent
                        visible: !diffRow.pairRow
                        color: root.kindBackground(modelData.kind)
                    }
                    Rectangle {
                        visible: diffRow.pairRow
                        width: Math.max(0, diffRow.seamX - 1)
                        height: parent.height
                        color: modelData.del
                            ? root.kindBackground("del") : "transparent"
                    }
                    Rectangle {
                        visible: diffRow.pairRow
                        x: diffRow.seamX + 1
                        width: parent.width - x
                        height: parent.height
                        color: modelData.add
                            ? root.kindBackground("add") : "transparent"
                    }
                    Rectangle {
                        visible: diffRow.pairRow
                        x: diffRow.seamX
                        width: 1
                        height: parent.height
                        color: Theme.borderSubtle
                    }

                    // ── Unified / plain rows ────────────
                    Text {
                        visible: !diffRow.pairRow && !diffRow.plain
                                 && diffRow.gutters
                        x: 4
                        width: 32
                        text: modelData.oldNo > 0 ? String(modelData.oldNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        visible: !diffRow.pairRow && !diffRow.plain
                                 && diffRow.gutters
                        x: 40
                        width: 32
                        text: modelData.newNo > 0 ? String(modelData.newNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        id: fullBody
                        visible: !diffRow.pairRow
                        x: diffRow.plain ? 4 : diffRow.bodyX
                        width: Math.max(0, parent.width - x - 4)
                        text: modelData.text !== undefined && modelData.text.length > 0
                            ? modelData.text : " "
                        color: root.kindColor(modelData.kind)
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }

                    // ── Split rows ──────────────────────
                    Text {
                        visible: diffRow.pairRow && diffRow.gutters
                        x: 4
                        width: 32
                        text: diffRow.pairRow && modelData.oldNo > 0
                            ? String(modelData.oldNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        id: leftBody
                        visible: diffRow.pairRow
                        x: diffRow.leftX
                        width: Math.max(0, diffRow.seamX - 4 - diffRow.x)
                        text: diffRow.pairRow && modelData.oldText.length > 0
                            ? modelData.oldText : " "
                        color: modelData.del ? Theme.statusDanger : Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }

                    Text {
                        visible: diffRow.pairRow && diffRow.gutters
                        x: diffRow.seamX + 5
                        width: 32
                        text: diffRow.pairRow && modelData.newNo > 0
                            ? String(modelData.newNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        id: rightBody
                        visible: diffRow.pairRow
                        x: diffRow.rightX
                        width: Math.max(0, parent.width - x - 4)
                        text: diffRow.pairRow && modelData.newText.length > 0
                            ? modelData.newText : " "
                        color: modelData.add ? Theme.statusSuccess : Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                }
            }
            // ── Split seam drag handle ─────────────────
            // One handle for the whole list, not one per row: the per-row
            // 1px seams above draw the continuous line, this is the only
            // thing that moves it. Deltas are measured in scene coordinates
            // for the same reason SplitBar does it — the handle travels with
            // the seam, so local deltas would feed back into the layout and
            // make the seam jitter around the cursor.
            //
            // The ListView is inset 4px inside this box, so the row-space
            // seam has to be offset by the same amount to land on the line.
            Item {
                id: splitHandle
                objectName: "diffSplitHandle"
                visible: root.viewMode === "split" && root.hasDiff
                x: 4 + root.splitXFor(lineList.width) - width / 2
                y: 0
                width: 11
                height: parent.height

                readonly property bool active: dragArea.pressed
                readonly property bool hot: dragArea.containsMouse || splitHandle.active

                // Grip: invisible at rest so it never fights the diff, an
                // accent bar while hovered or dragged.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: splitHandle.hot ? 3 : 1
                    color: splitHandle.hot ? Theme.brandPrimary : Theme.borderSubtle
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor

                    property real lastSceneX: 0
                    onPressed: (mouse) => {
                        lastSceneX = mapToItem(null, mouse.x, mouse.y).x
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        const sceneX = mapToItem(null, mouse.x, mouse.y).x
                        const delta = sceneX - lastSceneX
                        lastSceneX = sceneX
                        root.dragSplit(delta)
                    }
                    onDoubleClicked: (mouse) => {
                        root.splitTouched = false
                        root.splitRatio = root.autoSplitRatio
                    }
                }

                ToolTip.visible: dragArea.containsMouse && !splitHandle.active
                ToolTip.delay: 600
                ToolTip.text: qsTr("拖动调节两栏宽度 · 双击按内容自适应")

                Accessible.role: Accessible.Separator
                Accessible.name: qsTr("改动前后两栏分隔条")
                Accessible.description: qsTr("拖动调节原内容与改动后内容的宽度，双击按内容自适应")
            }
        }

        Text {
            width: parent.width
            visible: !root.hasDiff
            text: root.emptyText
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            topPadding: 12
        }

        Text {
            width: parent.width
            visible: root.diffTruncated
            height: visible ? implicitHeight : 0
            text: qsTr("差异过大，仅显示截断后的部分；撤销仍使用完整的改动前内容。")
            color: Theme.statusWarning
            font.family: Theme.fontSans
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("差异内容")
}
