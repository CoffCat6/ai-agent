pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 对话区左侧的用户消息导航导轨（长短不一的线条 = 每一次提问）。
//
// 设计取舍：
//   * 线长按**正文全长**映射（不是 preview 长度）——否则一条 500 字的
//     长消息和一条 30 字的短消息在被截断后画成一样长，导轨就失去了
//     「哪次问得多」的信息。三档而非连续映射：连续映射会让同一会话里
//     线长忽长忽短，用户无法形成稳定预期。
//   * 高度跟随记录数（上限 60% 视口高），记录少时不硬拉满——3 条线摊在
//     一屏 60% 的高度里会稀得看不出这是一组。
//   * 超过上限时整体收缩间距，而不是让导轨自己滚动：导轨的作用是
//     「一眼看全局」，能滚动就失去意义。
//   * 点击不发信号给外部去算行号——entry 里本来就带着 trace 行号，
//     直接透出 `jumpRequested(row)`，少一层换算就少一处出错机会。
Item {
    id: root

    // AgentVM.userMessageIndex 的原样数据：
    // [{row, messageId, preview, length, timestamp}, …]
    property var entries: []
    // 当前视口落在第几条（-1 = 还没滚过第一条）。
    property int activeIndex: -1
    // 视口高度，用来夹导轨自身高度。
    property real viewportHeight: 0

    signal jumpRequested(int row)

    // 当前悬停的条目（供唯一的共享 ToolTip 渲染）。null = 没有悬停。
    property var hoveredEntry: null
    property int hoveredNumber: 0
    // 悬停的槽位标识（delegate 的 index），用于"从 A 直接移到 B"时不闪。
    property int hoveredSlotIndex: -1

    readonly property int count: entries ? entries.length : 0
    // 单条没有导航价值：既不能"跳转"，也不该占一条视觉轨道。
    readonly property bool navigable: count > 1

    // 线长上限，也是最长那档（正文 ≥80 字）。
    readonly property int barWidthMax: 14
    // 每条占用的固定槽高。线本身只有 2px 高，槽留 16px 让相邻两条不粘连。
    readonly property int slotHeight: 16
    // 上限：视口高的 60%。再高会与右上角的过程折叠徽章抢位置。
    readonly property int maxTrackHeight: Math.max(0, Math.round(viewportHeight * 0.6))
    readonly property int naturalHeight: count * slotHeight
    readonly property int trackHeight: Math.min(naturalHeight, maxTrackHeight)
    // 装不下时的实际槽高——等比压缩，线长不变（线长表达的是内容量，
    // 不该因为记录变多而被压扁）。除以 count 而不是 count-1：最后一条
    // 自身也占一个槽，漏掉它会让导轨整体溢出的那一个槽高。
    readonly property real effectiveSlot: count > 0
        ? Math.max(2, trackHeight / count)
        : slotHeight

    visible: navigable && viewportHeight > 0
    width: 18
    height: trackHeight
    z: 4

    // 正文全长 → 线长三档。阈值取 20/80：一句话提问、常规提问、
    // 带上下文的详细说明。
    function barWidth(length) {
        if (length < 20) return 6
        if (length < 80) return 10
        return barWidthMax
    }

    function timeText(timestamp) {
        if (!timestamp || timestamp <= 0) return ""
        return Qt.formatDateTime(new Date(timestamp), "hh:mm")
    }

    Repeater {
        model: root.entries

        delegate: Item {
            id: slot
            required property var modelData
            required property int index

            // 装不下时按 effectiveSlot 压缩（见根节点注释），装得下时
            // 它恰好等于 slotHeight。
            y: index * root.effectiveSlot
            width: root.width
            height: Math.max(2, root.effectiveSlot)

            readonly property bool active: index === root.activeIndex
            readonly property bool hovered: hoverArea.containsMouse

            readonly property real baseWidth: root.barWidth(modelData.length || 0)

            Rectangle {
                id: bar
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                // active / hover 时稍长一点；active 常驻，hover 只在该条上。
                width: slot.baseWidth + (slot.active || slot.hovered ? 4 : 0)
                height: 2
                radius: 1
                color: slot.active
                    ? Theme.brandPrimary
                    : (slot.hovered ? Theme.textSecondary : Theme.textTertiary)
                opacity: slot.active ? 1.0 : (slot.hovered ? 0.9 : 0.55)

                // 120ms：导航件要有反馈感，但不能拖慢快速扫视。
                Behavior on width {
                    NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                }
                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            MouseArea {
                id: hoverArea
                anchors.fill: parent
                // 命中区比线条本身宽：2px 高的线直接点会很难对准。
                anchors.rightMargin: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.jumpRequested(slot.modelData.row)
            }

            // 悬停只记录"哪一条"，提示本体是下面那个唯一的 ToolTip。
            // 50 条记录各带一个 ToolTip + 自定义 contentItem 太重，而任意
            // 时刻至多一个可见。
            onHoveredChanged: {
                if (slot.hovered) {
                    root.hoveredEntry = slot.modelData
                    root.hoveredNumber = slot.index + 1
                    root.hoveredSlotIndex = slot.index
                } else if (root.hoveredSlotIndex === slot.index) {
                    // 只有"当前这条"离开时才清空：鼠标从 A 直接移到 B 时
                    // A 的 hovered=false 会先于 B 的 true 触发，无条件清空
                    // 会让提示闪一下。
                    root.hoveredEntry = null
                    root.hoveredSlotIndex = -1
                    root.hoveredNumber = 0
                }
            }

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("跳到第 %1 条提问：%2")
                .arg(slot.index + 1)
                .arg(slot.modelData.preview || "")
            Accessible.onPressAction: root.jumpRequested(slot.modelData.row)
        }
    }

    // 唯一的悬停提示。用自定义 contentItem 而不是 ToolTip.text：需要
    // 「序号 + 时间」一行、分隔线、正文多行，裸 text 排不出来。
    ToolTip {
        id: sharedTip
        visible: root.hoveredEntry !== null && root.hoveredSlotIndex >= 0
        // 导轨条目密集，用户会快速掠过；默认 500ms 在这里偏慢。
        delay: 400
        padding: 0

        contentItem: Rectangle {
            implicitWidth: tipColumn.implicitWidth + 20
            implicitHeight: tipColumn.implicitHeight + 16
            color: Theme.cardBackground
            radius: 6
            border.width: 1
            border.color: Theme.cardBorder

            Column {
                id: tipColumn
                anchors.centerIn: parent
                spacing: 6

                Row {
                    spacing: 8

                    Text {
                        text: qsTr("第 %1 条").arg(root.hoveredNumber)
                        color: Theme.brandPrimary
                        font.pixelSize: 10
                        font.family: Theme.fontSans
                        font.bold: true
                    }

                    Text {
                        readonly property string stamp: root.timeText(
                            root.hoveredEntry ? (root.hoveredEntry.timestamp || 0) : 0)
                        text: stamp
                        visible: stamp !== ""
                        color: Theme.textTertiary
                        font.pixelSize: 10
                        font.family: Theme.fontMono
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.borderSubtle
                }

                Text {
                    readonly property string full: root.hoveredEntry
                        ? (root.hoveredEntry.preview || "") : ""
                    text: full
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.Wrap
                    width: Math.min(280, Math.max(140, implicitWidth))
                    maximumLineCount: 6
                    elide: Text.ElideRight
                }
            }
        }
    }
}
