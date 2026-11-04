import QtQuick
import NexusAI
import NexusAI.ViewModels

// 按 modelData.type 把 trace 节点分派到对应的消息组件（docs/19 §4.1）。
// 用单个 Loader 只实例化被命中的那个组件：一个 delegate 不会为了渲染一条
// 用户消息而顺带实例化 6 个子组件，长会话（200+ 节点）下省下大量对象。
// 折叠状态由页面（AgentChatPage 的 traceListView）持有，经回调注入，
// 保证会话切换后折叠态不丢。
Item {
    id: root
    objectName: "traceNodeRouter"

    required property var modelData
    required property int index

    // 页面级"显示思考过程"总开关。
    property bool showProcess: true
    // 当前会话 id。无 id 的旧节点的折叠键用它做命名空间，跨会话不撞车。
    property string sessionId: ""
    // 折叠状态读写回调（来自页面 ListView）。
    property var detailExpandedFn
    property var toggleDetailFn
    // A1 (hit-locate): 搜索跳转后被短暂置 true，由聊天页的高亮计时器清除。
    property bool highlighted: false
    // 高亮强度（底色透明度）。搜索命中用 0.10（"这是你要找的"），
    // 导轨跳转用 0.06（只是"确认落点"，不该和搜索结果混淆）。
    property real highlightStrength: 0.10

    // A1 (hit-locate): 2s 自动复位高亮——避免搜索卡片残留高亮底色。
    Timer {
        id: highlightResetTimer
        interval: 2000
        onTriggered: highlighted = false
    }

    // A1 (hit-locate): highlighted 重新为 true 时重启 2s 复位计时。
    onHighlightedChanged: {
        if (highlighted)
            highlightResetTimer.restart()
    }

    readonly property string nodeType:
        (modelData !== undefined && modelData !== null)
            ? (modelData.type || "") : ""

    readonly property bool intermediate: nodeType === "thinking"
        || nodeType === "tool_call"
        || nodeType === "tool_result"
        || nodeType === "context"

    // waiting_confirm 必须在总开关关闭时仍可见，否则确认按钮不可达。
    readonly property bool processHidden: intermediate
        && !showProcess && modelData.status !== "waiting_confirm"

    // 内容区左右留白：超宽屏下把内容夹到 maxContentWidth 并居中（修 D2）。
    readonly property real hPad:
        Math.max(Theme.spaceXl, (width - Theme.maxContentWidth) / 2)
    readonly property real innerWidth: Math.max(0, width - hPad * 2)

    // 折叠 key：稳定优先。
    //   1) 节点 id（core 的 trace 节点都有 UUID）：流式更新内容变化也不变；
    //   2) 无 id 的旧/外部节点：v2:sessionId:index。trace 只追加不重排，
    //      行序在会话内稳定，切会话由 sessionId 隔开。
    // 旧版本的 `type:index:内容前缀` 键在流式增量下会随内容变化，导致
    // “刚要展开就被折回去”；这里额外暴露 legacyDetailKey 供页面回退读取。
    readonly property string detailKey: {
        const id = String(modelData.id || "")
        if (id !== "") return id
        return "v2:" + root.sessionId + ":" + index
    }
    readonly property string legacyDetailKey: {
        const id = String(modelData.id || "")
        if (id !== "") return id
        const detail = nodeType === "thinking"
            ? (modelData.reasoning || "")
            : ((modelData.args || "") !== "" ? modelData.args : (modelData.result || ""))
        return nodeType + ":" + index + ":" + detail.substring(0, 48)
    }
    readonly property string detailText: nodeType === "thinking"
        ? (modelData.reasoning || "")
        : ((modelData.args || "") !== "" ? modelData.args : (modelData.result || ""))
    readonly property bool detailDefault: detailText.length <= 400
    readonly property bool processExpanded: detailExpandedFn !== undefined
        && detailExpandedFn(detailKey, detailDefault, legacyDetailKey)

    // 消息块之间的垂直节奏（docs/19 §4.5）：用户/助手块间距更大，
    // 同侧连续过程卡片更紧凑。首行不留额外间距。
    readonly property int topGap: index === 0 ? 0
        : ((nodeType === "user_message" || nodeType === "summary")
           ? Theme.spaceLg : Theme.spaceSm)

    width: parent ? parent.width : 0
    height: processHidden ? 0 : implicitHeight
    visible: !processHidden
    implicitHeight: topGap + (loader.item ? loader.item.implicitHeight : 0)

    // A1 (hit-locate): 命中定位的短暂高亮底色（z 置底，不遮挡交互）。
    Rectangle {
        id: highlight
        z: -1
        anchors.fill: parent
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        radius: 8
        color: root.highlighted
            ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                      Theme.brandPrimary.b, root.highlightStrength)
            : "transparent"
        // 边框跟着强度走：弱高亮配 1.5px 还是会抢眼。
        border.width: root.highlighted
            ? (root.highlightStrength >= 0.10 ? 1.5 : 1.0) : 0
        border.color: Theme.brandPrimary
        Behavior on color { ColorAnimation { duration: Theme.motionSlow } }
    }

    Loader {
        id: loader
        // Folded process rows need neither layout nor their heavy text blocks.
        active: !root.processHidden
        // 按类型只加载命中的组件（未知类型走 fallbackComp）。
        sourceComponent: {
            if (nodeType === "user_message") return userComp
            if (nodeType === "summary") return assistantComp
            if (intermediate) return processComp
            if (nodeType === "metrics") return metricsComp
            if (nodeType === "error") return errorComp
            return fallbackComp
        }
        x: hPad
        y: topGap
        width: innerWidth
        // 高度由内容决定；delegate 的 implicitHeight 读 loader.item。
        height: item ? item.implicitHeight : 0
    }

    Component {
        id: userComp
        UserMessageBubble {
            width: loader.width
            modelData: root.modelData
            index: root.index
        }
    }

    Component {
        id: assistantComp
        AssistantMessage {
            width: loader.width
            modelData: root.modelData
            index: root.index
        }
    }

    Component {
        id: processComp
        ProcessCard {
            width: loader.width
            modelData: root.modelData
            expanded: root.processExpanded
            onToggleRequested: {
                if (root.toggleDetailFn !== undefined)
                    root.toggleDetailFn(root.detailKey, root.detailDefault,
                                        root.legacyDetailKey)
            }
        }
    }

    Component {
        id: metricsComp
        MetricsLine {
            width: loader.width
            modelData: root.modelData
        }
    }

    Component {
        id: errorComp
        ErrorCard {
            width: loader.width
            modelData: root.modelData
            index: root.index
        }
    }

    // 未知类型兜底：保留文本，避免节点被静默丢弃。
    Component {
        id: fallbackComp
        Text {
            width: loader.width
            text: root.modelData.title !== undefined ? root.modelData.title : ""
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            renderType: Text.NativeRendering
        }
    }
}
