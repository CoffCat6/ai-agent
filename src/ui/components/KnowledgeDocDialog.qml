import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

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
    property var chunks: []   // [{chunk_index, chunk_total, text, ...}]
    property var meta: null   // entry of AgentVM.knowledgeDocuments

    function openFor(name) {
        docName = name
        open()  // content loads in onAboutToShow
    }

    function reload() {
        chunks = AgentVM.documentChunks(docName)
        meta = null
        const docs = AgentVM.knowledgeDocuments
        for (let i = 0; i < docs.length; ++i) {
            if (docs[i].name === docName) { meta = docs[i]; break }
        }
    }

    function toClipboardText() {
        var lines = []
        for (var i = 0; i < chunks.length; ++i)
            lines.push("[" + (i + 1) + "/" + chunks.length + "] " + (chunks[i].text || ""))
        return lines.join("\n\n")
    }

    onAboutToShow: reload()

    background: Rectangle {
        color: Theme.bgSecondary
        radius: 8
        border.width: 1
        border.color: Theme.borderSubtle
    }

    contentItem: ColumnLayout {
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
                color: Theme.surfaceHover

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
