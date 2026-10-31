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
    // docs/31 O3: 备份目录里的快照列表（最新在前）。
    property var backups: []
    property string pendingDeletePath: ""
    // docs/31 O5: 导出范围（分类 id）。空数组 = 全部设置。
    property var exportCategories: []

    Component.onCompleted: refreshBackups()

    function refreshBackups() {
        backups = AgentVM.databaseBackups()
    }

    function exportScopeLabel() {
        if (page.exportCategories.length === 0) return qsTr("全部设置")
        const names = []
        for (let i = 0; i < page.exportCategories.length; ++i)
            names.push(PreferenceRegistry.categoryName(page.exportCategories[i]))
        return names.join(qsTr("、"))
    }

    function formatSize(bytes) {
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB"
        if (bytes >= 1024) return Math.round(bytes / 1024) + " KB"
        return bytes + " B"
    }

    // restore=false 只填摘要；restore=true 在完整性通过时直接弹恢复确认。
    // 损坏的备份只允许预览，不允许恢复（沿用 docs/24 R4）。
    function previewBackup(path, restore) {
        const info = AgentVM.previewDatabaseBackup(path)
        if (info.error) {
            page.statusIsError = true
            page.statusMessage = qsTr("无法读取该备份文件：%1").arg(info.error)
            return
        }
        page.pendingRestorePath = path
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
        if (restore && info.integrityOk)
            confirmRestoreDialog.open()
    }

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
            subtitle: qsTr("应用启动时可生成一份数据库快照，也可按固定间隔定时备份；只保留最近指定份数，更早的自动删除。")

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

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderSubtle
            }

            // docs/31 S4.2 N6：定时备份只开放五档（0 = 关闭）。写回的是
            // 小时数；SettingsService 会再归一一次，导入的旧配置也不会出现
            // 下拉框里选不中的值。
            SettingRow {
                objectName: "memory/backupIntervalHours"
                label: qsTr("定时备份间隔")
                description: qsTr("应用运行期间按此间隔生成快照；关闭后只保留启动备份与手动备份。")

                ComboBox {
                    id: backupIntervalCombo
                    Layout.preferredWidth: Theme.controlWidthSm
                    property var hourOptions: [0, 1, 6, 24, 168]
                    model: [qsTr("关闭"), qsTr("每 1 小时"), qsTr("每 6 小时"),
                            qsTr("每 1 天"), qsTr("每 7 天")]
                    Component.onCompleted: {
                        const index = hourOptions.indexOf(Settings.backupIntervalHours)
                        currentIndex = index >= 0 ? index : 0
                    }
                    onActivated: Settings.backupIntervalHours = hourOptions[currentIndex]
                    Connections {
                        target: Settings
                        function onBackupIntervalHoursChanged() {
                            const index = backupIntervalCombo.hourOptions.indexOf(
                                Settings.backupIntervalHours)
                            backupIntervalCombo.currentIndex = index >= 0 ? index : 0
                        }
                    }
                    Accessible.name: qsTr("定时备份间隔")
                }
            }
        }

        SettingCard {
            objectName: "backup/settings"
            Layout.fillWidth: true
            title: qsTr("导出设置")
            subtitle: qsTr("选择保存位置，生成可用于迁移设置的 JSON 备份。可按分类只导出需要的部分；请妥善保管备份文件。")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("导出范围：%1").arg(page.exportScopeLabel())
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }
                AppButton {
                    objectName: "backup/exportScope"
                    text: qsTr("选择范围…")
                    onClicked: exportScopeDialog.open()
                }
                AppButton {
                    objectName: "backup/export"
                    text: qsTr("导出 JSON…")
                    onClicked: exportDialog.open()
                }
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
                        page.refreshBackups()
                    }
                }
                AppButton {
                    text: qsTr("打开备份目录")
                    onClicked: AgentVM.openDatabaseBackupsFolder()
                }
                AppButton {
                    text: qsTr("从其他位置恢复…")
                    onClicked: previewDialog.open()
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("刷新")
                    onClicked: page.refreshBackups()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderSubtle
            }

            Text {
                visible: page.backups.length === 0
                Layout.fillWidth: true
                text: qsTr("备份目录里还没有快照。开启「启动时自动备份」后，应用每次启动都会生成一份。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }

            // 列表本身就是"有哪些备份"的答案：时间、大小、来源，加上
            // 预览/恢复/删除。删除前二次确认，且只允许删备份目录内的文件。
            Repeater {
                model: page.backups

                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: autoBadge.implicitWidth + 10
                        height: 16
                        radius: height / 2
                        color: modelData.isAuto
                            ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                      Theme.brandPrimary.b, 0.12)
                            : Theme.surfaceHover
                        Text {
                            id: autoBadge
                            anchors.centerIn: parent
                            text: modelData.isAuto ? qsTr("自动") : qsTr("手动")
                            color: modelData.isAuto ? Theme.brandPrimary
                                                    : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: new Date(modelData.mtimeMs).toLocaleString()
                                  + " · " + page.formatSize(modelData.sizeBytes)
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.fileName
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                        }
                    }

                    AppButton {
                        text: qsTr("预览")
                        onClicked: page.previewBackup(modelData.path, false)
                    }
                    AppButton {
                        text: qsTr("恢复")
                        onClicked: page.previewBackup(modelData.path, true)
                    }
                    IconButton {
                        icon: "trash"
                        iconSize: 14
                        tooltip: qsTr("删除此备份")
                        Layout.alignment: Qt.AlignVCenter
                        Accessible.name: qsTr("删除备份 %1").arg(modelData.fileName)
                        onClicked: {
                            page.pendingDeletePath = modelData.path
                            confirmDeleteDialog.open()
                        }
                    }
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
            page.statusIsError = !SettingsBackup.exportSettings(
                selectedFile.toString(), page.exportCategories)
            page.statusMessage = page.statusIsError
                ? (SettingsBackup.lastError || qsTr("导出失败。"))
                : qsTr("设置已导出（%1）。已知密钥字段已过滤；请检查自由文本和 MCP 命令参数。")
                    .arg(page.exportScopeLabel())
        }
    }

    // docs/31 O5：分类多选。options 是带 selected 的副本，重开时按页面
    // 上的选择重建；勾选一次整体替换数组，让 Repeater 重新绑定 checked
    // （CheckBox 的用户点击会截断 checked 的绑定）。
    Dialog {
        id: exportScopeDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 460
        height: 480
        padding: Theme.spaceXl
        background: DialogBackground {}

        property var options: []

        function reloadOptions() {
            const ids = SettingsBackup.exportableCategories()
            const list = []
            for (let i = 0; i < ids.length; ++i) {
                list.push({
                    id: ids[i],
                    name: PreferenceRegistry.categoryName(ids[i]),
                    selected: page.exportCategories.indexOf(ids[i]) >= 0
                })
            }
            options = list
        }

        onOpened: reloadOptions()

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: qsTr("选择导出范围")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("只导出选中的分类；一个都不选等于导出全部设置。模型配置与 MCP 服务器分别归入「模型配置」「MCP 服务器」。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: exportScopeDialog.availableWidth - Theme.spaceLg
                    spacing: 0

                    Repeater {
                        model: exportScopeDialog.options

                        delegate: CheckBox {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            text: modelData.name
                            checked: modelData.selected
                            Accessible.name: modelData.name
                            onToggled: {
                                const next = exportScopeDialog.options.slice()
                                next[index] = {
                                    id: modelData.id,
                                    name: modelData.name,
                                    selected: checked
                                }
                                exportScopeDialog.options = next
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: exportScopeDialog.close()
                }
                AppButton {
                    text: qsTr("确定")
                    highlighted: true
                    onClicked: {
                        const selected = []
                        for (let i = 0; i < exportScopeDialog.options.length; ++i) {
                            if (exportScopeDialog.options[i].selected)
                                selected.push(exportScopeDialog.options[i].id)
                        }
                        page.exportCategories = selected
                        exportScopeDialog.close()
                    }
                }
            }
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
        onAccepted: page.previewBackup(selectedFile.toString(), true)
    }

    // docs/31 O3：删除二次确认。路径校验在后端（只允许备份目录内的文件），
    // 这里只负责让用户再点一次。
    Dialog {
        id: confirmDeleteDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 420
        padding: Theme.spaceXl
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: qsTr("删除这份备份？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("备份文件将从备份目录中永久删除，无法撤销。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: confirmDeleteDialog.close()
                }
                AppButton {
                    text: qsTr("删除")
                    highlighted: true
                    onClicked: {
                        confirmDeleteDialog.close()
                        const err = AgentVM.deleteDatabaseBackup(page.pendingDeletePath)
                        page.pendingDeletePath = ""
                        page.statusIsError = err.length > 0
                        page.statusMessage = err.length > 0
                            ? err : qsTr("备份已删除。")
                        page.refreshBackups()
                    }
                }
            }
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
        objectName: "backup/importConfirmDialog"
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 500
        height: 560
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
                    // docs/31 O4：逐键差异。只列会变化或新增的键，按分类
                    // 分组的标题由 changes[i-1] 与当前项比较得到；完整列表
                    // 最多 200 条，超出部分只报数量。
                    Text {
                        Layout.fillWidth: true
                        text: (page.importPreview.changesTotal || 0) > 0
                            ? qsTr("将被改变的设置（%1 项）：").arg(
                                  page.importPreview.changesTotal)
                            : qsTr("备份中的设置与当前值完全一致，恢复不会改变任何值。")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        objectName: "backup/importChanges"
                        model: page.importPreview.changes || []

                        delegate: ColumnLayout {
                            required property var modelData
                            required property int index
                            objectName: "backup/importChangeRow"
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                visible: index === 0
                                    || (page.importPreview.changes[index - 1]
                                            .categoryName !== modelData.categoryName)
                                text: modelData.categoryName
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                                font.bold: true
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm

                                Text {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: modelData.title
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.maximumWidth: 120
                                    text: modelData.currentDisplay
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: "→"
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                }
                                Text {
                                    Layout.maximumWidth: 120
                                    text: modelData.importedDisplay
                                    color: Theme.brandPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: (page.importPreview.changesTruncated || 0) > 0
                        text: qsTr("另有 %1 项变更未列出。").arg(
                            page.importPreview.changesTruncated)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
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
