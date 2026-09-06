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

    color: Theme.bgSecondary
    border.width: 0
    clip: true

    // Fade the content out while collapsing
    readonly property real contentFade: AgentVM.isPanelVisible ? 1.0 : 0.0

    // Document import dialog (handled off-GUI-thread by AgentViewModel)
    FileDialog {
        id: kbFileDialog
        nameFilters: [qsTr("文本文件 (*.txt *.md *.json *.csv *.log)"), qsTr("所有文件 (*)")]
        onAccepted: AgentVM.importDocument(selectedFile)
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
                Layout.fillWidth: true
            }

            Text {
                // Privacy statement (audit P1-7): local storage/retrieval does
                // NOT mean the content stays local - passages travel with the
                // request to whichever model service is configured.
                text: qsTr("文档内容会随提问发送给当前模型服务")
                color: Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontSans
                Layout.fillWidth: true
                visible: AgentVM.knowledgeDocuments.length > 0
            }

            // Import-in-progress indicator (embedding runs in background)
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
                    text: qsTr("导入中…")
                    color: Theme.brandPrimary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
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
                tooltip: qsTr("导入文档")
                onClicked: kbFileDialog.open()
            }
        }

        Text {
            visible: AgentVM.knowledgeDocuments.length === 0
            text: qsTr("导入文本文件（.txt / .md …），\n模型即可检索其内容回答问题")
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
                                text: modelData.chunks + qsTr(" 段 · ") + modelData.chars + qsTr(" 字符")
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
