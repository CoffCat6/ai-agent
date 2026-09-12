import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    id: page

    // Future "general" preferences registered in PreferenceRegistry render
    // here; today the category is empty and the section stays hidden.
    property var preferenceKeys: PreferenceRegistry.preferencesInCategory("general")

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 24

        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: flick.width
            spacing: 16

            Item { Layout.preferredHeight: 4 }

            Text {
                text: qsTr("通用设置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Text {
                text: qsTr("本页修改实时保存，不受底部「保存 / 取消」影响")
                color: Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontSans
                opacity: 0.8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            ColumnLayout {
                visible: page.preferenceKeys.length > 0
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 16

                Text {
                    text: qsTr("偏好")
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
                        displayName: PreferenceRegistry.displayName(modelData)
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 8

                Text { text: qsTr("应用行为"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: qsTr("上下文窗口 (Tokens)"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.bold: true; font.family: Theme.fontMono }
                    TextField {
                        id: contextField
                        Layout.fillWidth: true
                        text: Settings.contextTokens
                        placeholderText: "8192"
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary; selectionColor: Theme.brandPrimary; selectedTextColor: "#ffffff"
                        font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans
                        validator: IntValidator { bottom: 2048; top: 1048576 }
                        background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: contextField.activeFocus ? 2 : 1; border.color: contextField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                        onEditingFinished: { const parsed = parseInt(text); Settings.contextTokens = isNaN(parsed) ? 8192 : parsed; text = Settings.contextTokens }
                    }
                }

                CheckBox { checked: Settings.simulationMode; onToggled: Settings.simulationMode = checked; text: qsTr("仿真演示模式（不调用真实 LLM）"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
                CheckBox { checked: Settings.allowFullDiskRead; onToggled: Settings.allowFullDiskRead = checked; text: qsTr("允许读取整个磁盘"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
                CheckBox { checked: Settings.localOnlyMode; onToggled: Settings.localOnlyMode = checked; text: qsTr("仅本地模式"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }
                CheckBox { checked: Settings.allowLocalNetworkFetch; onToggled: Settings.allowLocalNetworkFetch = checked; text: qsTr("允许 http_fetch 访问本机与内网地址"); font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontSans }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: qsTr("日志级别"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                    ComboBox {
                        Layout.preferredWidth: 220
                        property var levelNames: ["debug", "info", "warning", "critical"]
                        model: levelNames
                        onActivated: Settings.logLevel = currentText
                        Component.onCompleted: { const i = levelNames.indexOf(Settings.logLevel); currentIndex = i >= 0 ? i : 1 }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: qsTr("知识库嵌入模型"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                    ComboBox {
                        Layout.fillWidth: true
                        editable: true
                        model: ["nomic-embed-text", "bge-m3", "mxbai-embed-large", "all-minilm"]
                        editText: Settings.embeddingModel
                        onActivated: Settings.embeddingModel = currentText
                        onAccepted: Settings.embeddingModel = editText
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; height: 1; color: Theme.borderSubtle }

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
