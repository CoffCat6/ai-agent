import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// docs/53: 图片附件的放大查看（Lightbox）。全屏半透明模态 + 等比适配的
// 原图（data URL 直接进 Image）；点击任意处或 Esc 关闭。由 AttachmentChip
// 在图片 chip 上内嵌实例化（chip 数量少，实例随消息存在，打开才渲染）。
Popup {
    id: root

    property string source: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - 48, 1600) : 800
    height: parent ? parent.height - 48 : 600
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.92)
        radius: Theme.radiusMd
    }

    contentItem: Item {
        Image {
            anchors.fill: parent
            anchors.margins: Theme.spaceLg
            source: root.source
            fillMode: Image.PreserveAspectFit
            asynchronous: true

            TapHandler {
                onTapped: root.close()
            }
        }
        // 右上角关闭提示（低调，不抢视觉）。
        Text {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spaceSm
            text: qsTr("点击任意处关闭 (Esc)")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeNano
            font.family: Theme.fontSans
        }
    }
}
