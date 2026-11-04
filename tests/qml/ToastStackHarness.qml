import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// AppToast 纵向栈夹具（无 C++ 测试目标时可用 qml.exe 手工运行）。
// 四个实例覆盖：先后排列、最多 3 条、超限时最旧一条立即消退、自动消退。
Window {
    id: win

    width: 720
    height: 480
    visible: true
    color: Theme.bgPrimary

    property var lastYs: []
    property var lastVisible: []

    function showAll() {
        t1.show("第一条：普通通知")
        t2.show("第二条：保存成功")
        t3.show("第三条：网络重试中")
        t4.show("第四条：应淘汰最旧一条")
    }

    function snapshot() {
        lastYs = [t1.y, t2.y, t3.y, t4.y]
        lastVisible = [t1.visible, t2.visible, t3.visible, t4.visible]
        console.log("TOAST_YS=" + lastYs.join(","))
        console.log("TOAST_VISIBLE=" + lastVisible.join(","))
    }

    AppToast {
        id: t1
        objectName: "toast1"
        duration: 60000
        message: ""
    }
    AppToast {
        id: t2
        objectName: "toast2"
        accentColor: Theme.statusSuccess
        duration: 60000
    }
    AppToast {
        id: t3
        objectName: "toast3"
        accentColor: Theme.statusWarning
        duration: 60000
    }
    AppToast {
        id: t4
        objectName: "toast4"
        accentColor: Theme.textSecondary
        duration: 60000
    }

    Timer {
        interval: 250
        running: true
        onTriggered: win.showAll()
    }
    Timer {
        interval: 700
        running: true
        onTriggered: {
            win.snapshot()
            Qt.quit()
        }
    }
}