import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

Item {
    id: page

    Accessible.name: qsTr("设置与备份页")

    property string importPath: ""
    property var importPreview: ({})
    property string statusMessage: ""
    property bool statusIsError: false


    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 32
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: flick.width
            spacing: 16

            Item { Layout.preferredHeight: 4 }

            Text {
                text: qsTr("设置与备份")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: 24
            }

            Text {
                text: qsTr("将应用设置保存为 JSON 文件，或从备份文件恢复。已知密钥字段和 SQLite 数据（如聊天记录与长期记忆）不包含在内；自由文本及 MCP 命令参数可能含有敏感内容。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                implicitHeight: exportCol.implicitHeight + 32
                radius: 8
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: exportCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 8

                    Text {
                        text: qsTr("导出设置")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.bold: true
                        font.family: Theme.fontSans
                    }
                    Text {
                        text: qsTr("选择保存位置，生成可用于迁移设置的 JSON 备份。请妥善保管备份文件。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    AppButton {
                        text: qsTr("导出 JSON…")
                        onClicked: exportDialog.open()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                implicitHeight: importCol.implicitHeight + 32
                radius: 8
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: importCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 8

                    Text {
                        text: qsTr("从备份恢复")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.bold: true
                        font.family: Theme.fontSans
                    }
                    Text {
                        text: qsTr("先预览将恢复的设置，再确认导入；高风险权限及开机自启需在恢复后重新确认。建议恢复后重启应用。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    AppButton {
                        text: qsTr("选择 JSON 备份…")
                        onClicked: importDialog.open()
                    }
                }
            }

            Text {
                visible: page.statusMessage.length > 0
                text: page.statusMessage
                color: page.statusIsError ? Theme.statusDanger : Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Accessible.role: Accessible.StaticText
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("导出设置")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON 文件 (*.json)")]
        onAccepted: {
            page.statusIsError = !SettingsBackup.exportSettings(selectedFile.toString())
            page.statusMessage = page.statusIsError
                ? (SettingsBackup.lastError || qsTr("导出失败。"))
                : qsTr("设置已导出。已知密钥字段已过滤；请检查自由文本和 MCP 命令参数。")
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("选择设置备份")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("JSON 文件 (*.json)")]
        onAccepted: {
            page.importPath = ""
            page.importPreview = ({})
            page.statusMessage = ""
            const path = selectedFile.toString()
            const preview = SettingsBackup.previewImport(path)
            if (!preview.valid) {
                page.statusIsError = true
                page.statusMessage = preview.error || SettingsBackup.lastError || qsTr("无法预览此备份文件。")
                return
            }
            page.importPath = path
            page.importPreview = preview
            confirmImportDialog.open()
        }
    }

    Dialog {
        id: confirmImportDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 440
        height: 390
        padding: 20
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("确认恢复设置？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: confirmImportDialog.availableWidth - 16
                    spacing: 10

                    Text {
                        text: qsTr("即将恢复的分类：%1").arg(
                            (page.importPreview.sections || []).length > 0
                                ? page.importPreview.sections.join("、") : qsTr("无"))
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: (page.importPreview.skippedSecrets || []).length > 0
                        text: qsTr("备份不含以下密钥：%1").arg(
                            (page.importPreview.skippedSecrets || []).join("、"))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: (page.importPreview.warnings || []).length > 0
                        text: qsTr("备份提示：%1").arg(
                            (page.importPreview.warnings || []).join("；"))
                        color: Theme.statusDanger
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Text {
                        text: qsTr("匹配相同端点的本机密钥会保留；新端点需要重新配置密钥。SQLite 数据不会恢复。恢复会覆盖相应设置，MCP 服务立即禁用；请重启应用并重新确认高风险权限和开机自启。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: confirmImportDialog.close()
                }
                AppButton {
                    text: qsTr("确认恢复")
                    highlighted: true
                    enabled: page.importPath.length > 0 && page.importPreview.valid === true
                    onClicked: {
                        // Consume the preview before invoking the potentially destructive import.
                        const path = page.importPath
                        page.importPath = ""
                        page.importPreview = ({})
                        confirmImportDialog.close()
                        page.statusIsError = !SettingsBackup.importSettings(path)
                        page.statusMessage = page.statusIsError
                            ? (SettingsBackup.lastError || qsTr("恢复失败。"))
                            : (SettingsBackup.restartRequired
                                ? qsTr("设置已恢复，请重启应用后检查高风险权限和开机自启。")
                                : qsTr("设置已恢复，建议重启应用并重新确认高风险权限和开机自启。"))
                    }
                }
            }
        }
    }
}
