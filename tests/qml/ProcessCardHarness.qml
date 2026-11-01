pragma ComponentBehavior: Bound
import QtQuick
import NexusAI
import NexusAI.ViewModels

// docs/32 §9: ProcessCard 子代理过程日志的渲染夹具。
//
// 由 tests/test_process_card_qml.cpp 驱动。锁住一条曾真实翻车的转换语义：
// modelData.subagentLog 经 **ListView role**（QAbstractListModel role →
// QV4）进 QML 后是 sequence wrapper，Array.isArray() 对它返回 false ——
// ProcessCard 若用 Array.isArray 判型，过程日志会静默消失（数据链路全部
// 正常，UI 永远走 args 代码块分支）。修复后改为判 undefined/null + length。
//
// 为什么必须走 ListView：同一份 QVariantMap 经 context/setProperty 注入时
// 嵌套 QVariantList 会转成原生 JS Array（isArray=true），只有 role 路径才
// 产生 wrapper——夹具必须与真实 TraceModel 喂入方式同构才能复现。
Window {
    id: root
    visible: true
    width: 700
    height: 500

    // 由 C++ 注入（rowsModel 一行数据，setRow 可换内容）。
    property bool cardExpanded: true

    ListView {
        id: list
        anchors.fill: parent
        model: rowsModel
        // ProcessCard 根已声明 required property var modelData，role 直接
        // 填充——delegate 里不能再声明同名 required property（那会遮蔽组件
        // 内部绑定引用的属性，表现为全 undefined）。真机 TraceNodeRouter 用
        // 显式赋值 modelData: root.modelData，此处直达，语义相同。
        delegate: ProcessCard {
            id: card
            objectName: "card"
            width: list.width
            expanded: root.cardExpanded
        }
    }

    // C++ 侧断言通道：hasSubagentLog 判型、日志长度与首条 phase、body 状态，
    // 以及 rawIsArray —— role 路径下嵌套 QVariantList 不是原生 JS 数组，
    // 该断言锁住这个转换语义（回归判型若改回 Array.isArray 会连带红）。
    // Window 不是 Item，从 contentItem 起递归找 delegate 里的卡片。
    function probe() {
        const card = getChild(contentItem, "card")
        if (!card) return { found: false }
        const body = getChild(card, "bodyLoader")
        return {
            found: true,
            hasSubagentLog: card.hasSubagentLog,
            rawIsArray: Array.isArray(card.modelData.subagentLog),
            logLength: card.hasSubagentLog ? card.subagentLog.length : -1,
            firstPhase: card.hasSubagentLog ? card.subagentLog[0].phase : "",
            bodyActive: body ? body.active : false,
            bodyObjectName: (body && body.item) ? body.item.objectName : "",
        }
    }

    function getChild(item, name) {
        for (let i = 0; i < item.children.length; i++) {
            const child = item.children[i]
            if (child.objectName === name) return child
            const found = getChild(child, name)
            if (found) return found
        }
        return null
    }
}
