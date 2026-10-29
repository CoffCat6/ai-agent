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

    function requestResetDefaults() {
        Settings.resetLoggingToDefaults()
    }

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

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("日志与诊断")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("日志可能包含本地路径和错误上下文，请选择安全的保存位置。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            SettingCard {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.bottomMargin: 16
                title: qsTr("日志")
                subtitle: qsTr("达到单文件大小上限后自动轮转；只保留最新的指定数量文件。")

                SettingRow {
                    objectName: "logging/level"
                    label: qsTr("最低记录级别")
                    description: qsTr("低于该级别的日志不会写入文件；排查问题时临时调到 debug。")

                    ComboBox {
                        id: logLevelCombo
                        Layout.preferredWidth: 160
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
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
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
                    spacing: 8
                    AppButton { text: qsTr("选择目录"); onClicked: logDirectoryDialog.open() }
                    AppButton { text: qsTr("恢复默认目录"); onClicked: Settings.logDirectory = "" }
                    AppButton { text: qsTr("打开目录"); onClicked: AgentVM.openLogFolder() }
                }
                FolderDialog {
                    id: logDirectoryDialog
                    title: qsTr("选择日志保存目录")
                    onAccepted: Settings.logDirectory = selectedFolder.toString()
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

            Item { Layout.fillHeight: true }
        }
    }
}
