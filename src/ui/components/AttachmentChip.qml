import QtQuick
import NexusAI
import NexusAI.ViewModels

// 单个附件的可视单元（docs/19 §4.3）：图片缩略图或文本文件芯片。
// 由调用方显式传入 kind / source / name，避免依赖不同来源的字段形状
// （tracer 的 images 是裸 data URL 字符串，files 是 {name, content}）。
Rectangle {
    id: root

    property string kind: "file"      // "image" | "file"
    property string source: ""        // image: data URL
    property string name: ""          // file: 文件名

    readonly property bool isImage: kind === "image"

    width: isImage ? 96 : chipRow.width + Theme.spaceSm * 2
    height: isImage ? 72 : 28
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
            icon: "export"
            size: 12
            iconColor: Theme.textSecondary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.name
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            elide: Text.ElideRight
            width: Math.min(implicitWidth, 140)
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: root.isImage ? qsTr("图片附件") : root.name
}
