pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

// 「按模型参数」编辑器（docs/43 §6.1/§6.2）：models 列表里的每个模型可
// 独立覆盖 temperature / maxOutputTokens / numCtx / reasoningEffort /
// contextLength 的任意子集，缺键 = 跟随 profile 默认（「默认 + 覆盖」两层
// 心智模型）。
//
// 数据流对齐 ModelSelector 读 Settings.profiles 的模式：宿主把编辑中的
// profile map 接进 profile 属性，覆盖值 / 徽章计数 / 生效来源全部从这份
// QML property 派生（Settings 的 profilesChanged → 宿主 editing 重算 →
// 本组件的绑定链自动刷新）；提交逐键走 Settings.setModelParam /
// clearModelParams 专用入口（空串 = 删除覆盖键，回到跟随默认），不拼整份
// modelParams 提交，避免 UI 并发拼装互相覆盖丢键（docs/43 §6.2 第 6 条）。
//
// 根用 Item 而不是 ColumnLayout：清除确认 Dialog 要与可视内容平级，挂进
// 布局根会被 qmllint 判成「受布局管理的子项设 anchors/尺寸」的未定义行为。
Item {
    id: root

    objectName: "model.perModelParams"

    // 宿主接线：profileId 供提交入口定位；profile 是编辑中的 map（models
    // / modelParams / apiProtocol 全从这里读）；preset 表由宿主传入，与
    // 卡片四/五共用同一份（单一来源，翻译 context 不漂移）。
    property string profileId: ""
    property var profile: null
    property var contextPresets: []
    property var outputPresets: []
    property var effortPresets: []

    // 正在编辑的模型：默认跟随 profile 当前模型，ComboBox 可切换；模型被
    // 删 / 列表重排时自动回退（页面 syncTestModel 同款推断逻辑）。
    property string selectedModel: ""

    readonly property var modelsList: root.profile !== null
        ? (root.profile.models || []) : []
    // num_ctx 是 Ollama 服务端的窗口参数，只有 ollama-chat 协议发送
    // （卡片四 num_ctx 行的可见条件同源，docs/43 R7：它与 Agent 侧的
    // 上下文压缩预算 contextLength 是两件事）。
    // 与卡片四的 page.isOllama（kind === "ollama"）等价：ollama 厂商在
    // 协议注册表（LlmProtocolRegistry descriptors）只有 ollama-chat 一条
    // 协议，normalizeEndpoint 保证 kind/provider/apiProtocol 成对自洽，
    // UI 协议选择行按厂商过滤且单协议时不显示——两轴在 UI 可构造的数据
    // 范围内一一对应。此处按 apiProtocol 判定，与能力表查询
    // （Settings.modelContextWindow）同源。
    readonly property bool isOllamaChat: root.profile !== null
        && root.profile.apiProtocol === "ollama-chat"

    // ComboBox 条目文本：模型名 + 覆盖计数（N=0 不带后缀——徽章只报事实，
    // 不为没配置的模型印一行「0 项覆盖」）。模型名是用户手填数据，渲染走
    // 纯文本（见下方 delegate 的 textFormat）。
    readonly property var comboEntries: {
        const out = []
        const models = root.modelsList
        for (let i = 0; i < models.length; ++i) {
            const count = root.overrideCount(models[i])
            out.push(count > 0
                ? qsTr("%1 · %2 项覆盖").arg(models[i]).arg(count)
                : models[i])
        }
        return out
    }
    readonly property int selectedOverrideCount: root.overrideCount(root.selectedModel)

    // 当前生效上下文预算的四级链（与 C++ resolveContextLimitForProfile 同
    // 序，docs/43 §4.2）：模型覆盖 > 默认（profile.contextLength）> 能力表
    // > 全局 contextTokens。
    readonly property int perModelContext: {
        const n = parseInt(root.overrideValue("contextLength")) || 0
        return n > 0 ? n : 0
    }
    readonly property int profileContext: {
        const n = parseInt(root.profileValue("contextLength")) || 0
        return n > 0 ? n : 0
    }
    // 依赖项（profile / selectedModel）都是 QML property，能力表查询本身是
    // 纯函数，依赖变化即重算（卡片五 tableContext 同款模式）。
    readonly property int tableContext: root.profile !== null
        && root.selectedModel !== ""
        ? Settings.modelContextWindow(root.profile.apiProtocol,
                                      root.selectedModel) : 0
    readonly property int effectiveContext: root.perModelContext > 0
        ? root.perModelContext
        : (root.profileContext > 0 ? root.profileContext
          : (root.tableContext > 0 ? root.tableContext : Settings.contextTokens))
    readonly property string contextSource: root.perModelContext > 0 ? qsTr("模型覆盖")
        : (root.profileContext > 0 ? qsTr("默认（所有模型）")
        : (root.tableContext > 0 ? qsTr("模型能力表") : qsTr("全局默认")))

    // 指定模型的覆盖键数（徽章 / 清除按钮 / 确认文案共用）。
    function overrideCount(model) {
        if (root.profile === null) return 0
        const params = root.profile.modelParams
        if (params === undefined || params === null) return 0
        const entry = params[String(model)]
        if (entry === undefined || entry === null) return 0
        return Object.keys(entry).length
    }

    // 当前选中模型的覆盖值；空串 = 未覆盖（跟随默认）。
    function overrideValue(key) {
        if (root.profile === null || root.selectedModel === "") return ""
        const params = root.profile.modelParams
        if (params === undefined || params === null) return ""
        const entry = params[root.selectedModel]
        if (entry === undefined || entry === null) return ""
        const value = entry[key]
        return (value === undefined || value === null) ? "" : String(value)
    }

    // profile 级默认值（「留空 = 跟随默认（当前 X）」提示用）。
    function profileValue(key) {
        if (root.profile === null) return ""
        const value = root.profile[key]
        return (value === undefined || value === null) ? "" : String(value)
    }

    // effort 档位的显示文案：默认值 high → 「高」。
    function effortLabel(value) {
        const v = String(value || "")
        for (let i = 0; i < root.effortPresets.length; ++i)
            if (String(root.effortPresets[i].v) === v) return root.effortPresets[i].label
        return v
    }

    function submitOverride(key, value) {
        if (root.profileId === "" || root.selectedModel === "") return
        // String() 统一收口：ParamSelectRow 的预设值是数字、SettingsField
        // 的是字符串，落库前都按 setModelParam 的 QString 契约转一道。
        Settings.setModelParam(root.profileId, root.selectedModel, key,
                               String(value))
    }

    // 选中要编辑的模型（ComboBox onActivated 与测试共用入口）：列表外的
    // 模型名直接忽略，避免脏值把表单切到不存在的模型上。
    function selectModel(model) {
        if (root.modelsList.indexOf(model) < 0) return
        root.selectedModel = model
    }

    function syncSelectedModel() {
        const models = root.modelsList
        if (models.length === 0) {
            root.selectedModel = ""
            return
        }
        if (models.indexOf(root.selectedModel) >= 0) return
        const current = root.profile !== null ? String(root.profile.model || "") : ""
        root.selectedModel = models.indexOf(current) >= 0 ? current : models[0]
    }

    // SettingsField 是命令式回填（text 双向绑定会跟用户输入打架，页面
    // syncFields 同款约定：正在敲的字段不回写）；ParamSelectRow 只绑
    // currentValue，其内部 refresh 自带 activeFocus 保护，无需命令式同步。
    function syncRows() {
        if (!tempRow.input.activeFocus) {
            const text = root.overrideValue("temperature")
            tempRow.text = text
            tempRow.noteValue(text)
        }
    }

    onModelsListChanged: root.syncSelectedModel()
    onSelectedModelChanged: root.syncRows()
    onProfileChanged: {
        root.syncSelectedModel()
        root.syncRows()
    }
    Component.onCompleted: {
        root.syncSelectedModel()
        root.syncRows()
    }

    // 宿主卡片 body 是 ColumnLayout：宽度由外层分配，高度上报内容高度。
    Layout.fillWidth: true
    implicitHeight: editorBody.implicitHeight

    // 可视内容收进这层布局；Dialog 平级放外层（见根注释）。
    ColumnLayout {
        id: editorBody
        anchors.fill: parent
        spacing: Theme.spaceXs

            // ── 模型选择 + 当前覆盖计数 ────────────────────
            RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Text {
                text: qsTr("按模型参数")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            ComboBox {
                id: modelCombo
                objectName: "model.paramModelCombo"
                visible: root.modelsList.length > 0
                Layout.preferredWidth: Theme.controlWidthMd
                model: root.comboEntries
                // 显式依赖 comboEntries：覆盖计数变化会让 ComboBox 重置内部
                // 索引，绑定里带上它的长度才能跟着重算回正确位置。
                currentIndex: {
                    const idx = root.modelsList.indexOf(root.selectedModel)
                    return idx >= 0 && idx < root.comboEntries.length ? idx : -1
                }
                // 收起态显示走同一条红线：默认 contentItem 的 Text 是
                // AutoText，模型名（可来自远端 fetchModelsForProfile 拉取）
                // 含富文本标记会被解析——显式 PlainText。textFormat 只能在
                // contentItem 上设（W2 踩坑：ComboBox/ItemDelegate 上直接设
                // 是 non-existent property）。
                contentItem: Text {
                    leftPadding: 12
                    rightPadding: 12
                    text: modelCombo.displayText
                    textFormat: Text.PlainText
                    color: Theme.textPrimary
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                // 模型名是用户手填数据：条目文本强制纯文本渲染，防 <b> 之类被
                // 当作富文本解析（ItemDelegate 没有 textFormat 属性，只能在
                // contentItem 上补）。
                delegate: ItemDelegate {
                    id: comboEntry
                    required property var modelData
                    required property int index
                    width: modelCombo.width
                    highlighted: modelCombo.highlightedIndex === comboEntry.index
                    contentItem: Text {
                        leftPadding: 8
                        rightPadding: 8
                        text: comboEntry.modelData
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        color: comboEntry.highlighted
                            ? Theme.brandPrimary : Theme.textPrimary
                    }
                }
                Accessible.name: qsTr("选择要编辑的模型")
                onActivated: (index) => {
                    const models = root.modelsList
                    if (index >= 0 && index < models.length)
                        root.selectModel(models[index])
                }
            }

            Text {
                visible: root.selectedOverrideCount > 0
                text: qsTr("当前 %1 项覆盖").arg(root.selectedOverrideCount)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }

            Item { Layout.fillWidth: true }
        }

            Rectangle {
                visible: root.modelsList.length > 0
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.borderSubtle
            }

        // 空态：models 为空时无从按模型配置，指路到上方的添加输入框。
        Text {
            visible: root.modelsList.length === 0
            Layout.fillWidth: true
            text: qsTr("先在上方添加至少一个模型，才能为单个模型单独配置参数。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.WordWrap
        }

        // ── 覆盖表单：留空 / 「跟随默认」= 删除覆盖键 ────────
        ColumnLayout {
            visible: root.modelsList.length > 0
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            SettingsField {
                id: tempRow
                objectName: "model.paramRowTemperature"
                // 覆盖状态写进 label 纯文本（docs/43 §6.4：不单靠颜色区分）。
                label: root.overrideValue("temperature") !== ""
                    ? qsTr("temperature（已覆盖）") : qsTr("temperature")
                placeholderText: qsTr("0 - 2，留空 = 跟随默认")
                validator: DoubleValidator { locale: "C"; bottom: 0; top: 2; decimals: 2 }
                // 防抖默认 600ms：停顿即提交（这里没有「当前模型」那种半截名字
                // 灌列表的问题）；页面销毁补交（切设置分类丢编辑的坑，同卡片四）。
                commitOnDestroy: true
                onSubmitted: (value) => root.submitOverride("temperature", value.trim())
            }

            Text {
                Layout.fillWidth: true
                text: root.profileValue("temperature") !== ""
                    ? qsTr("留空 = 跟随默认（当前 %1）")
                          .arg(root.profileValue("temperature"))
                    : qsTr("留空 = 服务默认（不发送）")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            ParamSelectRow {
                id: outRow
                objectName: "model.paramRowMaxOutput"
                label: root.overrideValue("maxOutputTokens") !== ""
                    ? qsTr("最大输出 tokens（已覆盖）") : qsTr("最大输出 tokens")
                entries: root.outputPresets
                currentValue: root.overrideValue("maxOutputTokens")
                autoLabel: qsTr("跟随默认")
                placeholderText: qsTr("自定义上限，留空 = 服务默认")
                validator: IntValidator { bottom: 1 }
                commitOnDestroy: true
                onSubmitted: (value) => root.submitOverride("maxOutputTokens", value)
            }

            Text {
                Layout.fillWidth: true
                text: root.profileValue("maxOutputTokens") !== ""
                    ? qsTr("留空 = 跟随默认（当前 %1）")
                          .arg(root.profileValue("maxOutputTokens"))
                    : qsTr("留空 = 服务默认")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                visible: root.isOllamaChat
                Layout.fillWidth: true
                spacing: Theme.spaceXs

                ParamSelectRow {
                    id: numCtxRow
                    objectName: "model.paramRowNumCtx"
                    label: root.overrideValue("numCtx") !== ""
                        ? qsTr("num_ctx（已覆盖）") : qsTr("num_ctx")
                    entries: root.contextPresets
                    currentValue: root.overrideValue("numCtx")
                    autoLabel: qsTr("跟随默认")
                    placeholderText: qsTr("自定义窗口，留空 = 不发送")
                    validator: IntValidator { bottom: 1 }
                    commitOnDestroy: true
                    onSubmitted: (value) => root.submitOverride("numCtx", value)
                }

                Text {
                    Layout.fillWidth: true
                    text: root.profileValue("numCtx") !== ""
                        ? qsTr("留空 = 跟随默认（当前 %1）")
                              .arg(root.profileValue("numCtx"))
                        : qsTr("留空 = 不发送")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontSans
                    wrapMode: Text.WordWrap
                }
            }

            ParamSelectRow {
                id: effortRow
                objectName: "model.paramRowEffort"
                label: root.overrideValue("reasoningEffort") !== ""
                    ? qsTr("思考强度（已覆盖）") : qsTr("思考强度")
                entries: root.effortPresets
                currentValue: root.overrideValue("reasoningEffort")
                // 枚举参数不存在自定义值（与卡片五 effortRow 同款）。
                allowCustom: false
                autoLabel: qsTr("跟随默认")
                onSubmitted: (value) => root.submitOverride("reasoningEffort", value)
            }

            Text {
                Layout.fillWidth: true
                text: root.profileValue("reasoningEffort") !== ""
                    ? qsTr("跟随默认（当前 %1）")
                          .arg(root.effortLabel(root.profileValue("reasoningEffort")))
                    : qsTr("跟随默认（不发送）")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            ParamSelectRow {
                id: contextRow
                objectName: "model.paramRowContext"
                label: root.overrideValue("contextLength") !== ""
                    ? qsTr("上下文预算（已覆盖）") : qsTr("上下文预算")
                entries: root.contextPresets
                currentValue: root.overrideValue("contextLength")
                autoLabel: qsTr("跟随默认")
                placeholderText: qsTr("自定义预算，留空 = 自动")
                validator: IntValidator { bottom: 2048; top: 1048576 }
                commitOnDestroy: true
                onSubmitted: (value) => root.submitOverride("contextLength", value)
            }

            // 四级生效来源（docs/43 §6.2 第 2 条）：常显当前生效值与来源，
            // 「默认层 + 模型覆盖」两层心智的困惑点（R6）靠它消解。
            Text {
                Layout.fillWidth: true
                text: root.effectiveContext > 0
                    ? qsTr("当前生效 %1 tokens（来源：%2）")
                          .arg(root.effectiveContext).arg(root.contextSource)
                    : qsTr("当前生效 %1 tokens（来源：全局默认，能力表里没有这个模型）")
                          .arg(Settings.contextTokens)
                color: root.perModelContext > 0 || root.profileContext > 0
                    || root.tableContext > 0 ? Theme.textSecondary : Theme.statusWarning
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontMono
                wrapMode: Text.WordWrap
            }
        }

        // ── 清除入口（破坏性操作，二次确认）────────────────
        RowLayout {
            visible: root.modelsList.length > 0
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            AppButton {
                id: clearButton
                objectName: "model.paramClearButton"
                text: qsTr("清除该模型全部覆盖")
                buttonColor: "transparent"
                textColor: Theme.statusDanger
                border.color: Theme.borderSubtle
                // 没有覆盖就没有可清除的东西；clearModelParams 幂等无害，但
                // 禁用态本身就在表达「当前状态」。
                enabled: root.selectedOverrideCount > 0
                onClicked: clearDialog.open()
            }
        }
    }   // editorBody（Dialog 与可视内容平级，挂在根 Item 上）

    // 二次确认（项目红线同款：写清删的是什么、影响与不可撤销性；取消
    // 零写入；Esc / 点击弹窗外关闭走 closePolicy 默认行为）。
    Dialog {
        id: clearDialog
        objectName: "model.paramClearDialog"
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 380
        height: clearDialogContent.implicitHeight + Theme.spaceXl * 2
        padding: Theme.spaceXl
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: DialogBackground {}

        contentItem: ColumnLayout {
            id: clearDialogContent
            spacing: Theme.spaceSm

            Text {
                text: qsTr("清除按模型参数")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.bold: true
                font.family: Theme.fontSans
            }

            Text {
                Layout.fillWidth: true
                // 模型名是用户数据：纯文本渲染防注入（工程约定）。
                textFormat: Text.PlainText
                text: root.selectedModel !== ""
                    ? qsTr("将删除「%1」的全部 %2 项参数覆盖，该模型回退跟随默认，操作不可撤销。")
                            .arg(root.selectedModel)
                            .arg(root.selectedOverrideCount)
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
                    onClicked: clearDialog.close()
                }

                AppButton {
                    objectName: "model.paramClearConfirmButton"
                    text: qsTr("清除")
                    buttonColor: "transparent"
                    textColor: Theme.statusDanger
                    border.color: Theme.statusDanger
                    onClicked: {
                        clearDialog.close()
                        if (root.profileId === "" || root.selectedModel === "") return
                        Settings.clearModelParams(root.profileId, root.selectedModel)
                    }
                }
            }
        }
    }
}