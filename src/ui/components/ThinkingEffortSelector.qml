import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 思考强度选择器（跟随默认 / 关闭 / 低 / 中 / 高）。
//
// 归属与"模型配置"一致：读写活动 profile 的 reasoningEffort 字段，切换即
// 写回 QSettings 并触发 Settings.activeProfileChanged，由 main.cpp 的
// applySettings 推给模板 agent 与每个标签的 ProviderSet——对新建会话与正在
// 跑的会话都立即生效，不需要重启。
//
// 档位是"意图"而不是线协议字段：OpenAI 落到 reasoning_effort、Anthropic 折
// 算成 thinking.budget_tokens、Ollama 退化成布尔 think，映射各家自理。
Item {
    id: root

    implicitWidth: effortRow.implicitWidth + 20
    implicitHeight: 28

    readonly property string current: {
        const v = Settings.activeProfile.reasoningEffort
        return v === undefined ? "" : String(v)
    }

    // 未设置的 profile 在界面上显示为"跟随默认"，与实际发出去的报文一致
    // （未设置 = 该字段整体不发）。
    readonly property var levels: [
        { value: "",         label: qsTr("思考·默认"), desc: qsTr("不发送思考参数，由模型自己决定") },
        { value: "off",      label: qsTr("思考·关闭"), desc: qsTr("明确要求不要思考，响应最快、最省 token") },
        { value: "low",      label: qsTr("思考·低"),   desc: qsTr("少量推理预算，适合简单问答") },
        { value: "medium",   label: qsTr("思考·中"),   desc: qsTr("均衡；工具型任务推荐") },
        { value: "high",     label: qsTr("思考·高"),   desc: qsTr("最大推理投入，慢且贵，适合难题") }
    ]

    readonly property string currentLabel: {
        for (var i = 0; i < levels.length; ++i)
            if (levels[i].value === current) return levels[i].label
        return qsTr("思考·默认")
    }

    // 只有"中/高"值得用品牌色点标识强度，默认与关闭保持中性。
    readonly property bool emphasized: current === "medium" || current === "high"

    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("思考强度") + " · " + currentLabel
    Accessible.description: {
        for (var i = 0; i < levels.length; ++i)
            if (levels[i].value === current) return levels[i].desc
        return ""
    }

    function selectLevel(value) {
        const id = Settings.activeProfileId
        if (!id) return
        Settings.updateProfile(id, { reasoningEffort: value })
    }

    Rectangle {
        id: triggerBox
        anchors.fill: parent
        radius: 6
        color: triggerMA.containsMouse || effortPopup.opened
            ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: effortPopup.opened || root.emphasized
            ? Theme.brandPrimary : "transparent"

        Behavior on color { ColorAnimation { duration: 150 } }

        Row {
            id: effortRow
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                width: 6
                height: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: root.emphasized ? Theme.brandPrimary : Theme.textSecondary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.currentLabel
                color: triggerMA.containsMouse || effortPopup.opened
                    ? Theme.brandPrimary : Theme.textSecondary
                font.pixelSize: 10
                font.family: Theme.fontMono
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: effortPopup.opened ? "chevron-up" : "chevron-down"
                size: 11
                iconColor: triggerMA.containsMouse || effortPopup.opened
                    ? Theme.brandPrimary : Theme.textSecondary
            }
        }

        MouseArea {
            id: triggerMA
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: effortPopup.opened ? effortPopup.close() : effortPopup.open()
        }
    }

    // 触发器贴着窗口底边，所以和另两个选择器一样向上弹。
    Popup {
        id: effortPopup
        x: 0
        y: -height - 6
        width: 320
        height: effortContent.implicitHeight + 12
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: effortContent
            spacing: 2

            Text {
                text: qsTr("思考强度 · 保存到当前模型配置")
                color: Theme.textSecondary
                font.pixelSize: 10
                font.bold: true
                font.family: Theme.fontMono
                Layout.leftMargin: 8
                Layout.topMargin: 2
            }

            Repeater {
                model: root.levels

                delegate: Rectangle {
                    id: levelItem
                    required property var modelData
                    readonly property bool isSelected: modelData.value === root.current
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    Layout.preferredHeight: 34
                    radius: 5
                    color: levelMA.containsMouse ? Theme.surfaceHover : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6

                        AppIcon {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            icon: "check"
                            size: 12
                            iconColor: Theme.brandPrimary
                            visible: levelItem.isSelected
                        }

                        Item {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            visible: !levelItem.isSelected
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                text: modelData.label
                                color: levelItem.isSelected
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                                font.bold: levelItem.isSelected
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.desc
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                font.family: Theme.fontSans
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: levelMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            effortPopup.close()
                            root.selectLevel(modelData.value)
                        }
                    }
                }
            }
        }
    }
}
