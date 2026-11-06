pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 模型配置页：档案条 + 当前 profile 的全部字段。
//
// 布局是「页头 / 档案条 / 分组卡片」：档案条常驻在滚动区外，芯片切换与
// 启用/复制/删除随时可达，不用滚到长列末尾才找到主操作；不再设状态底栏，
// 保存结果由设置中心的变更回执承载。本页走 SettingsService 即时保存，
// 不受对话框底栏影响。
Item {
    id: page
    clip: true

    Accessible.name: qsTr("模型配置页")

    property string editingId: Settings.activeProfileId
    property var editing: {
        const list = Settings.profiles
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === editingId) return list[i]
        return list.length > 0 ? list[0] : null
    }

    // 添加模型与「添加」按钮共用同一提交条件：有可编辑配置且输入非空。
    readonly property bool canAddModel: page.editing !== null
        && addModelField.text.trim().length > 0

    property bool testing: false
    property string testResult: ""
    property bool testOk: false

    // 「从端点拉取模型」的独立状态机：与「测试连接」互不影响（列举失败
    // 不代表聊天不可用，反之亦然）。
    property bool fetchingModels: false
    property string fetchModelsResult: ""
    property bool fetchModelsOk: false

    // 连接测试用哪个模型：默认当前模型，用户可从本配置的模型列表里换一个
    // （典型场景：刚加进来的新模型名还不确定，想单独验证它能不能通，又不想把
    // 当前模型改掉）。列表为空时按钮直接禁用，见下方说明文案。
    readonly property var testModels: page.editing !== null
        ? (page.editing.models || []).filter((m) => String(m).trim() !== "") : []
    property string testModel: ""
    // 用户是否在下拉里亲手选过（选过就不再跟着当前模型走）；下面两个属性记录
    // 上次同步落哪条配置上，换配置时重置。
    property bool testModelPinned: false
    property string testModelProfileId: ""

    // 档位取十进制，和协议能力表同源（gpt-4o 的窗口是 128000 而不是 131072）。
    // 按 1024 进制取整的话，点「128K」写进 profile 的值既与模型真实窗口不符，
    // 也不会让芯片亮起来（比较的是 131072）。
    readonly property var contextPresets: [
        { v: 128000,  label: "128K" },
        { v: 200000,  label: "200K" },
        { v: 256000,  label: "256K" },
        { v: 1000000, label: "1M" }
    ]
    readonly property var outputPresets: [
        { v: 32000,  label: "32K" },
        { v: 64000,  label: "64K" },
        { v: 128000, label: "128K" },
        { v: 256000, label: "256K" }
    ]
    // 来源（kind）：厂商芯片与「+ 新建」共用这一张表。custom 是一条不预填端点与
    // 模型的 OpenAI 兼容配置，不是第五种线协议。
    readonly property var kindOptions: [
        { kind: "openai",    label: qsTr("OpenAI 兼容") },
        { kind: "anthropic", label: qsTr("Claude") },
        // 任务 #24（批次四）：Gemini 原生 GenerateContent 协议。协议/端点/
        // 模型预设来自注册表（endpointOptions 动态驱动），这里只补来源项。
        { kind: "gemini",    label: qsTr("Gemini") },
        { kind: "ollama",    label: qsTr("Ollama 本地") },
        { kind: "custom",    label: qsTr("自定义网关") }
    ]
    // 下拉选项只写单位与档位名，不加解释（解释在字段自身的灰色 placeholder 或
    // 卡片副标题里）。
    readonly property var effortPresets: [
        { v: "off",      label: qsTr("关闭") },
        { v: "low",      label: qsTr("低") },
        { v: "medium",   label: qsTr("中") },
        { v: "high",     label: qsTr("高") }
    ]

    readonly property string editingKind: page.editing !== null
        ? (page.editing.kind || page.editing.provider) : ""
    readonly property bool isCustom: page.editingKind === "custom"
    readonly property bool isOllama: page.editingKind === "ollama"
    readonly property string editingEffort: page.editing !== null
        ? (page.editing.reasoningEffort || "") : ""

    // 同一个厂商可以有多条协议（OpenAI 兼容 = Chat Completions / Responses）。
    readonly property var protocolOptions: page.editing !== null
        ? Settings.endpointOptions(page.editing.provider) : []

    // 下面三个只是输入框提示，不写进 profile；自定义必须给与厂商无关的中性提示。
    readonly property string baseUrlPlaceholder: {
        if (page.isCustom)
            return qsTr("例如 https://my-gateway.example.com/v1")
        if (page.editingKind === "anthropic")
            return "https://api.anthropic.com"
        if (page.editingKind === "gemini")
            return "https://generativelanguage.googleapis.com"
        if (page.isOllama)
            return "http://localhost:11434"
        return "https://api.openai.com/v1"
    }
    readonly property string modelPlaceholder: {
        if (page.isCustom)
            return qsTr("网关上真实在跑的模型名，例如 glm-4")
        if (page.editingKind === "anthropic")
            return "claude-sonnet-4-5"
        if (page.editingKind === "gemini")
            return "gemini-2.5-flash"
        if (page.isOllama)
            return "qwen2.5:7b"
        return "gpt-4o-mini"
    }
    readonly property string apiKeyEnvVar: {
        if (page.editingKind === "anthropic")
            return "ANTHROPIC_API_KEY"
        if (page.editingKind === "gemini")
            return "GEMINI_API_KEY"
        return "OPENAI_API_KEY"
    }
    readonly property string apiKeyPlaceholder: page.isCustom
        ? qsTr("按网关要求填写（可留空走环境变量）")
        : qsTr("或留空，读环境变量 %1").arg(page.apiKeyEnvVar)

    // 缺哪项就说什么：自定义没有默认值可退，空配置启用后只会在请求期失败。
    readonly property string missingFields: {
        if (page.editing === null) return ""
        const missing = []
        if (String(page.editing.baseUrl || "").trim() === "") missing.push(qsTr("服务地址"))
        if (String(page.editing.model || "").trim() === "") missing.push(qsTr("模型名"))
        return missing.join("、")
    }
    readonly property bool ready: page.editing !== null && page.missingFields === ""

    // 上下文预算的生效值与来源，四级链（docs/43 §4.2）：模型覆盖
    // （modelParams[当前模型].contextLength）> 默认（profile.contextLength，
    // 即下方「上下文预算」行）> 模型能力表 > 全局默认。芯片比较的是这个
    // 生效值，所以留空时也能看出「当前走的是能力表 128K」。
    readonly property int perModelContext: {
        if (page.editing === null) return 0
        const params = page.editing.modelParams
        if (params === undefined || params === null) return 0
        const entry = params[String(page.editing.model || "")]
        if (entry === undefined || entry === null) return 0
        const n = parseInt(entry.contextLength) || 0
        return n > 0 ? n : 0
    }
    readonly property int manualContext: parseInt(contextLengthRow.input.text) || 0
    readonly property int tableContext: page.editing !== null
        ? Settings.modelContextWindow(page.editing.apiProtocol,
                                      String(page.editing.model || "")) : 0
    readonly property int effectiveContext: page.perModelContext > 0
        ? page.perModelContext
        : (page.manualContext > 0 ? page.manualContext : page.tableContext)
    readonly property string contextSource: page.perModelContext > 0 ? qsTr("模型覆盖")
        : (page.manualContext > 0 ? qsTr("默认（所有模型）")
        : (page.tableContext > 0 ? qsTr("模型能力表") : qsTr("全局默认")))

    function setField(field, value) {
        // 正在敲的字段不回写：聊天侧切模型、别处加模型都会触发 profilesChanged，
        // 无条件同步会把用户还没提交的输入冲掉。
        if (field === undefined || field === null || field.activeFocus) return
        field.text = (value === undefined || value === null) ? "" : String(value)
    }

    // 同步字段文本并刷新「已知值」基线：SettingsField 销毁时只在文本与基线不同
    // 才补一次提交，基线不跟着走的话页面每次重建都会把原值再写一遍。
    function setRow(row, value) {
        if (row === undefined || row === null) return
        const text = (value === undefined || value === null) ? "" : String(value)
        page.setField(row.input, text)
        if (!row.input.activeFocus) row.noteValue(text)
    }

    function syncFields() {
        const p = page.editing
        page.setRow(nameRow, p ? p.displayName : "")
        // B1-1：密钥永不回显。profiles() 已不含 apiKey，这里也显式置空，
        // 避免将来有人把明文重新接进来。
        page.setRow(keyRow, "")
        page.setRow(urlRow, p ? p.baseUrl : "")
        page.setRow(modelRow, p ? p.model : "")
        page.setRow(tempRow, p ? p.temperature : "")
        page.syncTestModel()
    }

    // 测试用模型：默认当前模型；不在了或刚换了配置就重新推断。
    function syncTestModel() {
        const p = page.editing
        const models = page.testModels
        if (p === null || models.length === 0) {
            page.testModel = ""
            page.testModelProfileId = ""
            page.testModelPinned = false
            return
        }
        if (page.testModelProfileId !== p.id) {
            page.testModelProfileId = p.id
            page.testModelPinned = false
        }
        if (page.testModelPinned && models.indexOf(page.testModel) >= 0)
            return
        page.testModelPinned = false
        const current = String(p.model || "")
        page.testModel = models.indexOf(current) >= 0 ? current : models[0]
    }

    function submitField(key, value) {
        if (page.editing === null) return
        // 模型一致性（当前模型必须在列表里、换端点后重算整份列表）都由
        // SettingsService.updateProfile 负责，这里不再补第二次写入：两次写会多一
        // 次 profilesChanged，编辑器会把刚输入的字段再同步一遍。
        Settings.updateProfile(page.editing.id, { [key]: value })
    }

    // 输入框回车与「添加」按钮共用的提交入口：两处走同一段 trim / 清空
    // 逻辑，避免一条路径漏了校验把空模型或半截名字写进列表。
    function commitAddModel() {
        if (page.editing === null) return
        const name = addModelField.text.trim()
        if (name.length === 0) return
        Settings.addModelToProfile(page.editing.id, name)
        addModelField.text = ""
    }

    // 按模型参数的覆盖计数（docs/43 §6.2 第 3 条）：chips 徽章与编辑器的
    // ComboBox 条目共用同一语义——modelParams[model] 的显式键数。
    function modelOverrideCount(model) {
        if (page.editing === null) return 0
        const params = page.editing.modelParams
        if (params === undefined || params === null) return 0
        const entry = params[String(model)]
        if (entry === undefined || entry === null) return 0
        return Object.keys(entry).length
    }

    onEditingChanged: page.syncFields()
    Component.onCompleted: page.syncFields()

    Connections {
        target: Settings
        function onProfilesChanged() { page.syncFields() }
    }

    Connections {
        target: ConnectionTester
        function onTestFinished(ok, message) {
            page.testing = false
            page.testOk = ok
            page.testResult = message
        }

        function onModelsFetched(ok, models, message) {
            page.fetchingModels = false
            page.fetchModelsOk = ok
            if (!ok) {
                page.fetchModelsResult = message
                return
            }
            const id = page.editing !== null ? page.editing.id : ""
            const known = page.editing !== null ? (page.editing.models || []) : []
            let fresh = 0
            for (let i = 0; i < models.length; ++i) {
                const name = String(models[i])
                if (name.length === 0 || known.indexOf(name) >= 0) continue
                fresh += 1
            }
            // 拉取只负责把候选摆出来，不再替用户全量落库：同一端点常常列出
            // 几十个模型，直接灌进列表会把聊天选择器淹掉。
            if (fresh === 0) {
                page.fetchModelsResult = qsTr("共 %1 个模型，全部已在列表中。")
                    .arg(models.length)
                return
            }
            page.fetchModelsResult = qsTr("共 %1 个模型，其中 %2 个是新的。")
                .arg(models.length).arg(fresh)
            if (id.length > 0)
                fetchedModelsDialog.openForProfile(id, models, known)
        }
    }

    // 页头 / 档案条 / 滚动区三块用锚定定位，而不是塞进一个 ColumnLayout：
    // ScrollView 进了布局就会用隐式尺寸参与分配，而它的隐式宽度又由 contentWidth ←
    // availableWidth ← width 反馈而来，每一轮 pass 都把自己缩得更窄（实测 96 → 68）。
    SettingsPageHeader {
        id: pageHeader
        anchors.top: page.top
        anchors.left: page.left
        anchors.right: page.right
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        anchors.topMargin: Theme.spaceMd
        title: qsTr("模型服务配置")
        description: qsTr("点芯片切换要编辑的配置，拖动芯片可调整顺序；改动即时写入配置，不需要另点保存。")
    }

    // ── 档案条：切换 / 新建 / 拖拽排序配置 + 主操作 ─────
    // profile 芯片从「配置来源」卡里提出来常驻在此：它们是本页的主导航
    // （切换编辑对象），与任何一张卡的字段都不是一件事；放在滚动区外，
    // 内容滚多远都还能换配置。拖拽排序的 DropArea 也随之离开 ScrollView，
    // 不再与滚动手势抢事件。
    // 启用/复制/删除同样上移到条右端：它们都作用于当前编辑的配置，跟
    // 芯片同属「管这批配置」的语义层，原来沉在底栏要视线上下两头跑。
    Rectangle {
        id: profileBar
        anchors.top: pageHeader.bottom
        anchors.left: page.left
        anchors.right: page.right
        height: profileBarRow.implicitHeight + Theme.spaceMd * 2
        color: "transparent"

        RowLayout {
            id: profileBarRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spaceXl
            anchors.rightMargin: Theme.spaceXl
            spacing: Theme.spaceMd

            Flow {
                id: profileBarFlow
                // 拖动失败（没落在芯片上）时芯片会停在拖走的位置；改一下
                // 间距触发 Flow 重新排布。用独立的 tick 而不是直接写
                // spacing，避免破坏 Theme 绑定，也避免累积漂移。
                property int reflowTick: 0
                Layout.fillWidth: true
                spacing: Theme.spaceSm + profileBarFlow.reflowTick * 0.01

                Repeater {
                    model: Settings.profiles

                    delegate: Rectangle {
                        id: profileChip
                        required property var modelData
                        required property int index
                        readonly property bool isActive: modelData.id === Settings.activeProfileId
                        readonly property bool isEditing: modelData.id === page.editingId

                        width: chipRow.implicitWidth + 24
                        height: 30
                        radius: 15
                        color: profileChip.isActive
                            ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                      Theme.brandPrimary.b, 0.15)
                            : Theme.bgPrimary
                        border.width: profileChip.isEditing ? 2 : 1
                        border.color: profileChip.isEditing
                            ? Theme.brandPrimary : Theme.borderSubtle
                        z: chipDrag.drag.active ? 2 : 0
                        opacity: chipDrag.drag.active ? 0.75 : 1

                        Accessible.role: Accessible.Button
                        Accessible.name: chipText.text

                        // docs/31 S4.4（4a）：拖拽芯片调整顺序，落盘由
                        // moveProfile 负责；点击仍只是切换编辑对象
                        // （拖动时不触发）。
                        Drag.active: chipDrag.drag.active
                        Drag.source: profileChip
                        Drag.hotSpot: Qt.point(profileChip.width / 2,
                                               profileChip.height / 2)

                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: Theme.spaceXs

                            Rectangle {
                                visible: profileChip.isActive
                                width: 6; height: 6; radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.brandPrimary
                            }

                            Text {
                                id: chipText
                                // 同一厂商的两条协议光看厂商名分不出来，带上协议标签。
                                text: {
                                    const name = profileChip.modelData.displayName
                                        || profileChip.modelData.provider
                                    const opts = Settings.endpointOptions(
                                        profileChip.modelData.provider)
                                    if (opts.length < 2) return name
                                    for (let i = 0; i < opts.length; ++i)
                                        if (opts[i].apiProtocol
                                                === profileChip.modelData.apiProtocol)
                                            return name + " · " + opts[i].label
                                    return name
                                }
                                color: profileChip.isActive
                                    ? Theme.brandPrimary : Theme.textPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontSans
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: chipDrag
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            drag.target: profileChip
                            drag.axis: Drag.XAndYAxis
                            property bool dragMoved: false
                            onPressed: dragMoved = false
                            onPositionChanged: {
                                if (drag.active) dragMoved = true
                            }
                            onReleased: {
                                // 成功落点会重建 delegate；未命中时把拖走的
                                // 芯片交回 Flow 重新排布。
                                if (dragMoved)
                                    profileBarFlow.reflowTick = 1 - profileBarFlow.reflowTick
                                dragMoved = false
                            }
                            onClicked: page.editingId = profileChip.modelData.id
                        }

                        DropArea {
                            anchors.fill: parent
                            // 拖动中的芯片跟着光标走，它自己的 DropArea 会
                            // 一直盖在目标上；禁用后落点才会交给下面的芯片。
                            enabled: !profileChip.Drag.active
                            onDropped: (drop) => {
                                const source = drop.source
                                if (source === null || source === undefined
                                        || source.index === undefined
                                        || source.index === profileChip.index)
                                    return
                                // 重排会重建 Repeater delegate；先取值再延后
                                // 一拍，不在 DropArea 的事件栈里销毁当前芯片。
                                const fromIndex = source.index
                                const toIndex = profileChip.index
                                Qt.callLater(() => Settings.moveProfile(
                                                   fromIndex, toIndex))
                            }
                        }
                    }
                }

                Rectangle {
                    width: addChipText.implicitWidth + 24
                    height: 30
                    radius: 15
                    color: addChipMA.containsMouse ? Theme.surfaceHover : "transparent"
                    border.width: 1
                    border.color: addChipMA.containsMouse
                        ? Theme.brandPrimary : Theme.borderSubtle

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("新建模型配置")

                    Text {
                        id: addChipText
                        anchors.centerIn: parent
                        text: qsTr("+ 新建")
                        color: addChipMA.containsMouse
                            ? Theme.brandPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    MouseArea {
                        id: addChipMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: addDialog.open()
                    }
                }
            }

            // 主操作组：跟随当前编辑的配置；空列表时连同空态一起隐藏。
            RowLayout {
                id: barActions
                visible: page.editing !== null
                spacing: Theme.spaceSm

                AppButton {
                    text: page.editing !== null
                          && page.editing.id === Settings.activeProfileId
                          ? qsTr("✓ 使用中") : qsTr("启用此配置")
                    highlighted: enabled
                    enabled: page.ready && page.editing !== null
                             && page.editing.id !== Settings.activeProfileId
                    onClicked: Settings.setActiveProfile(page.editing.id)
                }

                AppButton {
                    text: qsTr("复制")
                    // docs/31 S4.4（4a）：复制除 API Key 外的全部字段；新配置
                    // 立即选中以便先补密钥，启用关系保持不变。
                    enabled: page.editing !== null
                    onClicked: {
                        if (page.editing === null) return
                        const source = page.editing
                        const base = source.displayName || source.provider
                        const id = Settings.duplicateProfile(
                            source.id, qsTr("%1 副本").arg(base))
                        if (id !== "") page.editingId = id
                    }
                }

                AppButton {
                    text: qsTr("删除")
                    buttonColor: "transparent"
                    textColor: Theme.statusDanger
                    border.color: Theme.borderSubtle
                    enabled: page.editing !== null && Settings.profiles.length > 1
                    // 破坏性操作二次确认（红线 6）：删除同时清掉该配置的加密
                    // 密钥且没有撤销窗口，绝不直接执行。
                    onClicked: deleteDialog.open()
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.borderSubtle
        }
    }

    // ── 空状态：一条配置都没有时给一条明确的路 ─────────
    // 原来此时滚动区整个隐藏，只剩底栏一行小字；现在给居中的引导，
    // 直接复用「选来源 → 新建」的 addDialog 流程。
    Item {
        id: emptyState
        anchors.top: profileBar.bottom
        anchors.bottom: page.bottom
        anchors.left: page.left
        anchors.right: page.right
        visible: page.editing === null

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.spaceMd
            width: Math.min(360, emptyState.width - Theme.spaceXl * 2)

            Text {
                Layout.fillWidth: true
                text: qsTr("还没有模型配置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("新建一条配置，填上服务地址与模型名，聊天区就能开始工作。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            AppButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("新建第一个配置")
                highlighted: true
                onClicked: addDialog.open()
            }
        }
    }

    ScrollView {
        id: scroller
        anchors.top: profileBar.bottom
        anchors.left: page.left
        anchors.right: page.right
        anchors.bottom: page.bottom
        clip: true
        visible: page.editing !== null
        leftPadding: Theme.spaceXl
        rightPadding: Theme.spaceXl
        topPadding: Theme.spaceSm
        bottomPadding: Theme.spaceMd
        contentWidth: availableWidth
        contentHeight: pageContent.implicitHeight
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: pageContent
            width: scroller.availableWidth
            spacing: Theme.spaceMd

            // ── 卡片一：配置来源 ─────────────────────────
            // （profile 芯片已上移到页头下的常驻档案条；本卡只剩
            // 「这条配置是谁」的三个字段。）
            SettingCard {
                objectName: "model.profile"
                Layout.fillWidth: true
                title: qsTr("配置来源")
                subtitle: qsTr("厂商决定默认地址，接口协议决定报文格式；模型由你自己填。")

                SettingsField {
                    id: nameRow
                    label: qsTr("显示名称")
                    placeholderText: qsTr("例如：工作用 GPT、本地 qwen…")
                    // 切换设置分类会销毁本页且不触发 editingFinished，编辑中的值
                    // 原本静默丢失（docs/20 §3.6 同类缺陷）。
                    commitOnDestroy: true
                    onSubmitted: (value) => page.submitField("displayName", value)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Repeater {
                        model: page.kindOptions

                        delegate: AppButton {
                            required property var modelData
                            text: modelData.label
                            highlighted: page.editing !== null
                                && page.editingKind === modelData.kind
                            onClicked: page.submitField("kind", modelData.kind)
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    visible: page.isCustom
                    Layout.fillWidth: true
                    text: qsTr("自定义不预填服务地址与模型名，也不带入厂商预设模型；请求仍按 OpenAI Chat Completions 报文发出。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    visible: page.protocolOptions.length > 1
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    Text {
                        text: qsTr("接口协议")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.bold: true
                        font.family: Theme.fontMono
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Repeater {
                            model: page.protocolOptions

                            delegate: AppButton {
                                required property var modelData
                                text: modelData.label
                                highlighted: page.editing !== null
                                    && page.editing.apiProtocol === modelData.apiProtocol
                                onClicked: page.submitField("apiProtocol",
                                                             modelData.apiProtocol)
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        visible: page.editing !== null
                            && page.editing.apiProtocol === "openai-responses"
                        Layout.fillWidth: true
                        text: qsTr("Responses 需要端点显式实现 /responses；多数「OpenAI 兼容」网关只提供 Chat Completions。")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ── 卡片二：服务连接 ─────────────────────────
            SettingCard {
                objectName: "model.connection"
                Layout.fillWidth: true
                title: qsTr("服务连接")
                subtitle: page.isOllama
                    ? qsTr("本地服务不需要 API Key；地址指向 Ollama 的 host:port。")
                    : qsTr("API Key 保存时按当前用户加密（Windows DPAPI）。")

                SettingsField {
                    id: urlRow
                    label: qsTr("服务地址")
                    placeholderText: page.baseUrlPlaceholder
                    commitOnDestroy: true
                    onSubmitted: (value) => page.submitField("baseUrl", value.trim())
                }

                SettingsField {
                    id: keyRow
                    visible: !page.isOllama
                    label: qsTr("API Key")
                    secret: true
                    // 只暴露「是否已配置」：输入框永远为空，真值只留在 C++。
                    statusText: page.editing !== null && page.editing.hasApiKey === true
                        ? qsTr("已配置") : qsTr("未配置")
                    statusOk: page.editing !== null && page.editing.hasApiKey === true
                    placeholderText: page.apiKeyPlaceholder
                    // 填了 Key 又直接切分类/关设置最容易踩："填了却没保存"最难
                    // 察觉，这里补一次销毁提交（commitIfDirty 读 text 而非掩码）。
                    commitOnDestroy: true
                    onSubmitted: (value) => page.submitField("apiKey", value.trim())
                }

                Text {
                    visible: !page.isOllama
                    Layout.fillWidth: true
                    text: page.editing !== null && page.editing.hasApiKey === true
                        ? qsTr("密钥已配置（不回显）；留空保存会保留原密钥，输入新值才替换。")
                        : qsTr("密钥为空：请求前会先读环境变量 %1。").arg(page.apiKeyEnvVar)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    AppButton {
                        text: qsTr("测试连接")
                        // 模型列表为空时无从选起：与其用空模型名去撞 404，不如直接禁用
                        // 并把原因写在旁边（下面那行）。
                        enabled: !page.testing && page.ready
                                 && page.testModels.length > 0
                        onClicked: {
                            page.testing = true
                            page.testResult = ""
                            // B1-1：只传 profileId，密钥由 C++ 内部取。
                            ConnectionTester.testProfile(page.editing.id,
                                                         page.testModel)
                        }
                    }

                    ComboBox {
                        id: testModelCombo
                        visible: page.testModels.length > 0
                        Layout.preferredWidth: Theme.controlWidthMd
                        model: page.testModels
                        currentIndex: page.testModels.indexOf(page.testModel)
                        Accessible.name: qsTr("测试用模型")
                        onActivated: (index) => {
                            page.testModel = currentText
                            page.testModelPinned = true
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    Layout.fillWidth: true
                    text: page.testModels.length > 0
                        ? qsTr("测试用模型只影响本次请求，不会改写当前模型。")
                        : qsTr("模型列表为空，没东西可测：先在上方「模型」卡片里添加一个模型名。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                StatusLine {
                    // 整段显示：单行截断会把 401 / 路径 404 这类根因吃掉半句。
                    visible: page.testing || page.testResult !== ""
                    text: page.testing ? qsTr("正在连接（最多等 20 秒）…") : page.testResult
                    state: page.testing ? "busy" : (page.testOk ? "success" : "danger")
                    mono: !page.testing && !page.testOk
                }
            }

            // ── 卡片三：模型 ─────────────────────────────
            SettingCard {
                objectName: "model.models"
                Layout.fillWidth: true
                title: qsTr("模型")
                subtitle: qsTr("模型名不再按厂商预填：可以自己添加，也可以从端点拉取后勾选要添加的模型，列表会同步到聊天输入区的选择器。")

                SettingsField {
                    id: modelRow
                    label: qsTr("当前模型")
                    placeholderText: page.modelPlaceholder
                    // 唯一显式退出防抖提交的字段：每次 submitted 都会被
                    // SettingsService 并进 models 列表，停顿提交会把 "g"/"gp"
                    // 这类半截名字灌进列表。保持回车/失焦 + 销毁兜底。
                    commitDebounce: 0
                    commitOnDestroy: true
                    onSubmitted: (value) => page.submitField("model", value)
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    Repeater {
                        model: page.editing !== null ? (page.editing.models || []) : []

                        delegate: Rectangle {
                            id: modelChip
                            required property var modelData
                            readonly property bool isCurrent: page.editing !== null
                                && modelData === page.editing.model

                            width: modelChipRow.implicitWidth + 22
                            height: 26
                            radius: Theme.radiusField
                            color: modelChip.isCurrent
                                ? Qt.rgba(Theme.brandPrimary.r, Theme.brandPrimary.g,
                                          Theme.brandPrimary.b, 0.15)
                                : Theme.bgPrimary
                            border.width: 1
                            border.color: modelChip.isCurrent
                                ? Theme.brandPrimary : Theme.borderSubtle

                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("设为当前模型 %1").arg(modelData)

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Settings.selectModelForProfile(
                                    page.editing.id, modelChip.modelData)
                            }

                            Row {
                                id: modelChipRow
                                anchors.centerIn: parent
                                spacing: Theme.spaceXs

                                Text {
                                    // 模型名是用户手填数据：纯文本渲染防注入。
                                    textFormat: Text.PlainText
                                    text: modelChip.modelData
                                    color: modelChip.isCurrent
                                        ? Theme.brandPrimary : Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    visible: modelChip.modelData !== ""
                                        && page.modelOverrideCount(modelChip.modelData) > 0
                                    // docs/43 §6.2（3）：覆盖状态用纯文本徽章
                                    // 表达（不单靠颜色），N>0 才显示。
                                    text: qsTr("%1 项覆盖")
                                        .arg(page.modelOverrideCount(modelChip.modelData))
                                    color: modelChip.isCurrent
                                        ? Theme.brandPrimary : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontSans
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: {
                                        const cw = Settings.modelContextWindow(
                                            page.editing.apiProtocol, modelChip.modelData)
                                        return cw > 0 ? qsTr("%1K").arg(Math.round(cw / 1000))
                                                      : qsTr("未知")
                                    }
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.family: Theme.fontMono
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Item {
                                    width: 14
                                    height: parent.height

                                    Text {
                                        anchors.centerIn: parent
                                        text: "×"
                                        color: removeMA.containsMouse
                                            ? Theme.statusDanger : Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                    }

                                    MouseArea {
                                        id: removeMA
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Settings.removeModelFromProfile(
                                            page.editing.id, modelChip.modelData)
                                    }
                                }
                            }
                        }
                    }
                }

                // 添加模型行：输入框独占整行（原先塞在 chips 的 Flow 里，
                // 宽度被挤到 160px），回车与「添加」按钮共用 commitAddModel。
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    TextField {
                        id: addModelField
                        objectName: "model.addModelField"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        Layout.alignment: Qt.AlignVCenter
                        placeholderText: page.isCustom
                            ? qsTr("添加网关模型名…") : qsTr("添加模型…")
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary
                        selectedTextColor: Theme.selectedTextColor
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        background: Rectangle {
                            radius: Theme.radiusField
                            color: Theme.surfaceHover
                            border.width: addModelField.activeFocus ? 2 : 1
                            border.color: addModelField.activeFocus
                                ? Theme.brandPrimary : Theme.borderSubtle
                        }
                        Accessible.name: qsTr("添加模型到列表")
                        onAccepted: page.commitAddModel()
                    }

                    AppButton {
                        objectName: "model.addModelButton"
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("添加")
                        // 与回车提交同一条件：空输入 / 无可编辑配置时不可点。
                        enabled: page.canAddModel
                        onClicked: page.commitAddModel()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    AppButton {
                        objectName: "modelFetchModels"
                        text: page.fetchingModels
                            ? qsTr("正在拉取…") : qsTr("从端点拉取模型")
                        // 没有服务地址就无从列举；按钮禁用但把原因写在下面。
                        enabled: !page.fetchingModels && page.editing !== null
                                 && String(page.editing.baseUrl || "").trim() !== ""
                        onClicked: {
                            page.fetchingModels = true
                            page.fetchModelsResult = ""
                            // B1-1：同上，QML 不再接触密钥。
                            ConnectionTester.fetchModelsForProfile(page.editing.id)
                        }
                    }

                    StatusLine {
                        Layout.fillWidth: true
                        visible: page.fetchingModels || page.fetchModelsResult !== ""
                        text: page.fetchingModels
                            ? qsTr("正在向端点请求模型列表（最多等 20 秒）…")
                            : page.fetchModelsResult
                        state: page.fetchingModels ? "busy"
                             : (page.fetchModelsOk ? "success" : "danger")
                        mono: !page.fetchingModels && !page.fetchModelsOk
                    }
                }

                Text {
                    visible: page.editing !== null
                             && String(page.editing.baseUrl || "").trim() === ""
                    Layout.fillWidth: true
                    text: qsTr("先在上方「服务地址」里填端点，才能从端点拉取模型。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                Text {
                    // 空列表不是坏数据，但看起来像坏了——说清下一步做什么。
                    visible: page.editing !== null
                        && (page.editing.models || []).length === 0
                    Layout.fillWidth: true
                    text: qsTr("列表还是空的：模型名不再自动预填，在下方输入一个名字回车或点「添加」即可。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                // docs/43 §6.1/§6.2：按模型参数编辑区。chips 管列表与当前
                // 模型，这里管单个模型的参数覆盖；presets 与卡片四/五共用
                // 同一张表，提交走 Settings.setModelParam / clearModelParams
                // 专用入口（不拼整份 modelParams，防并发覆盖丢失）。
                ModelParamEditor {
                    id: perModelEditor
                    profileId: page.editing !== null ? page.editing.id : ""
                    profile: page.editing
                    contextPresets: page.contextPresets
                    outputPresets: page.outputPresets
                    effortPresets: page.effortPresets
                }
            }

            // ── 卡片四：生成参数（默认层，docs/43 §6.1）──
            // 每项一行、标签定宽：原来三列并排每列只有 170px，五个档位芯片
            // 被挤成三行，切厂商时列数还会跳变。这里的值降级为「所有模型的
            // 默认」，单个模型的独立覆盖在卡片三的按模型参数编辑区。
            SettingCard {
                objectName: "model.generation"
                Layout.fillWidth: true
                title: qsTr("默认生成参数（所有模型）")
                subtitle: qsTr("这里是全部模型共用的默认值，留空 = 服务默认不发送；单个模型的独立参数到上方「模型」卡片里按模型覆盖。")

                SettingsField {
                    id: tempRow
                    label: qsTr("temperature")
                    placeholderText: qsTr("0 - 2，留空不发送")
                    validator: DoubleValidator { locale: "C"; bottom: 0; top: 2; decimals: 2 }
                    commitOnDestroy: true
                    onSubmitted: (value) => page.submitField("temperature", value.trim())
                }

                ParamSelectRow {
                    id: outRow
                    label: qsTr("最大输出 tokens")
                    entries: page.outputPresets
                    currentValue: page.editing !== null ? (page.editing.maxOutputTokens || "") : ""
                    placeholderText: qsTr("自定义上限，留空 = 服务默认")
                    validator: IntValidator { bottom: 1 }
                    commitOnDestroy: true
                    onSubmitted: (value) => page.submitField("maxOutputTokens", value)
                }

                ColumnLayout {
                    visible: page.isOllama
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    Text {
                        text: qsTr("Ollama 的上下文窗口就是 num_ctx")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    ParamSelectRow {
                        id: numCtxRow
                        label: qsTr("num_ctx")
                        entries: page.contextPresets
                        currentValue: page.editing !== null ? (page.editing.numCtx || "") : ""
                        placeholderText: qsTr("自定义窗口，留空 = 不发送")
                        validator: IntValidator { bottom: 1 }
                        commitOnDestroy: true
                        onSubmitted: (value) => page.submitField("numCtx", value)
                    }
                }
            }

            // ── 卡片五：思考与上下文（默认层，docs/43 §6.1）──
            SettingCard {
                objectName: "model.reasoning"
                Layout.fillWidth: true
                title: qsTr("默认思考与上下文")
                subtitle: qsTr("思考强度与上下文预算在这里是全部模型共用的默认值，单个模型的独立覆盖在上方「模型」卡片里配置；聊天输入区的快捷选择框读写的是当前启用的配置。")

                ParamSelectRow {
                    id: effortRow
                    label: qsTr("思考强度")
                    entries: page.effortPresets
                    currentValue: page.editingEffort
                    allowCustom: false
                    autoLabel: qsTr("默认")
                    onSubmitted: (value) => page.submitField("reasoningEffort", value)
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("OpenAI 兼容按档位发送 reasoning_effort；Claude 折算为思考预算（输出上限要留得住预算，否则自动退回不思考）；Ollama 只有开/关，低中高均按开处理。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    ParamSelectRow {
                        id: contextLengthRow
                        // profile 级预算在新心智模型里是「默认层」（可被
                        // 模型覆盖），label 不再叫「覆盖」（docs/43 §6.1）。
                        label: qsTr("上下文预算")
                        entries: page.contextPresets
                        currentValue: page.editing !== null ? (page.editing.contextLength || "") : ""
                        placeholderText: qsTr("自定义预算，留空 = 自动")
                        validator: IntValidator { bottom: 2048; top: 1048576 }
                        commitOnDestroy: true
                        onSubmitted: (value) => page.submitField("contextLength", value)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: page.effectiveContext > 0
                            ? qsTr("当前生效 %1 tokens（来源：%2）")
                                  .arg(page.effectiveContext).arg(page.contextSource)
                            : qsTr("当前生效 %1 tokens（来源：全局默认，能力表里没有这个模型）")
                                  .arg(Settings.contextTokens)
                        color: page.tableContext > 0 || page.manualContext > 0
                            ? Theme.textSecondary : Theme.statusWarning
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        wrapMode: Text.WordWrap
                    }

                    // 不阻断，只提醒：max_output 超过上下文窗口时服务端会直接拒。
                    Text {
                        readonly property int requestedOutput: parseInt(outRow.input.text) || 0
                        visible: page.effectiveContext > 0
                            && requestedOutput >= page.effectiveContext
                        Layout.fillWidth: true
                        text: qsTr("最大输出 %1 已不小于上下文窗口 %2，请求可能被服务端拒绝。")
                                  .arg(requestedOutput).arg(page.effectiveContext)
                        color: Theme.statusWarning
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // 拉取结果不自动入库：先让用户在弹出的列表里勾选要添加的模型。
    // profileId 由 openForProfile 快照，对话框期间切换档案也不会写错配置。
    FetchedModelsDialog {
        id: fetchedModelsDialog
    }

    // 新建前先选来源：不选就只能得到一个 OpenAI 预设，这正是自定义要绕开的坑。
    Dialog {
        id: addDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 320
        height: addDialogContent.implicitHeight + Theme.spaceXl * 2
        padding: Theme.spaceXl
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: addDialogContent
            spacing: Theme.spaceSm

            Text {
                text: qsTr("新建模型配置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            Repeater {
                model: page.kindOptions

                delegate: AppButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    onClicked: {
                        addDialog.close()
                        page.editingId = Settings.addProfile(modelData.kind)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("厂商配置会带上默认地址，模型名一律留空由你自己填——预填的官方模型列表未必是你实际调用的那个。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }
        }
    }

    // 删除前二次确认：写清删的是哪条配置、密钥一并清除、不可撤销。
    // 取消不产生任何写入。
    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 380
        height: deleteDialogContent.implicitHeight + Theme.spaceXl * 2
        padding: Theme.spaceXl
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: deleteDialogContent
            spacing: Theme.spaceSm

            Text {
                text: qsTr("删除模型配置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            Text {
                Layout.fillWidth: true
                text: page.editing !== null
                    ? qsTr("将删除「%1」，该配置的 API Key 会一并清除，操作不可撤销。")
                          .arg(page.editing.displayName || page.editing.provider)
                    : ""
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("取消")
                    onClicked: deleteDialog.close()
                }

                AppButton {
                    objectName: "modelDeleteConfirmButton"
                    text: qsTr("删除")
                    buttonColor: "transparent"
                    textColor: Theme.statusDanger
                    border.color: Theme.statusDanger
                    onClicked: {
                        deleteDialog.close()
                        if (page.editing === null) return
                        Settings.removeProfile(page.editing.id)
                        page.editingId = Settings.activeProfileId
                    }
                }
            }
        }
    }
}
