import QtQuick
import QtQuick.Layouts
import NexusAI

// Reusable button component with tech-minimalist styling
Rectangle {
    id: root

    // ── Public Properties ───────────────────────────────
    property string text: ""
    property color buttonColor: Theme.borderSubtle
    property color textColor: Theme.textPrimary
    property bool highlighted: false
    property bool enabled: true

    // ── Signals ────────────────────────────────────────
    signal clicked()

    // ── Styling ────────────────────────────────────────
    implicitHeight: 32
    implicitWidth: buttonText.implicitWidth + 24
    radius: 6
    color: highlighted ? Theme.brandPrimary : (hoverArea.containsMouse ? Theme.surfaceHover : buttonColor)
    opacity: enabled ? 1.0 : 0.5
    border.width: 1
    border.color: highlighted ? Theme.brandPrimary : Theme.borderSubtle

    // Press animation
    scale: pressArea.containsPress ? 0.98 : 1.0
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
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (root.enabled) root.clicked()
    }

    MouseArea {
        id: pressArea
        anchors.fill: parent
        onPressed: if (root.enabled) parent.scale = 0.98
        onReleased: parent.scale = 1.0
        propagateComposedEvents: true
    }
}
