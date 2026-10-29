import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/17 A6: 独立"安全"分类。M1 直连（即时保存）。
// 内容：读取沙箱（读策略 + 用户黑名单）与隐私门（仅本地模式、内网访问）。
// 项目目录的授权入口在侧栏"导入项目"，不在设置页。
SettingsPage {
    id: page

    Accessible.name: qsTr("安全设置页")

    title: qsTr("安全")
    description: qsTr("本页修改实时保存。判定顺序：系统目录封锁 → 黑名单 → 允许层（用户目录 ∪ 当前项目）。")

    // ── 文件读取沙箱 ──────────────────────────────
    SettingsSection {
        title: qsTr("文件读取沙箱")

        SettingRow {
            objectName: "security/allowFullDiskRead"
            label: qsTr("允许读取整个磁盘")
            description: qsTr("关闭时读取仅限文档/下载/桌面/应用目录与当前项目；开启后除系统目录与黑名单外全部可读，模型读到的内容可能发送给云端模型。")

            CheckBox {
                checked: Settings.allowFullDiskRead
                onToggled: Settings.allowFullDiskRead = checked
                Accessible.name: qsTr("允许读取整个磁盘")
            }
        }

        // docs/17 P1-4: 项目目录写入放行（默认只读）。
        SettingRow {
            objectName: "security/allowProjectWrite"
            label: qsTr("允许写入当前项目目录")
            description: qsTr("默认项目目录只读，写入仅限用户目录；开启后模型可以修改当前项目中的文件（写入类工具仍受会话权限模式约束，黑名单始终优先）。")

            CheckBox {
                checked: Settings.allowProjectWrite
                onToggled: Settings.allowProjectWrite = checked
                Accessible.name: qsTr("允许写入当前项目目录")
            }
        }

        // ── 用户黑名单编辑器 ──
        Text {
            objectName: "security/readDenyList"
            text: qsTr("黑名单（始终拒绝，优先于一切允许规则）")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeMicro
            font.family: Theme.fontMono
            font.letterSpacing: 0.4
            Layout.topMargin: Theme.spaceXs
        }
        Text {
            text: qsTr("内置默认拦截：~/.ssh、~/.gnupg、~/.aws 及 *.pem / *.key / *.ppk / *.kdbx。以下为追加项，支持目录（或目录名，命中任意层级）与通配模式如 *.env。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.Wrap
            font.family: Theme.fontSans
            Layout.fillWidth: true
        }

        ListView {
            id: denyListView
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 160)
            clip: true
            spacing: 2
            model: Settings.readDenyList

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: denyListView.width
                height: 30
                radius: Theme.radiusField
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceSm
                    anchors.rightMargin: Theme.spaceXs
                    spacing: Theme.spaceSm

                    AppIcon {
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        icon: "shield"
                        size: 12
                        iconColor: Theme.statusDanger
                    }
                    Text {
                        id: denyPathText
                        Layout.fillWidth: true
                        text: modelData
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        elide: Text.ElideRight

                        // docs/25 C5: 路径被 elide 截断时悬停查看完整值
                        HoverHandler { id: denyPathHover }
                        ToolTip.visible: denyPathHover.hovered && denyPathText.truncated
                        ToolTip.text: denyPathText.text
                    }
                    Rectangle {
                        id: denyDelBtn
                        width: 20; height: 20; radius: Theme.radiusSm
                        color: denyDelMA.containsMouse ? Theme.surfaceHover : "transparent"
                        border.width: denyDelBtn.activeFocus ? 2 : 0
                        border.color: Theme.statusDanger
                        AppIcon {
                            anchors.centerIn: parent
                            icon: "close"
                            size: 11
                            iconColor: denyDelMA.containsMouse
                                ? Theme.statusDanger : Theme.textSecondary
                        }
                        // docs/25 D3: 黑名单条目删除按钮补键盘与读屏可达
                        function removeAt() {
                            const list = Settings.readDenyList.slice()
                            list.splice(index, 1)
                            Settings.readDenyList = list
                        }
                        MouseArea {
                            id: denyDelMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: denyDelBtn.removeAt()
                        }
                        activeFocusOnTab: true
                        Keys.onReturnPressed: denyDelBtn.removeAt()
                        Keys.onSpacePressed: denyDelBtn.removeAt()
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("移除黑名单条目 %1").arg(denyPathText.text)
                        Accessible.onPressAction: denyDelBtn.removeAt()
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: denyListView.count === 0
                text: qsTr("暂无追加项")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            TextField {
                id: denyInput
                Layout.fillWidth: true
                placeholderText: qsTr("目录路径、目录名或通配模式（如 D:\\secrets、.git、*.env）")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontMono
                background: Rectangle {
                    radius: Theme.radiusField
                    color: Theme.surfaceHover
                    border.width: denyInput.activeFocus ? 2 : 1
                    border.color: denyInput.activeFocus
                        ? Theme.brandPrimary : Theme.borderSubtle
                }
                Accessible.name: qsTr("新增黑名单条目")
                onAccepted: denyAddButton.addEntry()
            }

            AppButton {
                id: denyAddButton
                text: qsTr("添加")
                enabled: denyInput.text.trim().length > 0
                onClicked: addEntry()

                function addEntry() {
                    const entry = denyInput.text.trim()
                    if (entry.length === 0) return
                    const list = Settings.readDenyList.slice()
                    // 去重（大小写不敏感，Windows 语义）。
                    for (let i = 0; i < list.length; ++i) {
                        if (String(list[i]).toLowerCase() === entry.toLowerCase()) {
                            denyInput.text = ""
                            return
                        }
                    }
                    list.push(entry)
                    Settings.readDenyList = list
                    denyInput.text = ""
                }
            }
        }
    }

    // ── 隐私门 ────────────────────────────────────
    SettingsSection {
        Layout.bottomMargin: Theme.spaceXs
        title: qsTr("隐私门")

        SettingRow {
            objectName: "security/localOnlyMode"
            label: qsTr("仅本地模式")
            description: qsTr("使用云端模型时拦截知识库搜索、联网搜索与全部 MCP 工具（工具页会显示拦截徽标）；本地模型不受影响。")

            CheckBox {
                checked: Settings.localOnlyMode
                onToggled: Settings.localOnlyMode = checked
                Accessible.name: qsTr("仅本地模式")
            }
        }

        SettingRow {
            objectName: "security/allowLocalNetworkFetch"
            label: qsTr("允许 http_fetch 访问本机与内网地址")
            description: qsTr("关闭时 http_fetch 拒绝本机/内网目标（防 SSRF）；仅在确有访问内网服务的需求时开启。")

            CheckBox {
                checked: Settings.allowLocalNetworkFetch
                onToggled: Settings.allowLocalNetworkFetch = checked
                Accessible.name: qsTr("允许 http_fetch 访问本机与内网地址")
            }
        }
    }
}
