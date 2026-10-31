import QtQuick
import QtQuick.Window
import NexusAI

// docs/31 N10：关于页的构建信息必须来自 AppInfo，而不是页面上另写一份；
// 直接加载真实 GeneralPage，锁定文本绑定与开源许可列表。
Window {
    id: win
    width: 900
    height: 720
    visible: true

    GeneralPage {
        anchors.fill: parent
    }
}
