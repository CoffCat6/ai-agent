import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    id: page

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
                visible: page.preferenceKeys.length === 0
                text: qsTr("暂无可配置项")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 40
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Repeater {
                model: page.preferenceKeys

                delegate: PreferenceControl {
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.fillWidth: true
                    prefKey: modelData
                    displayName: PreferenceRegistry.displayName(modelData)
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
