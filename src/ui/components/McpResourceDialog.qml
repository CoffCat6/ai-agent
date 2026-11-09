import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/48: MCP 资源选择器 —— 输入框 + 菜单「MCP 资源…」打开，列出各已连接
// 服务器声明的资源（搜索过滤 + 服务器徽标），点击即 McpManager.readResource；
// 读取结果由 InputConsole 的 resourceRead 回程接成附件。
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: parent
    padding: 0
    width: 480
    height: Math.min(520, parent ? parent.height - 24 : 520)

    // 搜索过滤（name/uri/description 大小写不敏感子串）。
    property string query: ""
    readonly property var allResources: McpManager.listResources()
    readonly property var filtered: {
        const q = query.toLowerCase()
        if (q.length === 0) return allResources
        const out = []
        for (let i = 0; i < allResources.length; ++i) {
            const r = allResources[i] || {}
            if (String(r.name || "").toLowerCase().indexOf(q) >= 0
                    || String(r.uri || "").toLowerCase().indexOf(q) >= 0
                    || String(r.description || "").toLowerCase().indexOf(q) >= 0)
                out.push(r)
        }
        return out
    }

    function openFor() {
        query = ""
        if (!visible) open()
    }

    // 选中资源：宿主（InputConsole）登记 pending 标记并发起读取——
    // pending 标记在宿主侧，回程才不会被竞态丢弃。
    signal resourceActivated(string server, string uri, string name)

    background: DialogBackground {}

    contentItem: ColumnLayout {
        Accessible.name: qsTr("MCP 资源选择器")
        spacing: 8

        // Title
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.topMargin: 18
            Layout.rightMargin: 20
            spacing: 8

            AppIcon {
                icon: "globe"
                size: 18
                iconColor: Theme.brandPrimary
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("MCP 资源")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
            }
        }

        // 搜索框
        TextField {
            id: searchField
            objectName: "mcpResourceSearch"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            placeholderText: qsTr("搜索名称 / URI / 描述…")
            onTextChanged: dialog.query = text
        }

        // 空态
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: dialog.filtered.length === 0
            text: dialog.query.length > 0
                ? qsTr("没有匹配的资源。")
                : qsTr("暂无可用资源。\n连接声明了 resources 能力的 MCP 服务器后在此读取。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        // 资源列表
        ListView {
            id: resourceList
            objectName: "mcpResourceList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            clip: true
            visible: dialog.filtered.length > 0
            model: dialog.filtered
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: resourceRow
                required property var modelData
                required property int index

                width: ListView.view ? ListView.view.width : 0
                height: 56
                radius: 6
                color: rowHover.hovered ? Theme.surfaceHover : "transparent"

                HoverHandler { id: rowHover }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dialog.resourceActivated(resourceRow.modelData.server,
                                                 resourceRow.modelData.uri,
                                                 resourceRow.modelData.name)
                        dialog.accept()
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: resourceRow.modelData.title.length > 0
                                ? resourceRow.modelData.title
                                : resourceRow.modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideRight
                        }
                        // 服务器来源徽标
                        Rectangle {
                            implicitWidth: serverLabel.implicitWidth + 10
                            implicitHeight: 16
                            radius: 8
                            color: Qt.rgba(Theme.brandPrimary.r,
                                           Theme.brandPrimary.g,
                                           Theme.brandPrimary.b, 0.12)

                            Text {
                                id: serverLabel
                                anchors.centerIn: parent
                                text: resourceRow.modelData.server
                                color: Theme.brandPrimary
                                font.pixelSize: Theme.fontSizeMicro
                                font.family: Theme.fontSans
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: resourceRow.modelData.uri
                              + (resourceRow.modelData.mimeType.length > 0
                                 ? " · " + resourceRow.modelData.mimeType : "")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeMicro
                        font.family: Theme.fontMono
                        elide: Text.ElideMiddle
                    }
                }

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("读取资源 %1").arg(resourceRow.modelData.name)
            }
        }
    }
}
