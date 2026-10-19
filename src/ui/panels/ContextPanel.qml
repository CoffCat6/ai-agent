import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// Context panel - context budget and knowledge base management.
// Collapse/expand is driven by AgentVM.isPanelVisible: main.qml animates
// this item's width to 0 and hosts the floating toggle handle.
Rectangle {
    id: root

    signal documentDetailsRequested(string name)

    Accessible.name: qsTr("上下文面板")

    color: Theme.bgSecondary
    border.width: 0
    clip: true

    // Fade the content out while collapsing
    readonly property real contentFade: AgentVM.isPanelVisible ? 1.0 : 0.0

    // Document import dialog (handled off-GUI-thread by AgentViewModel).
    // docs/25: 多选 → 批量导入链（VM 内按文件串行推进）；单选 → 既有
    // 单文件路径（保留其分类错误提示）。
    FileDialog {
        id: kbFileDialog
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("支持的文件 (*.txt *.md *.json *.csv *.log *.pdf)"), qsTr("所有文件 (*)")]
        onAccepted: AgentVM.importDocuments(selectedFiles)
    }

    // Left border separator
    Rectangle {
        width: 1
        height: parent.height
        anchors.left: parent.left
        color: Theme.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        opacity: root.contentFade

        Behavior on opacity { NumberAnimation { duration: 150 } }

        // ── Task List Section ────────────────────────
        // Fed by the todo_write tool through AgentVM.todoList; the component
        // hides itself (and collapses to zero height) when the list is empty.
        TodoListPanel {
            id: todoPanel
            Layout.fillWidth: true
            todos: AgentVM.todoList
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderSubtle
            visible: todoPanel.hasItems
        }

        // ── Knowledge Base Section ───────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: AgentVM.knowledgeDocuments.length > 0
                    ? qsTr("知识库 · %1").arg(AgentVM.knowledgeDocuments.length)
                    : qsTr("知识库")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }

            Text {
                id: privacyNote
                // Privacy statement (audit P1-7): local storage/retrieval does
                // NOT mean the content stays local - passages travel with the
                // request to whichever model service is configured. Batch 14:
                // point at the local-only mode switch that gates this off.
                // Elide instead of fillWidth: two stretching Texts in one
                // RowLayout overlap their paint areas on long translations.
                text: qsTr("文档内容会随提问发送给当前模型服务；如需阻止，请在设置中开启“仅本地模式”")
                color: Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontSans
                Layout.fillWidth: true
                elide: Text.ElideRight
                visible: AgentVM.knowledgeDocuments.length > 0

                // docs/25 C5: 说明被 elide 截断时悬停查看全文
                HoverHandler { id: privacyNoteHover }
                ToolTip.visible: privacyNoteHover.hovered && privacyNote.truncated
                ToolTip.text: privacyNote.text
            }

            // Import-in-progress indicator (embedding runs in background).
            // docs/25: 批量导入时展示逐文件进度并提供取消。
            Row {
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
                                ? qsTr("已嵌入 %1/%2").arg(AgentVM.knowledgeEmbedDone).arg(AgentVM.knowledgeEmbedTotal)
                                : qsTr("导入中…")
                    color: Theme.brandPrimary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                }

                // docs/25: 取消——不再开始后续文件；正在导入的文件会完成。
                IconButton {
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

            // Import document action
            IconButton {
                width: 24
                height: 24
                radius: 5
                icon: "add"
                iconSize: 14
                opacity: AgentVM.knowledgeBusy ? 0.35 : 1.0
                enabled: !AgentVM.knowledgeBusy
                tooltip: qsTr("导入文档（可多选）")
                onClicked: kbFileDialog.open()
            }
        }

        // docs/25: 批量导入结束后的失败重试入口——只在有失败项且空闲时
        // 可见；重跑以文件系统当前内容为准。
        AppButton {
            Layout.fillWidth: true
            visible: !AgentVM.knowledgeBusy && AgentVM.batchImportFailedCount > 0
            text: qsTr("重试失败的导入（%1）").arg(AgentVM.batchImportFailedCount)
            onClicked: AgentVM.retryFailedBatchImports()
            Accessible.name: qsTr("重试失败的批量导入")
        }

        Text {
            visible: AgentVM.knowledgeDocuments.length === 0
            text: qsTr("导入文本或 PDF 文件（.txt / .md / .pdf …），\n模型即可检索其内容回答问题")
            color: Theme.textSecondary
            font.pixelSize: 10
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: AgentVM.knowledgeDocuments.length > 0

            ListView {
                id: kbList
                model: AgentVM.knowledgeDocuments
                spacing: 4

                delegate: Rectangle {
                    width: kbList.width
                    height: kbRow.implicitHeight + 14
                    radius: 6
                    color: kbHover.hovered ? Theme.surfaceHover : "transparent"

                    // Passive hover detection (does not steal the delete click)
                    HoverHandler { id: kbHover }

                    // Open the detail view; stacked below the action buttons
                    // so their own areas win where they overlap.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.documentDetailsRequested(modelData.name)
                    }

                    RowLayout {
                        id: kbRow
                        anchors.fill: parent
                        anchors.margins: 7
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 8

                        AppIcon {
                            Layout.alignment: Qt.AlignVCenter
                            icon: "folder"
                            size: 15
                            iconColor: kbHover.hovered ? Theme.brandPrimary : Theme.textSecondary
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: modelData.name
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }

                                // Degraded-embedding badge: import ran while
                                // the embedding service was offline
                                Rectangle {
                                    visible: modelData.pseudo === true
                                    width: pseudoLabel.implicitWidth + 8
                                    height: 14
                                    radius: 7
                                    color: Qt.rgba(Theme.statusWarning.r, Theme.statusWarning.g,
                                                   Theme.statusWarning.b, 0.15)

                                    Text {
                                        id: pseudoLabel
                                        anchors.centerIn: parent
                                        text: qsTr("降级")
                                        color: Theme.statusWarning
                                        font.pixelSize: 8
                                        font.family: Theme.fontSans
                                    }

                                    MouseArea {
                                        id: pseudoTipArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }

                                    ToolTip.visible: pseudoTipArea.containsMouse
                                    ToolTip.delay: 300
                                    ToolTip.text: qsTr("导入时嵌入服务不可用，已使用本地降级嵌入，检索质量有限。可点击右侧刷新按钮重新嵌入。")
                                }
                            }

                            Text {
                                // docs/25 C2: 量词拼接模板化，单一可译模板
                                text: qsTr("%1 段 · %2 字符").arg(modelData.chunks).arg(modelData.chars)
                                color: Theme.textSecondary
                                font.pixelSize: 9
                                font.family: Theme.fontMono
                            }
                        }

                        // Re-embed action (degraded documents only, e.g.
                        // after the embedding service came back online)
                        IconButton {
                            visible: modelData.pseudo === true
                            width: 22
                            height: 22
                            radius: 5
                            icon: "refresh"
                            iconSize: 12
                            opacity: AgentVM.knowledgeBusy ? 0.35 : 1.0
                            enabled: !AgentVM.knowledgeBusy
                            tooltip: qsTr("重新嵌入")
                            onClicked: AgentVM.reembedDocument(modelData.name)
                        }

                        // Remove document action
                        IconButton {
                            width: 22
                            height: 22
                            radius: 5
                            icon: "close"
                            iconSize: 12
                            // docs/25 D5: 与同行「重新嵌入」按钮一致补 tooltip，
                            // 同时补齐读屏名（无 tooltip 时回落为图标名 "close"）
                            tooltip: qsTr("移除文档")
                            hoverBgColor: Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                                                  Theme.statusDanger.b, 0.15)
                            hoverIconColor: Theme.statusDanger
                            onClicked: AgentVM.removeDocument(modelData.name)
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true; visible: AgentVM.knowledgeDocuments.length === 0 }
    }
}
