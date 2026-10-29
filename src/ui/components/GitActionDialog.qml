import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// One confirmation surface for every destructive or history-rewriting Git
// action: the panel supplies the facts (what git will run, what is affected,
// how it can be recovered) and the user has to press confirm. Optional text
// input covers amend and stash, where the message is part of the command.
Dialog {
    id: dialog

    property string titleText: ""
    property var detailLines: []
    property string confirmText: qsTr("确认")
    property bool editable: false
    property bool requireText: false
    property string inputPlaceholder: ""
    property string noticeText: ""
    // Extra gating from the caller, on top of the text requirement below.
    property bool gateConfirmed: true

    readonly property string inputText: messageArea.text
    readonly property bool canConfirm: dialog.gateConfirmed
        && (!dialog.requireText || messageArea.text.trim().length > 0)

    signal confirmed(string text)

    function openWith(options) {
        const config = options || {}
        dialog.titleText = config.title || ""
        dialog.detailLines = config.details || []
        dialog.confirmText = config.confirm || qsTr("确认")
        dialog.editable = config.editable === true
        dialog.requireText = config.requireText === true
        dialog.inputPlaceholder = config.placeholder || ""
        dialog.noticeText = config.notice || ""
        dialog.gateConfirmed = config.enabled !== false
        messageArea.text = config.text || ""
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent ? parent.width - 32 : 440)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 16
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: dialog.titleText
            color: Theme.textPrimary
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSizeBody
            font.bold: true
            wrapMode: Text.Wrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 4
            visible: dialog.detailLines.length > 0

            Repeater {
                model: dialog.detailLines

                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: String(modelData)
                    color: Theme.textSecondary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 4
            visible: dialog.editable

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                radius: 6
                color: Theme.bgPrimary
                border.color: messageArea.activeFocus
                    ? Theme.brandPrimary : Theme.borderSubtle

                TextArea {
                    id: messageArea
                    objectName: "gitActionMessage"
                    anchors.fill: parent
                    anchors.margins: 6
                    padding: 0
                    placeholderText: dialog.inputPlaceholder
                    placeholderTextColor: Theme.textTertiary
                    color: Theme.textPrimary
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    background: Rectangle { color: "transparent" }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: dialog.requireText
                         && messageArea.text.trim().length === 0
                text: qsTr("必须填写说明。")
                color: Theme.statusWarning
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeMicro
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dialog.noticeText.length > 0
            text: dialog.noticeText
            color: Theme.statusWarning
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSizeMicro
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            spacing: 8

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("取消")
                onClicked: dialog.close()
            }

            AppButton {
                objectName: "gitActionConfirmButton"
                text: dialog.confirmText
                highlighted: true
                enabled: dialog.canConfirm
                opacity: enabled ? 1.0 : 0.4
                onClicked: dialog.confirmed(messageArea.text.trim())
            }
        }
    }
}
