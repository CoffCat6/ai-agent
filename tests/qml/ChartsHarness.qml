pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 任务 20：统计页三图表的渲染夹具（由 tests/test_charts_qml.cpp 驱动）。
//
// 布局对齐真实页面 src/ui/pages/UsageStatsPage.qml:1098-1203：
//   第一行 RowLayout（spacing 12）两张并排卡片（DonutChart +
//   LineTrendChart），第二行 BarCompareChart 整宽卡片；卡片均为
//   260 高、radius 8、bgPrimary 底、borderSubtle 边、内边距 12、
//   标题行（fontSizeCaption/textSecondary）+ 图表 fillWidth/fillHeight。
//   注意：真实页面卡片底色是 Theme.bgPrimary（浅色 #F5F7FA），
//   不是 bgSecondary（纯白）——夹具以真实页面为准。
//   第二行卡片的 Token/调用次数切换按钮（AppButton）与渲染无关，
//   夹具刻意省略以保持最小依赖。
//
// 数据是用户真机的真实形态（d30 时间段，总计 1,676,757 token）：
//   - byModel 7 个模型（step-5-preview 占 48%）
//   - byDay 19 天（09-28 ~ 10-17，10-08 断档是真实数据形态）
// 两组数据总和一致（1,676,757），与用户库聚合自洽。组件的
// required property var series 由本夹具注入，注入方式与真实页面的
// page.donutSeries / page.dayTrendSeries / page.barSeries 一致。
//
// import 同时带 NexusAI 与 NexusAI.ViewModels：前者提供图表组件
// （测试引擎经 addImportPath(build) 加载 build/NexusAI/qmldir 的
// 文件系统副本），后者提供 Theme 单例（注册在 nexus_viewmodels）。
// 图表组件自身的 import 行正在被修复（NexusAI → NexusAI.ViewModels），
// 夹具两份 import 都带上，保证修复前后都能加载。
Window {
    id: root
    visible: true
    width: 900
    height: 760

    // ── 用户真实数据（d30 聚合形态）────────────────────
    readonly property var donutSeries: [
        { label: "step-5-preview", value: 803877 },
        { label: "deepseek-v4-flash-0731", value: 725132 },
        { label: "glm-5.3-flash", value: 48273 },
        { label: "gemini-3.6-flash-high", value: 44133 },
        { label: "gemini-3.7-flash-high", value: 28344 },
        { label: "step-router-v1", value: 25462 },
        { label: "gemini-3.8-flash-high", value: 1536 },
    ]
    readonly property var dayTrendSeries: [
        { label: "09-28", value: 13207 },
        { label: "09-29", value: 59270 },
        { label: "09-30", value: 58496 },
        { label: "10-01", value: 129777 },
        { label: "10-02", value: 72816 },
        { label: "10-03", value: 30302 },
        { label: "10-04", value: 81206 },
        { label: "10-05", value: 47357 },
        { label: "10-06", value: 178552 },
        { label: "10-07", value: 93477 },
        { label: "10-09", value: 53216 },
        { label: "10-10", value: 28620 },
        { label: "10-11", value: 37890 },
        { label: "10-12", value: 6940 },
        { label: "10-13", value: 180185 },
        { label: "10-14", value: 1972 },
        { label: "10-15", value: 468935 },
        { label: "10-16", value: 126083 },
        { label: "10-17", value: 8456 },
    ]
    // token 口径下与 donutSeries 同源（真实页面按 barMode 切换重算）
    readonly property var barSeries: [
        { label: "step-5-preview", value: 803877 },
        { label: "deepseek-v4-flash-0731", value: 725132 },
        { label: "glm-5.3-flash", value: 48273 },
        { label: "gemini-3.6-flash-high", value: 44133 },
        { label: "gemini-3.7-flash-high", value: 28344 },
        { label: "step-router-v1", value: 25462 },
        { label: "gemini-3.8-flash-high", value: 1536 },
    ]

    // 供 C++ 像素断言取色基准：QColor → "#rrggbb" 字符串。
    // 不在测试里硬编码主题色值，主题令牌调整时基准自动跟随。
    function brandPrimaryColor() { return String(Theme.brandPrimary) }
    function cardBackgroundColor() { return String(Theme.bgPrimary) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── 第一行：环形图 + 折线图（对齐 UsageStatsPage.qml:1098）──
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                radius: 8
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        text: qsTr("模型 Token 分布")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                    DonutChart {
                        objectName: "donutChart"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        series: root.donutSeries
                        centerLabel: qsTr("总 Token")
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                radius: 8
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.borderSubtle

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        text: qsTr("按天走势")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }
                    LineTrendChart {
                        objectName: "trendChart"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        series: root.dayTrendSeries
                    }
                }
            }
        }

        // ── 第二行：柱状图整宽卡片（对齐 UsageStatsPage.qml:1159）──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            radius: 8
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.borderSubtle

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                Text {
                    text: qsTr("模型对比")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                }
                BarCompareChart {
                    objectName: "barChart"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    series: root.barSeries
                    mode: "tokens"
                }
            }
        }
    }
}