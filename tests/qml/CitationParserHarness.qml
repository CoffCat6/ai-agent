pragma ComponentBehavior: Bound
import QtQuick
import NexusAI.ViewModels

// docs/20 §5.1 (CITE-801): CitationParser QML 单例的运行时夹具。
//
// 由 tests/test_citation_qml.cpp 驱动。切分规则的唯一实现在
// src/viewmodels/CitationParser.h；本夹具证明 QML 真能调到注册进
// NexusAI.ViewModels 的那个单例、键名与 AssistantMessage 使用的字面量一致
// ——类型没注册成功时 AssistantMessage 的 chip 会静默失效，没有任何报错。
//
// 刻意只 import NexusAI.ViewModels（相对目录导入会绕过模块注册，测不到真东西）。
Window {
    id: root
    visible: true
    width: 400
    height: 300

    // 与 AssistantMessage.qml 里 delegate 读取 modelData 的方式完全一致。
    function split(text) { return CitationParser.parse(text) }
    function anyCitation(text) { return CitationParser.hasCitation(CitationParser.parse(text)) }
}
