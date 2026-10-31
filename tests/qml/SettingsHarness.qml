import QtQuick
import QtQuick.Controls
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// docs/31 S1（O1）：真实 SettingRow 的状态槽回归宿主。
//
// 行级"已修改"圆点与「恢复默认值」按钮的可见性由 valueState（revision +
// SettingsCatalog.describe）驱动。这里把两行放进真实组件树，C++ 侧只改
// 设置值、点按钮，断言可见性与复位结果——如果绑定漏读 revision，按钮
// 就会停在旧状态，这是纯 C++ 用例看不见的退化。
Window {
    id: win
    width: 760
    height: 480
    visible: true

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        SettingRow {
            id: autoStartRow
            objectName: "autoStartRow"
            width: parent.width
            label: qsTr("开机自启动")
            settingKey: "general.autoStart"

            Switch {
                objectName: "autoStartSwitch"
                checked: PreferenceStore.load("general.autoStart")
                onToggled: EditBuffer.stage("general.autoStart", checked)
            }
        }

        SettingRow {
            objectName: "secretRow"
            width: parent.width
            label: qsTr("联网搜索 API Key")
            settingKey: "webSearch/zhipuApiKey"

            Text {
                text: {
                    SettingsCatalog.revision
                    return SettingsCatalog.describe("webSearch/zhipuApiKey").display
                }
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
            }
        }
    }

    function describe(key) {
        return SettingsCatalog.describe(key)
    }

    function search(query) {
        return SettingsCatalog.search(query)
    }
}
