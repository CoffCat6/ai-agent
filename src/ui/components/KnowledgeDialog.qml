import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// Knowledge base management, in its own dialog.
//
// HISTORY: this lived in the right sidebar's ContextPanel alongside Git and
// the file browser. That placement was wrong on two counts:
//
//   1. SEMANTICS - the knowledge base is GLOBAL (one RagService for the whole
//      process, independent of project and session), while every other section
//      of that panel only exists inside a project. A local chat consequently
//      showed a panel holding nothing but the knowledge base.
//   2. SPACE - each row had to fit a name, a degraded badge, a "N chunks - M
//      chars" line and two action buttons into a 220-660px column. With a
//      dialog there is room to lay the list out as a table with aligned
//      columns, which is what a management list wants.
//
// It is opened from the sidebar's global tool strip (next to log / audit /
// settings) - the other three are global too, which is why that row is the
// right home. Like them it is a modal Dialog over Overlay.overlay; there are
// no separate OS windows in this application.
Dialog {
    id: dialog

    // Emitted when a document row is activated: the caller (main.qml) routes
    // it to the shared KnowledgeDocDialog, exactly as ContextPanel used to.
    signal documentDetailsRequested(string name)

    // These mirror ContextPanel's old contract so a host can drive the dialog
    // in a test without a database.
    property var documents: AgentVM.knowledgeDocuments

    readonly property int docCount: documents ? documents.length : 0
    // Totals across the list, for the header line.
    readonly property int totalChunks: {
        let n = 0
        for (let i = 0; i < docCount; ++i) n += documents[i].chunks || 0
        return n
    }
    readonly property int totalChars: {
        let n = 0
        for (let i = 0; i < docCount; ++i) n += documents[i].chars || 0
        return n
    }
    readonly property bool empty: docCount === 0   // library empty; listEmpty covers filtering
    property bool listEmpty: true   // single writer: refilter()
    // Set when "open source file" fails (file moved/deleted); cleared on any
    // successful open. Shown once, not per-row, because the cause is the same.
    property bool openFailed: false
    // Document-name filter. Empty shows everything (the ordinary list); a
    // query narrows to name matches. Name-only, not content: content search
    // is retrieval's job, this is "find the document row".
    property string filterText: ""
    // Single writer: refilter(). model: dialog.filteredDocuments needs a JS
    // array property (an inline block binding on ListView.model silently
    // dropped its result - measured: the block ran, count stayed 0), and the
    // assignment must come from QML-side so the changed handlers fire.
    property var filteredDocuments: []
    function refilter() {
        // String coercion first: a C++ setProperty can hand this a QVariant
        // wrapper that is neither a JS string nor undefined, and calling
        // string methods on it throws (measured TypeError at this line).
        const n = String(filterText === undefined || filterText === null
                         ? "" : filterText).trim().toLowerCase()
        const out = []
        for (let i = 0; i < documents.length; ++i)
            if (n.length === 0
                    || String(documents[i].name).toLowerCase().indexOf(n) >= 0)
                out.push(documents[i])
        filteredDocuments = out
        // Push into the view directly: inside a Popup the ListView misses the
        // var-property change above (plain-item hosts track it fine).
        docList.model = out
        listEmpty = out.length === 0
    }
    // The unfiltered branch restores the default binding target and the
    // library-empty meaning of listEmpty.
    onFilterTextChanged: refilter()
    onDocumentsChanged: refilter()
    Component.onCompleted: {
        refilter()
        listEmpty = empty
    }

    // Column widths, shared by the header row and the delegates so the two
    // cannot drift apart.
    readonly property int chunksColumnWidth: 80
    readonly property int charsColumnWidth: 110
    readonly property int statusColumnWidth: 90
    readonly property int actionsColumnWidth: 64

    function openPanel() { open() }

    // Test/embedding hook: assign `documents` from the QML side. A C++
    // setProperty on a var property fires the changed signal but does not
    // invalidate QML bindings that depend on it (Qt 6.9), so docCount and the
    // list stayed stale; routing through here keeps the notify chain intact.
    function setDocuments(list) { documents = list }

    // Compact "12.3k" for the char total; the exact number is in each row.
    function formatCount(n) {
        if (n < 1000) return String(n)
        if (n < 1000000) return (n / 1000).toFixed(1) + "k"
        return (n / 1000000).toFixed(2) + "M"
    }

    function openDocument(name) {
        if (!name) return
        dialog.documentDetailsRequested(name)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(900, parent ? parent.width - 64 : 900)
    height: Math.min(640, parent ? parent.height - 80 : 640)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 0

        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("知识库")
        Accessible.description: qsTr("管理供模型检索的本地文档")

        // ── Header ──────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 12
            Layout.topMargin: 16
            Layout.bottomMargin: 10
            spacing: 8

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                icon: "layers"
                size: 15
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("知识库")
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                elide: Text.ElideRight
                Accessible.name: text
            }

            // Summary: how much is actually indexed, at a glance.
            Text {
                objectName: "knowledgeSummary"
                Layout.alignment: Qt.AlignVCenter
                visible: dialog.docCount > 0
                text: qsTr("%1 个文档 · %2 段 · %3 字符")
                    .arg(dialog.docCount)
                    .arg(dialog.totalChunks)
                    .arg(dialog.formatCount(dialog.totalChars))
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeCaption
                Accessible.name: text
            }

            // Import progress. It lives in the header rather than in a banner
            // so the row reads as "the thing this dialog is busy with".
            Row {
                Layout.alignment: Qt.AlignVCenter
                visible: AgentVM.knowledgeBusy
                spacing: 6

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "refresh"
                    size: 13
                    iconColor: Theme.brandPrimary

                    RotationAnimation on rotation {
                        running: AgentVM.knowledgeBusy
                        loops: Animation.Infinite
                        from: 0
                        to: 360
                        duration: 1200
                    }
                }

                Text {
                    objectName: "knowledgeProgress"
                    anchors.verticalCenter: parent.verticalCenter
                    text: AgentVM.batchImportCancelling
                        ? qsTr("批量导入 %1/%2：正在取消…")
                              .arg(AgentVM.batchImportDone)
                              .arg(AgentVM.batchImportTotal)
                        : AgentVM.batchImportRunning
                            ? qsTr("批量导入 %1/%2：%3")
                                  .arg(AgentVM.batchImportDone)
                                  .arg(AgentVM.batchImportTotal)
                                  .arg(AgentVM.batchImportCurrent)
                            : AgentVM.knowledgeEmbedTotal > 0
                                ? qsTr("已嵌入 %1/%2")
                                      .arg(AgentVM.knowledgeEmbedDone)
                                      .arg(AgentVM.knowledgeEmbedTotal)
                                : qsTr("导入中…")
                    color: Theme.brandPrimary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                }

                IconButton {
                    objectName: "knowledgeCancelImport"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: AgentVM.batchImportRunning
                    enabled: !AgentVM.batchImportCancelling
                    opacity: AgentVM.batchImportCancelling ? 0.35 : 1.0
                    width: 20
                    height: 20
                    radius: 4
                    icon: "x"
                    iconSize: 10
                    tooltip: AgentVM.batchImportCancelling
                        ? qsTr("正在取消（等待当前文件完成）")
                        : qsTr("取消批量导入（未开始的文件将跳过）")
                    onClicked: AgentVM.cancelBatchImport()
                }
            }

            AppButton {
                objectName: "knowledgeImportButton"
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("导入文档")
                implicitHeight: 24
                highlighted: true
                enabled: !AgentVM.knowledgeBusy && !AgentVM.urlImportRunning
                opacity: enabled ? 1.0 : 0.4
                onClicked: fileDialog.open()
                Accessible.name: qsTr("导入文档到知识库（可多选）")
            }

            // URL import: the button reveals the address row. Inline rather
            // than a nested dialog — one fewer modal for a two-field action.
            AppButton {
                id: urlImportToggle
                objectName: "knowledgeUrlImportToggle"
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("从 URL 导入")
                implicitHeight: 24
                enabled: !AgentVM.knowledgeBusy && !AgentVM.urlImportRunning
                opacity: enabled ? 1.0 : 0.4
                onClicked: urlRow.visible = !urlRow.visible
                Accessible.name: qsTr("从网页地址导入知识库")
            }

            IconButton {
                objectName: "knowledgeCloseButton"
                Layout.alignment: Qt.AlignVCenter
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭知识库")
            }
        }

        // ── URL import row ──────────────────────────────
        // Hidden until the toggle is pressed; keeps the header compact for
        // users who never import from the web.
        RowLayout {
            id: urlRow
            objectName: "knowledgeUrlRow"
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 10
            spacing: 6
            visible: urlRow.visible

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 26
                radius: Theme.radiusSm
                color: Theme.bgPrimary
                border.width: 1
                border.color: urlInput.activeFocus
                    ? Theme.brandPrimary : Theme.borderSubtle

                TextInput {
                    id: urlInput
                    objectName: "knowledgeUrlInput"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeCaption
                    selectByMouse: true
                    clip: true
                    // Enter starts the import; Esc hides the row.
                    Keys.onReturnPressed: importUrlButton.clicked()
                    Keys.onEscapePressed: urlRow.visible = false
                    Accessible.name: qsTr("要导入的网页地址")

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: urlInput.text.length === 0
                        text: qsTr("https://example.com/article")
                        color: Theme.textTertiary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
            }

            AppButton {
                id: importUrlButton
                objectName: "knowledgeUrlImportButton"
                text: AgentVM.urlImportRunning
                    ? qsTr("抓取中…") : qsTr("抓取并导入")
                implicitHeight: 26
                highlighted: true
                enabled: !AgentVM.knowledgeBusy && !AgentVM.urlImportRunning
                         && urlInput.text.trim().length > 0
                opacity: enabled ? 1.0 : 0.4
                onClicked: {
                    dialog.openFailed = false
                    AgentVM.importFromUrl(urlInput.text.trim())
                    urlInput.text = ""
                }
                Accessible.name: qsTr("抓取网页并导入知识库")
            }
        }

        // ── Privacy note ────────────────────────────────
        // Audit P1-7: local storage does NOT mean the content stays local -
        // passages travel with the request to whichever model service is
        // configured. The dialog is wide enough to show the whole sentence, so
        // it is no longer elided behind a tooltip.
        Text {
            objectName: "knowledgePrivacyNote"
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 10
            text: qsTr("文档内容会随提问发送给当前模型服务；如需阻止，请在设置中开启“仅本地模式”。")
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        Text {
            objectName: "knowledgeOpenFailed"
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 8
            visible: dialog.openFailed
            text: qsTr("无法打开源文件（可能已被移动或删除）。")
            color: Theme.statusWarning
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        // Retry entry, only while there are failures and nothing is running.
        AppButton {
            objectName: "knowledgeRetryFailed"
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 8
            visible: !AgentVM.knowledgeBusy && AgentVM.batchImportFailedCount > 0
            text: qsTr("重试失败的导入（%1）").arg(AgentVM.batchImportFailedCount)
            onClicked: AgentVM.retryFailedBatchImports()
            Accessible.name: qsTr("重试失败的批量导入")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            color: Theme.borderSubtle
        }

        // ── Document filter ──────────────────────────────
        // Visible only while the library has documents to filter: an empty
        // library shows the import empty state instead.
        Rectangle {
            objectName: "knowledgeFilterBox"
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 8
            implicitHeight: 26
            visible: !dialog.empty
            radius: Theme.radiusSm
            color: Theme.bgPrimary
            border.width: 1
            border.color: filterInput.activeFocus
                ? Theme.brandPrimary : Theme.borderSubtle

            TextInput {
                id: filterInput
                objectName: "knowledgeFilterInput"
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeCaption
                selectByMouse: true
                clip: true
                text: dialog.filterText
                onTextChanged: dialog.filterText = text
                Keys.onEscapePressed: text = ""
                Accessible.name: qsTr("按名称过滤文档")

                Text {
                    anchors.fill: parent
                    verticalAlignment: Text.AlignVCenter
                    visible: filterInput.text.length === 0
                    text: qsTr("过滤文档…")
                    color: Theme.textTertiary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                }
            }
        }

        // ── Column header ───────────────────────────────
        // Fixed column widths shared with the delegates: a table that does not
        // line up is worse than a plain list.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            spacing: 8
            visible: !dialog.listEmpty

            Text {
                Layout.fillWidth: true
                text: qsTr("文档")
                color: Theme.textTertiary
                font.pixelSize: 9
                font.family: Theme.fontSans
            }
            Text {
                Layout.preferredWidth: dialog.chunksColumnWidth
                text: qsTr("段数")
                color: Theme.textTertiary
                font.pixelSize: 9
                font.family: Theme.fontSans
                horizontalAlignment: Text.AlignRight
            }
            Text {
                Layout.preferredWidth: dialog.charsColumnWidth
                text: qsTr("字符")
                color: Theme.textTertiary
                font.pixelSize: 9
                font.family: Theme.fontSans
                horizontalAlignment: Text.AlignRight
            }
            Text {
                Layout.preferredWidth: dialog.statusColumnWidth
                text: qsTr("状态")
                color: Theme.textTertiary
                font.pixelSize: 9
                font.family: Theme.fontSans
                horizontalAlignment: Text.AlignHCenter
            }
            Item { Layout.preferredWidth: dialog.actionsColumnWidth }
        }

        // ── Document list ───────────────────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            clip: true
            visible: !dialog.listEmpty

            ListView {
                id: docList
                objectName: "knowledgeDocList"
                // The unfiltered view binds the documents property directly
                // (the only form proven to update inside this Popup); a filter
                // query swaps in the narrowed array imperatively in refilter().
                model: dialog.documents
                spacing: 2

                delegate: Rectangle {
                    id: docRow
                    required property var modelData

                    width: docList.width
                    height: 34
                    radius: 5
                    color: rowHover.hovered ? Theme.surfaceHover : "transparent"

                    HoverHandler { id: rowHover }

                    // Row click opens the chunk viewer. The action buttons sit
                    // on top, so their own areas win where they overlap.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dialog.openDocument(docRow.modelData.name)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 8

                        AppIcon {
                            Layout.alignment: Qt.AlignVCenter
                            icon: "file"
                            size: 13
                            iconColor: rowHover.hovered ? Theme.brandPrimary : Theme.textSecondary
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: docRow.modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                            Accessible.name: text
                        }

                        Text {
                            Layout.preferredWidth: dialog.chunksColumnWidth
                            Layout.alignment: Qt.AlignVCenter
                            text: docRow.modelData.chunks || 0
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            horizontalAlignment: Text.AlignRight
                        }

                        Text {
                            Layout.preferredWidth: dialog.charsColumnWidth
                            Layout.alignment: Qt.AlignVCenter
                            text: (docRow.modelData.chars || 0).toLocaleString(Qt.locale(), 'f', 0)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            horizontalAlignment: Text.AlignRight
                        }

                        // Embedding state. "降级" is the one that needs action,
                        // so it carries a warning colour and the re-embed
                        // button beside it.
                        Item {
                            Layout.preferredWidth: dialog.statusColumnWidth
                            Layout.alignment: Qt.AlignVCenter
                            implicitHeight: 18

                            Text {
                                anchors.centerIn: parent
                                visible: docRow.modelData.pseudo !== true
                                text: qsTr("已嵌入")
                                color: Theme.statusSuccess
                                font.pixelSize: 9
                                font.family: Theme.fontSans
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                visible: docRow.modelData.pseudo === true
                                width: pseudoText.implicitWidth + 10
                                height: 15
                                radius: 7
                                color: Qt.rgba(Theme.statusWarning.r, Theme.statusWarning.g,
                                               Theme.statusWarning.b, 0.15)

                                Text {
                                    id: pseudoText
                                    anchors.centerIn: parent
                                    text: qsTr("降级")
                                    color: Theme.statusWarning
                                    font.pixelSize: 8
                                    font.family: Theme.fontSans
                                }

                                MouseArea {
                                    id: pseudoHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }

                                ToolTip.visible: pseudoHover.containsMouse
                                ToolTip.delay: 300
                                // The dialog is wide, so the explanation is not
                                // truncated the way it had to be in the sidebar.
                                ToolTip.text: qsTr("导入时嵌入服务不可用，已使用本地降级嵌入，检索质量有限。可点击右侧刷新按钮重新嵌入。")
                            }
                        }

                        // Open the original file, only where a source path
                        // exists (imports record it; restores from older
                        // databases carry an empty path).
                        IconButton {
                            objectName: "knowledgeOpenSourceButton"
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            Layout.alignment: Qt.AlignVCenter
                            visible: String(docRow.modelData.sourcePath || "").length > 0
                            radius: 5
                            icon: "export"
                            iconSize: 12
                            tooltip: qsTr("打开源文件")
                            Accessible.name: tooltip
                            onClicked: {
                                if (!ProjectFileVM.openAbsolute(
                                        String(docRow.modelData.sourcePath)))
                                    openFailed = true
                            }
                        }

                        // Re-embed, only offered where it means something:
                        // a degraded document whose embedding service may be
                        // back online.
                        // Copy the recorded source path/URL: the "original"
                        // may be a URL since URL import landed, so the
                        // clipboard gets whatever was recorded.
                        IconButton {
                            objectName: "knowledgeCopySourceButton"
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            Layout.alignment: Qt.AlignVCenter
                            visible: String(docRow.modelData.sourcePath || "").length > 0
                            radius: 5
                            icon: "copy"
                            iconSize: 12
                            tooltip: qsTr("复制源路径")
                            Accessible.name: tooltip
                            onClicked: AgentVM.copyToClipboard(
                                String(docRow.modelData.sourcePath))
                        }

                        IconButton {
                            objectName: "knowledgeReembedButton"
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            Layout.alignment: Qt.AlignVCenter
                            visible: docRow.modelData.pseudo === true
                            radius: 5
                            icon: "refresh"
                            iconSize: 12
                            opacity: AgentVM.knowledgeBusy ? 0.35 : 1.0
                            enabled: !AgentVM.knowledgeBusy
                            tooltip: qsTr("重新嵌入")
                            onClicked: AgentVM.reembedDocument(docRow.modelData.name)
                        }

                        IconButton {
                            objectName: "knowledgeRemoveButton"
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            Layout.alignment: Qt.AlignVCenter
                            radius: 5
                            icon: "close"
                            iconSize: 12
                            tooltip: qsTr("移除文档")
                            hoverBgColor: Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                                                  Theme.statusDanger.b, 0.15)
                            hoverIconColor: Theme.statusDanger
                            onClicked: AgentVM.removeDocument(docRow.modelData.name)
                        }
                    }
                }
            }
        }

        // ── Empty state ─────────────────────────────────
        Item {
            objectName: "knowledgeEmptyState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: dialog.listEmpty

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 80, 420)
                spacing: 10

                AppIcon {
                    Layout.alignment: Qt.AlignHCenter
                    icon: "layers"
                    size: 26
                    iconColor: Theme.textTertiary
                }

                Text {
                    Layout.fillWidth: true
                    text: dialog.filterText.length > 0
                        ? qsTr("没有名称匹配“%1”的文档。").arg(dialog.filterText)
                        : qsTr("知识库还没有文档")
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeBody
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.name: text
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("导入文本或 PDF 文件（.txt / .md / .json / .csv / .log / .pdf），\n模型即可检索其内容并回答问题。")
                    color: Theme.textTertiary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Accessible.name: text
                }
            }
        }
    }

    // Multi-select: the VM takes a list and runs a serial batch.
    FileDialog {
        id: fileDialog
        objectName: "knowledgeFileDialog"
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("支持的文件 (*.txt *.md *.json *.csv *.log *.pdf)"),
            qsTr("所有文件 (*)")
        ]
        onAccepted: AgentVM.importDocuments(selectedFiles)
    }
}
