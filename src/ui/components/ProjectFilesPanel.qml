import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Sidebar file browser for the active project.
//
// SCOPE: this is a READ-ONLY view. It never writes, creates, renames or
// deletes anything - writes reach the disk through the agent (and then show
// up in the review dialog) or through an external editor. Git status marks
// belong to GitPanel; putting them here too would blur which panel answers
// "what did I change" versus "what is in this project".
//
// WHY ONE LEVEL AT A TIME: ProjectFileVM.children() reads a single
// directory, and a folder's contents are requested the first time it is
// expanded. Indexing the whole tree up front would mean building a tree of
// tens of thousands of nodes in JS (the tree is not a ListView model here,
// it is a nested structure the delegate walks), and it could not show empty
// folders - which a file manager must.
//
// The filter rules live in ProjectFileIndex::listDir (C++), so this panel
// and the '@' mention indexer cannot drift apart about ignored directories
// or sensitive files.
Item {
    id: root

    // Project root. Empty = local chat: the panel shows a short hint and
    // never touches the filesystem.
    property string projectRoot: ""
    // The file whose content is shown in the preview (empty = none).
    property string selectedPath: ""
    // True while the preview pane is expanded.
    property bool previewVisible: false
    // Preview pane height. Default 320 rather than 240: the sidebar's own
    // margin leaves ~500px at the 600px minimum window, and 240 showed barely
    // a dozen lines of code. In-memory only - it is a per-look view
    // parameter, not a stable preference, so it is deliberately not
    // persisted (ui/panelWidth is, and that is the right line).
    property int previewHeight: 320
    readonly property int previewMinHeight: 100
    readonly property int previewDefaultHeight: 320

    // True while the Git/knowledge/file section hosting us is visible. Used
    // only to avoid doing work nobody can see; the tree itself is cheap
    // enough that a full deferral would be over-engineering.
    property bool active: false

    // collapsed state, keyed by relPath
    property var collapsed: ({})
    property string filterText: ""
    // Pre-lowercased/trimmed form. A plain (non-readonly) property updated by
    // onFilterTextChanged: a readonly binding would re-evaluate lazily, and
    // rebuild() runs in the same tick as the text change.
    property string filterNeedle: ""

    readonly property bool hasProject: projectRoot.length > 0
    readonly property string previewError: previewData.error !== undefined
        ? String(previewData.error) : ""
    readonly property string previewText: previewData.text !== undefined
        ? String(previewData.text) : ""
    readonly property int previewBytes: previewData.bytes !== undefined
        ? previewData.bytes : 0
    readonly property bool previewTruncated: previewData.truncated === true
    readonly property bool previewBinary: previewData.binary === true
    readonly property string previewName: {
        if (selectedPath.length === 0) return ""
        const parts = selectedPath.split("/")
        return parts[parts.length - 1]
    }

    // Loaded preview payload ({text,bytes,truncated,binary,error}).
    property var previewData: ({})

    // ── Tree model ──────────────────────────────────────
    // Flat rows for the ListView: {kind:"folder"|"file", name, relPath,
    // depth, isDir}. Rebuilt whenever a folder is expanded/collapsed or the
    // filter changes. Only expanded folders are walked, so the row count
    // tracks what is on screen, not the project size.
    property var rows: []

    function rebuild() {
        const out = []
        appendLevel("", 0, out)
        rows = out
    }

    function appendLevel(relPath, depth, out) {
        const entries = ProjectFileVM.children(relPath)
        const needle = root.filterNeedle
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            const name = String(entry.name)
            // Filtering applies to files and to folders whose own name
            // matches; a folder's children are still walked when it is open,
            // so a match deeper in the tree is not hidden by a non-matching
            // ancestor name only when the ancestor is expanded.
            if (needle.length > 0 && name.toLowerCase().indexOf(needle) < 0) {
                if (!entry.isDir) continue
            }
            // Folders start collapsed: a project root can hold hundreds of
            // directories and expanding everything up front would defeat the
            // point of listing lazily. `collapsed` holds the explicit state,
            // so the default has to be "closed" (undefined -> closed).
            const isCollapsed = collapsed[entry.relPath] !== false
            out.push({
                "kind": entry.isDir ? "folder" : "file",
                "name": name,
                "relPath": entry.relPath,
                "depth": depth,
                "isDir": entry.isDir === true,
                "collapsed": isCollapsed
            })
            if (entry.isDir && !isCollapsed)
                appendLevel(entry.relPath, depth + 1, out)
        }
    }

    function toggleFolder(relPath) {
        const next = {}
        for (const key in collapsed) next[key] = collapsed[key]
        // Default is collapsed, so an explicit "false" marks a folder as
        // expanded; toggling flips between the two states.
        if (next[relPath] === false) delete next[relPath]
        else next[relPath] = false
        collapsed = next
        rebuild()
    }

    function collapseAll() {
        // Every folder returns to the default (collapsed) state.
        collapsed = ({})
        rebuild()
    }

    function selectFile(relPath) {
        selectedPath = relPath
        previewVisible = true
        previewData = ProjectFileVM.preview(relPath)
    }

    function closePreview() {
        previewVisible = false
        selectedPath = ""
        previewData = ({})
    }

    function openFile(relPath) {
        if (!ProjectFileVM.openInDefaultApp(relPath))
            root.errorText = qsTr("无法打开该文件（可能已被删除或没有关联程序）。")
    }

    property string errorText: ""

    function refresh() {
        errorText = ""
        ProjectFileVM.clearCache()
        rebuild()
        if (selectedPath.length > 0)
            previewData = ProjectFileVM.preview(selectedPath)
    }

    // The seam sits between the tree and the preview, so dragging DOWN must
    // make the preview SHORTER - the divider follows the cursor. SplitBar
    // reports the raw pointer delta, so the sign is inverted here rather than
    // inside the component: that keeps SplitBar a plain "report the delta"
    // tool for every host.
    //
    // The upper bound is measured, not constant: the tree keeps its own
    // minimum (80px, set on the ListView) plus the divider's own height. A
    // fixed cap would push the tree out of existence on a short sidebar.
    function dragPreviewSeam(deltaPx) {
        if (!previewVisible) return
        const seamHeight = 7          // SplitBar's implicit height
        const treeMin = 80            // matches fileList's Layout.minimumHeight
        const available = content.height - treeMin - seamHeight
        const next = previewHeight - deltaPx
        previewHeight = Math.max(previewMinHeight,
                                 Math.min(next, Math.max(previewMinHeight, available)))
    }

    function resetPreviewHeight() {
        previewHeight = previewDefaultHeight
    }

    // Project root changes: the VM owns the cache, so drop it and rebuild.
    onProjectRootChanged: {
        errorText = ""
        collapsed = ({})
        ProjectFileVM.root = projectRoot
        closePreview()
        rows = []
        // Read projectRoot, not hasProject: inside a property-change handler
        // the derived binding has not re-evaluated yet, so hasProject still
        // holds its previous value and the rebuild was skipped.
        if (projectRoot.length > 0) rebuild()
    }

    Component.onCompleted: {
        ProjectFileVM.root = projectRoot
        if (projectRoot.length > 0) rebuild()
    }

    onFilterTextChanged: {
        filterNeedle = filterText ? filterText.toLowerCase().trim() : ""
        rebuild()
    }

    // Rebuild when a folder's contents change on disk (after refresh()).
    Connections {
        target: ProjectFileVM
        function onRootChanged() { if (root.projectRoot.length > 0) root.rebuild() }
    }

    // The panel itself stays visible in both states: a local chat shows the
    // hint below, so hiding the root would hide the explanation with it.
    implicitHeight: hasProject ? content.implicitHeight : hintText.implicitHeight

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 6
        // Hidden together with the panel body: the local-chat hint below
        // fills the same rectangle, and an invisible column that still
        // anchors over it would swallow the hint's visibility.
        visible: root.hasProject

        // ── Toolbar: filter + refresh ──────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 24
                radius: Theme.radiusSm
                color: Theme.bgPrimary
                border.width: 1
                border.color: filterInput.activeFocus
                    ? Theme.brandPrimary : Theme.borderSubtle

                TextInput {
                    id: filterInput
                    objectName: "projectFileFilter"
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 7
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    selectByMouse: true
                    clip: true
                    text: root.filterText
                    onTextChanged: root.filterText = text
                    Accessible.name: qsTr("按名称过滤项目文件")

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: filterInput.text.length === 0
                        text: qsTr("过滤文件…")
                        color: Theme.textTertiary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
            }

            IconButton {
                objectName: "projectFileCollapseAll"
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "chevron-up"
                iconSize: 11
                tooltip: qsTr("折叠全部目录")
                onClicked: root.collapseAll()
            }

            IconButton {
                objectName: "projectFileRefresh"
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 4
                icon: "refresh"
                iconSize: 11
                tooltip: qsTr("重新读取项目文件")
                onClicked: root.refresh()
            }
        }

        // ── File tree ──────────────────────────────────
        // The tree takes whatever the preview does not. Its minimum keeps a
        // few rows visible; the preview's minimum lives on the preview side,
        // so dragging can shrink either half but never to nothing.
        ListView {
            id: fileList
            objectName: "projectFileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: fileRow
                required property var modelData

                readonly property bool isFolder: modelData.isDir === true
                readonly property bool selected: !isFolder
                    && root.selectedPath === modelData.relPath

                width: fileList.width
                height: 22
                radius: 4
                color: selected ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                         Theme.brandPrimary.b, 0.16)
                                : (rowHover.hovered ? Theme.surfaceHover : "transparent")

                HoverHandler { id: rowHover }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onClicked: {
                        if (fileRow.isFolder) root.toggleFolder(fileRow.modelData.relPath)
                        else root.selectFile(fileRow.modelData.relPath)
                    }
                    onDoubleClicked: {
                        if (!fileRow.isFolder)
                            root.openFile(fileRow.modelData.relPath)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4 + fileRow.modelData.depth * 12
                    anchors.rightMargin: 6
                    spacing: 4

                    AppIcon {
                        Layout.alignment: Qt.AlignVCenter
                        icon: fileRow.isFolder
                            ? (fileRow.modelData.collapsed ? "chevron-right" : "chevron-down")
                            : "file"
                        size: 11
                        iconColor: fileRow.isFolder
                            ? Theme.textSecondary
                            : (fileRow.selected ? Theme.brandPrimary : Theme.textTertiary)
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: fileRow.modelData.name
                        color: fileRow.selected ? Theme.brandPrimary : Theme.textPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        elide: Text.ElideMiddle
                        Accessible.name: text
                    }
                }
            }
        }

        // ── Seam: tree ↔ preview ───────────────────────
        // Only present while the preview is open; a hidden divider would
        // still take layout space and leave a dead 7px band.
        SplitBar {
            objectName: "projectFilePreviewSeam"
            Layout.fillWidth: true
            visible: root.previewVisible
            orientation: "vertical"
            onDragged: (delta) => root.dragPreviewSeam(delta)
            onResetRequested: root.resetPreviewHeight()
        }

        // Empty states, distinct on purpose: "no files" and "filter matched
        // nothing" call for different reactions from the user.
        Text {
            objectName: "projectFileEmpty"
            Layout.fillWidth: true
            visible: root.rows.length === 0 && root.hasProject
            text: root.filterText.length > 0
                ? qsTr("没有匹配的文件。")
                : qsTr("该目录为空。")
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        // ── Preview ────────────────────────────────────
        Rectangle {
            objectName: "projectFilePreview"
            Layout.fillWidth: true
            // Height is user-draggable (the seam above); the minimum keeps a
            // usable few lines even at the smallest the sidebar gets.
            Layout.preferredHeight: root.previewVisible ? root.previewHeight : 0
            Layout.minimumHeight: root.previewVisible ? root.previewMinHeight : 0
            visible: root.previewVisible
            radius: Theme.radiusSm
            color: Theme.codeBackground
            border.width: 1
            border.color: Theme.borderSubtle
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    AppIcon {
                        Layout.alignment: Qt.AlignVCenter
                        icon: "file"
                        size: 11
                        iconColor: Theme.brandPrimary
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: root.previewName
                        color: Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                        Accessible.name: text
                    }

                    IconButton {
                        objectName: "projectFileOpen"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        radius: 4
                        icon: "export"
                        iconSize: 10
                        tooltip: qsTr("用系统默认程序打开")
                        onClicked: root.openFile(root.selectedPath)
                    }

                    IconButton {
                        objectName: "projectFileClosePreview"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        radius: 4
                        icon: "close"
                        iconSize: 10
                        tooltip: qsTr("收起预览")
                        onClicked: root.closePreview()
                    }
                }

                // Truncation / binary / error notices, each with its own
                // wording: "only the first part" must not read like a
                // complete file, and a binary file must not look empty.
                Text {
                    objectName: "projectFileNotices"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: {
                        if (root.previewError === "binary" || root.previewBinary)
                            return qsTr("二进制文件，无法预览。")
                        if (root.previewError === "missing")
                            return qsTr("文件已不存在，可能已被删除或改名。")
                        if (root.previewError === "unreadable")
                            return qsTr("无法读取该文件（权限不足或已被占用）。")
                        if (root.previewError === "outside")
                            return qsTr("该路径不在项目内，已拒绝读取。")
                        if (root.previewError.length > 0)
                            return qsTr("读取失败：%1").arg(root.previewError)
                        if (root.previewTruncated)
                            return qsTr("文件较大（%1 字节），仅显示前 512 KB。")
                                .arg(root.previewBytes)
                        return ""
                    }
                    color: root.previewError.length > 0 ? Theme.statusWarning
                                                        : Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    wrapMode: Text.Wrap
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    visible: root.previewText.length > 0

                    TextArea {
                        id: previewArea
                        objectName: "projectFilePreviewText"
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        text: root.previewText
                        color: Theme.textPrimary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        background: null
                        padding: 0
                        Accessible.name: qsTr("文件内容预览")
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.statusWarning
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }
    }

    // Local chat: explain the absence instead of showing an empty tree.
    Text {
        id: hintText
        objectName: "projectFileNoProject"
        anchors.fill: parent
        visible: !hasProject
        text: qsTr("当前会话未关联项目；在项目中打开的会话才能浏览项目文件。")
        color: Theme.textSecondary
        font.pixelSize: 10
        font.family: Theme.fontSans
        wrapMode: Text.Wrap
    }

    Accessible.role: Accessible.List
    Accessible.name: qsTr("项目文件")
}
