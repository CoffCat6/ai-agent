import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// G2: live status for the '@' tokens in the input draft.
//
// `checks` is the output of AgentViewModel.inspectFileReferences:
//   [{token, path, error}]
// error is "" when the token resolves to a project file; otherwise one of
// no-project / missing / sensitive / outside. The component is presentation
// only, so it can be rendered and tested without AgentVM.
Item {
    id: root

    property var checks: []
    readonly property int chipCount: checks.length

    implicitWidth: refRow.implicitWidth
    implicitHeight: 24
    visible: chipCount > 0

    function errorText(error) {
        switch (error) {
        case "no-project": return qsTr("未绑定项目，无法解析引用")
        case "missing": return qsTr("项目内未找到该文件")
        case "sensitive": return qsTr("敏感文件不可引用")
        case "outside": return qsTr("路径不在项目目录内")
        }
        return qsTr("无法引用")
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: refRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        interactive: contentWidth > width
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: refRow
            y: (flick.height - height) / 2
            height: 24
            spacing: 6

            Text {
                height: 24
                verticalAlignment: Text.AlignVCenter
                text: qsTr("引用")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
            }

            Repeater {
                model: root.checks

                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    objectName: "referenceChip"

                    readonly property bool failed: modelData.error !== ""
                    readonly property string status: failed ? "error" : "ok"
                    readonly property string label: failed
                        ? "@" + modelData.token : modelData.path

                    width: Math.min(chipText.implicitWidth, 220) + 30
                    height: 24
                    radius: 12
                    color: failed
                        ? Qt.rgba(Theme.statusDanger.r, Theme.statusDanger.g,
                                  Theme.statusDanger.b, 0.14)
                        : Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                  Theme.brandPrimary.b, 0.12)

                    AppIcon {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        icon: chip.failed ? "warning" : "file"
                        size: 11
                        iconColor: chip.failed ? Theme.statusDanger : Theme.brandPrimary
                    }

                    Text {
                        id: chipText
                        anchors.left: parent.left
                        anchors.leftMargin: 23
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.label
                        color: chip.failed ? Theme.statusDanger : Theme.textPrimary
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontMono
                        elide: Text.ElideMiddle
                    }

                    HoverHandler { id: chipHover }
                    ToolTip.visible: chipHover.hovered
                    ToolTip.delay: 300
                    ToolTip.text: chip.failed
                        ? qsTr("%1（%2）").arg(chip.label).arg(
                            root.errorText(chip.modelData.error))
                        : qsTr("该文件将随消息发送：%1").arg(chip.modelData.path)
                }
            }
        }
    }
}
