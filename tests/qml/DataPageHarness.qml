import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// docs/31 S4.5 N5：数据管理页的渲染与确认门槛宿主。
//
// 统计数据由 C++ 侧用 setStats() 直接塞进页面（不依赖真实数据库）——
// 这里只验证"拿到 stats 之后，体积与行数是否真的渲染出来"，以及三个
// 维护动作是否都过二次确认框（未点确认绝不执行）。纯 C++ 的
// collectStats 用例看不到这类退化：数据对了，但 Repeater 没绑上。
Window {
    id: win
    width: 1000
    height: 760
    visible: true

    DataPage {
        id: dataPage
        objectName: "dataPage"
        anchors.fill: parent
    }

    function setStats(stats) {
        dataPage.stats = stats
    }

    // Component.onCompleted 的首次 refresh 在测试进程里没有数据库，会把
    // status 置为「统计失败：数据库未就绪」；用例开头清掉这条初始反馈。
    function clearStatus() {
        dataPage.statusMessage = ""
        dataPage.statusIsError = false
    }
}
