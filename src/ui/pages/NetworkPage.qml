import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 网络与代理：原「通用设置」的网络代理区块独立成页。
// 代理作用于模型 / 向量 / 搜索 / MCP 四条 HTTP 链路，属于跨模块基础设施。
Item {
    id: page

    Accessible.name: qsTr("网络与代理页")

    function requestResetDefaults() {
        Settings.resetNetworkToDefaults()
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
                    text: qsTr("网络与代理")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("代理设置对模型服务、向量服务、联网搜索与 MCP HTTP 连接生效；本机地址始终直连。")
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
                title: qsTr("HTTP 代理")
                subtitle: qsTr("跟随系统会读取操作系统代理配置；手动代理对所有出站 HTTP 请求生效。")

                SettingRow {
                    label: qsTr("代理模式")

                    ComboBox {
                        id: proxyModeCombo
                        Layout.preferredWidth: 220
                        property var modeValues: ["system", "manual", "off"]
                        model: [qsTr("跟随系统"), qsTr("手动代理"), qsTr("不使用代理")]
                        Component.onCompleted: currentIndex = Math.max(0, modeValues.indexOf(Settings.proxyMode))
                        onActivated: Settings.proxyMode = modeValues[currentIndex]
                        Connections {
                            target: Settings
                            function onProxyModeChanged() {
                                const index = proxyModeCombo.modeValues.indexOf(Settings.proxyMode)
                                proxyModeCombo.currentIndex = index >= 0 ? index : 0
                            }
                        }
                        Accessible.name: qsTr("代理模式")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: Settings.proxyMode === "manual"

                    SettingsField {
                        id: proxyHostField
                        label: qsTr("代理服务器地址")
                        mono: true
                        text: Settings.proxyHost
                        // 基线跟着已保存值走：页面只是重建/切分类时，未编辑的
                        // 字段不会被 commitOnDestroy 再写一遍。
                        lastKnownValue: Settings.proxyHost
                        placeholderText: qsTr("proxy.example.com（不含 http://）")
                        // 防抖提交 + 切分类销毁兜底（Loader 销毁不触发
                        // editingFinished）。
                        commitDebounce: 400
                        commitOnDestroy: true
                        onSubmitted: (value) => Settings.proxyHost = value
                    }

                    SettingRow {
                        label: qsTr("代理端口")

                        SpinBox {
                            id: proxyPortSpin
                            from: 1
                            to: 65535
                            value: Settings.proxyPort
                            editable: true
                            onValueModified: Settings.proxyPort = value
                            Connections {
                                target: Settings
                                function onProxyPortChanged() { proxyPortSpin.value = Settings.proxyPort }
                            }
                            Accessible.name: qsTr("代理端口")
                        }
                    }

                    SettingsField {
                        id: proxyUserField
                        label: qsTr("代理用户名")
                        mono: true
                        text: Settings.proxyUsername
                        lastKnownValue: Settings.proxyUsername
                        placeholderText: qsTr("代理认证用户名")
                        commitOnDestroy: true
                        onSubmitted: (value) => Settings.proxyUsername = value
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        SettingsField {
                            id: proxyPasswordField
                            label: qsTr("代理密码")
                            secret: true
                            // 空值 = 不修改（留空不覆盖已保存的密码）；写入成功
                            // 后清空输入框。
                            ignoreEmptySubmit: true
                            clearOnSubmit: true
                            commitOnDestroy: true
                            placeholderText: Settings.proxyPasswordSet
                                ? qsTr("已配置（输入新值可覆盖，留空不修改）")
                                : qsTr("未配置")
                            onSubmitted: (value) => Settings.proxyPassword = value
                        }

                        AppButton {
                            text: qsTr("清除密码")
                            enabled: Settings.proxyPasswordSet
                            opacity: enabled ? 1.0 : 0.4
                            onClicked: Settings.proxyPassword = ""
                            Accessible.name: qsTr("清除代理密码")
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    AppButton {
                        text: qsTr("测试代理")
                        enabled: !Settings.proxyTesting
                        onClicked: Settings.testProxy()
                        Accessible.name: qsTr("测试代理连接")
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Settings.proxyTestResult
                        visible: text.length > 0
                        color: Settings.proxyTesting
                            ? Theme.textSecondary
                            : (Settings.proxyTestOk ? Theme.statusSuccess : Theme.statusWarning)
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
