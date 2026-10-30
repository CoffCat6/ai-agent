import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 工具管理页的一行：名称 + 来源/级别徽标 + 说明 + 启停开关。
//
// 内置工具与 MCP 工具共用本组件；「按来源分组」由宿主页面负责，
// 行内仍保留来源标签，避免折叠组标题后看不出这一行是谁提供的。
Rectangle {
    id: root
    objectName: "toolRow"

    required property var modelData

    Layout.fillWidth: true
    implicitHeight: row.implicitHeight + Theme.spaceLg
    color: Theme.bgSecondary
    radius: Theme.radiusMd
    border.width: 1
    border.color: Theme.borderSubtle

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        spacing: Theme.spaceMd

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.spaceXs

            RowLayout {
                spacing: Theme.spaceSm
                Text {
                    text: root.modelData.name
                    color: Theme.textPrimary
                    font.bold: true
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                }
                Text {
                    text: root.modelData.source === "MCP" ? "· MCP" : "· " + qsTr("内置")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                Text {
                    text: "· " + (root.modelData.level === "safe" ? qsTr("自动")
                                : root.modelData.level === "requires_confirm" ? qsTr("需确认")
                                : qsTr("危险"))
                    // docs/25 C6: 危险级别色归 Theme token
                    color: root.modelData.level === "dangerous"
                        ? Theme.statusDanger : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                }
                // docs/17 B1: 策略拦截徽标——Switch 只反映用户层，
                // 被仅本地模式等策略拦截时在此可见。
                Text {
                    visible: root.modelData.blockedByPolicy === true
                    text: "· " + qsTr("仅本地模式拦截")
                    color: Theme.statusWarning
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                    font.family: Theme.fontSans
                }
            }

            Text {
                text: root.modelData.description
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        Switch {
            id: toolEnableSwitch
            // docs/25 D3: 读屏可区分是哪个工具的开关
            Accessible.name: qsTr("启用工具 %1").arg(root.modelData.name)
            checked: root.modelData.enabled
            onToggled: AgentVM.setToolEnabled(root.modelData.name, checked)
        }
    }
}
