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
                    readonly property int half: Math.floor(width / 2)

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
                        width: Math.max(0, diffRow.half - 1)
                        height: parent.height
                        color: modelData.del
                            ? root.kindBackground("del") : "transparent"
                    }
                    Rectangle {
                        visible: diffRow.pairRow
                        x: diffRow.half + 1
                        width: parent.width - x
                        height: parent.height
                        color: modelData.add
                            ? root.kindBackground("add") : "transparent"
                    }
                    Rectangle {
                        visible: diffRow.pairRow
                        x: diffRow.half
                        width: 1
                        height: parent.height
                        color: Theme.borderSubtle
                    }

                    // ── Unified / plain rows ────────────
                    Text {
                        visible: !diffRow.pairRow && !diffRow.plain
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
                        x: diffRow.plain ? 4 : 78
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
                        visible: diffRow.pairRow
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
                        x: 40
                        width: Math.max(0, diffRow.half - 44)
                        text: diffRow.pairRow && modelData.oldText.length > 0
                            ? modelData.oldText : " "
                        color: modelData.del ? Theme.statusDanger : Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }

                    Text {
                        visible: diffRow.pairRow
                        x: diffRow.half + 5
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
                        x: diffRow.half + 41
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