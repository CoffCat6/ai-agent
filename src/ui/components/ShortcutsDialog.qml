import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/20 §4.4（D9 快捷键速查表）：静态两列列表。
// 快捷键清单是唯一事实源——main.qml 的 Shortcut 与这里的条目一一对应，
// 新增/修改快捷键时两处必须同步（表格里注明了动作归属，方便核对）。
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 520
    height: 460


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
            font.pixelSize: 10
            font.family: Theme.fontSans
            opacity: 0.8
            Layout.fillWidth: true
        }

        // ── Two-column shortcut list ──────────────────
        ColumnLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.fillWidth: true
            spacing: 0

            Repeater {
                model: [
                    { seq: "Ctrl+,",  action: qsTr("打开设置中心") },
                    { seq: "Ctrl+L",  action: qsTr("打开审计日志") },
                    { seq: "Ctrl+B",  action: qsTr("显示/隐藏上下文面板") },
                    { seq: "Ctrl+[",  action: qsTr("显示/隐藏侧边栏") },
                    { seq: "Ctrl+D",  action: qsTr("切换深色/浅色主题") },
                    { seq: "Ctrl+/",  action: qsTr("打开本速查表") },
                    { seq: "Enter",   action: qsTr("发送消息") },
                    { seq: "Shift+Enter", action: qsTr("输入换行") },
                    { seq: "↑ / ↓",   action: qsTr("回溯/前进输入历史") },
                    { seq: "Ctrl+V",  action: qsTr("粘贴图片附件") }
                ]

                delegate: RowLayout {
                    required property var modelData
                    width: parent.width
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
                            text: modelData.seq
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.bold: true
                            font.family: Theme.fontMono
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.action
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

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
}
