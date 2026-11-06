pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 拉取模型后的「选择要添加的模型」对话框。
//
// 为什么不让拉取结果直接落库：一个端点往往一次列出几十个模型，全量灌进
// 聊天选择器会把用户真正在用的那几个淹没。这里只展示候选，勾选确认后才
// 逐个调用 Settings.addModelToProfile；已存在的模型置灰并标「已添加」，
// 既避免重复写入，也让用户一眼看清这次真正新增了什么。
Dialog {
    id: root

    objectName: "fetchedModels.dialog"

    // 远端返回的模型名（字符串列表）。模型名是用户不可控的远端数据，
    // 展示处一律走 Text.PlainText。
    property var fetchedModels: []
    // 打开时该配置已有的模型，用于置灰与去重。
    property var existingModels: []
    // 打开瞬间的 profile 快照：对话框期间用户切了档案也不会写错配置。
    property string profileId: ""

    // 勾选状态用名字列表维护：与 delegate 的 checked 解耦，全选/全不选
    // 只改这份数据，再由 selectionRevision 触发各行的勾选框同步。
    property var selectedNames: []
    property int selectionRevision: 0

    readonly property var entries: {
        const out = []
        const fetched = root.fetchedModels || []
        const existing = root.existingModels || []
        for (let i = 0; i < fetched.length; ++i) {
            const name = String(fetched[i])
            out.push({ name: name, existing: existing.indexOf(name) >= 0 })
        }
        return out
    }

    readonly property int totalCount: entries.length
    readonly property int existingCount: {
        let count = 0
        for (let i = 0; i < entries.length; ++i)
            if (entries[i].existing) count += 1
        return count
    }
    readonly property int selectedCount: selectedNames.length


    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(460, parent ? parent.width - 48 : 460)
    height: Math.min(parent ? parent.height - 80 : 520,
                     dialogContent.implicitHeight + Theme.spaceXl * 2)
    padding: Theme.spaceXl
    // Esc 关闭；不响应点击外部，避免误触把已经勾好的选择丢掉。
    closePolicy: Popup.CloseOnEscape
    background: DialogBackground {}

    contentItem: ColumnLayout {
        id: dialogContent
        spacing: Theme.spaceSm

        Accessible.name: qsTr("选择要添加的模型")

        Text {
            Layout.fillWidth: true
            text: qsTr("选择要添加的模型")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.bold: true
            font.family: Theme.fontSans
        }

        Text {
            Layout.fillWidth: true
            // 摘要行同时说明总量与「已在列表中」的数量，让用户知道这次
            // 拉取有多少是真正的新候选。
            text: qsTr("共 %1 个模型，其中 %2 个已在列表中")
                .arg(root.totalCount).arg(root.existingCount)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Item {
                id: selectAllButton
                objectName: "fetchedModels.selectAll"
                implicitWidth: selectAllLabel.implicitWidth
                implicitHeight: selectAllLabel.implicitHeight
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("全选")

                signal clicked()

                Keys.onReturnPressed: selectAllButton.clicked()
                Keys.onSpacePressed: selectAllButton.clicked()
                onClicked: root.selectAll()

                Text {
                    id: selectAllLabel
                    text: qsTr("全选")
                    color: Theme.brandPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    anchors.centerIn: parent
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: selectAllButton.clicked()
                }
            }

            Text {
                text: "·"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }

            Item {
                id: clearAllButton
                objectName: "fetchedModels.clearAll"
                implicitWidth: clearAllLabel.implicitWidth
                implicitHeight: clearAllLabel.implicitHeight
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("全不选")

                signal clicked()

                Keys.onReturnPressed: clearAllButton.clicked()
                Keys.onSpacePressed: clearAllButton.clicked()
                onClicked: root.clearAll()

                Text {
                    id: clearAllLabel
                    text: qsTr("全不选")
                    color: Theme.brandPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    anchors.centerIn: parent
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: clearAllButton.clicked()
                }
            }

            Item { Layout.fillWidth: true }
        }

        ListView {
            id: modelList
            objectName: "fetchedModels.list"
            Layout.fillWidth: true
            // 上限 320px：模型很多时列表内部滚动，不让对话框顶到屏幕外。
            Layout.preferredHeight: Math.min(320, contentHeight)
            visible: root.totalCount > 0
            clip: true
            spacing: Theme.spaceXs
            model: root.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("可添加的模型列表")

            delegate: RowLayout {
                id: modelRow
                required property var modelData

                width: ListView.view ? ListView.view.width : implicitWidth
                spacing: Theme.spaceSm

                CheckBox {
                    id: modelCheck
                    // 已存在的项不可再选：即使被程序改动也不允许进入提交集合。
                    checked: !modelRow.modelData.existing
                             && root.isSelected(modelRow.modelData.name)
                    enabled: !modelRow.modelData.existing
                    Accessible.name: String(modelRow.modelData.name)
                    onClicked: root.setSelected(modelRow.modelData.name, checked)

                    // 用户点击会截断 checked 绑定；全选/全不选后由 revision
                    // 显式回填每一行，保证视觉与 selectedNames 一致。
                    Connections {
                        target: root
                        function onSelectionRevisionChanged() {
                            modelCheck.checked = !modelRow.modelData.existing
                                && root.isSelected(modelRow.modelData.name)
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    // 模型名来自远端，必须纯文本渲染，防止把 <b> 之类当富文本。
                    text: String(modelRow.modelData.name)
                    textFormat: Text.PlainText
                    color: modelRow.modelData.existing
                        ? Theme.textTertiary : Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontMono
                    elide: Text.ElideMiddle
                }

                Text {
                    visible: modelRow.modelData.existing
                    text: qsTr("已添加")
                    textFormat: Text.PlainText
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Item { Layout.fillWidth: true }

            AppButton {
                objectName: "fetchedModels.cancel"
                text: qsTr("取消")
                onClicked: root.close()
            }

            AppButton {
                objectName: "fetchedModels.accept"
                text: qsTr("添加所选 (%1)").arg(root.selectedCount)
                highlighted: root.selectedCount > 0
                enabled: root.selectedCount > 0
                onClicked: root.commit()
            }
        }
    }

    onOpened: {
        root.resetSelection()
        root.selectionRevision += 1
    }

    // 页面只传快照，由对话框自己算默认勾选：新模型全选、已存在的全不选。
    function openForProfile(profile, fetched, existing) {
        root.profileId = String(profile || "")
        root.fetchedModels = fetched || []
        root.existingModels = existing || []
        root.resetSelection()
        root.open()
    }

    function isSelected(name) {
        return root.selectedNames.indexOf(String(name)) >= 0
    }

    function resetSelection() {
        const picked = []
        for (let i = 0; i < root.entries.length; ++i) {
            const entry = root.entries[i]
            if (!entry.existing && picked.indexOf(entry.name) < 0)
                picked.push(entry.name)
        }
        root.selectedNames = picked
    }

    function setSelected(name, selected) {
        const key = String(name)
        const next = root.selectedNames.slice()
        const at = next.indexOf(key)
        if (selected && at < 0)
            next.push(key)
        else if (!selected && at >= 0)
            next.splice(at, 1)
        root.selectedNames = next
        root.selectionRevision += 1
    }

    function selectAll() {
        const next = []
        for (let i = 0; i < root.entries.length; ++i) {
            const entry = root.entries[i]
            if (!entry.existing && next.indexOf(entry.name) < 0)
                next.push(entry.name)
        }
        root.selectedNames = next
        root.selectionRevision += 1
    }

    function clearAll() {
        root.selectedNames = []
        root.selectionRevision += 1
    }

    function commit() {
        if (root.profileId.length === 0 || root.selectedCount === 0)
            return
        const picked = root.selectedNames.slice()
        for (let i = 0; i < picked.length; ++i)
            Settings.addModelToProfile(root.profileId, picked[i])
        root.selectedNames = []
        // Dialog.accept() 会发出 accepted 信号并关闭。
        root.accept()
    }
}