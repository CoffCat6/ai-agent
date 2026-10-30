import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// 日志与诊断：原「通用设置」的日志区块独立成页。
// 日志级别 / 目录 / 轮转只影响本地诊断文件，与模型、界面无关。
Item {
    id: page

    Accessible.name: qsTr("日志与诊断页")

    // 诊断包导出的结果提示（成功时带目录路径，失败时带原因）。
    property string diagnosticsMessage: ""
    property bool diagnosticsIsError: false

    function requestResetDefaults() {
        Settings.resetLoggingToDefaults()
    }

    SettingsPage {
        anchors.fill: parent

        title: qsTr("日志与诊断")
        description: qsTr("日志可能包含本地路径和错误上下文，请选择安全的保存位置。")

        SettingCard {
            Layout.fillWidth: true
            title: qsTr("日志")
            subtitle: qsTr("达到单文件大小上限后自动轮转；只保留最新的指定数量文件。")

            SettingRow {
                objectName: "logging/level"
                label: qsTr("最低记录级别")
                description: qsTr("低于该级别的日志不会写入文件；排查问题时临时调到 debug。")

                ComboBox {
                    id: logLevelCombo
                    Layout.preferredWidth: Theme.controlWidthMd
                    property var levelNames: ["debug", "info", "warning", "critical"]
                    model: levelNames
                    onActivated: Settings.logLevel = currentText
                    Connections {
                        target: Settings
                        function onLogLevelChanged() {
                            const index = logLevelCombo.levelNames.indexOf(Settings.logLevel)
                            logLevelCombo.currentIndex = index >= 0 ? index : 1
                        }
                    }
                    Component.onCompleted: {
                        const index = levelNames.indexOf(Settings.logLevel)
                        currentIndex = index >= 0 ? index : 1
                    }
                    Accessible.name: qsTr("日志级别")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderSubtle
            }

            Text {
                objectName: "logging/directory"
                text: qsTr("日志目录")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontMono
                font.letterSpacing: 0.4
            }
            Text {
                Layout.fillWidth: true
                text: Settings.logDirectory
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                wrapMode: Text.WrapAnywhere
                Accessible.name: qsTr("当前日志目录")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                AppButton { text: qsTr("选择目录"); onClicked: logDirectoryDialog.open() }
                AppButton { text: qsTr("恢复默认目录"); onClicked: Settings.logDirectory = "" }
                AppButton { text: qsTr("打开目录"); onClicked: AgentVM.openLogFolder() }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderSubtle
            }

            SettingRow {
                objectName: "logging/maxFileSizeMb"
                label: qsTr("单个日志文件上限（MB）")
                description: qsTr("达到上限后自动创建新文件。")

                SpinBox {
                    from: 1
                    to: 1024
                    value: Settings.logMaxFileSizeMb
                    editable: true
                    onValueModified: Settings.logMaxFileSizeMb = value
                    Accessible.name: qsTr("单个日志文件大小上限，兆字节")
                }
            }

            SettingRow {
                objectName: "logging/retentionFiles"
                label: qsTr("保留日志文件数")
                description: qsTr("轮转后只保留最新的指定数量文件，更早的自动删除。")

                SpinBox {
                    from: 1
                    to: 100
                    value: Settings.logRetentionFiles
                    editable: true
                    onValueModified: Settings.logRetentionFiles = value
                    Accessible.name: qsTr("保留日志文件数")
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            title: qsTr("诊断包")
            subtitle: qsTr("把脱敏设置、最近的日志和环境信息放进一个目录，反馈问题时附上即可。密钥字段与 MCP 环境变量不会写入。")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                AppButton {
                    objectName: "logging/exportDiagnostics"
                    text: qsTr("导出诊断包")
                    onClicked: diagnosticsFolderDialog.open()
                    Accessible.name: qsTr("选择目录并导出诊断包")
                }
                Text {
                    visible: page.diagnosticsMessage !== ""
                    Layout.fillWidth: true
                    text: page.diagnosticsMessage
                    color: page.diagnosticsIsError ? Theme.statusDanger : Theme.statusSuccess
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WrapAnywhere
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }
    }

    FolderDialog {
        id: logDirectoryDialog
        title: qsTr("选择日志保存目录")
        onAccepted: Settings.logDirectory = selectedFolder.toString()
    }

    FolderDialog {
        id: diagnosticsFolderDialog
        title: qsTr("选择诊断包保存位置")
        onAccepted: {
            // 导出一个新目录 nexus-diagnostics-<时间戳>/，不覆盖旧包。
            const folder = SettingsBackup.exportDiagnostics(selectedFolder.toString())
            page.diagnosticsIsError = folder === ""
            page.diagnosticsMessage = folder !== ""
                ? qsTr("已导出：%1").arg(folder)
                : (SettingsBackup.lastError || qsTr("导出失败。"))
        }
    }
}
