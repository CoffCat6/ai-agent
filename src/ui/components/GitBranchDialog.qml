import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Read-only branch view: local branches with their upstream and how far the
// branch has drifted from it, plus the configured remotes and their tracking
// refs. Switching, creating and fetching are deliberately absent — this panel
// only reports what the repository already knows.
Dialog {
    id: dialog

    property var branches: []
    property var remotes: []
    property var tags: []
    property bool loading: false
    property string errorText: ""
    property string requestToken: ""

    readonly property int branchCount: branches.length
    readonly property int remoteCount: remotes.length
    readonly property int tagCount: tags.length

    function reload() {
        branches = []
        remotes = []
        tags = []
        errorText = ""
        requestToken = ""
        loading = true
        requestToken = GitVM.requestBranchView()
        if (requestToken.length === 0) loading = false
    }

    function openFor(refresh) {
        open()
        if (refresh === true || dialog.branchCount === 0)
            dialog.reload()
    }

    function trackLabel(entry) {
        if (!entry.upstream || entry.upstream.length === 0)
            return qsTr("无上游")
        if (entry.trackState === "gone")
            return qsTr("上游已删除")
        if (entry.trackState === "diverged")
            return qsTr("↑%1 ↓%2").arg(entry.ahead).arg(entry.behind)
        if (entry.trackState === "ahead")
            return qsTr("↑%1").arg(entry.ahead)
        if (entry.trackState === "behind")
            return qsTr("↓%1").arg(entry.behind)
        return qsTr("已同步")
    }

    Connections {
        target: GitVM

        function onBranchViewReady(requestId, payload, error) {
            if (requestId !== dialog.requestToken) return
            dialog.branches = payload.branches || []
            dialog.remotes = payload.remotes || []
            dialog.tags = payload.tags || []
            dialog.errorText = error
            dialog.loading = false
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 48 : 560)
    height: Math.min(560, parent ? parent.height - 80 : 560)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            spacing: 8

            AppIcon {
                icon: "branch"
                size: 14
                iconColor: Theme.brandPrimary
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("分支与远端")
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
            }

            IconButton {
                icon: "refresh"
                enabled: !dialog.loading
                opacity: enabled ? 1.0 : 0.35
                tooltip: qsTr("重新读取分支信息")
                onClicked: dialog.reload()
            }

            IconButton {
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭分支视图")
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: qsTr("仅查看：本面板不切换分支、不创建分支，也不访问远端。")
            color: Theme.textTertiary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeMicro
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.loading
            text: qsTr("正在读取分支…")
            color: Theme.textSecondary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: !dialog.loading && dialog.errorText.length > 0
            text: dialog.errorText
            color: Theme.statusWarning
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.Wrap
        }

        // Flickable + Repeater: a ListView here would collapse to height 0
        // inside this layout and never create its delegates.
        Flickable {
            objectName: "gitBranchFlickable"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            visible: !dialog.loading && (dialog.branchCount > 0
                                         || dialog.remoteCount > 0
                                         || dialog.tagCount > 0)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentHeight: branchColumn.implicitHeight
            ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: branchColumn
                width: parent ? parent.width : 0
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    visible: dialog.branchCount > 0
                    text: qsTr("本地分支 · %1").arg(dialog.branchCount)
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                }

                Repeater {
                    model: dialog.branches

                    delegate: Rectangle {
                        id: branchRow
                        objectName: "gitBranchRow"
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        height: 38
                        radius: 6
                        color: modelData.current ? Theme.surfaceHover : "transparent"
                        border.color: Theme.borderSubtle
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: branchRow.modelData.name
                                color: Theme.textPrimary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeCaption
                                elide: Text.ElideRight
                            }

                            Text {
                                visible: branchRow.modelData.current
                                text: qsTr("当前")
                                color: Theme.brandPrimary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeNano
                                font.bold: true
                            }

                            Text {
                                text: dialog.trackLabel(branchRow.modelData)
                                color: branchRow.modelData.trackState === "gone"
                                    ? Theme.statusWarning : Theme.textSecondary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeNano
                            }

                            Text {
                                visible: branchRow.modelData.upstream.length > 0
                                text: branchRow.modelData.upstream
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeNano
                                elide: Text.ElideMiddle
                                Layout.maximumWidth: 130
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.remoteCount > 0
                    text: qsTr("远端 · %1").arg(dialog.remoteCount)
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                }

                Repeater {
                    model: dialog.remotes

                    delegate: ColumnLayout {
                        id: remoteBlock
                        objectName: "gitRemoteBlock"
                        required property var modelData

                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            Layout.fillWidth: true
                            text: remoteBlock.modelData.name
                            color: Theme.textPrimary
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: remoteBlock.modelData.url.length > 0
                            text: remoteBlock.modelData.url
                            color: Theme.textTertiary
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeNano
                            elide: Text.ElideMiddle
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("跟踪分支 %1 个")
                                .arg(remoteBlock.modelData.branchCount)
                            color: Theme.textSecondary
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.fontSizeNano
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.tagCount > 0
                    text: qsTr("标签 · %1").arg(dialog.tagCount)
                    color: Theme.textSecondary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                }

                // Read-only: creating, moving or deleting a tag is a repository
                // write and is not offered anywhere in this panel.
                Repeater {
                    model: dialog.tags

                    delegate: Rectangle {
                        id: tagRow
                        objectName: "gitTagRow"
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        height: 26
                        radius: 5
                        color: "transparent"
                        border.color: Theme.borderSubtle
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8

                            Text {
                                text: tagRow.modelData.name
                                color: Theme.brandPrimary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeCaption
                                elide: Text.ElideRight
                                Layout.maximumWidth: 150
                            }

                            Text {
                                Layout.fillWidth: true
                                text: tagRow.modelData.subject
                                color: Theme.textSecondary
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.fontSizeNano
                                elide: Text.ElideRight
                            }

                            Text {
                                text: tagRow.modelData.target
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeNano
                            }

                            Text {
                                // Annotated tags have their own object and date;
                                // lightweight ones are only a pointer.
                                text: tagRow.modelData.annotated
                                    ? qsTr("附注") : qsTr("轻量")
                                color: Theme.textTertiary
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.fontSizeNano
                            }
                        }
                    }
                }
            }
        }
    }
}
