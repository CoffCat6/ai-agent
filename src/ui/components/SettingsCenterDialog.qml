import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 760
    height: 560

    property string currentCategory: "model"

    signal settingsSaved()
    signal settingsError(string message)

    Accessible.name: qsTr("设置中心")
    Accessible.description: qsTr("配置模型、界面、工具、MCP 服务器等应用选项")

    function openCategory(id) {
        currentCategory = id
        open()
    }

    background: DialogBackground {}

    onOpened: {
        const last = PreferenceStore.load("settings.lastCategory")
        if (last && PreferenceRegistry.categories().indexOf(last) >= 0)
            currentCategory = last
    }

    onClosed: {
        if (EditBuffer.dirty) EditBuffer.rollback()
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.bgSecondary

            Item {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 16

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        text: qsTr("设置")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeH2
                        font.bold: true
                        font.family: Theme.fontSans
                    }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        icon: "x"
                        iconSize: 16
                        onClicked: dialog.close()
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            CategoryNav {
                id: categoryNav
                Layout.preferredWidth: 180
                Layout.fillHeight: true
                currentCategory: dialog.currentCategory
                onCategorySelected: (id) => {
                    dialog.currentCategory = id
                    PreferenceStore.save("settings.lastCategory", id)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPrimary

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "model"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/ModelConfigPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "interface"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/InterfacePage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "general"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/GeneralPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "security"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/SecurityPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "mcp"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/McpPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "tools"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/ToolsPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "memory"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/MemoryPage.qml"
                }

                Loader {
                    anchors.fill: parent
                    active: dialog.currentCategory === "usage"
                    visible: active
                    source: "qrc:/qt/qml/NexusAI/src/ui/pages/UsageStatsPage.qml"
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.bgSecondary

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }

            Item {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20

                // docs/17（设置统一最终决策）: 所有分类均为即时保存，底栏
                // 只保留"恢复默认"（仅声明式偏好分类可用）与"关闭"。
                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    AppButton {
                        text: qsTr("恢复默认")
                        // Categories without registered preferences (e.g. 模型配置,
                        // which saves through SettingsService instead) have
                        // nothing this button could reset.
                        visible: PreferenceRegistry.preferencesInCategory(
                                     dialog.currentCategory).length > 0
                        onClicked: confirmResetDialog.open()
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        text: qsTr("关闭")
                        highlighted: true
                        onClicked: dialog.close()
                    }
                }
            }
        }
    }

    // docs/17: 即时保存后不再有"放弃修改"路径；confirmCancelDialog 已移除，
    // onClosed 的 rollback 仅兜底丢弃校验失败遗留的 staged 残值。
    Dialog {
        id: confirmResetDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 360
        height: 160

        background: DialogBackground {}
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: qsTr("确认将当前分类的配置项恢复为默认值？")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                Layout.leftMargin: 20
                Layout.topMargin: 20
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.rightMargin: 20
                Layout.bottomMargin: 16
                spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消")
                    onClicked: confirmResetDialog.close()
                }
                AppButton {
                    text: qsTr("恢复默认")
                    highlighted: true
                    onClicked: {
                        EditBuffer.resetToDefault(
                            PreferenceRegistry.preferencesInCategory(dialog.currentCategory))
                        confirmResetDialog.close()
                    }
                }
            }
        }
    }
}
