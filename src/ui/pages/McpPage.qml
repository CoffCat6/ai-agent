import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// MCP server management (Batch 31b): list configured servers with live
// connection state, add/edit/remove/enable them; tools surface in the chat
// once a server connects. Add and edit share McpServerEditor — editing used
// to be unsupported (delete + re-add, which dropped every tool).
Item {
    id: page

    Accessible.name: qsTr("MCP 服务器管理页")

    // docs/31 N7：配置导入/导出的结果提示与导入预览。
    property string transferMessage: ""
    property bool transferIsError: false
    property string importPath: ""
    property string importPolicy: "skip"
    property var importPreview: ({ ok: false, servers: [], total: 0, importable: 0,
                                   conflictCount: 0, withSecrets: 0, error: "" })

    SettingsPage {
        anchors.fill: parent

        title: qsTr("MCP 服务器")
        description: qsTr("接入外部工具服务器；其工具经确认后由智能体调用。修改即时生效。")
        // 设置搜索用：目录条目 mcp/servers 锚定页头标题。
        headerObjectName: "mcp/servers"

        // ── 新增入口 ───────────────────────────────────
        // 表单收进弹窗：常驻表单只服务"添加"，编辑还得另开一套字段；
        // 一个按钮 + 共用弹窗，两种场景同一份校验。
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Text {
                visible: page.transferMessage !== ""
                Layout.fillWidth: true
                text: page.transferMessage
                color: page.transferIsError ? Theme.statusDanger : Theme.statusSuccess
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WrapAnywhere
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            AppButton {
                objectName: "mcpExport"
                text: qsTr("导出配置…")
                enabled: McpManager.servers.length > 0
                onClicked: exportDialog.open()
                Accessible.name: qsTr("导出 MCP 服务器配置")
            }

            AppButton {
                objectName: "mcpImport"
                text: qsTr("导入配置…")
                onClicked: importFileDialog.open()
                Accessible.name: qsTr("导入 MCP 服务器配置")
            }

            AppButton {
                objectName: "mcpAddServer"
                text: qsTr("添加服务器…")
                onClicked: serverEditor.openForAdd()
            }
        }

        // ── Server cards ───────────────────────────────
        Repeater {
            model: McpManager.servers

            delegate: Rectangle {
                Layout.fillWidth: true
                implicitHeight: serverCard.implicitHeight + Theme.spaceXl
                radius: Theme.radiusMd
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: serverCard
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceSm

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Rectangle {
                            // Connection state dot: green connected,
                            // brand connecting, red failed, gray off.
                            width: 8
                            height: 8
                            radius: width / 2
                            // Layout-managed item: anchors are undefined
                            // behavior inside a RowLayout (Qt warns), so
                            // center via the layout instead.
                            Layout.alignment: Qt.AlignVCenter
                            readonly property int state: modelData.state
                            color: !modelData.enabled ? Theme.textSecondary
                                 : state === 2 ? Theme.statusSuccess
                                 : state === 3 ? Theme.statusDanger
                                 : Theme.brandPrimary
                        }

                        Text {
                            text: modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBody
                            font.bold: true
                            font.family: Theme.fontSans
                        }

                        Rectangle {
                            width: transportBadge.width + 12
                            height: 18
                            radius: Theme.radiusBadge
                            color: Theme.surfaceHover
                            Text {
                                id: transportBadge
                                anchors.centerIn: parent
                                text: modelData.transport
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontMono
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: qsTr("%1 个工具").arg(modelData.toolCount)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        // docs/46: 该服务器声明的提示词模板数（0 时隐藏，
                        // 空服务器卡片不加噪音）。
                        Text {
                            visible: modelData.promptCount > 0
                            text: qsTr("%1 个提示词").arg(modelData.promptCount)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }
                        // docs/48: 服务器声明的资源数（同上，0 隐藏）。
                        Text {
                            visible: modelData.resourceCount > 0
                            text: qsTr("%1 个资源").arg(modelData.resourceCount)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                        }

                        Switch {
                            id: mcpEnableSwitch
                            // docs/25 D3: 读屏可区分是哪台服务器的开关
                            Accessible.name: qsTr("启用服务器 %1").arg(modelData.name)
                            checked: modelData.enabled
                            onToggled: McpManager.setEnabled(
                                modelData.name, checked)
                        }

                        IconButton {
                            // 配置改错了（命令拼错、端点失效）不必删了重建：
                            // 编辑会复用同一条配置并重建客户端。
                            icon: "edit"
                            iconSize: 16
                            tooltip: qsTr("编辑服务器")
                            restIconColor: Theme.textSecondary
                            hoverIconColor: Theme.brandPrimary
                            onClicked: serverEditor.openForEdit(modelData)
                        }

                        IconButton {
                            visible: modelData.enabled
                            icon: "refresh"
                            iconSize: 16
                            tooltip: qsTr("重新连接")
                            restIconColor: Theme.textSecondary
                            hoverIconColor: Theme.brandPrimary
                            onClicked: McpManager.reconnect(modelData.name)
                        }

                        IconButton {
                            icon: "trash"
                            iconSize: 16
                            // docs/25 D5: 无 tooltip 时 IconButton 读屏名回落为
                            // 图标名 "trash"，不可读——补 tooltip 即补 Accessible.name
                            tooltip: qsTr("删除服务器")
                            restIconColor: Theme.textSecondary
                            hoverIconColor: Theme.statusDanger
                            onClicked: {
                                removeConfirm.pendingName = modelData.name
                                removeConfirm.open()
                            }
                        }
                    }

                    Text {
                        id: endpointText
                        Layout.fillWidth: true
                        text: modelData.endpoint
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        elide: Text.ElideRight

                        // docs/25 C5: elide 截断时悬停可看完整端点
                        HoverHandler { id: endpointHover }
                        ToolTip.visible: endpointHover.hovered && endpointText.truncated
                        ToolTip.text: endpointText.text
                    }

                    // 已连接服务器的工具名预览（一行省略 + 悬停看全量）：
                    // 排查「这个工具到底来自哪台服务器」不必再切到工具页。
                    Text {
                        id: toolsText
                        readonly property var toolNames: modelData.tools || []
                        visible: toolNames.length > 0
                        Layout.fillWidth: true
                        text: toolNames.join(" · ")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontMono
                        elide: Text.ElideRight
                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("工具：%1").arg(text)

                        HoverHandler { id: toolsHover }
                        ToolTip.visible: toolsHover.hovered && toolsText.truncated
                        ToolTip.text: toolNames.join("\n")
                    }

                    Text {
                        // docs/25 C4: McpManager.lastError 是全局共享字段，
                        // 多服务器失败时逐条重复展示同一错误会误导归属——
                        // 仅当失败项唯一时展示具体错误；多个失败统一提示，
                        // 细节见日志。
                        visible: modelData.state === 3
                                 && McpManager.lastError !== ""
                        Layout.fillWidth: true
                        text: McpManager.servers.filter(
                                  function(s) { return s.state === 3 }).length === 1
                              ? McpManager.lastError
                              : qsTr("多个服务器连接失败，详见日志")
                        color: Theme.statusDanger
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // docs/31 N7：导出/导入 MCP 服务器配置。
    FileDialog {
        id: exportDialog
        title: qsTr("导出 MCP 服务器配置")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON 文件 (*.json)")]
        onAccepted: {
            const result = McpManager.exportServers(selectedFile.toString())
            page.transferIsError = !result.ok
            if (!result.ok) {
                page.transferMessage = result.error || qsTr("导出失败。")
                return
            }
            page.transferMessage = result.secretsOmitted > 0
                ? qsTr("已导出 %1 台服务器；其中 %2 台的环境变量/请求头未包含，请手动补全。")
                    .arg(result.count).arg(result.secretsOmitted)
                : qsTr("已导出 %1 台服务器；导入后默认禁用，需要手动启用。").arg(result.count)
        }
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("选择 MCP 服务器配置文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("JSON 文件 (*.json)")]
        onAccepted: {
            const path = selectedFile.toString()
            const preview = McpManager.previewServerImport(path)
            page.transferMessage = ""
            if (!preview.ok) {
                page.transferIsError = true
                page.transferMessage = preview.error || qsTr("导入失败。")
                return
            }
            page.importPath = path
            page.importPolicy = "skip"
            page.importPreview = preview
            importConfirm.open()
        }
    }

    Dialog {
        id: importConfirm
        objectName: "mcp/importDialog"
        modal: true
        anchors.centerIn: parent
        padding: 0
        width: 520
        height: 460
        background: DialogBackground {}

        // 每次打开都回到「跳过同名」，与 page.importPolicy 的初值一致。
        onOpened: importPolicyCombo.currentIndex = 0

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.topMargin: Theme.spaceLg
                Layout.fillWidth: true
                text: qsTr("导入 MCP 服务器配置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            Text {
                objectName: "mcp/importSummary"
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.fillWidth: true
                text: qsTr("共 %1 台：可导入 %2 台，同名冲突 %3 台，%4 台包含环境变量或请求头。导入的服务器默认禁用。")
                    .arg(page.importPreview.total)
                    .arg(page.importPreview.importable)
                    .arg(page.importPreview.conflictCount)
                    .arg(page.importPreview.withSecrets)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Text {
                    text: qsTr("同名服务器")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }

                ComboBox {
                    id: importPolicyCombo
                    objectName: "mcp/importPolicy"
                    Layout.preferredWidth: Theme.controlWidthMd
                    property var policies: [
                        { label: qsTr("跳过同名（推荐）"), value: "skip" },
                        { label: qsTr("覆盖同名"), value: "overwrite" },
                        { label: qsTr("重命名后添加"), value: "rename" }
                    ]
                    model: policies
                    textRole: "label"
                    currentIndex: 0
                    onActivated: page.importPolicy = policies[currentIndex].value
                    Accessible.name: qsTr("同名服务器处理方式")
                }

                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle
                radius: Theme.radiusSm
                clip: true

                ListView {
                    id: importList
                    objectName: "mcp/importList"
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXs
                    model: page.importPreview.servers || []
                    clip: true
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: ColumnLayout {
                        width: importList.width
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            Text {
                                text: modelData.name || qsTr("（未命名）")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                visible: modelData.conflict === true
                                text: qsTr("同名")
                                color: Theme.statusWarning
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: (modelData.error || "") !== ""
                            text: modelData.error || ""
                            color: Theme.statusDanger
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontSans
                            wrapMode: Text.WrapAnywhere
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: (modelData.endpoint || "") !== ""
                            text: modelData.endpoint || ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeMicro
                            font.family: Theme.fontMono
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.rightMargin: Theme.spaceLg
                Layout.bottomMargin: Theme.spaceLg
                spacing: Theme.spaceSm

                AppButton {
                    text: qsTr("取消")
                    onClicked: importConfirm.close()
                }
                AppButton {
                    objectName: "mcp/importConfirm"
                    text: qsTr("导入")
                    enabled: page.importPreview.importable > 0
                    onClicked: {
                        const result = McpManager.importServers(
                            page.importPath, page.importPolicy)
                        page.transferIsError = !result.ok
                        page.transferMessage = result.ok
                            ? qsTr("已导入：新增 %1 台，覆盖 %2 台，跳过 %3 台；导入的服务器默认禁用。")
                                .arg(result.added).arg(result.replaced).arg(result.skipped)
                            : (result.error || qsTr("导入失败。"))
                        importConfirm.close()
                    }
                }
            }
        }
    }

    // Add / edit dialog (shared, one instance for every card).
    McpServerEditor {
        id: serverEditor
    }

    // Removal confirmation (shared, one instance for every card).
    Dialog {
        id: removeConfirm
        property string pendingName: ""

        modal: true
        anchors.centerIn: parent
        padding: 0
        width: 320
        height: 130

        background: DialogBackground {}

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.topMargin: Theme.spaceLg
                Layout.fillWidth: true
                text: qsTr("移除服务器 %1？").arg(removeConfirm.pendingName)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.rightMargin: Theme.spaceLg
                Layout.bottomMargin: Theme.spaceLg
                spacing: Theme.spaceSm

                AppButton {
                    text: qsTr("取消")
                    onClicked: removeConfirm.close()
                }
                AppButton {
                    text: qsTr("移除")
                    onClicked: {
                        McpManager.removeServer(removeConfirm.pendingName)
                        removeConfirm.close()
                    }
                }
            }
        }
    }
}
