import QtQuick
import QtQuick.Window

// docs/20 §3.4（A4）拖放边界回归测试的被测组件：与生产 main.qml 的
// DropArea 契约一致——
//   - 根级、铺满整窗（DropArea 是唯一会设置 ItemAcceptsDrops 的类型，
//     平台光标"允许"依赖它）；
//   - onEntered 接受含 URL 的拖拽；
//   - onDropped 把 drop.urls **原样**交给 C++（QML 的 url 对象没有
//     toLocalFile()，绝不能在这里解析路径）。
// `Sink` 由 C++ 注入，签名与 AgentViewModel::handleDroppedFiles 同形。
Window {
    id: win
    // 必须 visible：拖放分派会跳过不可见 item（QQuickDeliveryAgent 里
    // !item->isVisible() 直接 return false），没有可见窗口就收不到拖放。
    visible: true
    width: 400
    height: 300

    DropArea {
        id: windowDrop
        anchors.fill: parent

        onEntered: function(drag) {
            if (drag.hasUrls)
                drag.acceptProposedAction()
        }

        onDropped: function(drop) {
            if (!drop.hasUrls)
                return
            Sink.handleDroppedFiles(drop.urls)
            drop.acceptProposedAction()
        }
    }
}
