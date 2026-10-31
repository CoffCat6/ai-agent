import QtQuick
import QtQuick.Window
import NexusAI

// docs/31 O9：字体下拉的过滤与等宽约束需要真实控件才能回归
// （纯 C++ 只能证明 FontProbe 的集合正确，证明不了下拉真的用了它）。
Window {
    id: win
    width: 900
    height: 720
    visible: true

    InterfacePage {
        anchors.fill: parent
    }
}
