import QtQuick

// docs/20 §3.4（A4）根因锁：QML 的 url 包装对象原型上只有 toString/valueOf，
// **没有 toLocalFile()**。C++ 侧 test_droparea 断言这里的探测结果，一旦
// Qt 改变了这个前提就会失败，提醒复核 main.qml 的 DropArea 实现与注释
// （"为什么路径解析必须在 C++ 做"）。
QtObject {
    id: probe

    readonly property var urlValue: Qt.resolvedUrl("file:///C:/tmp/a%20b.txt")

    // "undefined" 表示没有该成员（预期）；"function" 表示 Qt 新增了它。
    readonly property string toLocalFileType: typeof urlValue.toLocalFile
    // toString() 必须始终可用：main.qml 依赖它做 file: 前缀判断是安全的。
    readonly property string href: urlValue.toString()
}
