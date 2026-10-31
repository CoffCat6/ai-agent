import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/31 S4.5 N5: 数据管理总览。DB 体积（主文件 + WAL）与各表行数的
// 只读统计，加三个维护动作：按保留天数立即清理、清空用量、VACUUM。
// 统计走 AgentVM.requestDataStats()（QtConcurrent + 自包含短连接），
// 结果经 dataStatsReady 回来——页面只消费，绝不自己碰数据库。
//
// 红线 6：三个动作都会删数据或独占数据库，全部二次确认；确认框预告
// 后果（清空不可撤销、VACUUM 期间界面短暂卡顿），并提示可先备份。
Item {
    id: page

    Accessible.name: qsTr("数据管理页")
    Accessible.description: qsTr("查看数据库体积与各表行数，执行立即清理、清空用量与压缩数据库")

    // collectStats 的返回（见 SqliteMemory::collectStats 的键说明）。
    property var stats: ({})
    property bool statsLoading: false
    property string statusMessage: ""
    property bool statusIsError: false
    // 待二次确认的动作 id：prune | clearUsage | vacuum。
    property string pendingAction: ""
    // 备份目录最新一份快照（databaseBackups() 首项），仅作状态展示。
    property var latestBackup: ({})

    Component.onCompleted: refresh()

    Connections {
        target: AgentVM
        function onDataStatsReady(result) {
            page.statsLoading = false
            page.stats = result
            if (result.error) {
                page.statusIsError = true
                page.statusMessage = qsTr("统计失败：%1").arg(result.error)
            }
        }
    }

    function refresh() {
        page.statsLoading = true
        AgentVM.requestDataStats()
        const backups = AgentVM.databaseBackups()
        page.latestBackup = backups.length > 0 ? backups[0] : {}
    }

    function formatSize(bytes) {
        const v = Number(bytes) || 0
        if (v >= 1048576) return (v / 1048576).toFixed(1) + " MB"
        if (v >= 1024) return Math.round(v / 1024) + " KB"
        return v + " B"
    }

    function countOf(key) {
        const v = page.stats[key]
        return v === undefined ? "—" : String(v)
    }

    function showStatus(message, isError) {
        page.statusMessage = message
        page.statusIsError = isError
    }

    function runPendingAction() {
        let err = ""
        if (page.pendingAction === "prune") {
            err = AgentVM.pruneByRetentionNow()
            if (err === "")
                showStatus(qsTr("已按保留天数清理审计与用量记录"), false)
        } else if (page.pendingAction === "clearUsage") {
            err = AgentVM.clearUsageNow()
            if (err === "")
                showStatus(qsTr("已清空全部 Token 用量记录"), false)
        } else if (page.pendingAction === "vacuum") {
            err = AgentVM.vacuumDatabaseNow()
            if (err === "")
                showStatus(qsTr("数据库已压缩"), false)
        }
        page.pendingAction = ""
        if (err !== "") {
            showStatus(err, true)
            return
        }
        refresh()
    }

    SettingsPage {
        anchors.fill: parent

        title: qsTr("数据管理")
        description: qsTr("数据库体积、各表行数与维护操作。删除类操作前建议先在「备份与恢复」生成一份快照。")

        SettingCard {
            objectName: "storage/overview"
            Layout.fillWidth: true
            title: qsTr("数据库概览")
            subtitle: page.stats.dbPath !== undefined
                      ? page.stats.dbPath : qsTr("统计加载中…")

            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: Theme.spaceLg
                rowSpacing: Theme.spaceSm

                Text {
                    objectName: "storageDbSizeText"
                    Layout.fillWidth: true
                    text: qsTr("数据库文件：%1").arg(page.formatSize(page.stats.fileSizeBytes))
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                }
                Text {
                    objectName: "storageWalSizeText"
                    Layout.fillWidth: true
                    text: qsTr("WAL 文件：%1").arg(page.formatSize(page.stats.walSizeBytes))
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                }

                // 行数网格：标签 + 数值两列一组的紧凑呈现。表名与
                // collectStats 的键一一对应，缺表（旧库未迁移）显示 —。
                Repeater {
                    model: [
                        { key: "sessions", label: qsTr("会话") },
                        { key: "messages", label: qsTr("消息") },
                        { key: "episodic", label: qsTr("长期记忆") },
                        { key: "knowledgeDocuments", label: qsTr("知识库文档") },
                        { key: "knowledgeChunks", label: qsTr("知识库分块") },
                        { key: "traceNodes", label: qsTr("过程轨迹") },
                        { key: "fileCheckpoints", label: qsTr("文件检查点") },
                        { key: "usageLog", label: qsTr("活动记录") },
                        { key: "tokenUsage", label: qsTr("Token 用量") },
                        { key: "auditLog", label: qsTr("审计记录") }
                    ]
                    Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.label + qsTr("：%1").arg(page.countOf(modelData.key))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spaceSm
                AppButton {
                    objectName: "storageRefreshButton"
                    text: page.statsLoading ? qsTr("统计中…") : qsTr("刷新统计")
                    enabled: !page.statsLoading
                    onClicked: page.refresh()
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: page.statusMessage !== ""
                    text: page.statusMessage
                    color: page.statusIsError ? Theme.statusDanger : Theme.statusSuccess
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    Accessible.role: Accessible.StatusBar
                    Accessible.name: text
                }
            }
        }

        SettingCard {
            objectName: "storage/maintenance"
            Layout.fillWidth: true
            title: qsTr("维护操作")
            subtitle: qsTr("这些操作会删除数据或独占数据库，点击后都需要二次确认；建议先备份。")

            ColumnLayout {
                width: parent.width
                spacing: Theme.spaceMd

                SettingRow {
                    objectName: "storage/pruneNow"
                    label: qsTr("按保留天数清理")
                    description: qsTr("按「记忆管理」页设置的保留天数，立即清理过期的审计与 Token 用量记录。")

                    AppButton {
                        objectName: "storagePruneButton"
                        text: qsTr("立即清理")
                        onClicked: {
                            page.pendingAction = "prune"
                            storageConfirmDialog.open()
                        }
                    }
                }

                SettingRow {
                    objectName: "storage/clearUsage"
                    label: qsTr("清空用量")
                    description: qsTr("删除全部 Token 用量明细；用量统计与配额计数随之归零，无法撤销。")

                    AppButton {
                        objectName: "storageClearUsageButton"
                        text: qsTr("清空")
                        onClicked: {
                            page.pendingAction = "clearUsage"
                            storageConfirmDialog.open()
                        }
                    }
                }

                SettingRow {
                    objectName: "storage/vacuum"
                    label: qsTr("压缩数据库")
                    description: qsTr("执行 VACUUM 回收已删除数据占用的空间。期间界面可能短暂卡顿；数据库被占用时会失败。")

                    AppButton {
                        objectName: "storageVacuumButton"
                        text: qsTr("压缩")
                        onClicked: {
                            page.pendingAction = "vacuum"
                            storageConfirmDialog.open()
                        }
                    }
                }
            }
        }

        SettingCard {
            objectName: "storage/backupEntry"
            Layout.fillWidth: true
            title: qsTr("备份入口")
            subtitle: page.latestBackup.fileName !== undefined
                      ? qsTr("最近备份：%1（%2，%3）")
                            .arg(page.latestBackup.fileName)
                            .arg(page.formatSize(page.latestBackup.sizeBytes))
                            .arg(new Date(page.latestBackup.mtimeMs).toLocaleString())
                      : qsTr("备份目录中还没有快照；「备份与恢复」页可生成与恢复。")

            RowLayout {
                width: parent.width
                spacing: Theme.spaceSm

                AppButton {
                    objectName: "storageBackupNowButton"
                    text: qsTr("立即备份")
                    onClicked: {
                        const err = AgentVM.backupDatabaseNow()
                        if (err === "") {
                            showStatus(qsTr("备份已生成"), false)
                            page.refresh()
                        } else {
                            showStatus(err, true)
                        }
                    }
                }
                AppButton {
                    objectName: "storageOpenBackupsButton"
                    text: qsTr("打开备份目录")
                    onClicked: AgentVM.openDatabaseBackupsFolder()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    // 三个动作共用的二次确认框。标题/正文随 pendingAction 变化，测试经
    // objectName 定位确认按钮，模拟真实点击链路。
    Dialog {
        id: storageConfirmDialog
        objectName: "storageConfirmDialog"
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 420
        padding: Theme.spaceXl
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: page.pendingAction === "prune"
                      ? qsTr("立即清理过期记录？")
                      : page.pendingAction === "clearUsage"
                        ? qsTr("清空全部 Token 用量？")
                        : qsTr("压缩数据库？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }
            Text {
                Layout.fillWidth: true
                text: page.pendingAction === "prune"
                      ? qsTr("将删除超过保留天数的审计与用量记录，无法撤销。")
                      : page.pendingAction === "clearUsage"
                        ? qsTr("全部 Token 用量明细将被删除，用量统计与配额计数归零，无法撤销。")
                        : qsTr("执行期间界面可能短暂卡顿；若数据库正被占用会失败，可稍后重试。")
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
                    objectName: "storageCancelButton"
                    text: qsTr("取消")
                    onClicked: {
                        page.pendingAction = ""
                        storageConfirmDialog.close()
                    }
                }
                AppButton {
                    objectName: "storageConfirmButton"
                    text: qsTr("确认执行")
                    highlighted: true
                    onClicked: {
                        storageConfirmDialog.close()
                        page.runPendingAction()
                    }
                }
            }
        }
    }
}
