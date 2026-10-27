import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 会话模板菜单（可复用的独立组件）。
//
// 此前这段逻辑内联在 Sidebar 的「新对话」窄条里，直接绑死了 tmplMA 的锚点，
// 只能在侧栏可见时使用；且用的是未主题化的裸 Menu（main.cpp 强制 Basic
// 风格，渲染成 Qt 默认浅灰矩形）。抽出来后三个宿主共用：
//   1. 侧栏「新对话」右缘窄条（原位置）；
//   2. 会话标签栏的「+」（右键/长按）；
//   3. 空会话时聊天区的空态入口。
//
// 宿主只需：
//   SessionTemplateMenu { id: m; onManageRequested: ... }
//   m.openBelow(triggerItem)   // 贴在 trigger 下方
//
// 应用动作**一律走 AgentVM.startFromSessionTemplate**——那段逻辑顺序敏感
// （profileId 必须早于建标签），不允许在 QML 侧再写第二份。
Item {
    id: root

    // 「管理模板…」通往设置中心 prompts 分类；本组件不持有对话框实例。
    signal manageRequested()

    // 菜单纵向展开位置由调用方选择：贴在 trigger 下方（侧栏/空态）或
    // 上方（贴近窗口底部时）。两者都按 trigger 的几何自动夹进窗口。
    function openBelow(trigger) { openAt(trigger, true) }
    function openAbove(trigger) { openAt(trigger, false) }

    // 调试用：记录最近一次 openAt 的入参与计算结果。
    property string lastOpenDebug: ""

    // Popup 没有 Menu 的 popup(item, x, y) 方法，也没有 mapToItem：
    // 这里手工算场景坐标再写 x/y 并 open()。
    function openAt(trigger, below) {
        if (!trigger) { lastOpenDebug = "no-trigger"; return }
        const pt = trigger.mapToItem(null, 0, 0)
        const winW = templatePopup.Window.width
        const winH = templatePopup.Window.height
        // x：让菜单右缘对齐 trigger 右缘，并夹进窗口（窄条在侧栏最右侧时
        // 否则会伸到窗口外）。
        const x = Math.max(8, Math.min(pt.x + trigger.width - templatePopup.width,
                                       Math.max(8, winW - templatePopup.width - 8)))
        // y：优先贴 trigger 下方；下方放不下就翻到上方；都放不下则夹进窗口。
        const belowY = pt.y + trigger.height + 4
        const aboveY = pt.y - templatePopup.height - 4
        let y = below ? belowY : aboveY
        if (y + templatePopup.height > winH - 4 && aboveY >= 4)
            y = aboveY
        y = Math.max(4, Math.min(y, Math.max(4, winH - templatePopup.height - 4)))
        lastOpenDebug = "pt=" + pt.x + "," + pt.y + " x=" + x + " y=" + y
        templatePopup.x = x
        templatePopup.y = y
        templatePopup.open()
    }

    // 分类页里那套「配置不变 / 只读计划」文案的同一口径，避免两处措辞
    // 漂移（设置页仍保留自己的副本，因为它是列表而非菜单）。
    readonly property var permissionLabels: [
        { mode: -1, label: qsTr("权限不变") },
        { mode: AgentVM.permissionModePlanOnly, label: qsTr("只读计划") },
        { mode: AgentVM.permissionModeConfirmChanges, label: qsTr("变更确认") },
        { mode: AgentVM.permissionModeAutoEdit, label: qsTr("自动编辑") },
        { mode: AgentVM.permissionModeFullAccess, label: qsTr("完全访问") }
    ]

    function permissionText(mode) {
        for (let i = 0; i < permissionLabels.length; ++i)
            if (permissionLabels[i].mode === mode) return permissionLabels[i].label
        return qsTr("权限不变")
    }

    function profileText(profileId) {
        if (!profileId) return qsTr("配置不变")
        const list = Settings.profiles
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === profileId)
                return list[i].displayName || list[i].provider || qsTr("配置不变")
        // 模板引用的配置已被删除：如实说明，别假装"配置不变"。
        return qsTr("配置已删除")
    }

    // 摘要行：模型配置 · 权限（首条消息在菜单里不展开，避免行高失控）。
    function templateSubtitle(tmpl) {
        return profileText(tmpl.profileId) + " · " + permissionText(tmpl.permissionMode)
    }

    function applyTemplate(tmpl) {
        templatePopup.close()
        AgentVM.startFromSessionTemplate(tmpl)
    }

    readonly property int templateCount: Settings.sessionTemplates.length
    // 便于宿主做触发器的"展开中"高亮。
    readonly property bool popped: templatePopup.opened
    // 渲染回归用：弹出层在**场景**坐标里的上下沿，以及最矮一行的行高。
    // Popup 不是 Item（没有 mapToItem），借它的 parent 映射。
    readonly property Item popupSceneParent: templatePopup.parent
    readonly property point popupScenePos: popupSceneParent
        ? popupSceneParent.mapToItem(null, templatePopup.x, templatePopup.y)
        : Qt.point(0, 0)
    readonly property real popupTop: popupScenePos.y
    readonly property real popupBottom: popupScenePos.y + templatePopup.height
    function minRowHeight() {
        var min = Number.MAX_VALUE
        for (var i = 0; i < templateRepeater.count; ++i) {
            const item = templateRepeater.itemAt(i)
            if (item && item.height < min) min = item.height
        }
        return min === Number.MAX_VALUE ? -1 : min
    }
    function close() { templatePopup.close() }

    Popup {
        id: templatePopup
        // 打开时接受键盘焦点，菜单项才能进入焦点链（与另几个选择器一致）。
        focus: true
        width: 280
        height: content.implicitHeight + 12
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: content
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 4
                Layout.topMargin: 2
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: qsTr("会话模板")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                    font.family: Theme.fontMono
                }
                Text {
                    text: qsTr("共 %1 个").arg(root.templateCount)
                    color: Theme.textTertiary
                    font.pixelSize: 10
                    font.family: Theme.fontMono
                }
            }

            // ── 空白新对话 ─────────────────────────────
            Rectangle {
                id: blankItem
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.preferredHeight: 34
                radius: 5
                color: blankMA.containsMouse || blankItem.activeFocus
                    ? Theme.surfaceHover : "transparent"

                function activate() {
                    templatePopup.close()
                    AgentVM.newLocalChat()
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    AppIcon {
                        Layout.preferredWidth: 13
                        Layout.preferredHeight: 13
                        icon: "add"
                        size: 13
                        iconColor: Theme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("空白新对话")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                }

                MouseArea {
                    id: blankMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: blankItem.activate()
                }
                activeFocusOnTab: true
                Keys.onReturnPressed: blankItem.activate()
                Keys.onSpacePressed: blankItem.activate()
                Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                Accessible.role: Accessible.MenuItem
                Accessible.name: qsTr("空白新对话")
                Accessible.onPressAction: blankItem.activate()
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

            // ── 模板列表 ───────────────────────────────
            Repeater {
                id: templateRepeater
                model: Settings.sessionTemplates

                delegate: Rectangle {
                    id: templateItem
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    // 两行（名称 + 配置/权限摘要）；首条消息故意不显示，
                    // 它是动作的结果、不是选模板时要比较的维度。
                    Layout.preferredHeight: 48
                    radius: 5
                    color: templateMA.containsMouse || templateItem.activeFocus
                        ? Theme.surfaceHover : "transparent"

                    function activate() {
                        root.applyTemplate(modelData)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        AppIcon {
                            Layout.preferredWidth: 13
                            Layout.preferredHeight: 13
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 5
                            icon: "preset"
                            size: 13
                            iconColor: Theme.textSecondary
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.templateSubtitle(modelData)
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: templateMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: templateItem.activate()
                    }
                    activeFocusOnTab: true
                    Keys.onReturnPressed: templateItem.activate()
                    Keys.onSpacePressed: templateItem.activate()
                    Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                    Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: modelData.name
                    Accessible.description: root.templateSubtitle(modelData)
                    Accessible.onPressAction: templateItem.activate()
                }
            }

            // ── 空态 ───────────────────────────────────
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                visible: root.templateCount === 0
                text: qsTr("还没有模板。在「设置 → 提示词与指令」里新建，例如「代码审查」＝只读计划 + 首条消息。")
                color: Theme.textSecondary
                font.pixelSize: 10
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

            // ── 管理入口 ───────────────────────────────
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
                    templatePopup.close()
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
                        text: qsTr("管理模板…")
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
                Accessible.name: qsTr("管理模板…")
                Accessible.onPressAction: manageItem.activate()
            }
        }
    }
}
