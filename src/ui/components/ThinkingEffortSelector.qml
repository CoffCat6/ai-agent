import QtQuick
import QtQuick.Controls
import NexusAI.ViewModels

// 思考强度选择器：进度条式分段档位条。
//
// 原先是「触发器 + 上拉菜单」的下拉：档位藏在弹层里，改一档要走
// 开-找-点-关四步。现在五档（默认/关闭/低/中/高）平铺在一根分段条上——
// 每段是一个可点的档位，填充到选中档即成一根进度条，改档 = 点一下；
// 键盘为 slider 语义，左右方向键直接改档并生效。会话级语义不变：改的
// 是当前会话的临时值（AgentVM.setChatReasoningEffort），不碰 profile
// 默认层；运行中的任务继续用发起时的快照。
//
// 填充按「步进位置」算（选第 k 档 → 前 k+1 段亮）：「默认」是跟随
// profile 的特殊档而不是零强度，关闭也不是「思考最多」，所以条上表达
// 的是选择位置，档位含义由标签与悬停提示承载（不单靠颜色，docs/43 §6.4）。
Item {
    id: root

    implicitWidth: effortRow.implicitWidth + 20
    implicitHeight: 28

    readonly property string current: {
        const v = AgentVM.chatModelProfile.reasoningEffort
        return v === undefined ? "" : String(v)
    }

    // 未设置的 profile 在界面上显示为"跟随默认"，与实际发出去的报文一致
    // （未设置 = 该字段整体不发）。顺序即条上位置：默认 → 关闭 → 低 → 中 → 高。
    readonly property var levels: [
        { value: "",         label: qsTr("思考·默认"), desc: qsTr("不发送思考参数，由模型自己决定") },
        { value: "off",      label: qsTr("思考·关闭"), desc: qsTr("明确要求不要思考，响应最快、最省 token") },
        { value: "low",      label: qsTr("思考·低"),   desc: qsTr("少量推理预算，适合简单问答") },
        { value: "medium",   label: qsTr("思考·中"),   desc: qsTr("均衡；工具型任务推荐") },
        { value: "high",     label: qsTr("思考·高"),   desc: qsTr("最大推理投入，慢且贵，适合难题") }
    ]

    // 自定义网关刚建好时活动配置还没有模型名，此时思考档位既不会随请求发出也没有
    // 任何效果——与其让人点了没反应，不如显式禁用并说明原因。
    readonly property bool usable: AgentVM.modelConfigured && !AgentVM.stateBusy
    // docs/25 D4: 整体禁用让 Tab 焦点链跳过不可用触发器（enabled 会向下传播
    // 到 MouseArea，取代原先单独的 triggerMA.enabled 绑定）。
    enabled: root.usable

    readonly property int selectedIndex: {
        for (var i = 0; i < root.levels.length; ++i)
            if (root.levels[i].value === root.current) return i
        return 0
    }
    readonly property string currentLabel: root.levels[root.selectedIndex].label
    readonly property string currentDesc: root.levels[root.selectedIndex].desc

    // 只有"中/高"值得用品牌色点标识强度，默认与关闭保持中性。
    readonly property bool emphasized: current === "medium" || current === "high"

    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("思考强度") + " · " + root.currentLabel
    Accessible.description: root.currentDesc
    // docs/25 D4: 键盘可达——Tab 聚焦，左右方向键改档（slider 语义，
    // 按下即生效，与分段条的点击路径走同一个 selectIndex）。
    activeFocusOnTab: true
    Keys.onLeftPressed: root.selectIndex(root.selectedIndex - 1)
    Keys.onRightPressed: root.selectIndex(root.selectedIndex + 1)

    function selectLevel(value) {
        AgentVM.setChatReasoningEffort(value)
    }

    function selectIndex(index) {
        if (index < 0 || index >= root.levels.length) return
        root.selectLevel(root.levels[index].value)
    }

    // 档位说明随弹层一并删除后仍需可达：悬停段位时短暂展示。
    ToolTip {
        id: levelTip
        parent: triggerBox
        font.pixelSize: Theme.fontSizeMicro
        font.family: Theme.fontSans
    }

    Rectangle {
        id: triggerBox
        objectName: "thinkingEffortTrigger"
        anchors.fill: parent
        radius: 6
        opacity: root.usable ? 1.0 : 0.5
        // HoverHandler 而非 MouseArea：段位上的 MouseArea 不会抢走悬停态，
        // 指针在段位之间/之外移动时药丸底色连续响应。
        HoverHandler { id: pillHover }
        color: pillHover.hovered || root.activeFocus
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: root.activeFocus ? Theme.brandPrimary : "transparent"

        Behavior on color { ColorAnimation { duration: Theme.motionBase } }

        Row {
            id: effortRow
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                width: 6
                height: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: root.emphasized ? Theme.brandPrimary : Theme.textSecondary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.usable ? root.currentLabel : qsTr("思考·未配置模型")
                color: root.usable && (pillHover.hovered || root.activeFocus)
                    ? Theme.brandPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontMono
            }

            // 分段档位条：填充到选中档即"进度条"。未填充段用 textSecondary
            // 半透明而不是 surfaceHover/bgPrimary——药丸底色在悬停时会变，
            // 段位在两种底色下都要保持可见。
            Row {
                objectName: "thinkingEffortBar"
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Repeater {
                    model: root.levels

                    delegate: Rectangle {
                        id: segment
                        required property var modelData
                        required property int index
                        readonly property bool filled: root.selectedIndex >= segment.index

                        width: 16
                        height: 8
                        radius: 4
                        color: segment.filled ? Theme.brandPrimary
                            : Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g,
                                      Theme.textSecondary.b, 0.25)

                        MouseArea {
                            id: segmentMA
                            anchors.fill: parent
                            // 8px 高的段位太小：向外扩一圈作为点击热区。
                            anchors.margins: -3
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectIndex(segment.index)
                            onHoveredChanged: {
                                if (segmentMA.containsMouse)
                                    levelTip.show(segment.modelData.label + " · "
                                                  + segment.modelData.desc, 2000)
                            }
                        }

                        // 读屏沿用旧弹层的单选语义：每段一枚选项。
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: segment.modelData.label
                        Accessible.description: segment.modelData.desc
                        Accessible.checked: root.selectedIndex === segment.index
                        Accessible.onPressAction: root.selectIndex(segment.index)
                    }
                }
            }
        }
    }
}
