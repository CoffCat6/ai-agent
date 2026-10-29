import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 设置页统一骨架：整页滚动 + 页头 + 统一页边距与段间距。
//
// 页面只声明内容，不再自己写 Flickable / contentHeight / 左右边距：
//
//     SettingsPage {
//         title: qsTr("日志与诊断")
//         description: qsTr("日志可能包含本地路径和错误上下文…")
//         SettingCard { Layout.fillWidth: true; ... }
//     }
//
// 内容列已按 horizontalMargin 收边，子项不要再加 Layout.leftMargin/rightMargin。
// 固定页头 + 内部列表的页面（记忆/用量/活动/项目/提示词）沿用各自结构，
// 但页头统一用 SettingsPageHeader。
Item {
    id: root

    property string title: ""
    property string description: ""
    // 搜索目录需要锚定页头标题时使用（对象名必须与 SettingsCatalog 条目一致）。
    property string headerObjectName: ""
    // 段与段之间；也作为页头与首段之间的间距。
    property int contentSpacing: Theme.spaceLg
    property int horizontalMargin: Theme.spaceXl
    // 供页面在需要时（如恢复默认后）复位滚动位置。
    property alias contentY: scroller.contentY

    default property alias content: body.data

    Flickable {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight + root.horizontalMargin
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: column
            width: scroller.width
            spacing: root.contentSpacing

            Item { Layout.preferredHeight: Theme.spaceXs }

            SettingsPageHeader {
                objectName: root.headerObjectName
                Layout.fillWidth: true
                Layout.leftMargin: root.horizontalMargin
                Layout.rightMargin: root.horizontalMargin
                title: root.title
                description: root.description
            }

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                Layout.leftMargin: root.horizontalMargin
                Layout.rightMargin: root.horizontalMargin
                spacing: root.contentSpacing
            }
        }
    }
}
