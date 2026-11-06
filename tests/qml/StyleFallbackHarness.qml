import QtQuick
import QtQuick.Controls
import QtQuick.Window

// NexusStyle 未登记控件的回退能力夹具（tests/test_style_qml.cpp 专用）。
//
// TabBar / Dial 都不在 NexusStyle/qmldir 中，Qt Quick Controls 应自动回退到
// Basic 样式：实例化成功、隐式尺寸非零、且不产生 "not installed" 警告。
// 单文件的意义是让 C++ 侧只在加载本夹具期间挂消息钩子，避免其它测试噪声。
Window {
    visible: true
    width: 400
    height: 300

    TabBar {
        objectName: "fallbackTabBar"
        width: 300
        TabButton { text: "One" }
        TabButton { text: "Two" }
    }

    Dial {
        objectName: "fallbackDial"
        y: 80
    }
}