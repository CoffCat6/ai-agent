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
    // docs/24 R4: the backup file staged for restore by the preview dialog.
    property string pendingRestorePath: ""
    property var pendingRestoreInfo: ({})

    // 本页只有自动备份两项是可复位的 M1 键；其余是导出/恢复入口。
    function requestResetDefaults() {
        Settings.resetBackupToDefaults()
    }

    SettingsPage {
        anchors.fill: parent

        title: qsTr("设置与备份")
        description: qsTr("将应用设置保存为 JSON 文件，或从备份文件恢复。已知密钥字段和 SQLite 数据（如聊天记录与长期记忆）不包含在内；自由文本及 MCP 命令参数可能含有敏感内容。")

        SettingCard {
            objectName: "backup/automatic"
            Layout.fillWidth: true
            title: qsTr("自动备份")
            subtitle: qsTr("应用启动时自动生成一份数据库快照；只保留最近指定份数，更早的自动删除。")

            SettingRow {
                objectName: "memory/backupOnStartup"
                label: qsTr("启动时自动备份")
                description: qsTr("关闭后仅在手动点击「立即备份」时生成快照。")

                Switch {
                    checked: Settings.backupOnStartup
                    onToggled: Settings.backupOnStartup = checked
                    Accessible.name: qsTr("启动时自动备份")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderSubtle
            }

            SettingRow {
                objectName: "memory/backupKeepCount"
                label: qsTr("备份保留数量")
                description: qsTr("轮转时只保留最新的指定份数，范围 1–20。")

                SpinBox {
                    from: 1
                    to: 20
                    value: Settings.backupKeepCount
                    editable: true
                    onValueModified: Settings.backupKeepCount = value
                    Accessible.name: qsTr("备份保留数量")
                }
            }
        }

        SettingCard {
            objectName: "backup/settings"
            Layout.fillWidth: true
            title: qsTr("导出设置")
            subtitle: qsTr("选择保存位置，生成可用于迁移设置的 JSON 备份。请妥善保管备份文件。")

            AppButton {
                text: qsTr("导出 JSON…")
                onClicked: exportDialog.open()
            }
        }

        SettingCard {
            Layout.fillWidth: true
            title: qsTr("从备份恢复")
            subtitle: qsTr("先预览将恢复的设置，再确认导入；高风险权限及开机自启需在恢复后重新确认。建议恢复后重启应用。")

            AppButton {
                text: qsTr("选择 JSON 备份…")
                onClicked: importDialog.open()
            }
        }

        // ── docs/23 D1: 数据库备份 ──────────────────────
        SettingCard {
            objectName: "backup/database"
            Layout.fillWidth: true
            title: qsTr("数据库备份")
            subtitle: qsTr("聊天记录、思考轨迹与知识库保存在 SQLite 数据库中，设置 JSON 备份不包含它们。启动时会自动备份并保留最近 %1 份；此处可随时手动备份，或预览任一备份文件的内容概况。").arg(Settings.backupKeepCount)

            RowLayout {
                spacing: Theme.spaceSm
                AppButton {
                    text: qsTr("立即备份")
                    onClicked: {
                        const err = AgentVM.backupDatabaseNow()
                        page.statusIsError = err.length > 0
                        page.statusMessage = err.length > 0
                            ? err
                            : qsTr("数据库已备份到 %1。").arg(
                                  AgentVM.lastDatabaseBackupPath())
                    }
                }
                AppButton {
                    text: qsTr("预览备份文件…")
                    onClicked: previewDialog.open()
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
            Accessible.role: Accessible.StaticText
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

    // docs/23 D1: read-only peek into a SQLite backup snapshot.
    FileDialog {
        id: previewDialog
        title: qsTr("选择数据库备份")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SQLite 数据库 (*.db)")]
        onAccepted: {
            const info = AgentVM.previewDatabaseBackup(selectedFile.toString())
            if (info.error) {
                page.statusIsError = true
                page.statusMessage = qsTr("无法读取该备份文件：%1").arg(info.error)
                return
            }
            page.pendingRestorePath = selectedFile.toString()
            page.pendingRestoreInfo = info
            page.statusIsError = !info.integrityOk
            page.statusMessage = qsTr("会话 %1 个，消息 %2 条，知识库文档 %3 份，最新消息时间 %4%5")
                .arg(info.sessions)
                .arg(info.messages)
                .arg(info.knowledgeDocs)
                .arg(info.newestTimestamp > 0
                     ? new Date(info.newestTimestamp).toLocaleString()
                     : qsTr("（无消息）"))
                .arg(info.integrityOk
                     ? ""
                     : qsTr("；警告：完整性检查未通过，可能已损坏"))
            // docs/24 R4: a healthy snapshot goes straight to the restore
            // confirmation; a broken one stays preview-only.
            if (info.integrityOk)
                confirmRestoreDialog.open()
        }
    }

    // docs/24 R4: restore confirmation — the swap closes all tabs and stops
    // running tasks, and the GUI blocks for the duration of the swap.
    Dialog {
        id: confirmRestoreDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 440
        padding: Theme.spaceXl
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: qsTr("恢复数据库")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }
            Text {
                text: qsTr("将把当前数据库替换为该备份：会话 %1 个、消息 %2 条。恢复会关闭全部标签并停止在跑任务，期间界面会短暂无响应。当前未备份的新数据将丢失（启动自动备份仍可用）。")
                    .arg(page.pendingRestoreInfo.sessions)
                    .arg(page.pendingRestoreInfo.messages)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spaceSm
                AppButton {
                    text: qsTr("取消")
                    onClicked: confirmRestoreDialog.close()
                }
                AppButton {
                    text: qsTr("恢复")
                    highlighted: true
                    onClicked: {
                        confirmRestoreDialog.close()
                        const err = AgentVM.restoreDatabaseFrom(
                            page.pendingRestorePath)
                        page.statusIsError = err.length > 0
                        page.statusMessage = err.length > 0
                            ? err
                            : qsTr("数据库已恢复，知识库正在重新装载。")
                    }
                }
            }
        }
    }

    Dialog {
        id: confirmImportDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 440
        height: 390
        padding: Theme.spaceXl
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

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
                    width: confirmImportDialog.availableWidth - Theme.spaceLg
                    spacing: Theme.spaceMd

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
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: (page.importPreview.warnings || []).length > 0
                        text: qsTr("备份提示：%1").arg(
                            (page.importPreview.warnings || []).join("；"))
                        color: Theme.statusDanger
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Text {
                        text: qsTr("匹配相同端点的本机密钥会保留；新端点需要重新配置密钥。SQLite 数据不会恢复。恢复会覆盖相应设置，MCP 服务立即禁用；请重启应用并重新确认高风险权限和开机自启。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
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
