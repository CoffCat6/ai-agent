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
    // Line-number gutters cost 72px in unified mode and 40px in split mode
    // (see the geometry block below). The review tab hosts this view inline,
    // where the pane can be as narrow as ~180px - below that the gutters
    // would leave more room for nothing than for code, so they fold away and
    // the text takes the full width.
    readonly property bool showGutters: width >= 240

    // ── Geometry: one place, no magic numbers ───────────
    // Every split-mode offset in this file is derived from the constants
    // below. They used to be literals repeated in splitXFor(), dragSplit()
    // and each delegate child, which is exactly how the left column came to
    // be measured from the row instead of from its own left edge and painted
    // 36px over the new column: two places agreed on "40" and one did not.
    //
    // So: no number below is allowed to reappear in the delegate. Ask the
    // geometry properties/functions instead.

    // Gap between the diff box edge and the content, on both sides. The
    // ListView is inset by this much, so row space and the seam handle's
    // space differ by exactly this.
    readonly property int contentInset: 4
    // Width of one line-number gutter cell.
    readonly property int gutterWidth: 32
    // Gap between a gutter cell and the text that follows it.
    readonly property int gutterGap: 4
    // Width of the seam rule itself, centred on the seam's x.
    readonly property int seamWidth: 1

    // Left edge of the OLD column's text. Without gutters the text starts at
    // the content inset; with them it starts past the old line-number cell.
    readonly property int oldBodyX: showGutters
        ? contentInset + gutterWidth + gutterGap
        : contentInset
    // Left edge of the NEW column's text, relative to the seam. Measured
    // from the seam rather than from the row so it cannot drift when the
    // seam moves.
    readonly property int newBodyOffset: showGutters
        ? gutterGap + gutterWidth + gutterGap
        : contentInset
    // Where the unified-mode body text starts: past both gutter cells.
    readonly property int unifiedBodyX: showGutters
        ? contentInset + 2 * (gutterWidth + gutterGap)
        : contentInset
    // Right edge of the OLD column's text, relative to the seam: leave the
    // seam plus half its width so the rule never sits on a glyph.
    readonly property int seamToTextGap: contentInset

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

    // Pixels the two columns' text may share, i.e. the row width minus
    // everything that is not text. Single definition of "the span" for the
    // clamp, the drag and the content-aware ratio.
    function textSpanFor(rowWidth) {
        const left = root.oldBodyX
        const right = root.contentInset
        return Math.max(0, rowWidth - left - right)
    }

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
        // monospace is ~6.2px per char).
        const capacity = Math.max(20, root.textSpanFor(width) / 6.2)
        if (total <= capacity) return 0.5
        return Math.max(0.25, Math.min(0.75, leftNeed / total))
    }

    // Width of the OLD column's text box for a given seam. The delegate must
    // use this instead of subtracting offsets by hand: it is the one
    // expression that knows the text starts at oldBodyX.
    function oldBodyWidthFor(rowWidth, seamX) {
        return Math.max(0, seamX - root.seamToTextGap - root.oldBodyX)
    }

    // Left edge and width of the NEW column's text box, in row coordinates.
    function newBodyXFor(seamX) {
        return seamX + root.newBodyOffset
    }

    function newBodyWidthFor(rowWidth, seamX) {
        return Math.max(0, rowWidth - root.newBodyXFor(seamX) - root.contentInset)
    }

    // Smallest/largest seam x that still leaves minColumnText of text on both
    // sides. Derived from the body-width functions above rather than from
    // offsets, so a change to the gutter or inset cannot desync the clamp
    // from the boxes it is protecting.
    //
    // The gap is part of the old body's span: putting the seam at
    // oldBodyX + minColumnText without it left the text at minColumnText-4,
    // i.e. the clamp promised a width it did not deliver.
    function minSeamXFor() {
        return root.oldBodyX + root.seamToTextGap + root.minColumnText
    }

    // Largest seam x: past it the new body would be narrower than
    // minColumnText. Expressed through newBodyOffset / contentInset for the
    // same reason as the lower limit.
    function maxSeamXFor(rowWidth) {
        const hi = rowWidth - root.newBodyOffset - root.contentInset - root.minColumnText
        const lo = root.minSeamXFor()
        return Math.max(lo + 1, hi)
    }

    // The seam's x, clamped so neither column drops below minColumnText.
    // Single source of truth for every split-mode offset below.
    function splitXFor(rowWidth) {
        const want = root.oldBodyX + root.textSpanFor(rowWidth) * root.splitRatio
        return Math.max(root.minSeamXFor(),
                        Math.min(root.maxSeamXFor(rowWidth), want))
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
    // from the same text span the clamp uses.
    function dragSplit(deltaPx) {
        if (root.viewMode !== "split") return
        const rowWidth = lineList.width
        if (rowWidth <= 0) return
        const span = root.textSpanFor(rowWidth)
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
                ScrollBar.vertical: AppScrollBar {}

                delegate: Item {
                    id: diffRow
                    required property var modelData

                    readonly property bool pairRow: modelData.kind === "pair"
                    readonly property bool plain: root.isPlain(modelData.kind)
                    // The seam position is shared by every row (and by the
                    // drag handle), so the columns line up down the whole
                    // diff instead of drifting per row.
                    readonly property real seamX: root.splitXFor(diffRow.width)
                    // Column geometry comes from root, never from literals:
                    // the old body's width in particular has to be measured
                    // from oldBodyX, not from the row's left edge.
                    readonly property bool gutters: root.showGutters
                    readonly property int bodyX: root.unifiedBodyX
                    readonly property int leftX: root.oldBodyX
                    readonly property int rightX: root.newBodyXFor(diffRow.seamX)

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
                        width: Math.max(0, diffRow.seamX - root.seamWidth)
                        height: parent.height
                        color: modelData.del
                            ? root.kindBackground("del") : "transparent"
                    }
                    Rectangle {
                        visible: diffRow.pairRow
                        x: diffRow.seamX + root.seamWidth
                        width: parent.width - x
                        height: parent.height
                        color: modelData.add
                            ? root.kindBackground("add") : "transparent"
                    }
                    Rectangle {
                        visible: diffRow.pairRow
                        x: diffRow.seamX
                        width: root.seamWidth
                        height: parent.height
                        color: Theme.borderSubtle
                    }

                    // ── Unified / plain rows ────────────
                    Text {
                        visible: !diffRow.pairRow && !diffRow.plain
                                 && diffRow.gutters
                        x: root.contentInset
                        width: root.gutterWidth
                        text: modelData.oldNo > 0 ? String(modelData.oldNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMicro
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        visible: !diffRow.pairRow && !diffRow.plain
                                 && diffRow.gutters
                        x: root.contentInset + root.gutterWidth + root.gutterGap
                        width: root.gutterWidth
                        text: modelData.newNo > 0 ? String(modelData.newNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMicro
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        id: fullBody
                        visible: !diffRow.pairRow
                        x: diffRow.plain ? root.contentInset : diffRow.bodyX
                        width: Math.max(0, parent.width - x - root.contentInset)
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
                        x: root.contentInset
                        width: root.gutterWidth
                        text: diffRow.pairRow && modelData.oldNo > 0
                            ? String(modelData.oldNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMicro
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        id: leftBody
                        objectName: "diffSplitOldText"
                        visible: diffRow.pairRow
                        x: diffRow.leftX
                        // Measured by the geometry function, which knows the
                        // text starts at oldBodyX. Hand-subtracting offsets
                        // here is what once let this column run 36px past the
                        // seam and paint over the new one.
                        width: root.oldBodyWidthFor(diffRow.width, diffRow.seamX)
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
                        x: diffRow.seamX + root.gutterGap + root.seamWidth
                        width: root.gutterWidth
                        text: diffRow.pairRow && modelData.newNo > 0
                            ? String(modelData.newNo) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMicro
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        id: rightBody
                        objectName: "diffSplitNewText"
                        visible: diffRow.pairRow
                        x: diffRow.rightX
                        width: root.newBodyWidthFor(diffRow.width, diffRow.seamX)
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
            // The ListView is inset by contentInset inside this box, so the
            // row-space seam has to be offset by the same amount to land on
            // the line.
            Item {
                id: splitHandle
                objectName: "diffSplitHandle"
                visible: root.viewMode === "split" && root.hasDiff
                x: root.contentInset + root.splitXFor(lineList.width) - width / 2
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
                    Behavior on color { ColorAnimation { duration: Theme.motionFast } }
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
            font.pixelSize: Theme.fontSizeMicro
            wrapMode: Text.Wrap
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("差异内容")
}
