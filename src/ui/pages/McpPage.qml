import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// MCP server management (Batch 31b): list configured servers with live
// connection state, add/remove/enable them; tools surface in the chat once
// a server connects.
ScrollView {
    id: page
    clip: true

    Accessible.name: qsTr("MCP 服务器管理页")

    ColumnLayout {
        width: page.availableWidth
        spacing: 12

        Item { Layout.preferredHeight: 4 }

        Text {
            objectName: "mcp/servers"
            text: qsTr("MCP 服务器")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
            Layout.leftMargin: 24
        }

        Text {
            text: qsTr("接入外部工具服务器；其工具经确认后由智能体调用。修改即时生效。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
        }

        // ── Server cards ───────────────────────────────
        Repeater {
            model: McpManager.servers

            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                height: serverCard.implicitHeight + 20
                radius: 8
                color: Theme.bgSecondary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: serverCard
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            // Connection state dot: green connected,
                            // brand connecting, red failed, gray off.
                            width: 8
                            height: 8
                            radius: 4
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
                            radius: 9
                            color: Theme.surfaceHover
                            Text {
                                id: transportBadge
                                anchors.centerIn: parent
                                text: modelData.transport
                                color: Theme.textSecondary
                                font.pixelSize: 10
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

                        Switch {
                            id: mcpEnableSwitch
                            // docs/25 D3: 读屏可区分是哪台服务器的开关
                            Accessible.name: qsTr("启用服务器 %1").arg(modelData.name)
                            checked: modelData.enabled
                            onToggled: McpManager.setEnabled(
                                modelData.name, checked)
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

        // ── Add server form ────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            height: addForm.implicitHeight + 20
            radius: 8
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: addForm
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Text {
                    text: qsTr("添加服务器")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.bold: true
                    font.family: Theme.fontSans
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        spacing: 4
                        Text { text: qsTr("名称"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                        TextField {
                            id: nameField
                            Accessible.name: qsTr("名称")
                            Layout.preferredWidth: 160
                            placeholderText: qsTr("my-server")
                            placeholderTextColor: Theme.textSecondary
                            color: Theme.textPrimary
                            selectByMouse: true
                            font.pixelSize: Theme.fontSizeBody
                            background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: nameField.activeFocus ? 2 : 1; border.color: nameField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                        }
                    }

                    ColumnLayout {
                        spacing: 4
                        Text { text: qsTr("类型"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                        ComboBox {
                            id: transportBox
                            Accessible.name: qsTr("类型")
                            Layout.preferredWidth: 120
                            model: ["stdio", "http"]
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: transportBox.currentText === "stdio"
                        Text { text: qsTr("命令与参数（空格分隔）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                        TextField {
                            id: commandField
                            Accessible.name: qsTr("命令与参数（空格分隔）")
                            Layout.fillWidth: true
                            placeholderText: qsTr("npx -y @modelcontextprotocol/server-filesystem D:/docs")
                            placeholderTextColor: Theme.textSecondary
                            color: Theme.textPrimary
                            selectByMouse: true
                            font.pixelSize: Theme.fontSizeBody
                            background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: commandField.activeFocus ? 2 : 1; border.color: commandField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: transportBox.currentText === "http"
                        Text { text: qsTr("服务端点 URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontMono }
                        TextField {
                            id: urlField
                            Accessible.name: qsTr("服务端点 URL")
                            Layout.fillWidth: true
                            placeholderText: qsTr("https://mcp.example.com/mcp")
                            placeholderTextColor: Theme.textSecondary
                            color: Theme.textPrimary
                            selectByMouse: true
                            font.pixelSize: Theme.fontSizeBody
                            background: Rectangle { radius: 6; color: Theme.bgPrimary; border.width: urlField.activeFocus ? 2 : 1; border.color: urlField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    AppButton {
                        text: qsTr("添加并连接")
                        onClicked: {
                            const transport = transportBox.currentText
                            let server = {
                                name: nameField.text.trim(),
                                transport: transport,
                                enabled: true
                            }
                            if (server.name === "") return
                            if (transport === "stdio") {
                                const parts = commandField.text.trim().split(/\s+/)
                                if (parts.length === 0 || parts[0] === "") return
                                server.command = parts[0]
                                server.args = parts.slice(1)
                            } else {
                                server.url = urlField.text.trim()
                                if (server.url === "") return
                            }
                            McpManager.addServer(server)
                            nameField.text = ""
                            commandField.text = ""
                            urlField.text = ""
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 8 }
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
            spacing: 12

            Text {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 16
                Layout.fillWidth: true
                text: qsTr("移除服务器 %1？").arg(removeConfirm.pendingName)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                spacing: 8

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
