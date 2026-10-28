import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Reasoning effort belongs to the current conversation. Running tasks
// keep their configuration snapshot; changes here do not edit defaults.
Item {
    id: root

    implicitWidth: effortRow.implicitWidth + 20
    implicitHeight: 28

    readonly property string current: {
        const v = AgentVM.chatModelProfile.reasoningEffort
        return v === undefined ? "" : String(v)
    }

    // 未设置的 profile 在界面上显示为"跟随默认"，与实际发出去的报文一致
    // （未设置 = 该字段整体不发）。
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
    // 到 MouseArea/Popup，取代原先单独的 triggerMA.enabled 绑定）。
    enabled: root.usable

    readonly property string currentLabel: {
        for (var i = 0; i < levels.length; ++i)
            if (levels[i].value === current) return levels[i].label
        return qsTr("思考·默认")
    }

    // 只有"中/高"值得用品牌色点标识强度，默认与关闭保持中性。
    readonly property bool emphasized: current === "medium" || current === "high"

    // 回归用例用：弹层是否打开（Popup 不是 Item，测试取不到它）。
    readonly property bool opened: effortPopup.opened

    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("思考强度") + " · " + currentLabel
    Accessible.description: {
        for (var i = 0; i < levels.length; ++i)
            if (levels[i].value === current) return levels[i].desc
        return ""
    }
    // docs/25 D4: 触发器键盘可达——Tab 聚焦，回车/空格/下方向键开合菜单
    Accessible.onPressAction: root.toggleMenu()
    activeFocusOnTab: true
    function toggleMenu() {
        effortPopup.opened ? effortPopup.close() : effortPopup.open()
    }
    Keys.onReturnPressed: root.toggleMenu()
    Keys.onSpacePressed: root.toggleMenu()
    Keys.onDownPressed: root.toggleMenu()

    function selectLevel(value) {
        AgentVM.setChatReasoningEffort(value)
    }

    Rectangle {
        id: triggerBox
        objectName: "thinkingEffortTrigger"
        anchors.fill: parent
        radius: 6
        opacity: root.usable ? 1.0 : 0.5
        color: triggerMA.containsMouse || effortPopup.opened || root.activeFocus
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: effortPopup.opened || root.activeFocus
            ? Theme.brandPrimary : "transparent"

        Behavior on color { ColorAnimation { duration: 150 } }

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
                color: root.usable && (triggerMA.containsMouse || effortPopup.opened)
                    ? Theme.brandPrimary : Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontMono
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: effortPopup.opened ? "chevron-up" : "chevron-down"
                size: 11
                iconColor: triggerMA.containsMouse || effortPopup.opened
                    ? Theme.brandPrimary : Theme.textSecondary
            }
        }

        MouseArea {
            id: triggerMA
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // 再点一次关闭（修复）：原先 onClicked + CloseOnPressOutside 的组合
            // 有竞态——弹层在**按下**时就被关掉，抬起时 onClicked 读到 opened
            // 为 false 又打开一次，表现为"点第二次还是开着"。
            // 现在把判断放在 onPressed（此刻弹层状态还没被外部点击策略改写），
            // 并让弹层改用 CloseOnReleaseOutside：鼠标释放时才处理"点外面"，
            // 于是触发器自己的 pressed 处理总是先看到真实状态。
            // 必须在 onPressed 而不是 onReleased/onClicked 上做这个判断：
            // 弹层的 CloseOnReleaseOutside 会在**鼠标释放**时先把弹层关掉，
            // 于是 onReleased/onClicked 读到的 opened 恒为 false，toggle 就
            // 又打开了——用户看到的正是"点第二次还是开着，只能点别处关"。
            // onPressed 发生在弹层的外部点击处理之前，读到的才是点击前状态。
            onPressed: effortPopup.opened ? effortPopup.close() : effortPopup.open()
        }
    }

    // 触发器贴着窗口底边，所以和另两个选择器一样向上弹。
    Popup {
        id: effortPopup
        // docs/25 D4: 打开时接受键盘焦点，菜单项才能进入焦点链
        focus: true
        x: 0
        y: -height - 6
        width: 320
        height: effortContent.implicitHeight + 12
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: effortContent
            spacing: 2

            Text {
                text: qsTr("思考强度 · 当前会话")
                color: Theme.textSecondary
                font.pixelSize: 10
                font.bold: true
                font.family: Theme.fontMono
                Layout.leftMargin: 8
                Layout.topMargin: 2
            }

            Repeater {
                model: root.levels

                delegate: Rectangle {
                    id: levelItem
                    required property var modelData
                    readonly property bool isSelected: modelData.value === root.current
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    Layout.preferredHeight: 34
                    radius: 5
                    color: levelMA.containsMouse || levelItem.activeFocus
                        ? Theme.surfaceHover : "transparent"

                    // docs/25 D4: 菜单项键盘激活 + 读屏单选语义
                    function activate() {
                        effortPopup.close()
                        root.selectLevel(modelData.value)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6

                        AppIcon {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            icon: "check"
                            size: 12
                            iconColor: Theme.brandPrimary
                            visible: levelItem.isSelected
                        }

                        Item {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            visible: !levelItem.isSelected
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                text: modelData.label
                                color: levelItem.isSelected
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                font.bold: levelItem.isSelected
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.desc
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: levelMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: levelItem.activate()
                    }
                    activeFocusOnTab: true
                    Keys.onReturnPressed: levelItem.activate()
                    Keys.onSpacePressed: levelItem.activate()
                    Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                    Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.label
                    Accessible.description: modelData.desc
                    Accessible.checked: levelItem.isSelected
                    Accessible.onPressAction: levelItem.activate()
                }
            }
        }
    }
}
