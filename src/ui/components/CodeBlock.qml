import QtQuick

import QtQuick.Layouts
import NexusAI
import NexusAI.ViewModels

// 代码块展示组件（docs/19 §5.2 + docs/45 §3.3）：
// 可选标题栏（语言标签 + 复制）+ 语法高亮正文 + 深浅色自适应的底部投影。
// - 根用 Item 承载「投影 + 卡片」两层：卡片自己 clip 出圆角，投影必须在
//   裁剪区外才能外扩——若沿用原「根 Rectangle + clip:true」，投影会被裁掉。
// - 投影颜色/模糊/位移走 Theme.codeBlockShadow* token：暗色纯黑下沉、亮色
//   冷灰浮起，两套取值随主题切换自动对应，不在 QML 里写死色值。
// - radiusMd + codeBackground，与气泡/卡片的分层一致。
Item {
    id: root

    property string text: ""
    // 调用方显式指定的语言标签（如围栏信息串 "python"）。
    property string languageLabel: ""
    property color bgColor: Theme.codeBackground
    property color textColor: Theme.textPrimary
    property bool copyable: true

    readonly property string displayLanguage:
        root.languageLabel !== "" ? root.languageLabel : highlighter.languageName
    readonly property bool hasHeader: root.displayLanguage !== ""

    // TextEdit 对自动换行内容没有隐式高度，须手动补。
    // docs/45 §3.3：内边距 8/12 收到 10/14 后，上下补偿同步 16 → 20
    // （contentHeight 已随行距 token 自动含入 1.5 倍行高，无需单独加成）。
    readonly property int bodyHeight: codeText.contentHeight + 20

    // 投影在卡片底部的外扩量，计入布局高度：段间距只有 12px，投影若完全
    // 溢出会压到下一段正文上（底部外扩 = 外扩像素 + 垂直位移）。
    readonly property int shadowFootprint:
        Theme.codeBlockShadowBlur + Theme.codeBlockShadowOffsetY

    implicitWidth: card.implicitWidth
    implicitHeight: card.implicitHeight + root.shadowFootprint

    // Passive hover detection (does not steal mouse events from text)
    HoverHandler {
        id: codeHover
    }

    // ── 卡片本体：圆角裁剪 + 边框 ────────────────────
    Rectangle {
        id: card
        objectName: "codeBlockCard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: (root.hasHeader ? headerBar.height : 0) + root.bodyHeight
        // 外部用固定高度装载时（ToolConfirmDialog 的 Layout.preferredHeight）
        // 以外部高度为准并裁剪；不设高度时按内容自然高度。
        height: Math.min(implicitHeight, root.height)
        radius: Theme.radiusMd
        color: root.bgColor
        border.width: 1
        border.color: Theme.cardBorder
        // 裁剪从根移到卡片：root 保持不裁剪，投影才能外扩。
        clip: true

        // ── Header: language label + copy ─────────────
        Rectangle {
            id: headerBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            visible: root.hasHeader
            height: 26
            color: Theme.cardHeaderBackground

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: root.displayLanguage
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeMicro
                font.weight: Font.DemiBold
            }

            CopyButton {
                id: copyBtn
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                // docs/25 D2: 聚焦时保持可见——CopyButton 已在 Tab 链内（docs/25 D1），
                // 鼠标移开后不因 hover 消失而丢失焦点。
                visible: root.copyable && root.text !== ""
                         && (codeHover.hovered || copyBtn.activeFocus)
                text: root.text
                bgColor: "transparent"
            }

            // 标题栏底部分隔线
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.cardBorder
            }
        }

        // ── Body ─────────────────────────────────────
        TextEdit {
            id: codeText
            objectName: "codeBlockText"
            anchors.top: root.hasHeader ? headerBar.bottom : parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            // docs/45 §3.3：内边距 8/12 → 10/14——等宽小字贴边显得局促，
            // 左侧多 4px 与代码缩进层级拉开距离。
            anchors.margins: 10
            anchors.leftMargin: 14
            text: root.text
            readOnly: true
            cursorVisible: false
            activeFocusOnPress: false
            activeFocusOnTab: false
            selectByMouse: true
            color: root.textColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: TextEdit.WrapAnywhere
            renderType: TextEdit.NativeRendering
            textFormat: TextEdit.PlainText

            // docs/45 D1：等宽代码 1.5 倍行距。TextEdit 没有 lineHeight 属性
            // （Qt 6.9 仅 Text 元素有），行距经 DocumentLineHeight 落到
            // QTextDocument 的 blockFormat 上——布局生效且交互语义不变。
            DocumentLineHeight {
                objectName: "codeBlockLineHeight"
                document: codeText.textDocument
                lineHeight: Theme.lineHeightCode
            }

            CodeHighlighter {
                id: highlighter
                document: codeText.textDocument
                text: root.text
            }
        }
    }

    // ── 投影：三层同心矩形向外逐层扩大、透明度递减，模拟柔和投影。
    //    零特效分层——不引 QtQuick.Effects（项目下限 6.5，见 ThemeProvider
    //    的阴影令牌注释），无 FBO、无每帧重缓存；深浅两套取值全走 token。
    Repeater {
        id: shadowLayers
        model: 3

        Rectangle {
            objectName: index === 0 ? "codeBlockShadow" : ""
            anchors.fill: card
            anchors.margins: -Math.round(Theme.codeBlockShadowBlur * (index + 1) / 3)
            // 顶部少外扩 offsetY，形成向下的重量感；底部随 margins 继续外扩。
            anchors.topMargin: -Math.round(Theme.codeBlockShadowBlur * (index + 1) / 3)
                                + Theme.codeBlockShadowOffsetY
            radius: card.radius
                    + Math.round(Theme.codeBlockShadowBlur * (index + 1) / 3)
            color: Theme.codeBlockShadowColor
            opacity: Theme.codeBlockShadowOpacity * Math.pow(0.55, index)
            z: -1 - index
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: root.hasHeader
        ? qsTr("代码块（%1）").arg(root.displayLanguage)
        : qsTr("代码块")
    // 用 JS 的 substring()：QML 字符串没有 QString.left()，调用会抛
    // TypeError（每次渲染代码块都会打到日志）。
    Accessible.description: root.text.substring(0, 80)
}