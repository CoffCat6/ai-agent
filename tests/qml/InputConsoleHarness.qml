import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for the '@' completion interaction. State is injected from
// C++ (see test_mention_interaction.cpp) so the test can drive real key and
// mouse input without a project or an LLM.
Window {
    id: win
    width: 640
    height: 240
    visible: true
    color: Theme.bgSecondary

    InputConsole {
        id: consoleRoot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
    }

    // Injecting the completion list from C++ through a var property is not
    // enough: a QVariantList assigned via setProperty is not visible to JS as
    // an array (length undefined), which is exactly what the completion
    // handlers check. Passing it through a QML function gives a real JS array.
    function setMentionResults(items, index) {
        consoleRoot.mentionIndex = index
        consoleRoot.mentionResults = items
        consoleRoot.mentionOpen = true
    }

    // 同 setMentionResults 的理由：QVariantList 经 C++ setProperty 注入后
    // 在 JS 里 length 是 undefined。这里完全在 JS 侧构造数组再赋给 dialog，
    // 复现"资源到达后菜单项是否出现"。count=0 即清空。
    function setMcpResources(dialog, count) {
        const list = []
        for (let i = 0; i < count; ++i) {
            list.push({ server: "s", uri: "file:///r" + i + ".txt",
                        name: "r" + i + ".txt", title: "", mimeType: "" })
        }
        dialog.allResources = list
    }

    // 整窗截图（contentItem 含 Overlay 补全面板），保存路径由测试侧给。
    function snapshot(path) {
        win.contentItem.grabToImage(function (result) {
            result.saveAs(String(path))
        }, Qt.size(win.width, win.height))
    }
}
