import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// MCP 服务器的新增 / 编辑弹窗。
//
// 为什么不是页面底部的常驻表单：原表单只支持"添加"，改地址或补一个
// Authorization 头只能删掉服务器重建（工具会全部掉线、任务可能中途
// 失效）。编辑与新增共用同一套字段，env / headers 以「每行一条」的
// 文本编辑，避免动态键值行在 QML 里同步 TextField 状态的麻烦。
Dialog {
    id: root

    objectName: "mcpServerEditor"

    // 非空表示编辑现有条目；保存时以它定位原配置（允许改名）。
    property string originalName: ""
    property string errorText: ""

    readonly property bool editing: originalName.length > 0
    readonly property bool stdio: transportBox.currentText === "stdio"

    signal saved()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 48 : 560)
    height: Math.min(560, parent ? parent.height - 80 : 560)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogBackground {}

    function openForAdd() {
        originalName = ""
        errorText = ""
        nameField.text = ""
        transportBox.currentIndex = 0
        commandField.text = ""
        urlField.text = ""
        envArea.text = ""
        headersArea.text = ""
        open()
        nameField.forceActiveFocus()
    }

    function openForEdit(server) {
        if (!server) return
        originalName = String(server.name || "")
        errorText = ""
        nameField.text = originalName
        transportBox.currentIndex = String(server.transport) === "http" ? 1 : 0
        commandField.text = formatCommand(server.command, server.args)
        urlField.text = String(server.url || "")
        envArea.text = formatPairs(server.env, "=")
        headersArea.text = formatPairs(server.headers, ":")
        open()
        nameField.forceActiveFocus()
    }

    function formatCommand(command, args) {
        const parts = []
        if (command) parts.push(String(command))
        const list = args || []
        for (let i = 0; i < list.length; ++i)
            if (String(list[i]).length > 0) parts.push(String(list[i]))
        return parts.join(" ")
    }

    // {key: value} → "KEY=VALUE" 每行一条。
    function formatPairs(map, sep) {
        if (!map) return ""
        const lines = []
        for (const key in map)
            lines.push(key + sep + String(map[key]))
        return lines.join("\n")
    }

    // 文本 → { ok, map } | { ok:false, line }。第一处非法行即返回，
    // 让用户知道改哪一行，而不是静默丢掉。
    function parsePairs(text, sep) {
        const map = {}
        const lines = String(text || "").split(/\r?\n/)
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i].trim()
            if (line.length === 0) continue
            const at = line.indexOf(sep)
            if (at <= 0 || line.slice(0, at).trim().length === 0)
                return { ok: false, line: i + 1 }
            map[line.slice(0, at).trim()] = line.slice(at + 1).trim()
        }
        return { ok: true, map: map }
    }

    function submit() {
        const name = nameField.text.trim()
        if (name.length === 0)
            return fail(qsTr("请填写服务器名称。"))
        const duplicate = McpManager.servers.some(function(entry) {
            return String(entry.name) === name && name !== root.originalName
        })
        if (duplicate)
            return fail(qsTr("已存在同名服务器：%1").arg(name))

        const server = { name: name, transport: transportBox.currentText }
        if (stdio) {
            const parts = commandField.text.trim().split(/\s+/)
            if (parts.length === 0 || parts[0] === "")
                return fail(qsTr("请填写启动命令。"))
            server.command = parts[0]
            server.args = parts.slice(1)
            const env = parsePairs(envArea.text, "=")
            if (!env.ok)
                return fail(qsTr("环境变量第 %1 行格式不对，应为 KEY=VALUE。").arg(env.line))
            if (Object.keys(env.map).length > 0) server.env = env.map
        } else {
            const url = urlField.text.trim()
            if (!/^https?:\/\/.+/.test(url))
                return fail(qsTr("请填写以 http:// 或 https:// 开头的端点 URL。"))
            server.url = url
            const headers = parsePairs(headersArea.text, ":")
            if (!headers.ok)
                return fail(qsTr("请求头第 %1 行格式不对，应为 Header: Value。").arg(headers.line))
            if (Object.keys(headers.map).length > 0) server.headers = headers.map
        }

        const ok = root.editing
            ? McpManager.updateServer(root.originalName, server)
            : McpManager.addServer(server)
        if (!ok)
            return fail(qsTr("保存失败：%1").arg(
                McpManager.lastError || qsTr("未知错误")))
        close()
        root.saved()
    }

    function fail(message) {
        errorText = message
        return false
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceSm

        Text {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceLg
            Layout.fillWidth: true
            text: root.editing ? qsTr("编辑服务器") : qsTr("添加服务器")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeH2
            font.bold: true
            font.family: Theme.fontSans
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // ── 名称 + 类型 ─────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            spacing: Theme.spaceMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXs
                Text { text: qsTr("名称"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    Accessible.name: qsTr("名称")
                    placeholderText: qsTr("my-server")
                    placeholderTextColor: Theme.textSecondary
                    color: Theme.textPrimary
                    selectionColor: Theme.brandPrimary
                    selectedTextColor: Theme.selectedTextColor
                    selectByMouse: true
                    font.pixelSize: Theme.fontSizeBody
                    background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: nameField.activeFocus ? 2 : 1; border.color: nameField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                }
            }

            ColumnLayout {
                spacing: Theme.spaceXs
                Text { text: qsTr("类型"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans }
                ComboBox {
                    id: transportBox
                    Accessible.name: qsTr("类型")
                    Layout.preferredWidth: Theme.controlWidthXs
                    model: ["stdio", "http"]
                    onActivated: root.errorText = ""
                }
            }
        }

        // ── stdio：命令与参数 + 环境变量 ─────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            spacing: Theme.spaceXs
            visible: root.stdio

            Text { text: qsTr("命令与参数（空格分隔）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans }
            TextField {
                id: commandField
                Layout.fillWidth: true
                Accessible.name: qsTr("命令与参数（空格分隔）")
                placeholderText: qsTr("npx -y @modelcontextprotocol/server-filesystem D:/docs")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                font.pixelSize: Theme.fontSizeBody
                background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: commandField.activeFocus ? 2 : 1; border.color: commandField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
            }

            Text { text: qsTr("环境变量（每行 KEY=VALUE，可留空）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans }
            TextArea {
                id: envArea
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                Accessible.name: qsTr("环境变量")
                placeholderText: qsTr("TOKEN=abc123")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                wrapMode: TextArea.WrapAnywhere
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: envArea.activeFocus ? 2 : 1; border.color: envArea.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
            }
        }

        // ── http：端点 URL + 请求头 ──────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            spacing: Theme.spaceXs
            visible: !root.stdio

            Text { text: qsTr("服务端点 URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans }
            TextField {
                id: urlField
                Layout.fillWidth: true
                Accessible.name: qsTr("服务端点 URL")
                placeholderText: qsTr("https://mcp.example.com/mcp")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                font.pixelSize: Theme.fontSizeBody
                background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: urlField.activeFocus ? 2 : 1; border.color: urlField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
            }

            Text { text: qsTr("请求头（每行 Header: Value，可留空）"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontSans }
            TextArea {
                id: headersArea
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                Accessible.name: qsTr("请求头")
                placeholderText: qsTr("Authorization: Bearer <token>")
                placeholderTextColor: Theme.textSecondary
                color: Theme.textPrimary
                selectionColor: Theme.brandPrimary
                selectedTextColor: Theme.selectedTextColor
                selectByMouse: true
                wrapMode: TextArea.WrapAnywhere
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: headersArea.activeFocus ? 2 : 1; border.color: headersArea.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
            }
        }

        Text {
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.statusDanger
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.rightMargin: Theme.spaceLg
            Layout.bottomMargin: Theme.spaceLg
            Layout.leftMargin: Theme.spaceLg
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("取消")
                onClicked: root.close()
            }
            AppButton {
                objectName: "mcpServerEditorSave"
                text: root.editing ? qsTr("保存") : qsTr("添加并连接")
                onClicked: root.submit()
            }
        }
    }
}
