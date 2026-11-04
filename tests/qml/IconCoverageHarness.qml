import QtQuick
import QtQuick.Window
import NexusAI

// AppIcon 覆盖夹具：锁住新增的 x / message / storage 三个 key。
// 该夹具不依赖 C++ 测试目标，可用：
//   QML_IMPORT_PATH=build qml.exe tests/qml/IconCoverageHarness.qml
// 运行后把三个图标抓成 icon-coverage.png，确认不是 menu 兜底三横线。
Window {
    id: win

    width: 260
    height: 110
    visible: true
    color: "#ffffff"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 24

        AppIcon {
            objectName: "iconX"
            icon: "x"
            size: 48
            iconColor: "#000000"
        }
        AppIcon {
            objectName: "iconMessage"
            icon: "message"
            size: 48
            iconColor: "#000000"
        }
        AppIcon {
            objectName: "iconStorage"
            icon: "storage"
            size: 48
            iconColor: "#000000"
        }
    }

    function grabbedPath() { return "icon-coverage.png" }

    Timer {
        interval: 400
        running: true
        onTriggered: {
            row.grabToImage(function(result) {
                const ok = result.saveToFile(win.grabbedPath())
                console.log("ICON_GRAB_SAVED=" + ok
                            + " SIZE=" + result.width + "x" + result.height)
                Qt.quit()
            })
        }
    }
}