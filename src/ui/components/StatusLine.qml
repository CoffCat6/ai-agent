import QtQuick
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 设置页的连接测试/即时反馈状态行：圆点 + 一段可换行的说明。
//
// state 统一了此前三处各写一套的配色与文案位置：
//   busy    品牌色圆点呼吸，"正在测试…"
//   success 成功绿，"连接正常"
//   warning 警示橙，可恢复的失败（代理/搜索服务）
//   danger  错误红，硬失败（模型协议/鉴权）
//   info    次要色，无结论文案
// 圆点让状态不只依赖颜色，读屏也能读到完整 text。
RowLayout {
    id: root

    property string text: ""
    property string state: "info"
    // 失败详情常含 URL / HTTP 状态码，等宽更易读。
    property bool mono: false

    readonly property color accent: state === "success" ? Theme.statusSuccess
                                    : state === "warning" ? Theme.statusWarning
                                    : state === "danger" ? Theme.statusDanger
                                    : Theme.brandPrimary

    Layout.fillWidth: true
    spacing: Theme.spaceSm
    visible: text.length > 0

    Rectangle {
        id: stateDot
        Layout.alignment: Qt.AlignVCenter
        width: 7
        height: 7
        radius: width / 2
        color: root.accent
    }

    // 测试中让圆点呼吸，明确"还在跑"而不是"没反应"。
    SequentialAnimation {
        id: busyPulse
        running: root.state === "busy"
        loops: Animation.Infinite
        onStopped: stateDot.opacity = 1
        NumberAnimation {
            target: stateDot
            property: "opacity"
            from: 1.0
            to: 0.25
            duration: 550
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: stateDot
            property: "opacity"
            from: 0.25
            to: 1.0
            duration: 550
            easing.type: Easing.InOutQuad
        }
    }

    Text {
        Layout.fillWidth: true
        text: root.text
        color: root.state === "info" ? Theme.textSecondary : root.accent
        font.pixelSize: Theme.fontSizeCaption
        font.family: root.mono ? Theme.fontMono : Theme.fontSans
        wrapMode: Text.Wrap
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
}
