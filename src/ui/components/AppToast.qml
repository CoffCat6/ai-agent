import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Transient toast banner anchored to the top edge of the overlay.
// accentColor picks the semantic border/text color (Theme.statusDanger for
// errors, Theme.statusSuccess for notices); duration is the auto-hide delay
// in milliseconds. Click anywhere on it to dismiss early. An optional
// action button (actionText) fires actionTriggered on click — used by the
// session-delete undo toast (docs/23 G7).
//
// 纵向栈：同一 Overlay 内的多个 AppToast 实例按显示先后自上而下排列，
// 不再依赖调用方手写 `anchors.topMargin: otherToast.visible ? 64 : 24`
// （那套偏移在"错误 + 撤销"同时出现时会重叠）。调用方仍然只持有自己的
// 实例——show() / showWithAction() 的 API 不变，无需额外 ToastManager。
Rectangle {
    id: root

    property string message: ""
    property color accentColor: Theme.statusSuccess
    property int duration: 3500
    property string actionText: ""
    signal actionTriggered()

    // ── 栈布局参数 ─────────────────────────────────────
    readonly property int stackBaseMargin: 24
    readonly property int stackGap: 8
    // 防止错误风暴把屏幕铺满：超过上限时最旧的一条立即消退。
    readonly property int maxVisible: 3
    // 供兄弟实例识别（不同 QML 文件实例之间没有类型判断手段）。
    readonly property bool isAppToast: true
    // 单调递增的显示序号；同一毫秒内连续 show() 也能稳定排序。
    property double shownAt: 0

    function show(msg) {
        shownAt = nextTimestamp()
        message = msg
        opacity = 1
        hideTimer.restart()
        enforceStackLimit()
    }

    function showWithAction(msg, action) {
        actionText = action
        show(msg)
    }

    function dismiss() {
        hideTimer.stop()
        opacity = 0
        actionText = ""
    }

    // 当前可见的兄弟 Toast（不含自己），按显示时间升序。
    function activeSiblings() {
        var list = []
        var p = root.parent
        if (!p || !p.children)
            return list
        var kids = p.children
        for (var i = 0; i < kids.length; ++i) {
            var k = kids[i]
            if (k !== root && k.isAppToast === true
                    && k.visible && k.opacity > 0)
                list.push(k)
        }
        list.sort(function(a, b) { return a.shownAt - b.shownAt })
        return list
    }

    function nextTimestamp() {
        var maxStamp = 0
        var p = root.parent
        if (p && p.children) {
            var kids = p.children
            for (var i = 0; i < kids.length; ++i) {
                var s = Number(kids[i].shownAt) || 0
                if (s > maxStamp)
                    maxStamp = s
            }
        }
        return maxStamp + 1
    }

    // 自己显示后，若总数超过 maxVisible，从最旧的开始退场。
    function enforceStackLimit() {
        var list = activeSiblings()
        var excess = list.length - maxVisible + 1
        for (var i = 0; i < excess; ++i)
            list[i].dismiss()
    }

    // 只累加排在自己前面的 Toast 高度；不读自身 x/y，避免绑定环。
    readonly property real stackY: {
        var siblings = activeSiblings()
        var y = stackBaseMargin
        for (var i = 0; i < siblings.length; ++i) {
            var t = siblings[i]
            if (t.shownAt < root.shownAt)
                y += t.height + stackGap
        }
        return y
    }

    parent: Overlay.overlay
    anchors.horizontalCenter: parent.horizontalCenter
    // 不再设置 anchors.top：调用方历史遗留的 anchors.topMargin 绑定因此
    // 变为惰性值（不参与定位），真正的位置由 y 绑定决定。
    y: root.stackY
    z: 100

    width: toastRow.implicitWidth + 32
    height: toastRow.implicitHeight + 20
    radius: 8
    color: Theme.bgSecondary
    border.width: 1
    border.color: root.accentColor
    opacity: 0
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }
    Behavior on y { NumberAnimation { duration: Theme.motionBase } }

    Row {
        id: toastRow
        // Above the dismiss MouseArea so the action button receives its
        // own clicks instead of the backdrop dismiss.
        z: 1
        anchors.centerIn: parent
        spacing: 12

        Text {
            id: toastText
            width: Math.min(implicitWidth, 460)
            text: root.message
            color: root.accentColor
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
        }

        AppButton {
            visible: root.actionText.length > 0
            text: root.actionText
            onClicked: {
                root.dismiss()
                root.actionTriggered()
            }
            Accessible.name: root.actionText
        }
    }

    Timer {
        id: hideTimer
        interval: root.duration
        onTriggered: root.dismiss()
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.dismiss()
    }

    Accessible.role: Accessible.Notification
    Accessible.name: root.message
}
