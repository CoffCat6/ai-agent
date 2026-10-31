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

    // docs/31 O6：最近日志面板的本地状态。只持有一次尾部读取的结果，
    // 面板本身不写任何设置（级别/行数/关键字都是本页的查看参数）。
    property var logTail: ({ ok: false, lines: [], matched: 0, truncated: false,
                             path: "", error: "" })
    property int tailLines: 200
    property string tailLevel: ""
    property string tailKeyword: ""
    property bool followTail: true

    // 只读文件尾部（见 LogTailReader），因此超大日志不会整读进内存。
    function refreshTail() {
        page.logTail = LogTail.read("", page.tailLines, page.tailLevel,
                                    page.tailKeyword)
        if (page.followTail && tailList.count > 0)
            tailList.positionViewAtEnd()
    }

    // 日志行按级别着色：WARN 用警示色、CRIT/FATAL 用危险色。
    function tailLineColor(line) {
        if (line.indexOf("[CRIT]") >= 0 || line.indexOf("[FATAL]") >= 0)
            return Theme.statusDanger
        if (line.indexOf("[WARN]") >= 0)
            return Theme.statusWarning
        return Theme.textPrimary
    }

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
            objectName: "logging/tail"
            Layout.fillWidth: true
            title: qsTr("最近日志")
            subtitle: qsTr("只读取日志文件的尾部（而不是整个文件），用于在应用内确认刚才发生了什么。修改级别/行数/关键字后立即生效。")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                ComboBox {
                    id: tailLevelCombo
                    objectName: "logging/tailLevel"
                    Layout.preferredWidth: Theme.controlWidthSm
                    property var options: [
                        { label: qsTr("全部级别"), value: "" },
                        { label: qsTr("调试及以上"), value: "debug" },
                        { label: qsTr("信息及以上"), value: "info" },
                        { label: qsTr("警告及以上"), value: "warning" },
                        { label: qsTr("严重"), value: "critical" }
                    ]
                    model: options
                    textRole: "label"
                    currentIndex: 0
                    onActivated: {
                        page.tailLevel = options[currentIndex].value
                        page.refreshTail()
                    }
                    Accessible.name: qsTr("日志级别过滤")
                }

                ComboBox {
                    id: tailLinesCombo
                    objectName: "logging/tailLines"
                    Layout.preferredWidth: Theme.controlWidthXs
                    property var counts: [
                        { label: qsTr("%1 行").arg(200), value: 200 },
                        { label: qsTr("%1 行").arg(500), value: 500 },
                        { label: qsTr("%1 行").arg(1000), value: 1000 },
                        { label: qsTr("%1 行").arg(2000), value: 2000 }
                    ]
                    model: counts
                    textRole: "label"
                    currentIndex: 0
                    onActivated: {
                        page.tailLines = counts[currentIndex].value
                        page.refreshTail()
                    }
                    Accessible.name: qsTr("显示行数")
                }

                TextField {
                    id: tailKeywordField
                    objectName: "logging/tailKeyword"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 120
                    placeholderText: qsTr("关键字过滤（大小写不敏感）")
                    selectByMouse: true
                    // 每次按键都重读文件没必要：合并成一次读取。
                    onTextChanged: {
                        page.tailKeyword = text
                        tailKeywordTimer.restart()
                    }
                    Accessible.name: qsTr("关键字过滤")
                }

                AppButton {
                    objectName: "logging/tailRefresh"
                    text: qsTr("刷新")
                    onClicked: page.refreshTail()
                    Accessible.name: qsTr("重新读取最近日志")
                }

                Text {
                    text: qsTr("跟随最新")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }

                Switch {
                    objectName: "logging/tailFollow"
                    checked: page.followTail
                    onToggled: page.followTail = checked
                    Accessible.name: qsTr("每 2 秒刷新并滚动到最新一行")
                }
            }

            Text {
                objectName: "logging/tailStatus"
                Layout.fillWidth: true
                text: {
                    const result = page.logTail
                    if (!result.ok)
                        return result.error || qsTr("尚未读取日志。")
                    const bytes = result.bytesRead || 0
                    const size = bytes >= 1024 * 1024
                        ? qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
                        : qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)))
                    let summary = qsTr("显示最近 %1 行（尾部块内匹配 %2 行，读取 %3）")
                        .arg((result.lines || []).length)
                        .arg(result.matched)
                        .arg(size)
                    if (result.truncated)
                        summary += qsTr("；更早的内容未载入")
                    return summary
                }
                color: page.logTail.ok ? Theme.textSecondary : Theme.statusDanger
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WrapAnywhere
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 280
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle
                radius: Theme.radiusSm
                clip: true

                ListView {
                    id: tailList
                    objectName: "logging/tailList"
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXs
                    model: page.logTail.lines || []
                    clip: true
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: Text {
                        width: tailList.width
                        text: modelData
                        color: page.tailLineColor(modelData)
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontMono
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                }

                Text {
                    objectName: "logging/tailEmpty"
                    anchors.centerIn: parent
                    visible: tailList.count === 0 && page.logTail.ok
                    text: qsTr("当前过滤条件下没有日志行。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
            }

            Text {
                Layout.fillWidth: true
                text: page.logTail.path
                visible: page.logTail.path !== ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontMono
                elide: Text.ElideMiddle
                Accessible.name: qsTr("正在读取的日志文件：%1").arg(page.logTail.path)
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

    // 跟随最新：只在页面可见时轮询，离开页面立刻停，避免后台无谓读盘。
    Timer {
        id: followTailTimer
        interval: 2000
        repeat: true
        running: page.followTail && page.visible
        onTriggered: page.refreshTail()
    }

    // 关键字输入的防抖：连续输入只触发一次尾部读取。
    Timer {
        id: tailKeywordTimer
        interval: 300
        onTriggered: page.refreshTail()
    }

    onVisibleChanged: if (visible) page.refreshTail()
    Component.onCompleted: if (visible) page.refreshTail()

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
