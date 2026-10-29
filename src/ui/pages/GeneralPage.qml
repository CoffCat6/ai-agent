import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 常规应用：真正的"应用级常规"设置——托盘、开机自启、系统通知。
//
// 原「通用设置」页曾同时承载模型行为、日志、网络代理与知识库嵌入；这些
// 区块已按领域拆到独立分类（智能体行为 / 日志与诊断 / 网络与代理 /
// 知识库嵌入），本页只保留声明式偏好，由 PreferenceControl 自动渲染。
Item {
    id: page

    Accessible.name: qsTr("常规应用页")

    property var preferenceKeys: PreferenceRegistry.preferencesInCategory("general")

    function requestResetDefaults() {
        PreferenceStore.resetBatch(page.preferenceKeys)
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
                    text: qsTr("常规应用")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("本页修改实时保存，无需手动提交；「恢复默认」在窗口底部。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                visible: page.preferenceKeys.length > 0
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 16

                Text {
                    text: qsTr("启动与通知")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }

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

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                height: 1
                color: Theme.borderSubtle
            }

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 16
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "NexusAI"; color: Theme.textPrimary; font.pixelSize: Theme.fontSizeH2; font.bold: true; font.family: Theme.fontSans }
                    Rectangle {
                        width: versionBadge.implicitWidth + 14; height: 18; radius: 9
                        color: Theme.brandPrimary; opacity: 0.15
                        Text { id: versionBadge; anchors.centerIn: parent; text: "v" + Settings.appVersion; color: Theme.brandPrimary; font.pixelSize: 10; font.bold: true; font.family: Theme.fontMono }
                    }
                    Item { Layout.fillWidth: true }
                }
                Text { text: qsTr("本地智能体桌面应用 · 多模型提供商 · 工具调用 · 本地知识库"); color: Theme.textSecondary; font.pixelSize: 10; font.family: Theme.fontSans; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
