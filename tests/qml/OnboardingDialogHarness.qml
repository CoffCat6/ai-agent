import QtQuick
import QtQuick.Window
import NexusAI

// docs/31 N1：首次启动向导的真实 QML 回归。
// main.qml 会在 shouldShow() 为真时调用 openOnboarding()，这里直接由
// 测试驱动同一条入口，避免依赖启动时序。
Window {
    id: win
    width: 760
    height: 620
    visible: true

    OnboardingDialog {
        objectName: "onboarding"
        parent: win.contentItem
        visible: true
    }
}
