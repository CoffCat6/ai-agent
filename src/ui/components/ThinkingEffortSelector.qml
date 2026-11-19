import QtQuick
import QtQuick.Controls
import NexusAI.ViewModels

// 思考强度选择器：进度条式分段档位条。
//
// 原先是「触发器 + 上拉菜单」的下拉：档位藏在弹层里，改一档要走
// 开-找-点-关四步。现在四档（关闭/低/中/高）平铺在一根分段条上——每段是
// 一个可点的档位，填充到选中档即成一根进度条，改档 = 点一下；键盘为
// slider 语义，左右方向键直接改档并生效。会话级语义不变：改的是当前会话
// 的临时值（AgentVM.setChatReasoningEffort），不碰 profile 默认层；运行中
// 的任务继续用发起时的快照。
//
// 「默认」不是条上的一段：它不是一个强度，而是"不发这个参数"。所以配置页
// 给 profile 设了默认档（低/中/高）时，这里直接回显那一档并把填充推进到它；
// 三层（会话临时值 / 模型覆盖 / profile 默认）都缺键时才显示「思考·默认」，
// 条上四段全不点亮（selectedIndex = -1）。
//
// 填充按「步进位置」算（选第 k 档 → 前 k+1 段亮）。档位含义由标签与悬停
// 提示承载，不单靠颜色（docs/43 §6.4）。
//
// 动效（全部走 Theme.motion* 令牌；「减少动效」开启时令牌归零，动画即时
// 消失——进行中的脉冲另有显式取消，见 segment.motionOn）：
//   * 段位填充色过渡 —— 推进/回退用颜色表达，不做位移：滑动会让人把
//     "选择位置"误读成某个连续量。
//   * 选中段脉冲 —— 填充时 scale 弹一下，延迟按段位序号递增，多段同时
//     变填充时形成一道从左到右的推进波纹。
//   * 悬停段放大 —— 16px 宽的目标偏小，放大给出可点中的即时反馈。
//   * 强度点与文案 —— 品牌色切换 + 换字淡入，档位变化在文字区也有落点。
// scale 是渲染变换，不参与 Row 布局，因此放大不会挤动相邻段位。
Item {
    id: root

    implicitWidth: effortRow.implicitWidth + 20
    implicitHeight: 28

    readonly property string current: {
        const v = AgentVM.chatModelProfile.reasoningEffort
        return v === undefined ? "" : String(v)
    }

    // 条上只放**真实档位**：关闭 → 低 → 中 → 高。原先最左端还有一个
    // 「默认」段，但它不是一个强度——它是"不发这个参数，由模型/配置决定"，
    // 占一段会让条读成"默认最弱、高最强"，而用户真正想知道的是"我现在处在
    // 哪一档"。配置页给 profile 设了默认档（低/中/高）时，这里直接回显那一
    // 档；三层都缺键时才回到「思考·默认」（四段全不点亮，见 noExplicitLevel）。
    readonly property var levels: [
        { value: "off",      label: qsTr("思考·关闭"), desc: qsTr("明确要求不要思考，响应最快、最省 token") },
        { value: "low",      label: qsTr("思考·低"),   desc: qsTr("少量推理预算，适合简单问答") },
        { value: "medium",   label: qsTr("思考·中"),   desc: qsTr("均衡；工具型任务推荐") },
        { value: "high",     label: qsTr("思考·高"),   desc: qsTr("最大推理投入，慢且贵，适合难题") }
    ]

    // 当前值不落在任何真实档位上 = 未显式指定（会话临时值、模型覆盖、
    // profile 默认三层都缺键）：请求里不发该字段，条上四段全不点亮。
    readonly property bool noExplicitLevel: root.selectedIndex < 0

    // 自定义网关刚建好时活动配置还没有模型名，此时思考档位既不会随请求发出也没有
    // 任何效果——与其让人点了没反应，不如显式禁用并说明原因。
    readonly property bool usable: AgentVM.modelConfigured && !AgentVM.stateBusy
    // docs/25 D4: 整体禁用让 Tab 焦点链跳过不可用触发器（enabled 会向下传播
    // 到 MouseArea，取代原先单独的 triggerMA.enabled 绑定）。
    enabled: root.usable

    // -1 = 未显式指定（不是"第零档"）。空串档位的语义是"不发参数"，
    // 与"关闭"（明确要求不思考）是两回事，所以不能折叠到第 0 段。
    readonly property int selectedIndex: {
        for (var i = 0; i < root.levels.length; ++i)
            if (root.levels[i].value === root.current) return i
        return -1
    }
    readonly property string currentLabel: root.noExplicitLevel
        ? qsTr("思考·默认") : root.levels[root.selectedIndex].label
    readonly property string currentDesc: root.noExplicitLevel
        ? qsTr("不发送思考参数，由模型的默认行为决定") 
        : root.levels[root.selectedIndex].desc

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

            // 强度标识点：中性档只是一枚灰点，切到「中/高」时换成品牌色并
            // 轻微放大——颜色与尺寸同时变，档位强弱在余光里也能感知。
            Rectangle {
                id: strengthDot
                width: 6
                height: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: root.emphasized ? Theme.brandPrimary : Theme.textSecondary
                scale: root.emphasized ? 1.35 : 1.0

                Behavior on color { ColorAnimation { duration: Theme.motionBase } }
                Behavior on scale {
                    NumberAnimation { duration: Theme.motionBase; easing.type: Easing.OutQuad }
                }
            }

            // 档位文案随选中项变化：换字时做一次极短的上浮淡入，让"改档了"
            // 这件事在文字区也有落点（颜色动画由下方 Behavior 承担）。
            Text {
                id: levelLabel
                anchors.verticalCenter: parent.verticalCenter
                text: root.usable ? root.currentLabel : qsTr("思考·未配置模型")
                color: root.usable && (pillHover.hovered || root.activeFocus)
                    ? Theme.brandPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontMono

                Behavior on color { ColorAnimation { duration: Theme.motionBase } }
                // 文案换字时做一次极短的上浮淡入：改档这件事在文字区也有落点。
                // 用 opacity+y 而不是换 Text 实例，避免布局抖动。
                onTextChanged: labelFade.restart()
                SequentialAnimation {
                    id: labelFade
                    NumberAnimation {
                        target: levelLabel
                        property: "opacity"
                        to: 0.35
                        duration: Theme.motionFast
                    }
                    NumberAnimation {
                        target: levelLabel
                        property: "opacity"
                        to: 1.0
                        duration: Theme.motionFast
                        easing.type: Easing.OutQuad
                    }
                }
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
                        // -1（未显式指定）时全段不亮：条件对 0..3 全为 false。
                        readonly property bool filled: root.selectedIndex >= segment.index

                        // 选中瞬间的脉冲量（0→1→0）：scale 由它和悬停态合成，
                        // 这样"刚被点中"和"指针停在这段上"两种反馈互不覆盖。
                        property real pulse: 0
                        readonly property bool hovered: segmentMA.containsMouse

                        width: 16
                        height: 8
                        radius: 4
                        // scale 是渲染变换、不参与 Row 的布局计算，放大不会把
                        // 相邻段位挤开（16px 宽 + 3px 间距，1.22 倍仍不重叠）。
                        scale: (segment.hovered ? 1.18 : 1.0) + segment.pulse * 0.22
                        color: segment.filled ? Theme.brandPrimary
                            : Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g,
                                      Theme.textSecondary.b, 0.25)

                        // 填充态变化用颜色过渡表示"推进/回退"，不做位移动画：
                        // 分段条表达的是选择位置，滑动会让人误读成某个连续量。
                        Behavior on color {
                            ColorAnimation { duration: Theme.motionBase; easing.type: Easing.OutQuad }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutQuad }
                        }

                        // 被填充时弹一下；延迟按段位序号递增，多次同时变填充
                        // 时形成一道从左到右的推进波纹。
                        //
                        // 「减少动效」不能只靠时长归零：已经在跑的
                        // SequentialAnimation 不会因为 duration 变了就立刻
                        // 结束（pulse 会停在中间值上）。所以这里显式判断令牌，
                        // 归零时直接停表并复位，不留半截动画。
                        readonly property bool motionOn: Theme.motionFast > 0
                        onMotionOnChanged: {
                            if (segment.motionOn) return
                            segmentPulse.stop()
                            segment.pulse = 0   // stop() 不会回滚到起点值
                        }
                        onFilledChanged: {
                            if (!segment.filled) return
                            if (!segment.motionOn) { segment.pulse = 0; return }
                            segmentPulse.restart()
                        }
                        SequentialAnimation {
                            id: segmentPulse
                            PauseAnimation { duration: segment.index * (Theme.motionFast * 0.5) }
                            NumberAnimation {
                                target: segment
                                property: "pulse"
                                to: 1.0
                                duration: Theme.motionFast
                                easing.type: Easing.OutQuad
                            }
                            NumberAnimation {
                                target: segment
                                property: "pulse"
                                to: 0.0
                                duration: Theme.motionBase
                                easing.type: Easing.OutQuad
                            }
                        }

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
