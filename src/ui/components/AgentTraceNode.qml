import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI

// Agent trace node - visualizes the thinking/tool-call/result timeline
Item {
    id: root

    required property var modelData

    // ── Layout ──────────────────────────────────────────
    implicitHeight: contentColumn.implicitHeight + 20
    height: implicitHeight

    // Determine node colors based on type
    readonly property color nodeColor: {
        var t = modelData.type
        if (t === "thinking") return Theme.brandPrimary
        if (t === "tool_call") return Theme.statusWarning
        if (t === "tool_result") return Theme.statusSuccess
        if (t === "summary") return Theme.brandPrimary
        if (t === "user_message") return Theme.textSecondary
        return Theme.textSecondary
    }

    // ── Timeline Axis ───────────────────────────────────
    Rectangle {
        id: timelineAxis
        width: 2
        height: parent.height
        x: 11
        color: Theme.borderSubtle
    }

    // ── Status Dot ──────────────────────────────────────
    Rectangle {
        id: statusDot
        width: 12
        height: 12
        radius: 6
        x: 6
        y: 4
        color: Theme.bgSecondary
        border.color: root.nodeColor
        border.width: 2

        // Breathing animation when running
        SequentialAnimation on opacity {
            running: modelData.status === "running"
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.3; duration: 800 }
            NumberAnimation { from: 0.3; to: 1.0; duration: 800 }
        }

        // Glow effect for running state (dark mode only)
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 8
            height: parent.height + 8
            radius: width / 2
            color: root.nodeColor
            opacity: modelData.status === "running" && Theme.isDark ? 0.2 : 0.0
            z: -1

            SequentialAnimation on opacity {
                running: modelData.status === "running" && Theme.isDark
                loops: Animation.Infinite
                NumberAnimation { from: 0.3; to: 0.05; duration: 800 }
                NumberAnimation { from: 0.05; to: 0.3; duration: 800 }
            }
        }
    }

    // ── Content Area ────────────────────────────────────
    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.leftMargin: 28
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 2
        spacing: 6

        // Node type label (small caption)
        Text {
            text: {
                var t = modelData.type
                if (t === "thinking") return "THINKING"
                if (t === "tool_call") return "TOOL CALL"
                if (t === "tool_result") return "TOOL RESULT"
                if (t === "summary") return "RESPONSE"
                if (t === "user_message") return "YOU"
                return t.toUpperCase()
            }
            color: Theme.textSecondary
            font.family: Theme.fontMono
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        // Title
        Text {
            Layout.fillWidth: true
            text: modelData.title
            color: modelData.type === "user_message" ? Theme.textPrimary : Theme.textPrimary
            font.family: modelData.type === "user_message" ? Theme.fontSans : Theme.fontMono
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            renderType: Text.NativeRendering
        }

        // Tool call arguments (shown for tool_call type)
        CodeBlock {
            Layout.fillWidth: true
            visible: modelData.type === "tool_call" && modelData.args !== ""
            text: modelData.args
        }

        // Tool result output
        CodeBlock {
            Layout.fillWidth: true
            visible: modelData.type === "tool_result" && modelData.result !== ""
            text: modelData.result
            textColor: Theme.statusSuccess
        }

        // Confirmation buttons (for waiting_confirm status)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: modelData.status === "waiting_confirm"

            AppButton {
                text: qsTr("拒绝")
                buttonColor: "transparent"
                textColor: Theme.statusDanger
                border.color: Theme.statusDanger
                onClicked: AgentVM.approveToolCall(modelData.id, false)
            }

            AppButton {
                text: qsTr("允许")
                highlighted: true
                onClicked: AgentVM.approveToolCall(modelData.id, true)
            }
        }

        // Status indicator text
        Text {
            visible: modelData.status === "running"
            text: "▌"
            color: Theme.brandPrimary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSizeBody

            SequentialAnimation on opacity {
                running: modelData.status === "running"
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.0; duration: 500 }
                NumberAnimation { from: 0.0; to: 1.0; duration: 500 }
            }
        }
    }
}
