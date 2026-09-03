import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Reusable button component with tech-minimalist styling
Rectangle {
    id: root

    // ── Public Properties ───────────────────────────────
    property string text: ""
    property color buttonColor: Theme.borderSubtle
    property color textColor: Theme.textPrimary
    property bool highlighted: false
    // `enabled` is the native Item property - do not redeclare it. Setting
    // it (or disabling an ancestor) propagates to the MouseArea below,
    // which then stops receiving events on its own.

    // ── Signals ────────────────────────────────────────
    signal clicked()

    // ── Styling ────────────────────────────────────────
    implicitHeight: 32
    implicitWidth: buttonText.implicitWidth + 24
    radius: 6
    color: highlighted ? Theme.brandPrimary
                       : (controlArea.containsMouse ? Theme.surfaceHover : buttonColor)
    opacity: enabled ? 1.0 : 0.5
    border.width: 1
    border.color: highlighted ? Theme.brandPrimary : Theme.borderSubtle

    // Press animation
    scale: controlArea.containsPress ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: 100 } }

    // ── Content ────────────────────────────────────────
    Text {
        id: buttonText
        anchors.centerIn: parent
        text: root.text
        color: highlighted ? Theme.bgPrimary : textColor
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontSans
        font.weight: highlighted ? Font.DemiBold : Font.Normal
    }

    // ── Interaction ───────────────────────────────────
    MouseArea {
        id: controlArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
