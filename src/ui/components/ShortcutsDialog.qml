import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/20 §4.4（D9 快捷键速查表）/ docs/31 N2：列表不再静态抄一份序列。
//
// 全局动作来自 ShortcutMap（与 main.qml 的 Shortcut、设置页共用同一张
// 动作表），用户改键后速查表立即显示新值；输入框内的固定按键（发送、
// 换行、历史、粘贴图片）不可改，单列在下方。
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 520
    height: Math.min(680, parent ? parent.height - 48 : 680)

    readonly property var fixedShortcuts: [
        { seq: "Enter",       action: qsTr("发送消息") },
        { seq: "Ctrl+Enter",  action: qsTr("发送消息（输入法组合键安全）") },
        { seq: "Shift+Enter", action: qsTr("输入换行") },
        { seq: "↑ / ↓",       action: qsTr("回溯/前进输入历史") },
        { seq: "Ctrl+V",      action: qsTr("粘贴图片附件") }
    ]

    background: DialogBackground {}

    contentItem: ColumnLayout {
        Accessible.name: qsTr("快捷键速查对话框")
        Accessible.description: qsTr("查看全部快捷键")
        spacing: 12

        Text {
            Layout.leftMargin: 24
            Layout.topMargin: 24
            text: qsTr("快捷键")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
        }

        Text {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: qsTr("全部快捷键一览")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            Layout.fillWidth: true
        }

        // 条目随动作表增长，固定高度会挤到页脚：列表可滚动。
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: parent.width
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("全局快捷键")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontSans
                }

                Repeater {
                    objectName: "shortcuts/globalList"
                    model: ShortcutMap.actions

                    delegate: ShortcutRow {
                        required property var modelData
                        seq: modelData.sequence
                        action: modelData.title
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    text: qsTr("输入框内固定按键")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontSans
                }

                Repeater {
                    objectName: "shortcuts/fixedList"
                    model: dialog.fixedShortcuts

                    delegate: ShortcutRow {
                        required property var modelData
                        seq: modelData.seq
                        action: modelData.action
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 24

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("关闭")
                highlighted: true
                onClicked: dialog.close()
            }
        }
    }

    component ShortcutRow: RowLayout {
        id: row

        required property string seq
        required property string action

        Layout.fillWidth: true
        spacing: 12

        Rectangle {
            Layout.preferredWidth: seqLabel.implicitWidth + 20
            Layout.preferredHeight: 26
            radius: 6
            color: Theme.surfaceHover
            border.width: 1
            border.color: Theme.borderSubtle

            Text {
                id: seqLabel
                anchors.centerIn: parent
                text: row.seq
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
            }
        }

        Text {
            Layout.fillWidth: true
            text: row.action
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            elide: Text.ElideRight
        }
    }
}
