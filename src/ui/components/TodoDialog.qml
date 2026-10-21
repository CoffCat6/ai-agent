import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 「任务清单」对话框 —— todo_write 产出的任务列表的可按需展开视图。
//
// 为什么从右栏搬进对话框：任务清单原本钉在 220–640px 的右栏顶部，既挤掉
// Git / 知识库分区的垂直空间，又只能露出一小截滚动视口。对话框给列表一个
// 宽裕的视口，标题栏直接回答"做到哪儿了"，而右栏把位置让给真正需要纵深
// 的审阅树与知识库。
//
// 宿主契约（与 GitCommitDialog 一致）：
//   * 宿主在 main.qml 里声明实例，并设置 `parent: Overlay.overlay`
//     （本组件不自己设置 parent —— 对话框必须落在宿主的 overlay 上）；
//   * 数据由宿主注入：`todos: AgentVM.todoList`；
//   * 打开方式：`todoDialog.open()`，或 `todoDialog.openFor(list)`。
//
// 顶栏角标要的"还剩几个没做完"：本组件暴露 `unfinishedCount`，但顶栏
// （TopBar.qml）拿不到这里的实例 —— 角标应直接读 AgentVM.todoList 计算：
//
//   readonly property int todoBadgeCount: {
//       var list = AgentVM.todoList
//       if (!list || list.length === 0) return 0
//       var n = 0
//       for (var i = 0; i < list.length; ++i)
//           if (list[i] && list[i].status !== "completed") ++n
//       return n
//   }
Dialog {
    id: dialog

    // ── Data ───────────────────────────────────────────
    // 宿主注入的任务数组（main.qml 里绑定 `AgentVM.todoList`）。组件本身
    // 不碰 ViewModel：这样它既能进对话框，也能在测试里用同样的形状喂数据。
    property var todos: []

    // ── Derived state (mirrors the embedded panel) ─────
    // 全部取自内嵌的 TodoListPanel，保证标题、进度条与列表读的是同一份数据。
    readonly property int itemCount: todoPanel.itemCount
    readonly property int completedCount: todoPanel.completedCount
    readonly property int inProgressCount: todoPanel.inProgressCount
    readonly property int pendingCount: todoPanel.pendingCount
    // 「还有几个没做完」= 全部 − 已完成（含进行中的那一个）。顶栏角标要
    // 的就是这个数；注意它与 panel 的 `pendingCount`（只算"还没开始"）
    // 不是一回事。
    readonly property int unfinishedCount: itemCount - completedCount
    readonly property bool hasItems: todoPanel.hasItems
    readonly property bool allCompleted: todoPanel.allCompleted
    readonly property real progressRatio: todoPanel.progressRatio
    readonly property string stateSummary: todoPanel.stateSummary
    readonly property int renderedCount: todoPanel.renderedCount
    readonly property real listViewportHeight: todoPanel.listViewportHeight
    readonly property real listContentHeight: todoPanel.listContentHeight
    readonly property bool listScrollable: todoPanel.listScrollable
    readonly property bool moreBelowFold: todoPanel.moreBelowFold
    // 列表所在底面的颜色（对话框内的下沉式列表井）。列表底部的渐隐必须
    // 收在这个颜色上，否则会在最后几行上盖出一条色带。
    readonly property color surfaceColor: listWell.color

    // ── Geometry ───────────────────────────────────────
    // 内边距写死成常量，好让高度预算与真实布局一致：标题行 14+32+10，
    // 脚部 8+32+14，列表井上下各 12。
    readonly property int headerHeight: 56
    readonly property int footerHeight: 54
    readonly property int wellInset: 12
    // 对话框能占的最大高度（窗口不够高时自动收窄）。
    readonly property int maxDialogHeight: Math.min(540, parent ? parent.height - 80 : 540)
    // 列表视口的硬上限：再高就只剩一大片等间距的行。
    readonly property int maxListHeight: 420
    // 装下全部内容需要多高（视口按封顶算，超出的部分滚动）。
    // 名字带 Dialog：Popup 自己有个 contentHeight（FINAL），不能覆盖。
    readonly property int contentDialogHeight: headerHeight + footerHeight + 2 * wellInset
                                       + todoPanel.chromeHeight
                                       + Math.min(todoPanel.listContentHeight, maxListHeight)
    // 对话框按内容长高：任务少时是一张紧凑的卡片，任务多时到上限就滚动。
    // 高度只依赖内容的自然高度（行高、换行），不依赖视口封顶，所以不会
    // 和下面的 listHeightCap 形成绑定环。
    readonly property int targetHeight: dialog.hasItems
        ? Math.max(240, Math.min(maxDialogHeight, contentDialogHeight))
        : Math.min(maxDialogHeight, 260)
    readonly property int bodyHeight: Math.max(0, height - headerHeight - footerHeight)
    // 列表视口的封顶高度。
    //
    // 为什么必须是具体数字而不是 0（"填满可用空间"）：面板在对话框里是
    // 锚定的，不是布局项 —— 传 0 时它会按内容高度无限长高，直接顶出对话框。
    // 所以这里按对话框自己的高度倒推：正文高度 − 列表井内边距 − 面板固定
    // 头部（进度条 + 状态行）。窗口再高，列表也不会超过对话框；窗口
    // 再矮，封顶值跟着变小，列表自己滚动。
    readonly property int listHeightCap: Math.max(0, Math.min(maxListHeight,
        targetHeight - headerHeight - footerHeight - 2 * wellInset - todoPanel.chromeHeight))

    // Opens the dialog. `list` is optional: the usual host already binds
    // `todos` (e.g. `todos: AgentVM.todoList`) and only calls openFor();
    // a host that owns the data can pass the array in one call. Passing a
    // list replaces the binding on `todos` — rebind afterwards if the live
    // connection matters.
    function openFor(list) {
        if (list !== undefined) dialog.todos = list
        dialog.open()
    }

    // 渐隐实际收尾的颜色 —— 从渐变本身读回（不是从 surfaceColor 抄的），
    // 用来证明渐隐真的融进了列表井的颜色。
    function fadeEndColor() { return todoPanel.fadeTerminalColor() }

    // ── Chrome ─────────────────────────────────────────
    anchors.centerIn: parent
    width: Math.min(640, parent ? parent.width - 48 : 640)
    height: dialog.targetHeight
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 0

        Accessible.name: qsTr("任务清单对话框")
        Accessible.description: qsTr("查看当前会话的任务清单与完成进度")

        // ── Header ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 14
            Layout.bottomMargin: 10
            Layout.preferredHeight: 32
            spacing: 8

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                icon: "check"
                size: 14
                iconColor: Theme.brandPrimary
            }

            // 标题即进度：不用逐行扫读就知道"做到哪儿了"。
            Text {
                id: titleText
                objectName: "todoDialogTitle"
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: dialog.hasItems
                    ? qsTr("任务清单 · %1/%2 已完成").arg(dialog.completedCount)
                                              .arg(dialog.itemCount)
                    : qsTr("任务清单")
                color: Theme.textPrimary
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                elide: Text.ElideRight
                Accessible.name: text
            }

            IconButton {
                Layout.alignment: Qt.AlignVCenter
                icon: "close"
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭任务清单")
            }
        }

        // ── Body ──────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 空态：没有任务时说清楚"这里本来会有什么"，而不是给用户一
            // 个空对话框。
            Item {
                id: emptyState
                objectName: "todoDialogEmptyState"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !dialog.hasItems

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(380, parent.width - 64)
                    spacing: 10

                    AppIcon {
                        Layout.alignment: Qt.AlignHCenter
                        icon: "check"
                        size: 22
                        iconColor: Theme.textTertiary
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("当前会话还没有任务清单。")
                        color: Theme.textSecondary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeBody
                        wrapMode: Text.Wrap
                        Accessible.name: text
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("代理调用 todo_write 记录任务后，任务会出现在这里。")
                        color: Theme.textTertiary
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.Wrap
                        Accessible.name: text
                    }
                }
            }

            // 列表井：比对话框底色更深一层的下沉面。行画在井里，底部渐隐
            // 收在井色上；面板通过 surfaceColor 拿到这个颜色。
            Rectangle {
                id: listWell
                objectName: "todoDialogWell"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: dialog.hasItems
                color: Theme.bgPrimary
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.borderSubtle
                clip: true

                TodoListPanel {
                    id: todoPanel
                    objectName: "todoDialogPanel"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: dialog.wellInset
                    height: implicitHeight
                    todos: dialog.todos
                    // 对话框自带标题栏，面板的头部行会重复一遍同样的
                    // 「任务清单 · 3/7」，而且它的折叠箭头在对话框里没有
                    // 意义 —— 关掉，只留进度条与状态行。
                    showHeader: false
                    // 渐隐要收在列表井的颜色上，不能写死侧栏的底色。
                    surfaceColor: listWell.color
                    // 具体封顶值，绝不传 0（否则列表会顶出对话框）。
                    listMaxHeight: dialog.listHeightCap
                }
            }
        }

        // ── Footer ─────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 14
            Layout.preferredHeight: 32

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("关闭")
                onClicked: dialog.close()
                Accessible.name: qsTr("关闭任务清单")
            }
        }
    }
}
