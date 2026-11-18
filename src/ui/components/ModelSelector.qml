import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Dropdown model & profile selector used below the chat input. The popup
// opens UPWARD because the trigger sits near the window bottom edge.
Item {
    id: root
    enabled: !AgentVM.stateBusy
    opacity: enabled ? 1.0 : 0.6

    signal settingsRequested()

    readonly property var activeProfile: AgentVM.chatModelProfile
    readonly property bool hasProfile: activeProfile
        && activeProfile.provider !== undefined && activeProfile.provider !== ""
    // 自定义网关可以在填模型名之前就被选中，列表里的空串会让选择器出现一行空白项。
    readonly property var currentModels: root.hasProfile
        ? (root.activeProfile.models || []).filter(function (m) { return String(m) !== "" })
        : []

    // docs/15 §4.3.2: brief hint of the new context window after switching
    // models so the user perceives the budget change (optional enhancement).
    ToolTip {
        id: windowHint
        parent: selectorBox
        font.pixelSize: Theme.fontSizeMicro
        font.family: Theme.fontMono
    }
    function showWindowHint(apiProtocol, model, profileId) {
        // docs/54：带 profileId 查询——配置页设置的按模型覆盖（上下文/视觉/
        // 工具）即时反映在选择反馈里，与徽章同口径。
        const caps = Settings.modelCapabilities(apiProtocol, model, profileId || "")
        const cw = caps.contextWindow
        const parts = [cw > 0
            ? qsTr("上下文窗口：%1K").arg(Math.round(cw / 1000))
            : qsTr("上下文窗口：未知（使用全局默认）")]
        if (caps.vision) parts.push(qsTr("支持视觉输入"))
        if (!caps.tools) parts.push(qsTr("工具调用受限"))
        windowHint.show(parts.join(" · "), 2500)
    }

    // ── docs/54 能力徽章：胶囊芯片 ────────────────────────────
    // 原先是点号连接的一整串小字（"128K · 视觉 · 工具"），数字与中文挤在
    // 一起、能力差异只能逐字读。改成三枚独立胶囊：上下文走中性底 + mono
    // （数字列稳定），视觉/工具走品牌着色（扫列表时先看到能力差异，缺失
    // 直接不出现芯片）。样式配方对齐 SettingsField 的 statusBadge
    // （accent 底 0.12 / 边 0.35；中性底取 bgPrimary——原因见芯片注释）。
    component CapsChip: Rectangle {
        id: chip
        property string label: ""
        property bool accent: false
        property bool mono: false
        // muted：未知上下文的 "?"——同样是中性底，但文字再退一档。
        property bool muted: false
        implicitWidth: chipText.implicitWidth + Theme.spaceSm + 4
        implicitHeight: 18
        radius: Theme.radiusBadge
        // 中性底用 bgPrimary 而不是 surfaceHover：弹层底是 bgSecondary，
        // 行悬停底是 surfaceHover——芯片在两种状态下都要保持对比（悬停时
        // 用 surfaceHover 会糊进行底）。
        color: chip.accent
            ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                      Theme.brandPrimary.b, 0.12)
            : Theme.bgPrimary
        border.width: 1
        border.color: chip.accent
            ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                      Theme.brandPrimary.b, 0.35)
            : Theme.borderSubtle

        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            // 模型名/能力文案是固定枚举 + 能力表数字：仍显式 PlainText，
            // 与本文件其他 Text 的红线一致（W2 踩坑）。
            textFormat: Text.PlainText
            color: chip.accent ? Theme.brandPrimary
                 : (chip.muted ? Theme.textTertiary : Theme.textSecondary)
            font.pixelSize: Theme.fontSizeNano
            font.family: chip.mono ? Theme.fontMono : Theme.fontSans
        }
    }

    // Section 1（模型行）/ Section 2（profile 行）共用的徽章行：三字段
    // 一次取齐（覆盖值优先于表值与启发式），读屏语义由 summary 提供。
    component CapsBadgeRow: Row {
        id: badgeRow
        property string apiProtocol: ""
        property string model: ""
        property string profileId: ""
        // 能力查询（modelCapabilities）是 Q_INVOKABLE：绑定依赖只登记表达式
        // 里**读到的属性**，函数内部的 profile 访问不建立依赖，而下面三个入参
        // 在「只改 vision / tools / contextLength 覆盖」时都不变。所以不能写成
        // 纯绑定——必须由 profilesChanged 显式驱动重算。
        //
        // 为什么不用「先读一次 Settings.profiles 建立依赖」：qmlcachegen 会把
        // 求值后立即丢弃的无副作用表达式当死代码消除，AOT 产物里既没有该读
        // 取也不再重算（源码解释执行时却有效，正是"改完要重启"只在正式包
        // 复现的原因）。这里改为普通属性 + Connections 命令式重算，行为不依赖
        // 编译器是否保留死读语句。
        property var caps: ({})
        function refreshCaps() {
            badgeRow.caps = Settings.modelCapabilities(
                badgeRow.apiProtocol, badgeRow.model, badgeRow.profileId)
        }
        onApiProtocolChanged: refreshCaps()
        onModelChanged: refreshCaps()
        onProfileIdChanged: refreshCaps()
        Component.onCompleted: refreshCaps()

        Connections {
            target: Settings
            function onProfilesChanged() { badgeRow.refreshCaps() }
            // 能力表的视觉启发式按「协议 + 模型名」查，协议切换同样要重算；
            // profile 级 modelParams 走 profilesChanged，覆盖作用域之外。
            function onActiveProfileChanged() { badgeRow.refreshCaps() }
        }
        readonly property string contextLabel: caps.contextWindow > 0
            ? Math.round(caps.contextWindow / 1000) + "K" : "?"
        readonly property bool hasVision: caps.vision === true
        readonly property bool hasTools: caps.tools === true
        // 读屏名称：芯片是纯视觉容器，语义文本放在行容器的 Accessible.name。
        readonly property string summary: {
            const parts = [contextLabel]
            if (hasVision) parts.push(qsTr("视觉"))
            if (hasTools) parts.push(qsTr("工具"))
            return parts.join(" · ")
        }
        spacing: Theme.spaceXs

        CapsChip {
            label: badgeRow.contextLabel
            mono: true
            muted: badgeRow.caps.contextWindow <= 0
        }
        CapsChip {
            visible: badgeRow.hasVision
            label: qsTr("视觉")
            accent: true
        }
        CapsChip {
            visible: badgeRow.hasTools
            label: qsTr("工具")
            accent: true
        }
    }

    implicitWidth: selectorRow.implicitWidth + 20
    implicitHeight: 28

    // docs/25 D4: 触发器键盘可达——Tab 聚焦，回车/空格/下方向键开合菜单。
    activeFocusOnTab: true
    function toggleMenu() {
        menuPopup.opened ? menuPopup.close() : menuPopup.open()
    }
    Keys.onReturnPressed: root.toggleMenu()
    Keys.onSpacePressed: root.toggleMenu()
    Keys.onDownPressed: if (root.enabled) root.toggleMenu()

    // 回归用例用：弹层是否打开（Popup 不是 Item，测试取不到它）。
    readonly property bool opened: menuPopup.opened

    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("模型选择器")
    Accessible.description: hasProfile
        ? (activeProfile.displayName || activeProfile.provider) + " · " + (activeProfile.model || "")
        : qsTr("未配置模型")
    Accessible.onPressAction: root.toggleMenu()

    // Trigger pill button
    Rectangle {
        id: selectorBox
        objectName: "modelSelectorTrigger"
        anchors.fill: parent
        radius: 6
        color: selectorMA.containsMouse || menuPopup.opened || root.activeFocus
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: menuPopup.opened || selectorMA.containsMouse || root.activeFocus
            ? Theme.brandPrimary : "transparent"

        Behavior on color { ColorAnimation { duration: Theme.motionBase } }

        Row {
            id: selectorRow
            anchors.centerIn: parent
            spacing: 6

            // Provider indicator dot
            Rectangle {
                width: 6
                height: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                visible: root.hasProfile
                color: root.activeProfile.provider === "ollama"
                    ? Theme.statusSuccess : Theme.brandPrimary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (!root.hasProfile) return qsTr("未配置模型 · 点击设置")
                    const p = root.activeProfile
                    return (p.displayName ? p.displayName : p.provider)
                        + " · " + (p.model ? p.model : qsTr("未指定模型"))
                }
                color: selectorMA.containsMouse || menuPopup.opened
                    ? Theme.brandPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSizeMicro
                font.family: Theme.fontMono
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: menuPopup.opened ? "chevron-up" : "chevron-down"
                size: 11
                iconColor: selectorMA.containsMouse || menuPopup.opened
                    ? Theme.brandPrimary : Theme.textSecondary
            }
        }

        MouseArea {
            id: selectorMA
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
            onPressed: menuPopup.opened ? menuPopup.close() : menuPopup.open()
        }
    }

    // Dropdown popup (opens upward, scrolls when tall)
    Popup {
        id: menuPopup
        // docs/25 D4: 打开时接受键盘焦点，菜单项才能进入焦点链
        focus: true
        x: 0
        y: -height - 6          // opens above the trigger
        width: Math.max(280, root.width)
        height: Math.min(menuContent.implicitHeight + 12,
                         Math.max(200, root.mapToItem(null, 0, 0).y - 12))
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside

        background: DialogBackground {}

        contentItem: ScrollView {
            id: menuScroll
            clip: true

            ColumnLayout {
                id: menuContent
                width: menuScroll.width
                spacing: 2

                // ── Section 1: models of the active profile ──
                Text {
                    visible: root.hasProfile && root.currentModels.length > 0
                    text: root.hasProfile
                        ? qsTr("模型 · %1").arg(root.activeProfile.displayName
                                               || root.activeProfile.provider)
                        : ""
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                    font.family: Theme.fontMono
                    Layout.leftMargin: 8
                    Layout.topMargin: 2
                }

                Repeater {
                    model: root.currentModels

                    delegate: Rectangle {
                        id: modelItem
                        readonly property bool isSelected: modelData === root.activeProfile.model
                        Layout.fillWidth: true
                        Layout.leftMargin: 2
                        Layout.rightMargin: 2
                        Layout.preferredHeight: 28
                        radius: 5
                        color: modelMA.containsMouse || modelItem.activeFocus
                            ? Theme.surfaceHover : "transparent"

                        // docs/25 D4: 菜单项键盘激活 + 读屏单选语义
                        function activate() {
                            AgentVM.selectChatModel(
                                root.activeProfile.id, modelData)
                            menuPopup.close()
                            root.showWindowHint(root.activeProfile.apiProtocol,
                                                modelData, root.activeProfile.id)
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
                                visible: modelItem.isSelected
                            }

                            Item {
                                Layout.preferredWidth: 12
                                Layout.preferredHeight: 12
                                visible: !modelItem.isSelected
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: modelItem.isSelected
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                font.bold: modelItem.isSelected
                                elide: Text.ElideRight
                            }

                            // docs/54: 能力徽章（上下文窗口 / 视觉 / 工具）。
                            // 胶囊芯片行；能力表未收录的模型上下文显示 "？"。
                            // 带 profileId：配置页的按模型覆盖优先于表值与启发式。
                            CapsBadgeRow {
                                objectName: "modelCapsBadge"
                                apiProtocol: root.activeProfile.apiProtocol
                                model: modelData
                                profileId: root.activeProfile.id
                                Accessible.name: summary
                            }
                        }

                        MouseArea {
                            id: modelMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: modelItem.activate()
                        }
                        activeFocusOnTab: true
                        Keys.onReturnPressed: modelItem.activate()
                        Keys.onSpacePressed: modelItem.activate()
                        Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                        Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData
                        Accessible.checked: modelItem.isSelected
                        Accessible.onPressAction: modelItem.activate()
                    }
                }

                Text {
                    visible: root.hasProfile && root.currentModels.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    text: qsTr("这条配置还没有可选模型：先在「管理模型配置…」里填写模型名。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    Layout.preferredHeight: 1
                    color: Theme.borderSubtle
                }

                // ── Section 2: profile switcher ──
                Text {
                    text: qsTr("配置 · 共 %1 个").arg(Settings.profiles.length)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeMicro
                    font.bold: true
                    font.family: Theme.fontMono
                    Layout.leftMargin: 8
                }

                Repeater {
                    model: Settings.profiles

                    delegate: Rectangle {
                        id: profileItem
                        readonly property bool isCurrent: modelData.id === root.activeProfile.id
                        Layout.fillWidth: true
                        Layout.leftMargin: 2
                        Layout.rightMargin: 2
                        Layout.preferredHeight: 28
                        radius: 5
                        color: profileMA.containsMouse || profileItem.activeFocus
                            ? Theme.surfaceHover : "transparent"

                        // docs/25 D4: 菜单项键盘激活 + 读屏单选语义
                        function activate() {
                            AgentVM.selectChatModel(modelData.id)
                            menuPopup.close()
                            root.showWindowHint(modelData.apiProtocol,
                                                modelData.model, modelData.id)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 6

                            Rectangle {
                                Layout.preferredWidth: 6
                                Layout.preferredHeight: 6
                                radius: 3
                                color: modelData.provider === "ollama"
                                    ? Theme.statusSuccess : Theme.brandPrimary
                                visible: profileItem.isCurrent
                            }

                            Item {
                                Layout.preferredWidth: 6
                                Layout.preferredHeight: 6
                                visible: !profileItem.isCurrent
                            }

                            Text {
                                Layout.fillWidth: true
                                text: (modelData.displayName || modelData.provider)
                                    + " · " + (modelData.model
                                               ? modelData.model : qsTr("未指定模型"))
                                color: profileItem.isCurrent
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }

                            // docs/54: profile 级能力徽章（同 Section 1 口径，
                            // 带 profileId 读该配置的按模型覆盖）。
                            CapsBadgeRow {
                                objectName: "profileCapsBadge"
                                apiProtocol: String(modelData.apiProtocol || "")
                                model: String(modelData.model || "")
                                profileId: String(modelData.id || "")
                                Accessible.name: summary
                            }
                        }

                        MouseArea {
                            id: profileMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: profileItem.activate()
                        }
                        activeFocusOnTab: true
                        Keys.onReturnPressed: profileItem.activate()
                        Keys.onSpacePressed: profileItem.activate()
                        Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                        Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: (modelData.displayName || modelData.provider)
                            + " · " + (modelData.model ? modelData.model : qsTr("未指定模型"))
                        Accessible.checked: profileItem.isCurrent
                        Accessible.onPressAction: profileItem.activate()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    Layout.preferredHeight: 1
                    color: Theme.borderSubtle
                }

                // ── Section 3: open full settings ──
                Rectangle {
                    id: settingsItem
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    Layout.bottomMargin: 2
                    Layout.preferredHeight: 30
                    radius: 5
                    color: settingsMA.containsMouse || settingsItem.activeFocus
                        ? Theme.surfaceHover : "transparent"

                    // docs/25 D4: 入口菜单项键盘激活
                    function activate() {
                        menuPopup.close()
                        root.settingsRequested()
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
                            text: qsTr("管理模型配置…")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                        }
                    }

                    MouseArea {
                        id: settingsMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsItem.activate()
                    }
                    activeFocusOnTab: true
                    Keys.onReturnPressed: settingsItem.activate()
                    Keys.onSpacePressed: settingsItem.activate()
                    Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                    Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: qsTr("管理模型配置…")
                    Accessible.onPressAction: settingsItem.activate()
                }
            }
        }
    }
}
