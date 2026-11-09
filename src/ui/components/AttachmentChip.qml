import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 单个附件的可视单元（docs/19 §4.3）：图片缩略图或文本文件芯片。
// 由调用方显式传入 kind / source / name，避免依赖不同来源的字段形状
// （tracer 的 images 是裸 data URL 字符串，files 是 {name, content}）。
//
// V6（docs/19 §6）：输入区预览与气泡内附件共用本组件——
//   * imageWidth/imageHeight 让预览用更小的缩略图（默认即气泡尺寸）；
//   * removable 打开右上角删除按钮，预览条用，气泡内不显示。
Rectangle {
    id: root

    property string kind: "file"      // "image" | "file"
    property string source: ""        // image: data URL
    property string name: ""          // file: 文件名
    // docs/48: 附件来源（"mcp" = MCP resources 读取，地球图标区分本地文件）
    property string origin: ""
    property int imageWidth: 96
    property int imageHeight: 72
    property bool removable: false

    signal removeRequested()

    readonly property bool isImage: kind === "image"

    width: isImage ? imageWidth : chipRow.width + Theme.spaceSm * 2
    height: isImage ? imageHeight : 28
    radius: Theme.radiusSm
    color: Theme.surfaceHover
    clip: true

    Image {
        visible: root.isImage
        anchors.fill: parent
        source: root.isImage ? root.source : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    Row {
        id: chipRow
        visible: !root.isImage
        anchors.centerIn: parent
        spacing: Theme.spaceXs

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            icon: root.origin === "mcp" ? "globe" : "export"
            size: 12
            iconColor: Theme.textSecondary
        }

        Text {
            id: chipName
            anchors.verticalCenter: parent.verticalCenter
            text: root.name
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            elide: Text.ElideRight
            width: Math.min(implicitWidth, 140)

            // docs/25 C5: 文件名被 140px 上限截断时悬停可看全名
            HoverHandler { id: chipNameHover }
            ToolTip.visible: chipNameHover.hovered && chipName.truncated
            ToolTip.text: chipName.text
        }
    }

    // 删除按钮（仅输入区预览用）
    Rectangle {
        visible: root.removable
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 2
        width: 18
        height: 18
        radius: 9
        color: removeArea.containsMouse ? Theme.statusDanger : Theme.bgPrimary
        opacity: 0.92

        Text {
            anchors.centerIn: parent
            text: "\u00d7"
            color: removeArea.containsMouse ? "#ffffff" : Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
        }

        MouseArea {
            id: removeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.removeRequested()
        }

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("移除附件")
        Accessible.onPressAction: root.removeRequested()
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: root.isImage ? qsTr("图片附件") : root.name
}
