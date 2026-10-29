import QtQuick
import NexusAI
import NexusAI.ViewModels

// Small copy-to-clipboard button with "copied" feedback
Rectangle {
    id: root

    property string text: ""
    property color bgColor: "transparent"

    width: label.implicitWidth + 16
    height: 22
    radius: 4
    color: copyMA.containsMouse ? Theme.surfaceHover : root.bgColor
    // docs/25 D1: 键盘焦点环（复制成功色优先级高于焦点色）
    border.width: 1
    border.color: root.activeFocus ? Theme.brandPrimary
                 : (root.copied ? Theme.statusSuccess : Theme.borderSubtle)
    opacity: copyMA.containsMouse || root.copied || root.activeFocus ? 1.0 : 0.55

    property bool copied: false

    // 复制动作提为函数：鼠标、键盘、无障碍三条激活路径共用。
    function copyToClipboard() {
        if (root.text.length > 0) {
            AgentVM.copyToClipboard(root.text)
            root.copied = true
            resetTimer.restart()
        }
    }

    Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }
    Behavior on border.color { ColorAnimation { duration: Theme.motionBase } }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.copied ? qsTr("已复制") : qsTr("复制")
        color: root.copied ? Theme.statusSuccess : Theme.textSecondary
        font.pixelSize: Theme.fontSizeMicro
        font.family: Theme.fontMono
    }

    MouseArea {
        id: copyMA
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.copyToClipboard()
    }

    // docs/25 D1: 键盘可达（Tab 聚焦 + 回车/空格激活）
    activeFocusOnTab: true
    Keys.onReturnPressed: root.copyToClipboard()
    Keys.onSpacePressed: root.copyToClipboard()

    Timer {
        id: resetTimer
        interval: 1500
        onTriggered: root.copied = false
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.copied ? qsTr("已复制") : qsTr("复制")
    Accessible.onPressAction: root.copyToClipboard()
}
