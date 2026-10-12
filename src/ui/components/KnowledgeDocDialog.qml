import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Imported-document detail viewer: document stats plus a chunk-by-chunk
// preview of what actually entered the vector store, with clipboard export.
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 680
    height: 560

    property string docName: ""
    property var chunks: []   // [{chunk_index, chunk_total, text, page, ...}]
    property var meta: null   // entry of AgentVM.knowledgeDocuments
    // docs/25: 引用定位目标（0 基 chunk_index；-1 = 无定位）。命中的分块
    // 卡片高亮 + 列表滚动定位，关闭对话框后复位。
    property int targetChunk: -1

    function openFor(name, targetChunkIndex) {
        docName = name
        targetChunk = targetChunkIndex === undefined ? -1 : targetChunkIndex
        open()  // content loads in onAboutToShow
    }

    function reload() {
        chunks = AgentVM.documentChunks(docName)
        meta = null
        const docs = AgentVM.knowledgeDocuments
        for (let i = 0; i < docs.length; ++i) {
            if (docs[i].name === docName) { meta = docs[i]; break }
        }
        // docs/25: 定位——documentChunks 按 chunk_index 排序且连续，列表
        // 下标即 chunk_index。Qt.callLater 让布局先完成再滚动定位。
        if (targetChunk >= 0 && targetChunk < chunks.length) {
            Qt.callLater(function() {
                chunkList.positionViewAtIndex(targetChunk, ListView.Beginning)
                chunkList.currentIndex = targetChunk
            })
        }
    }

    onClosed: targetChunk = -1

    function toClipboardText() {
        var lines = []
        for (var i = 0; i < chunks.length; ++i)
            lines.push("[" + (i + 1) + "/" + chunks.length + "] " + (chunks[i].text || ""))
        return lines.join("\n\n")
    }

    onAboutToShow: reload()

    background: DialogBackground {}

    contentItem: ColumnLayout {
        Accessible.name: qsTr("知识库文档详情")
        Accessible.description: qsTr("查看文档 %1 的分块预览与统计信息").arg(dialog.docName)
        spacing: 12

        // ── Header: icon + name + degraded badge ──────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.topMargin: 24
            Layout.rightMargin: 24
            spacing: 10

            Rectangle {
                width: 36
                height: 36
                radius: 8
                color: Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                               Theme.brandPrimary.b, 0.12)

                AppIcon {
                    anchors.centerIn: parent
                    icon: "folder"
                    size: 18
                    iconColor: Theme.brandPrimary
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: dialog.docName
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Text {
                    text: {
                        if (!dialog.meta) return qsTr("文档已不在知识库中")
                        const embedMode = dialog.meta.pseudo === true
                            ? qsTr("嵌入：本地降级") : qsTr("嵌入：服务")
                        return qsTr("%1 段 · %2 字符 · %3")
                            .arg(dialog.meta.chunks).arg(dialog.meta.chars).arg(embedMode)
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                }
            }

            // Degraded-embedding badge (same semantics as the panel list)
            Rectangle {
                visible: dialog.meta && dialog.meta.pseudo === true
                width: degradedLabel.implicitWidth + 16
                height: 22
                radius: 11
                color: Qt.rgba(Theme.statusWarning.r, Theme.statusWarning.g,
                               Theme.statusWarning.b, 0.15)

                Text {
                    id: degradedLabel
                    anchors.centerIn: parent
                    text: qsTr("降级嵌入")
                    color: Theme.statusWarning
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                }
            }
        }

        // ── Divider ───────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderSubtle
        }

        // ── Chunk list ────────────────────────────────
        ListView {
            id: chunkList
            model: dialog.chunks
            clip: true
            spacing: 8
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24

            ScrollBar.vertical: ScrollBar {}

            Text {
                anchors.centerIn: parent
                visible: chunkList.count === 0
                text: qsTr("没有可显示的分块")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
            }

            delegate: Rectangle {
                width: chunkList.width
                height: chunkText.implicitHeight + chunkHeader.implicitHeight + 26
                radius: 6
                // docs/25: 引用定位命中的分块卡片以品牌色高亮（打开期间
                // 保持，关闭对话框复位）。
                color: dialog.targetChunk === modelData.chunk_index
                    ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                              Theme.brandPrimary.b, 0.12)
                    : Theme.surfaceHover
                border.width: dialog.targetChunk === modelData.chunk_index ? 1 : 0
                border.color: Theme.brandPrimary

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            id: chunkHeader
                            text: qsTr("分块 %1 / %2")
                                .arg(modelData.chunk_index + 1).arg(modelData.chunk_total)
                                + (modelData.page >= 0
                                   ? qsTr(" · 第 %1 页").arg(modelData.page) : "")
                            color: Theme.brandPrimary
                            font.pixelSize: 9
                            font.family: Theme.fontMono
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: (modelData.text || "").length + " chars"
                            color: Theme.textSecondary
                            font.pixelSize: 9
                            font.family: Theme.fontMono
                        }
                    }

                    Text {
                        id: chunkText
                        text: modelData.text || ""
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // ── Footer ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 24
            spacing: 8

            // Copy all chunks (CopyButton carries the "已复制" feedback)
            CopyButton {
                text: dialog.toClipboardText()
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("关闭")
                highlighted: true
                onClicked: dialog.close()
            }
        }
    }
}
