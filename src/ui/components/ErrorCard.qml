import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

Item {
    id: root
    required property var modelData
    required property int index
    readonly property string text: modelData.title || ""
    readonly property bool latest: AgentVM.traceCount > 0
        && index === AgentVM.lastTraceIndexOfType("error")
        && AgentVM.state === AgentVM.stateError
    implicitHeight: card.implicitHeight

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.avatarSize + Theme.spaceSm
        radius: Theme.radiusMd
        color: Theme.statusDangerSurface
        border.width: 1
        border.color: Theme.statusDanger
        implicitHeight: content.implicitHeight + Theme.spaceMd * 2

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spaceMd
            spacing: Theme.spaceSm
            Text {
                Layout.fillWidth: true
                text: root.text
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                AppButton {
                    visible: root.latest
                    text: qsTr("重试本轮")
                    enabled: AgentVM.modelConfigured && !AgentVM.stateBusy
                    onClicked: AgentVM.regenerate()
                }
                AppButton {
                    text: qsTr("模型设置")
                    onClicked: AgentVM.openModelSettings()
                }
                CopyButton { text: root.text }
            }
        }
    }
    Accessible.role: Accessible.Alert
    Accessible.name: qsTr("错误")
    Accessible.description: root.text
}
