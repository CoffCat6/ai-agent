import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 会话级提示词预设选择器。
//
// 与 ModelSelector / PermissionModeSelector / ThinkingEffortSelector 同一套
// 形态：胶囊触发器贴输入框底部，点击向上弹主题化 Popup。放在这三个选择器
// 旁边而不是顶栏——顶栏只剩 40px 高、还要塞下状态点与两个角标按钮，长预设
// 名在那里一定会被挤掉。
//
// 触发胶囊始终回显"当前套用的是哪个预设"（AgentVM.activePromptPresetId），
// 未套用时显示全局默认；已套用但预设被删掉时仍显示名字并标注来源失效，
// 因为删除预设不影响已套用的会话是设置页给用户的明确承诺。
Item {
    id: root

    implicitWidth: presetRow.implicitWidth + 20
    implicitHeight: 28

    // 「管理预设…」通往设置中心 prompts 分类；本组件不持有对话框实例。
    signal manageRequested()

    readonly property string activeId: AgentVM.activePromptPresetId
    readonly property string activeName: AgentVM.activePromptPresetName
    readonly property bool applied: activeId.length > 0
    readonly property bool popupOpened: presetPopup.opened
    // 预设被删除后 id 仍在、列表里却查不到——只有这种情况才提示"来源已删除"。
    readonly property bool sourceMissing: {
        if (!applied) return false
        const list = Settings.promptPresets
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === activeId) return false
        return true
    }
    // 摘要落进标签页时按字符数截断：预设正文是一整段角色设定，塞进 10px
    // 的单行会把它压成半句话。
    function summarize(text) {
        const flat = String(text === undefined || text === null ? "" : text)
            .replace(/\s+/g, " ").trim()
        return flat
    }

    readonly property string triggerLabel: {
        if (sourceMissing) return qsTr("预设已删除 · %1").arg(activeName)
        if (applied) return activeName
        return qsTr("提示词·默认")
    }
    // 套用中才用品牌色：默认态与另三个选择器保持同样的中性灰，避免选项行
    // 上出现四个都在"强调"的胶囊。
    readonly property color accentColor:
        applied ? (sourceMissing ? Theme.statusWarning : Theme.brandPrimary)
                : Theme.textSecondary

    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("提示词预设") + " · " + triggerLabel
    Accessible.description: applied
        ? qsTr("当前会话套用预设「%1」，覆盖设置里的全局系统提示词").arg(activeName)
        : qsTr("当前会话未套用预设，使用设置里的全局系统提示词")
    Accessible.onPressAction: root.toggleMenu()

    // docs/25 D4: 触发器键盘可达——Tab 聚焦，回车/空格/下方向键开合菜单。
    activeFocusOnTab: true
    function toggleMenu() {
        presetPopup.opened ? presetPopup.close() : presetPopup.open()
    }
    Keys.onReturnPressed: root.toggleMenu()
    Keys.onSpacePressed: root.toggleMenu()
    Keys.onDownPressed: root.toggleMenu()

    // 测试/自检用：弹出层在**场景**坐标里的几何（渲染回归断言用）。
    // Popup 不是 Item，没有 mapToItem；但它的 parent 是 Item，先取 popup
    // 相对 parent 的 x/y 再交给 parent 映射到场景，语义才等价于"它画在
    // 屏幕的哪里"。
    readonly property Item popupSceneParent: presetPopup.parent
    readonly property point popupScenePos: popupSceneParent
        ? popupSceneParent.mapToItem(null, presetPopup.x, presetPopup.y)
        : Qt.point(0, 0)
    readonly property real popupLeft: popupScenePos.x
    readonly property real popupTop: popupScenePos.y
    readonly property real popupBottom: popupScenePos.y + presetPopup.height
    readonly property real popupWidth: presetPopup.width
    function presetRowCount() { return presetRepeater.count }
    // 最矮的一行：两行摘要要求足够行高，否则内容会被裁掉。
    function minPresetRowHeight() {
        var min = Number.MAX_VALUE
        for (var i = 0; i < presetRepeater.count; ++i) {
            const item = presetRepeater.itemAt(i)
            if (item && item.height < min) min = item.height
        }
        return min === Number.MAX_VALUE ? -1 : min
    }
    function presetRowHeight(index) {
        const item = presetRepeater.itemAt(index)
        return item ? item.height : -1
    }

    function applyPreset(preset) {
        presetPopup.close()
        AgentVM.applyPromptPreset(preset.id, preset.name, preset.prompt)
    }

    // ── 触发器胶囊 ────────────────────────────────────
    Rectangle {
        id: triggerBox
        objectName: "promptPresetTrigger"
        anchors.fill: parent
        radius: 6
        color: triggerMA.containsMouse || presetPopup.opened || root.activeFocus
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: presetPopup.opened || root.activeFocus
            ? root.accentColor : "transparent"

        Behavior on color { ColorAnimation { duration: Theme.motionBase } }

        Row {
            id: presetRow
            anchors.centerIn: parent
            spacing: 6

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: "preset"
                size: 12
                iconColor: triggerMA.containsMouse || presetPopup.opened
                    ? root.accentColor
                    : (root.applied ? root.accentColor : Theme.textSecondary)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.triggerLabel
                color: triggerMA.containsMouse || presetPopup.opened
                    ? root.accentColor
                    : (root.applied ? root.accentColor : Theme.textSecondary)
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontMono
                elide: Text.ElideRight
                // 长预设名不能把选项行顶出去：胶囊自身撑到约 260px 就截断。
                width: Math.min(implicitWidth, 168)
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: presetPopup.opened ? "chevron-up" : "chevron-down"
                size: 11
                iconColor: triggerMA.containsMouse || presetPopup.opened
                    ? root.accentColor : Theme.textSecondary
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
            onPressed: presetPopup.opened ? presetPopup.close() : presetPopup.open()
        }
    }

    // ── 向上弹出的预设列表 ────────────────────────────
    Popup {
        id: presetPopup
        // docs/25 D4: 打开时接受键盘焦点，菜单项才能进入焦点链。
        focus: true
        x: 0
        y: -height - 6          // 触发器贴着窗口底边，统一向上弹
        width: Math.max(320, root.width)
        height: content.implicitHeight + 12
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: content
            spacing: 2

            // ── 分区标题 + 当前状态 ──────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 4
                Layout.topMargin: 2
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: qsTr("提示词预设 · 当前会话")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                    font.family: Theme.fontMono
                }
                Text {
                    text: root.applied ? qsTr("已套用") : qsTr("跟随全局")
                    color: root.applied ? root.accentColor : Theme.textTertiary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontMono
                }
            }

            // ── 无预设（回落全局）─────────────────────
            Rectangle {
                id: noneItem
                readonly property bool isSelected: !root.applied
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.preferredHeight: 38
                radius: 5
                color: noneMA.containsMouse || noneItem.activeFocus
                    ? Theme.surfaceHover : "transparent"

                function activate() {
                    presetPopup.close()
                    AgentVM.clearPromptPreset()
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    AppIcon {
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        icon: "check"
                        size: 12
                        iconColor: Theme.brandPrimary
                        visible: noneItem.isSelected
                    }
                    Item {
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        visible: !noneItem.isSelected
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("无预设（使用全局系统提示词）")
                            color: noneItem.isSelected
                                ? Theme.brandPrimary : Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            font.bold: noneItem.isSelected
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Settings.systemPromptOverride.trim().length > 0
                                ? qsTr("设置页已配置自定义全局提示词")
                                : qsTr("设置页未配置，使用内置提示词")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                            elide: Text.ElideRight
                        }
                    }
                }

                MouseArea {
                    id: noneMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: noneItem.activate()
                }
                activeFocusOnTab: true
                Keys.onReturnPressed: noneItem.activate()
                Keys.onSpacePressed: noneItem.activate()
                Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                Accessible.role: Accessible.RadioButton
                Accessible.name: qsTr("无预设（使用全局系统提示词）")
                Accessible.checked: noneItem.isSelected
                Accessible.onPressAction: noneItem.activate()
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                Layout.preferredHeight: 1
                color: Theme.borderSubtle
            }

            // ── 预设列表：名称 + 提示词摘要 ───────────
            Repeater {
                id: presetRepeater
                model: Settings.promptPresets

                delegate: Rectangle {
                    id: presetItem
                    required property var modelData
                    readonly property bool isSelected: modelData.id === root.activeId
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    Layout.preferredHeight: 52
                    radius: 5
                    color: presetMA.containsMouse || presetItem.activeFocus
                        ? Theme.surfaceHover : "transparent"

                    function activate() {
                        root.applyPreset(modelData)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        AppIcon {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 6
                            icon: "check"
                            size: 12
                            iconColor: Theme.brandPrimary
                            visible: presetItem.isSelected
                        }
                        Item {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            visible: !presetItem.isSelected
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: presetItem.isSelected
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                font.bold: presetItem.isSelected
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                // 只给两行摘要：预设正文动辄几百字，列表要能
                                // 一屏扫完。
                                text: root.summarize(modelData.prompt)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: presetMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: presetItem.activate()
                    }
                    activeFocusOnTab: true
                    Keys.onReturnPressed: presetItem.activate()
                    Keys.onSpacePressed: presetItem.activate()
                    Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                    Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.name
                    Accessible.description: root.summarize(modelData.prompt)
                    Accessible.checked: presetItem.isSelected
                    Accessible.onPressAction: presetItem.activate()
                }
            }

            // ── 空态 ────────────────────────────────
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                visible: Settings.promptPresets.length === 0
                text: qsTr("还没有预设。用下面的「管理预设…」写下第一段角色设定。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                Layout.preferredHeight: 1
                color: Theme.borderSubtle
            }

            // ── 管理入口 ────────────────────────────
            Rectangle {
                id: manageItem
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.bottomMargin: 2
                Layout.preferredHeight: 30
                radius: 5
                color: manageMA.containsMouse || manageItem.activeFocus
                    ? Theme.surfaceHover : "transparent"

                function activate() {
                    presetPopup.close()
                    root.manageRequested()
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 6

                    AppIcon {
                        Layout.preferredWidth: 13
                        Layout.preferredHeight: 13
                        icon: "settings"
                        size: 13
                        iconColor: Theme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("管理预设…")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                }

                MouseArea {
                    id: manageMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: manageItem.activate()
                }
                activeFocusOnTab: true
                Keys.onReturnPressed: manageItem.activate()
                Keys.onSpacePressed: manageItem.activate()
                Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                Accessible.role: Accessible.MenuItem
                Accessible.name: qsTr("管理预设…")
                Accessible.onPressAction: manageItem.activate()
            }
        }
    }
}
