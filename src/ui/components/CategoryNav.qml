import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Rectangle {
    id: nav

    property string currentCategory: "model"

    signal categorySelected(string id)

    color: Theme.bgSecondary

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSubtle
    }

    Column {
        id: navColumn
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        Repeater {
            model: PreferenceRegistry.categories()

            delegate: Rectangle {
                width: nav.width
                height: 36
                color: isActive
                    ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                              Theme.brandPrimary.b, 0.08)
                    : (itemMA.containsMouse ? Theme.surfaceHover : "transparent")

                readonly property bool isActive: modelData === nav.currentCategory

                Rectangle {
                    visible: parent.isActive
                    width: 3
                    height: parent.height
                    color: Theme.brandPrimary
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    // Reading currentLocale re-evaluates this binding on a
                    // language switch; categoryName resolves the stored
                    // QT_TRANSLATE_NOOP source at query time.
                    text: {
                        I18n.currentLocale
                        return PreferenceRegistry.categoryName(modelData)
                    }
                    color: parent.isActive ? Theme.brandPrimary : Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    font.bold: parent.isActive
                }

                MouseArea {
                    id: itemMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: nav.categorySelected(modelData)
                }
            }
        }
    }
}
