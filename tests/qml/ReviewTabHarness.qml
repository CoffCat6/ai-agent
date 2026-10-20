import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// Phase 1 test harness: the review panel exactly as ContextPanel hosts it —
// inside a StackLayout slot with listMaxHeight 0 (fill the tab height).
Window {
    id: win
    width: 380
    height: 500
    visible: true
    color: Theme.bgSecondary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppButton {
                text: qsTr("审阅")
                implicitHeight: 26
                highlighted: true
            }
            AppButton {
                text: qsTr("知识库")
                implicitHeight: 26
            }
            Item { Layout.fillWidth: true }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            ColumnLayout {
                spacing: 8

                FileChangesPanel {
                    id: panel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    listMaxHeight: 0
                }

                Text {
                    Layout.fillWidth: true
                    visible: !panel.hasItems
                    text: qsTr("本次会话暂无可审阅的文件改动。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    wrapMode: Text.Wrap
                }

                Item {
                    Layout.fillHeight: true
                    visible: !panel.hasItems
                }
            }
        }
    }

    function setCheckpoints(items) { panel.checkpoints = items }
    function itemCount() { return panel.itemCount }
    function renderedCount() { return panel.renderedCount }
    function panelHeight() { return panel.height }
    function panelVisible() { return panel.visible }
}