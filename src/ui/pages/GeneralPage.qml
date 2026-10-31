import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// 常规应用：真正的"应用级常规"设置——托盘、开机自启、系统通知。
//
// 原「通用设置」页曾同时承载模型行为、日志、网络代理与知识库嵌入；这些
// 区块已按领域拆到独立分类（智能体行为 / 日志与诊断 / 网络与代理 /
// 知识库嵌入），本页只保留声明式偏好，由 PreferenceControl 自动渲染。
SettingsPage {
    id: page

    Accessible.name: qsTr("常规应用页")

    title: qsTr("常规应用")
    description: qsTr("本页修改实时保存，无需手动提交；「恢复默认」在窗口底部。")

    property var preferenceKeys: PreferenceRegistry.preferencesInCategory("general")

    // docs/31 N10：关于页的操作结果（打开数据目录 / 导出诊断包）。
    property string aboutMessage: ""
    property bool aboutIsError: false

    function requestResetDefaults() {
        PreferenceStore.resetBatch(page.preferenceKeys)
    }

    SettingsSection {
        visible: page.preferenceKeys.length > 0
        title: qsTr("启动与通知")

        Repeater {
            model: page.preferenceKeys

            delegate: PreferenceControl {
                Layout.fillWidth: true
                prefKey: modelData
                // Reading currentLocale re-evaluates this binding on
                // a language switch; displayName resolves the stored
                // QT_TRANSLATE_NOOP source at query time.
                displayName: {
                    I18n.currentLocale
                    return PreferenceRegistry.displayName(modelData)
                }
            }
        }
    }

    SettingsSection {
        title: qsTr("关于 NexusAI")

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            Text { text: "NexusAI"; color: Theme.textPrimary; font.pixelSize: Theme.fontSizeH2; font.bold: true; font.family: Theme.fontSans }
            Rectangle {
                width: versionBadge.implicitWidth + 14; height: 18; radius: Theme.radiusBadge
                color: Theme.brandPrimary; opacity: 0.15
                Text { id: versionBadge; anchors.centerIn: parent; text: "v" + Settings.appVersion; color: Theme.brandPrimary; font.pixelSize: Theme.fontSizeMicro; font.bold: true; font.family: Theme.fontMono }
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            text: qsTr("本地智能体桌面应用 · 多模型提供商 · 工具调用 · 本地知识库")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // docs/31 N10：构建信息只读展示；值与 QSysInfo / 编译期宏一致。
        SettingRow {
            objectName: "about/version"
            label: qsTr("应用版本")
            Text {
                objectName: "about/versionValue"
                text: AppInfo.appVersion
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }
        SettingRow {
            objectName: "about/qtVersion"
            label: qsTr("Qt 版本")
            Text {
                objectName: "about/qtVersionValue"
                text: AppInfo.qtVersion
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }
        SettingRow {
            objectName: "about/compiler"
            label: qsTr("编译器")
            Text {
                objectName: "about/compilerValue"
                text: AppInfo.compiler
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }
        SettingRow {
            objectName: "about/architecture"
            label: qsTr("CPU 架构")
            Text {
                objectName: "about/architectureValue"
                text: AppInfo.architecture
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
            }
        }
        SettingRow {
            objectName: "about/osVersion"
            label: qsTr("操作系统")
            Text {
                objectName: "about/osVersionValue"
                text: AppInfo.osVersion
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                elide: Text.ElideRight
            }
        }
        SettingRow {
            objectName: "about/dataDir"
            label: qsTr("数据目录")
            Text {
                objectName: "about/dataDirValue"
                text: AppInfo.dataDir
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                elide: Text.ElideMiddle
                Layout.maximumWidth: Theme.controlWidthLg
                Layout.alignment: Qt.AlignVCenter
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            AppButton {
                objectName: "about/openDataDir"
                text: qsTr("打开数据目录")
                onClicked: {
                    page.aboutMessage = ""
                    if (!AppInfo.openDataDir()) {
                        page.aboutIsError = true
                        page.aboutMessage = qsTr("无法打开数据目录。")
                    }
                }
                Accessible.name: qsTr("在文件管理器中打开应用数据目录")
            }
            AppButton {
                objectName: "about/exportDiagnostics"
                text: qsTr("导出诊断包")
                onClicked: diagnosticsFolderDialog.open()
                Accessible.name: qsTr("选择目录并导出诊断包")
            }
            AppButton {
                objectName: "about/licenses"
                text: qsTr("开源许可")
                onClicked: licensesDialog.open()
                Accessible.name: qsTr("查看开源组件许可")
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            objectName: "about/status"
            visible: page.aboutMessage !== ""
            Layout.fillWidth: true
            text: page.aboutMessage
            color: page.aboutIsError ? Theme.statusDanger : Theme.statusSuccess
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.WrapAnywhere
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    FolderDialog {
        id: diagnosticsFolderDialog
        title: qsTr("选择诊断包保存位置")
        onAccepted: {
            // 导出一个新目录 nexus-diagnostics-<时间戳>/，不覆盖旧包。
            const folder = SettingsBackup.exportDiagnostics(selectedFolder.toString())
            page.aboutIsError = folder === ""
            page.aboutMessage = folder !== ""
                ? qsTr("已导出：%1").arg(folder)
                : (SettingsBackup.lastError || qsTr("导出失败。"))
        }
    }

    Dialog {
        id: licensesDialog
        objectName: "about/licensesDialog"
        modal: true
        anchors.centerIn: parent
        width: 440
        padding: Theme.spaceXl
        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: Theme.spaceSm

            Text {
                text: qsTr("开源许可")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }
            Repeater {
                objectName: "about/licensesList"
                model: AppInfo.licenses
                delegate: Text {
                    required property string modelData
                    Layout.fillWidth: true
                    text: "• " + modelData
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                    wrapMode: Text.WrapAnywhere
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("本应用链接这些组件但未修改其源码；完整许可文本随组件分发。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    objectName: "about/licensesClose"
                    text: qsTr("关闭")
                    onClicked: licensesDialog.close()
                }
            }
        }
    }
}
