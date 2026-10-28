import QtQuick
import QtQuick.Window
import NexusAI

// IconButton 翻转动画的宿主：两个按钮，一个开启翻转一个不开启，
// 用来断言"该压扁的压扁、不该动的别动"。
Window {
    width: 260
    height: 140
    visible: true
    color: "#000000"

    IconButton {
        objectName: "flipBtn"
        x: 20; y: 20
        width: 80; height: 80
        iconSize: 72
        icon: "panel-collapse"
        flipOnIconChange: true
        restIconColor: "#ffffff"
        hoverIconColor: "#ffffff"
    }

    IconButton {
        objectName: "plainBtn"
        x: 140; y: 20
        width: 80; height: 80
        iconSize: 72
        icon: "panel-collapse"
        flipOnIconChange: false
        restIconColor: "#ffffff"
        hoverIconColor: "#ffffff"
    }
}
