pragma ComponentBehavior: Bound
import QtQuick
import NexusAI
import NexusAI.ViewModels

// docs/45 §3.4：对话文本排版（行距 / ATX 标题 / 代码块）的渲染夹具。
//
// 由 tests/test_chat_workflows.cpp 驱动。加载链路与该套件的 input()
// （InputConsole）同构：源码树文件系统加载本文件，import NexusAI 经
// build 目录的模块 qmldir 解析到 qrc 里的 workflow_ui 副本——不新开
// 测试可执行，直接挂进 test_chat_workflows（它已链接 viewmodelsplugin
// 并把 src/ui/*.qml 全量塞进 qrc，见 tests/CMakeLists.txt 的
// workflow_ui 资源）。
//
// 两个 AssistantMessage 实例分别锁两条渲染路径：
//   - plainMsg：无围栏正文 → 单 TextEdit（mdText，流式与终态同一条，
//     AC-5）；status 可切 "running" 验流式分支。
//   - splitMsg：含围栏/标题 → segments 拆分路径（heading/text/code 段）。
//
// C++ 侧不经过 JS 探针转发：modelData 直接 setProperty 注入（var 属性
// 自带 changed 信号，body/segments 绑定会重算，先例见同套件的
// traceDetailKeyIsStableWithoutNodeId），segments 是消息实例的
// property var 可直接读取；子项按 objectName 定位（assistantSeg /
// assistantHeading / assistantTextSeg / assistantCodeSeg 均带段序号
// 后缀，codeBlockText 在 CodeBlock 内部）。
Window {
    id: root
    visible: true
    width: 700
    height: 900

    // 默认给一份合法数据（required property 必须创建时可得），用例
    // 一律显式覆盖 modelData。
    property var plainData: ({ title: "默认正文", status: "completed" })
    property var splitData: ({ title: "默认拆分正文", status: "completed" })

    AssistantMessage {
        id: plainMsg
        objectName: "plainMsg"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        modelData: root.plainData
        index: 0
    }

    AssistantMessage {
        id: splitMsg
        objectName: "splitMsg"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: plainMsg.bottom
        anchors.topMargin: 16
        modelData: root.splitData
        index: 1
    }
}