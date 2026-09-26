import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Tool confirmation dialog - high-risk operation interception
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 440
    height: 380

    property string toolName: ""
    property string arguments: "{}"
    property string pendingToolCallId: ""

    Accessible.name: qsTr("工具执行确认对话框")
    Accessible.description: qsTr("AI 请求执行高危操作 %1，请选择允许或拒绝").arg(toolName)

    // Wire the dialog verdict back into the agent pipeline
    onAccepted: {
        if (pendingToolCallId !== "")
            AgentVM.approveToolCall(pendingToolCallId, true)
        pendingToolCallId = ""
    }
    onRejected: {
        if (pendingToolCallId !== "")
            AgentVM.approveToolCall(pendingToolCallId, false)
        pendingToolCallId = ""
    }

    background: DialogBackground {
        // Warning stripe
        Rectangle {
            width: 4
            height: parent.height
            color: Theme.statusWarning
            anchors.left: parent.left
        }

        // Subtle outer glow for elevation (dark mode only)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.width: 1
            border.color: Theme.statusWarning
            opacity: Theme.isDark ? 0.15 : 0.0
            z: -1
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        // Title
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.topMargin: 20
            Layout.rightMargin: 20
            spacing: 8

            AppIcon {
                icon: "warning"
                size: 20
                iconColor: Theme.statusWarning
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("AI 请求执行高危操作")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }
        }

        // Tool name
        RowLayout {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 8

            Text {
                text: qsTr("工具名称:")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
            }

            Text {
                text: dialog.toolName
                color: Theme.statusWarning
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontMono
                font.bold: true
            }
        }

        // Arguments header
        Text {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            text: qsTr("调用参数:")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
        }

        // Code block showing arguments
        CodeBlock {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            text: dialog.arguments
        }

        // Action buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 20
            spacing: 8

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("拒绝")
                textColor: Theme.statusDanger
                onClicked: dialog.reject()
            }

            AppButton {
                text: qsTr("允许执行")
                highlighted: true
                onClicked: dialog.accept()
            }
        }
    }
}
