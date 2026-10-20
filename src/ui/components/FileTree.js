.pragma library

// Pure path-tree helpers shared by the review tree, the Git status lists and
// the commit file tree. QML rebuilds the row array whenever items or the
// collapsed set change; for the 200-file budgets involved this is cheaper
// than maintaining a stateful C++ model.

function normalizePath(path) {
    return String(path === undefined || path === null ? "" : path)
        .replace(/\\/g, "/")
}

function basename(path) {
    const p = normalizePath(path)
    const i = p.lastIndexOf("/")
    return i < 0 ? p : p.substring(i + 1)
}

function dirOf(path) {
    const p = normalizePath(path)
    const i = p.lastIndexOf("/")
    return i <= 0 ? "" : p.substring(0, i + 1)
}

// Longest directory prefix shared by every path, always ending at a
// separator so a directory name is never cut in half.
function commonDir(paths) {
    if (!paths || paths.length === 0) return ""
    let prefix = dirOf(paths[0])
    for (let i = 1; i < paths.length && prefix.length > 0; ++i) {
        const dir = dirOf(paths[i])
        const limit = Math.min(prefix.length, dir.length)
        let j = 0
        while (j < limit && prefix[j] === dir[j]) ++j
        prefix = prefix.substring(0, j)
    }
    const cut = prefix.lastIndexOf("/")
    return cut < 0 ? "" : prefix.substring(0, cut + 1)
}

function compareNames(a, b) {
    const la = String(a).toLowerCase()
    const lb = String(b).toLowerCase()
    if (la < lb) return -1
    if (la > lb) return 1
    return a < b ? -1 : (a > b ? 1 : 0)
}

function numberOr(value) {
    const n = Number(value)
    return isFinite(n) ? n : 0
}

function statFor(item) {
    const added = item.addedLines !== undefined ? item.addedLines : item.added
    const removed = item.removedLines !== undefined ? item.removedLines : item.removed
    return { added: numberOr(added), removed: numberOr(removed) }
}

function newFolder(name, path) {
    return {
        name: name,
        path: path,
        children: {},
        files: [],
        fileCount: 0,
        addedTotal: 0,
        removedTotal: 0
    }
}

function insertEntry(root, entry) {
    const parts = entry.displayPath.split("/")
    let node = root
    let prefix = ""
    for (let d = 0; d < parts.length - 1; ++d) {
        if (parts[d].length === 0) continue
        prefix += (prefix.length > 0 ? "/" : "") + parts[d]
        let child = node.children[parts[d]]
        if (!child) {
            child = newFolder(parts[d], prefix)
            node.children[parts[d]] = child
        }
        node = child
    }
    node.files.push(entry)
}

function aggregate(node) {
    let count = node.files.length
    let added = 0
    let removed = 0
    for (const key in node.children) {
        const child = aggregate(node.children[key])
        count += child.count
        added += child.added
        removed += child.removed
    }
    for (let i = 0; i < node.files.length; ++i) {
        const stat = statFor(node.files[i].item)
        added += stat.added
        removed += stat.removed
    }
    node.fileCount = count
    node.addedTotal = added
    node.removedTotal = removed
    return { count: count, added: added, removed: removed }
}

function appendNode(node, depth, rows, collapsed, showFolders) {
    const names = []
    for (const key in node.children)
        names.push(key)
    names.sort(compareNames)

    for (let i = 0; i < names.length; ++i) {
        const child = node.children[names[i]]
        const isCollapsed = collapsed[child.path] === true
        if (showFolders) {
            rows.push({
                kind: "folder",
                path: child.path,
                name: child.name,
                depth: depth,
                collapsed: isCollapsed,
                fileCount: child.fileCount,
                addedLines: child.addedTotal,
                removedLines: child.removedTotal
            })
        }
        if (!isCollapsed)
            appendNode(child, showFolders ? depth + 1 : depth, rows,
                       collapsed, showFolders)
    }

    const files = node.files.slice()
    files.sort(function(a, b) { return compareNames(a.name, b.name) })
    for (let i = 0; i < files.length; ++i) {
        const entry = files[i]
        // File rows carry the original item's fields (path/undone/truncated/
        // stats keep the same names the delegates and tests read), plus the
        // tree coordinates. `item` keeps the untouched source object.
        const row = {}
        for (const key in entry.item)
            row[key] = entry.item[key]
        row.kind = "file"
        row.path = entry.item.path
        row.name = entry.name
        row.displayPath = entry.displayPath
        row.depth = depth
        row.item = entry.item
        const stat = statFor(entry.item)
        row.addedLines = stat.added
        row.removedLines = stat.removed
        rows.push(row)
    }
}

// items: array of objects with a `path` property (+ optional addedLines /
// removedLines). options:
//   basePath  - prefix stripped from the display path (project root or common
//               directory); paths outside it fall back to their full form
//   collapsed - { [folderPath]: true } set of collapsed folders
// Returns flattened rows: folder rows {kind:"folder", ...} and file rows
// {kind:"file", ...item, depth, displayPath, item}.
function buildRows(items, options) {
    const opts = options || {}
    const base = normalizePath(opts.basePath)
    const collapsed = opts.collapsed || {}
    const showFolders = opts.folders !== false

    const root = newFolder("", "")
    const list = items || []
    for (let i = 0; i < list.length; ++i) {
        const item = list[i]
        if (!item) continue
        const full = normalizePath(item.path)
        if (full.length === 0) continue
        let display = full
        if (base.length > 0 && full.indexOf(base) === 0)
            display = full.substring(base.length)
        while (display.charAt(0) === "/")
            display = display.substring(1)
        if (display.length === 0) display = full
        insertEntry(root, {
            item: item,
            displayPath: display,
            name: basename(display)
        })
    }

    aggregate(root)
    const rows = []
    appendNode(root, 0, rows, collapsed, showFolders)
    return rows
}

// Folder rows of the fully expanded tree — used by "collapse all".
function allFolderPaths(items, basePath) {
    const rows = buildRows(items, { basePath: basePath, collapsed: {} })
    const out = []
    for (let i = 0; i < rows.length; ++i) {
        if (rows[i].kind === "folder") out.push(rows[i].path)
    }
    return out
}