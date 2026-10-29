import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 网络与代理：原「通用设置」的网络代理区块独立成页。
// 代理作用于模型 / 向量 / 搜索 / MCP 四条 HTTP 链路，属于跨模块基础设施。
SettingsPage {
    id: page

    Accessible.name: qsTr("网络与代理页")

    title: qsTr("网络与代理")
    description: qsTr("代理设置对模型服务、向量服务、联网搜索与 MCP HTTP 连接生效；本机地址始终直连。")

    function requestResetDefaults() {
        Settings.resetNetworkToDefaults()
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("HTTP 代理")
        subtitle: qsTr("跟随系统会读取操作系统代理配置；手动代理对所有出站 HTTP 请求生效。")

        SettingRow {
            objectName: "network/proxyMode"
            label: qsTr("代理模式")

            ComboBox {
                id: proxyModeCombo
                Layout.preferredWidth: Theme.controlWidthLg
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
            spacing: Theme.spaceMd
            visible: Settings.proxyMode === "manual"

            SettingsField {
                id: proxyHostField
                objectName: "network/proxyHost"
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
                objectName: "network/proxyPort"
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
                objectName: "network/proxyUsername"
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
                spacing: Theme.spaceSm

                SettingsField {
                    id: proxyPasswordField
                    objectName: "network/proxyPassword"
                    label: qsTr("代理密码")
                    secret: true
                    // 空值 = 不修改（留空不覆盖已保存的密码）；写入成功
                    // 后清空输入框。
                    ignoreEmptySubmit: true
                    clearOnSubmit: true
                    commitOnDestroy: true
                    statusText: Settings.proxyPasswordSet
                        ? qsTr("已配置") : qsTr("未配置")
                    statusOk: Settings.proxyPasswordSet
                    placeholderText: Settings.proxyPasswordSet
                        ? qsTr("输入新值可覆盖，留空不修改")
                        : qsTr("输入代理密码")
                    onSubmitted: (value) => Settings.proxyPassword = value
                }

                AppButton {
                    text: qsTr("清除密码")
                    enabled: Settings.proxyPasswordSet
                    onClicked: Settings.proxyPassword = ""
                    Accessible.name: qsTr("清除代理密码")
                }
            }
        }

        RowLayout {
            objectName: "network/testing"
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXs
            spacing: Theme.spaceSm
            AppButton {
                text: Settings.proxyTesting ? qsTr("测试中…") : qsTr("测试代理")
                enabled: !Settings.proxyTesting
                onClicked: Settings.testProxy()
                Accessible.name: qsTr("测试代理连接")
            }
            StatusLine {
                text: Settings.proxyTestResult
                state: Settings.proxyTesting ? "busy"
                     : (Settings.proxyTestOk ? "success" : "warning")
            }
        }
    }
}
