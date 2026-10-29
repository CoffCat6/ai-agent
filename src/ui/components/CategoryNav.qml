import QtQuick
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 设置中心左侧导航：一级分组标题 + 组内分类。
//
// 分组来自 PreferenceRegistry.categoryGroups()（SettingsSchema 的
// CategoryGroupMeta），分类归属由 CategoryMeta::groupId 决定。分组只影响
// 展示层级，不改变任何存储键——settings.lastCategory 仍是分类 id。
//
// 用 Flickable 承载：分类从 12 个扩到 16 个后，固定 Column 会直接溢出
// 可视区且无法滚动。
Rectangle {
    id: nav

    property string currentCategory: "model"

    signal categorySelected(string id)

    color: Theme.bgSecondary

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSubtle
    }

    Flickable {
        id: navFlick
        anchors.fill: parent
        contentWidth: width
        contentHeight: navColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: navColumn.implicitHeight > navFlick.height
                ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: navColumn
            width: navFlick.width
            spacing: 0

            Repeater {
                model: PreferenceRegistry.categoryGroups()

                delegate: Column {
                    id: groupColumn
                    required property string modelData
                    required property int index

                    width: navColumn.width
                    spacing: 0

                    Item {
                        width: 1
                        height: groupColumn.index === 0 ? 8 : 12
                    }

                    Text {
                        // Reading currentLocale re-evaluates this binding on a
                        // language switch; groupName resolves the stored
                        // QT_TRANSLATE_NOOP source at query time.
                        text: {
                            I18n.currentLocale
                            return PreferenceRegistry.categoryGroupName(groupColumn.modelData)
                        }
                        leftPadding: 20
                        topPadding: 4
                        bottomPadding: 4
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                        font.family: Theme.fontMono
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    Repeater {
                        model: PreferenceRegistry.categoriesInGroup(groupColumn.modelData)

                        delegate: Rectangle {
                            id: navItem
                            required property string modelData

                            width: groupColumn.width
                            height: 34

                            readonly property bool isActive:
                                navItem.modelData === nav.currentCategory

                            color: isActive
                                ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                          Theme.brandPrimary.b, 0.08)
                                : (itemMA.containsMouse ? Theme.surfaceHover : "transparent")

                            Rectangle {
                                visible: navItem.isActive
                                width: 3
                                height: navItem.height
                                color: Theme.brandPrimary
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 20
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                // Reading currentLocale re-evaluates this
                                // binding on a language switch; categoryName
                                // resolves the stored QT_TRANSLATE_NOOP source
                                // at query time.
                                text: {
                                    I18n.currentLocale
                                    return PreferenceRegistry.categoryName(navItem.modelData)
                                }
                                color: navItem.isActive
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                font.bold: navItem.isActive
                                elide: Text.ElideRight
                            }

                            MouseArea {
                                id: itemMA
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: nav.categorySelected(navItem.modelData)
                            }

                            Accessible.role: Accessible.ListItem
                            Accessible.name: {
                                I18n.currentLocale
                                return PreferenceRegistry.categoryName(navItem.modelData)
                            }
                            Accessible.description: isActive ? qsTr("当前选中") : ""
                            Accessible.onPressAction:
                                nav.categorySelected(navItem.modelData)
                        }
                    }
                }
            }
        }
    }
}
