import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// usage_log activity timeline — settings center "活动记录" category.
// Used to live as the third block of UsageStatsPage; promoted to its own
// page because an audit trail is a different concern from statistics and
// quotas. Data comes from AgentVM.listUsageEvents(500) (full pull, id DESC
// newest first); category and keyword filtering happen locally in QML and
// never write back. Privacy contract (aligned with Activity.h): metadata
// only (action, target, result) — never conversation content, document
// content or API keys.
Item {
    id: page

    Accessible.name: qsTr("活动记录页")
    Accessible.description: qsTr("浏览用户操作、智能体活动与网络请求的使用轨迹")

    // ── 数据与过滤状态（usage_log 轨迹，QML 侧本地过滤）──
    property var usageEvents: []
    property int activityCategoryIndex: 0   // 0=全部 1=用户操作 2=智能体 3=网络
    property string activityKeyword: ""

    // 声明式过滤（仿 MemoryPage.applySearch 的"全量拉取 + 本地筛选"模式）：
    // 类别精确匹配 + 关键词对 action/target/detail 不区分大小写包含匹配。
    readonly property var activityEvents: {
        var cat = ["", "nexus.user", "nexus.agent", "nexus.network"][activityCategoryIndex] || ""
        var kw = activityKeyword.trim().toLowerCase()
        var out = []
        for (var i = 0; i < usageEvents.length; ++i) {
            var e = usageEvents[i]
            if (cat !== "" && e.category !== cat) continue
            if (kw !== "") {
                var hay = ((e.action || "") + " " + (e.target || "") + " " + (e.detail || "")).toLowerCase()
                if (hay.indexOf(kw) < 0) continue
            }
            out.push(e)
        }
        return out
    }

    // usage_log 活动时间线：全量拉取最近 500 条，QML 侧按类别/关键词过滤
    //（数据契约：每项 {timestamp(ms), category, action, target, requestId,
    // detail}，id DESC 最新在前；requestId 为次要信息，时间线不展示）。
    function refreshActivity() {
        usageEvents = AgentVM.listUsageEvents(500)
    }

    // 活动类别 → 友好名
    function activityCategoryLabel(category) {
        const map = {
            "nexus.user":    qsTr("用户"),
            "nexus.agent":   qsTr("智能体"),
            "nexus.network": qsTr("网络")
        }
        return map[category] || category || qsTr("未知")
    }

    // 类别徽章配色：用户=品牌色 / 智能体=成功色 / 网络=警示色
    function activityCategoryColor(category) {
        const map = {
            "nexus.user":    Theme.brandPrimary,
            "nexus.agent":   Theme.statusSuccess,
            "nexus.network": Theme.statusWarning
        }
        return map[category] || Theme.textSecondary
    }

    Component.onCompleted: refreshActivity()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 标题 ────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            Layout.leftMargin: 24
            Layout.rightMargin: 24

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                Text {
                    objectName: "activity/log"
                    text: qsTr("活动记录")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeH2
                    font.bold: true
                    font.family: Theme.fontSans
                }
                Text {
                    text: qsTr("浏览操作与网络请求的使用轨迹。仅记录操作元数据（操作类型、对象、结果），不包含对话内容、文档内容与 API 密钥。")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.fontSans
                    opacity: 0.8
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
        }

        // ── 时间线 ──────────────────────────────────────
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 12
            clip: true
            contentWidth: width
            contentHeight: detailCol.implicitHeight

            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: detailCol
                width: flick.width
                spacing: 16

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: activityCol.implicitHeight + 20
                    radius: 8
                    color: Theme.bgPrimary
                    border.width: 1
                    border.color: Theme.borderSubtle

                    ColumnLayout {
                        id: activityCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        // 工具行：类别筛选 + 关键词过滤 + 计数 + 刷新
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ComboBox {
                                id: activityCategoryCombo
                                // 140：en 条目最长 "User Actions" + 下拉指示
                                // 器 + 内边距，更窄有截断风险
                                Layout.preferredWidth: 140
                                model: [qsTr("全部"), qsTr("用户操作"), qsTr("智能体"), qsTr("网络")]
                                currentIndex: 0
                                onActivated: page.activityCategoryIndex = currentIndex
                                Accessible.name: qsTr("活动类别筛选")
                            }
                            TextField {
                                id: activitySearchField
                                Layout.preferredWidth: 200
                                placeholderText: qsTr("过滤操作 / 对象 / 说明…")
                                placeholderTextColor: Theme.textSecondary
                                color: Theme.textPrimary
                                selectByMouse: true
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                background: Rectangle {
                                    radius: 6
                                    color: Theme.bgSecondary
                                    border.width: activitySearchField.activeFocus ? 2 : 1
                                    border.color: activitySearchField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
                                }
                                onTextChanged: page.activityKeyword = text
                                Accessible.name: qsTr("过滤活动记录关键词")
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("共 %1 条").arg(page.activityEvents.length)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontMono
                            }
                            AppButton {
                                text: qsTr("刷新")
                                onClicked: page.refreshActivity()
                                Accessible.name: qsTr("刷新活动记录")
                            }
                        }

                        // 时间线：固定高度 + clip + 内部滚动条——外层是 Flickable，
                        // 高度不给定值会吞掉滚动手势，无法独立滚动。
                        ListView {
                            id: activityList
                            Layout.fillWidth: true
                            Layout.preferredHeight: 360
                            clip: true
                            spacing: 4
                            model: page.activityEvents
                            ScrollBar.vertical: ScrollBar {}

                            // 空态两种文案：全量无记录 vs 过滤后无匹配
                            Text {
                                visible: page.activityEvents.length === 0
                                anchors.centerIn: parent
                                text: page.usageEvents.length === 0
                                    ? qsTr("暂无活动记录")
                                    : qsTr("无匹配的活动记录")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontSans
                                opacity: 0.7
                            }

                            delegate: Rectangle {
                                required property var modelData
                                width: activityList.width
                                implicitHeight: 40
                                radius: 6
                                color: Theme.bgSecondary
                                border.width: 1
                                border.color: Theme.borderSubtle

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8

                                    // 时间（等宽小字）
                                    Text {
                                        text: modelData.timestamp
                                            ? Theme.formatDateTime(new Date(modelData.timestamp))
                                            : qsTr("—")
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 120
                                    }
                                    // 类别徽章：淡彩底 + 彩字
                                    Rectangle {
                                        id: activityBadge
                                        readonly property color badgeColor: page.activityCategoryColor(modelData.category)
                                        Layout.preferredHeight: 18
                                        implicitWidth: activityBadgeText.implicitWidth + 12
                                        radius: 4
                                        color: Qt.rgba(activityBadge.badgeColor.r, activityBadge.badgeColor.g, activityBadge.badgeColor.b, 0.15)
                                        Text {
                                            id: activityBadgeText
                                            anchors.centerIn: parent
                                            text: page.activityCategoryLabel(modelData.category)
                                            color: activityBadge.badgeColor
                                            font.pixelSize: 10
                                            font.family: Theme.fontSans
                                        }
                                    }
                                    // 操作名（等宽，如 task.start / doc.import）
                                    Text {
                                        text: modelData.action || ""
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontMono
                                        Layout.preferredWidth: 130
                                        elide: Text.ElideRight
                                    }
                                    // 操作对象（可为空，中段省略保留首尾）
                                    Text {
                                        visible: (modelData.target || "").length > 0
                                        text: modelData.target || ""
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 100
                                    }
                                    // 补充说明（次要色，可为空）
                                    Text {
                                        visible: (modelData.detail || "").length > 0
                                        text: modelData.detail || ""
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontSans
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 100
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 8 }
            }
        }
    }
}