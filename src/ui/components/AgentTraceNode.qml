import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// Agent trace node - visualizes the thinking/tool-call/result timeline.
//
// - Long detail blocks (tool arguments / results) are collapsed by default
//   (<= 400 chars stay open); the node header toggles them. The expanded
//   state is keyed by node id and stored on the containing ListView
//   (traceListView in AgentChatPage), so it survives the full trace model
//   resets that follow every agent update.
// - Intermediate steps (thinking / tool calls / results / context notices)
//   can be hidden entirely via the page-level "思考过程" toggle.
Item {
    id: root

    required property var modelData
    required property int index

    Accessible.name: modelData.type || qsTr("追踪节点")
    Accessible.description: modelData.label || ""

    // ── Process visibility (page-level toggle) ─────────
    readonly property bool intermediate: modelData.type === "thinking"
        || modelData.type === "tool_call"
        || modelData.type === "tool_result"
        || modelData.type === "context"
    // A pending confirmation must stay visible even with process hidden,
    // otherwise its approve/decline buttons would be unreachable.
    readonly property bool processHidden: intermediate
        && !traceListView.showProcess && modelData.status !== "waiting_confirm"

    // ── Detail block (args / result) ───────────────────
    readonly property string detailText: (modelData.args || "") !== ""
        ? modelData.args : (modelData.result || "")
    readonly property bool hasDetail: detailText !== "" && modelData.type !== "summary"
    readonly property string detailKey: (modelData.id || "") !== ""
        ? modelData.id
        // Nodes without an id (streaming intermediates) fall back to a
        // content-derived key; the index keeps identical-looking nodes
        // (e.g. repeated tool calls with the same first 48 chars) from
        // sharing one fold state.
        : modelData.type + ":" + index + ":" + detailText.substring(0, 48)
    // Short details start open; walls of tool output start collapsed.
    readonly property bool detailDefault: detailText.length <= 400
    readonly property bool detailExpanded: hasDetail
        && traceListView.detailExpanded(detailKey, detailDefault)

    // ── Layout ──────────────────────────────────────────
    implicitHeight: contentColumn.implicitHeight + 20
    height: processHidden ? 0 : implicitHeight
    visible: !processHidden

    // Determine node colors based on type
    readonly property color nodeColor: {
        var t = modelData.type
        if (t === "thinking") return Theme.brandPrimary
        if (t === "tool_call") return Theme.statusWarning
        if (t === "tool_result") return Theme.statusSuccess
        if (t === "summary") return Theme.brandPrimary
        if (t === "error") return Theme.statusDanger
        if (t === "user_message") return Theme.textSecondary
        return Theme.textSecondary
    }

    // ── Timeline Axis ───────────────────────────────────
    Rectangle {
        id: timelineAxis
        width: 2
        height: parent.height
        x: 11
        color: Theme.borderSubtle
    }

    // ── Status Dot ──────────────────────────────────────
    Rectangle {
        id: statusDot
        width: 12
        height: 12
        radius: 6
        x: 6
        y: 4
        color: Theme.bgSecondary
        border.color: root.nodeColor
        border.width: 2

        // Breathing animation when running. Drives an intermediate property
        // instead of opacity itself: `Animation on <property>` destroys the
        // declarative binding and leaves the value stranded at the last frame
        // when the animation stops, so the dot would stay dimmed forever.
        property real pulse: 1.0
        SequentialAnimation {
            running: modelData.status === "running"
            loops: Animation.Infinite
            NumberAnimation { target: statusDot; property: "pulse"; from: 1.0; to: 0.3; duration: 800 }
            NumberAnimation { target: statusDot; property: "pulse"; from: 0.3; to: 1.0; duration: 800 }
        }
        opacity: modelData.status === "running" ? pulse : 1.0

        // Glow effect for running state (dark mode only). Same pattern as
        // the dot above: the animation targets `pulse`, and Theme.isDark only
        // gates the opacity binding so a mid-run theme switch cannot strand
        // the glow at a non-zero opacity.
        Rectangle {
            id: statusGlow
            anchors.centerIn: parent
            width: parent.width + 8
            height: parent.height + 8
            radius: width / 2
            color: root.nodeColor
            z: -1

            property real pulse: 0.3
            SequentialAnimation {
                running: modelData.status === "running"
                loops: Animation.Infinite
                NumberAnimation { target: statusGlow; property: "pulse"; from: 0.3; to: 0.05; duration: 800 }
                NumberAnimation { target: statusGlow; property: "pulse"; from: 0.05; to: 0.3; duration: 800 }
            }
            opacity: modelData.status === "running" && Theme.isDark ? pulse : 0.0
        }
    }

    // ── Content Area ────────────────────────────────────
    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.leftMargin: 28
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 2
        spacing: 6

        // Node header: type caption + collapse chevron. Clicking anywhere on
        // it toggles the detail block below (when the node has one).
        Item {
            Layout.fillWidth: true
            height: headerColumn.implicitHeight

            Column {
                id: headerColumn
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 2

                Row {
                    id: headerRow
                    spacing: 6

                    // Node type label (small caption)
                    Text {
                        text: {
                            if (modelData === undefined || modelData === null)
                                return "!! MODEL_DATA_UNDEFINED !!"
                            var t = modelData.type
                            if (t === "thinking") return "THINKING"
                            if (t === "tool_call") return "TOOL CALL"
                            if (t === "tool_result") return "TOOL RESULT"
                            if (t === "summary") return "RESPONSE"
                            if (t === "error") return "ERROR"
                            if (t === "user_message") return "YOU"
                            if (t === "context") return "CONTEXT"
                            return t.toUpperCase()
                        }
                        color: modelData.type === "error"
                            ? Theme.statusDanger : Theme.textSecondary
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }

                    AppIcon {
                        visible: root.hasDetail
                        anchors.verticalCenter: parent.verticalCenter
                        icon: root.detailExpanded ? "chevron-up" : "chevron-down"
                        size: 11
                        iconColor: headerMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                    }
                }

                // Collapsed hint for detail content
                Text {
                    visible: root.hasDetail && !root.detailExpanded
                    text: qsTr("已折叠 · %1 字符，点击展开").arg(root.detailText.length)
                    color: Theme.textSecondary
                    opacity: 0.7
                    font.family: Theme.fontMono
                    font.pixelSize: 9
                }
            }

            MouseArea {
                id: headerMA
                anchors.fill: parent
                anchors.margins: -4
                enabled: root.hasDetail
                hoverEnabled: true
                cursorShape: root.hasDetail ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: traceListView.toggleDetail(root.detailKey, root.detailDefault)
            }
        }

        // Title (plain text for non-summary nodes)
        Text {
            visible: modelData.type !== "summary"
            Layout.fillWidth: true
            text: modelData.title
            color: modelData.type === "error" ? Theme.statusDanger : Theme.textPrimary
            font.family: modelData.type === "user_message" ? Theme.fontSans : Theme.fontMono
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            renderType: Text.NativeRendering
        }

        // Attached images & text files (Batch 30, F6)
        Row {
            visible: modelData.type === "user_message"
                     && (modelData.images !== undefined
                         || modelData.files !== undefined)
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: modelData.images !== undefined ? modelData.images : []

                delegate: Rectangle {
                    width: 96
                    height: 72
                    radius: 6
                    color: Theme.surfaceHover
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: modelData
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
            }

            Repeater {
                model: modelData.files !== undefined ? modelData.files : []

                delegate: Rectangle {
                    width: fileChip.width + 16
                    height: 28
                    radius: 4
                    color: Theme.surfaceHover

                    Row {
                        id: fileChip
                        anchors.centerIn: parent
                        spacing: 4

                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "export"
                            size: 12
                            iconColor: Theme.textSecondary
                        }

                        Text {
                            text: modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontSans
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }

        // Markdown-rendered response (summary nodes) with copy button
        Item {
            visible: modelData.type === "summary"
            Layout.fillWidth: true
            implicitHeight: mdText.implicitHeight

            Text {
                id: mdText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: 56
                text: modelData.title
                textFormat: Text.MarkdownText
                color: Theme.textPrimary
                linkColor: Theme.brandPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.Wrap
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }

            CopyButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: -4
                text: modelData.title
            }
        }

        // Tool call arguments (shown for tool_call type)
        CodeBlock {
            Layout.fillWidth: true
            visible: modelData.type === "tool_call" && (modelData.args || "") !== ""
                     && root.detailExpanded
            text: modelData.args
        }

        // Tool result output
        CodeBlock {
            Layout.fillWidth: true
            visible: modelData.type !== "tool_call" && modelData.type !== "summary"
                     && (modelData.result || "") !== "" && root.detailExpanded
            text: modelData.result
            textColor: Theme.statusSuccess
        }

        // Confirmation buttons (for waiting_confirm status)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: modelData.status === "waiting_confirm"

            AppButton {
                text: qsTr("拒绝")
                buttonColor: "transparent"
                textColor: Theme.statusDanger
                border.color: Theme.statusDanger
                onClicked: AgentVM.approveToolCall(modelData.id, false)
            }

            AppButton {
                text: qsTr("允许")
                highlighted: true
                onClicked: AgentVM.approveToolCall(modelData.id, true)
            }
        }

        // Status indicator text
        Text {
            visible: modelData.status === "running"
            text: "▌"
            color: Theme.brandPrimary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSizeBody

            SequentialAnimation on opacity {
                running: modelData.status === "running"
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.0; duration: 500 }
                NumberAnimation { from: 0.0; to: 1.0; duration: 500 }
            }
        }
    }
}
