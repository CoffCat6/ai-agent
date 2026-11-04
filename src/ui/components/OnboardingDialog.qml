import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import NexusAI
import NexusAI.ViewModels

// docs/31 N1：首次启动向导。
//
// 只负责交互与字段收集，「要不要弹」由 Onboarding（C++）判定；模型配置的
// 写入全部走 Settings.addProfile/updateProfile，不在这里碰 QSettings。
// 三步：语言与外观 → 模型服务（预设 + 地址 + 密钥 + 测试连接）→ 可选导入
// 旧设置。完成与跳过都写 general.onboarded，保证只打扰一次。
//
// 密钥语义与设置页一致：输入框空白 = 不修改，只在「测试连接」「下一步」
// 「完成」时读取 text 提交（绝不读 displayText）。
Dialog {
    id: dialog

    objectName: "onboardingDialog"

    // 0 语言与外观 / 1 模型服务 / 2 导入旧设置
    property int step: 0
    // 当前选中的来源（"openai" | "anthropic" | "gemini" | "ollama" | "custom"）
    property string kind: "openai"
    // 向导创建的配置 id；为空表示还没创建（跳过时不该留下空配置）
    property string profileId: ""
    property bool profileCreated: false
    // 用户改过任何字段：未改过且跳过时把向导创建的空配置删掉
    property bool touched: false
    property string message: ""
    property bool messageIsError: false

    property bool testing: false
    property string testState: "info"
    property string testText: ""

    property string importPath: ""
    property var importPreview: ({})
    property bool importDone: false
    property string importMessage: ""
    property bool importFailed: false

    property string localeMode: "auto"
    property string themeChoice: "auto"

    readonly property var kindOptions: [
        { kind: "openai",    label: qsTr("OpenAI 兼容") },
        { kind: "anthropic", label: qsTr("Claude") },
        { kind: "gemini",    label: qsTr("Gemini") },
        { kind: "ollama",    label: qsTr("Ollama 本地") },
        { kind: "custom",    label: qsTr("自定义网关") }
    ]
    readonly property var localeOptions: [
        { value: "auto",  label: qsTr("跟随系统") },
        { value: "zh-CN", label: qsTr("中文") },
        { value: "en",    label: qsTr("English") }
    ]
    readonly property var themeOptions: [
        { value: "auto",  label: qsTr("跟随系统") },
        { value: "light", label: qsTr("浅色") },
        { value: "dark",  label: qsTr("深色") }
    ]

    readonly property bool profileUsable:
        baseUrlField.text.trim().length > 0 && modelField.text.trim().length > 0
    readonly property bool canFinish:
        importDone || (profileId.length > 0 && profileUsable)

    modal: true
    anchors.centerIn: parent
    width: Math.min(600, parent ? parent.width - 48 : 600)
    height: Math.min(560, parent ? parent.height - 48 : 560)
    padding: 0
    // 向导不提供 Esc/点外部关闭：要么完成（写标记），要么显式跳过。
    closePolicy: Popup.NoAutoClose
    background: DialogBackground {}

    // 注意不能叫 reset()：QQuickPopup 已有同名方法，重名会被报成
    // "Duplicate method name: invalid override"。
    function resetState() {
        step = 0
        kind = "openai"
        profileId = ""
        profileCreated = false
        touched = false
        message = ""
        messageIsError = false
        testing = false
        testState = "info"
        testText = ""
        importPath = ""
        importPreview = ({})
        importDone = false
        importMessage = ""
        importFailed = false
        localeMode = String(PreferenceStore.load("ui.locale") || "auto")
        themeChoice = String(PreferenceStore.load("ui.themeMode") || "auto")
        baseUrlField.text = ""
        keyField.text = ""
        modelField.text = ""
    }

    function openOnboarding() {
        resetState()
        open()
    }

    function kindLabel(value) {
        for (let i = 0; i < kindOptions.length; ++i)
            if (kindOptions[i].kind === value) return kindOptions[i].label
        return value
    }

    function profileById(id) {
        const list = Settings.profiles
        for (let i = 0; i < list.length; ++i)
            if (String(list[i].id) === id) return list[i]
        return null
    }

    function fail(text) {
        messageIsError = true
        message = text
    }

    // 进入模型步骤时懒创建配置：跳过向导不应留下一条空配置。
    function ensureProfile() {
        if (profileId.length === 0) {
            const id = Settings.addProfile(kind)
            if (id.length === 0) {
                fail(qsTr("无法创建模型配置，请检查设置写入权限后重试。"))
                return
            }
            profileId = id
            profileCreated = true
            Settings.updateProfile(id, { displayName: kindLabel(kind) })
        }
        const profile = profileById(profileId)
        if (profile) {
            baseUrlField.text = String(profile.baseUrl || "")
            modelField.text = String(profile.model || "")
        }
    }

    function selectKind(value) {
        if (kind === value && profileId.length > 0) return
        kind = value
        touched = true
        message = ""
        testState = "info"
        testText = ""
        if (profileId.length === 0) {
            ensureProfile()
            keyField.text = ""
            return
        }
        // 换来源 = 换端点家族：显式清空模型与列表（同厂商换来源时
        // updateProfile 不会替我们清）。自定义网关还要清掉上一家的地址。
        const fields = {
            kind: value,
            model: "",
            models: [],
            displayName: kindLabel(value)
        }
        if (value === "custom") fields.baseUrl = ""
        Settings.updateProfile(profileId, fields)
        const profile = profileById(profileId)
        baseUrlField.text = profile ? String(profile.baseUrl || "") : ""
        modelField.text = ""
        keyField.text = ""
    }

    // 空白密钥 = 不修改（与设置页 secret + ignoreEmptySubmit 同一语义），
    // 只提交真正变化过的字段，避免每次翻页都触发一轮保存。
    function applyProfileFields() {
        if (profileId.length === 0) return
        const profile = profileById(profileId)
        if (!profile) return
        const fields = {}
        const baseUrl = baseUrlField.text.trim()
        const model = modelField.text.trim()
        if (baseUrl !== String(profile.baseUrl || "")) fields.baseUrl = baseUrl
        if (model !== String(profile.model || "")) fields.model = model
        if (keyField.text.length > 0) fields.apiKey = keyField.text
        if (Object.keys(fields).length > 0)
            Settings.updateProfile(profileId, fields)
        keyField.text = ""
    }

    function testConnection() {
        if (profileId.length === 0 || !profileUsable) return
        // applyProfileFields() 只提交非空密钥：用户刚输入的 key 会先落盘到
        // 当前 profile，再由 testProfile(profileId) 在 C++ 内部读取。
        // 空白密钥不覆盖已保存的密钥，测试也沿用已配置的那把。
        applyProfileFields()
        if (!profileById(profileId)) return
        testing = true
        testState = "busy"
        testText = qsTr("正在测试连接…")
        // B1-1：QML 不再持有/传递明文 apiKey；旧 test(..., apiKey, ...)
        // 仅保留给 C++ 单元测试与兼容调用。
        ConnectionTester.testProfile(profileId, modelField.text.trim())
    }

    function next() {
        message = ""
        if (step === 0) {
            step = 1
            ensureProfile()
            return
        }
        if (step === 1) {
            applyProfileFields()
            step = 2
            return
        }
        finish()
    }

    function back() {
        if (step > 0) step -= 1
    }

    // 既没完成也没导入：把向导自己建的空配置删掉，别把半成品留给用户。
    function cleanupAbandonedProfile() {
        if (!profileCreated || touched || profileId.length === 0) return
        if (modelField.text.trim().length > 0) return
        Settings.removeProfile(profileId)
        profileId = ""
    }

    function finish() {
        if (!importDone) applyProfileFields()
        if (!Onboarding.complete()) {
            fail(qsTr("无法保存向导状态，请稍后重试。"))
            return
        }
        close()
    }

    function skip() {
        cleanupAbandonedProfile()
        if (!Onboarding.complete()) {
            fail(qsTr("无法保存向导状态，请稍后重试。"))
            return
        }
        close()
    }

    function importSettings() {
        importFailed = false
        if (importPath.length === 0) return
        if (!SettingsBackup.importSettings(importPath)) {
            importFailed = true
            importMessage = SettingsBackup.lastError || qsTr("导入失败。")
            return
        }
        importDone = true
        importMessage = SettingsBackup.restartRequired
            ? qsTr("设置已导入，重启应用后生效。")
            : qsTr("设置已导入。")
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("选择设置备份")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("JSON 文件 (*.json)")]
        onAccepted: {
            dialog.importFailed = false
            dialog.importMessage = ""
            const path = selectedFile.toString()
            const preview = SettingsBackup.previewImport(path)
            if (!preview.valid) {
                dialog.importFailed = true
                dialog.importMessage = preview.error || SettingsBackup.lastError
                    || qsTr("无法读取此备份文件。")
                return
            }
            dialog.importPath = path
            dialog.importPreview = preview
        }
    }

    Connections {
        target: ConnectionTester
        enabled: dialog.opened
        function onTestFinished(ok, message) {
            if (!dialog.testing) return
            dialog.testing = false
            dialog.testState = ok ? "success" : "danger"
            dialog.testText = message
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("首次启动向导")

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.topMargin: Theme.spaceXl
            spacing: Theme.spaceXs

            Text {
                Layout.fillWidth: true
                text: dialog.step === 0 ? qsTr("欢迎使用 NexusAI")
                      : dialog.step === 1 ? qsTr("配置模型服务")
                      : qsTr("导入旧设置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Text {
                Layout.fillWidth: true
                text: dialog.step === 0
                      ? qsTr("先选择界面语言与主题，随时可以在设置中心修改。")
                      : dialog.step === 1
                        ? qsTr("填写模型服务地址、密钥与模型名。密钥只保存在本机并加密，不会进入设置备份。")
                        : qsTr("如果你在另一台设备或旧版本里导出过设置备份，可以在这里恢复；也可以直接跳过。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spaceXs
                text: qsTr("第 %1 步，共 3 步").arg(dialog.step + 1)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontSans
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.topMargin: Theme.spaceLg
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: AppScrollBar {}

            ColumnLayout {
                width: parent.width
                spacing: Theme.spaceLg

                // ── 第 1 步：语言与主题 ─────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs
                    visible: dialog.step === 0

                    Text {
                        text: qsTr("界面语言")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Repeater {
                            model: dialog.localeOptions

                            delegate: AppButton {
                                required property var modelData
                                objectName: "onboarding/locale/" + modelData.value
                                text: modelData.label
                                highlighted: dialog.localeMode === modelData.value
                                onClicked: {
                                    dialog.localeMode = modelData.value
                                    EditBuffer.stage("ui.locale", modelData.value)
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.topMargin: Theme.spaceSm
                        text: qsTr("主题")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Repeater {
                            model: dialog.themeOptions

                            delegate: AppButton {
                                required property var modelData
                                objectName: "onboarding/theme/" + modelData.value
                                text: modelData.label
                                highlighted: dialog.themeChoice === modelData.value
                                onClicked: {
                                    dialog.themeChoice = modelData.value
                                    EditBuffer.stage("ui.themeMode", modelData.value)
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // ── 第 2 步：模型服务 ───────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs
                    visible: dialog.step === 1

                    Text {
                        text: qsTr("服务来源")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Repeater {
                            model: dialog.kindOptions

                            delegate: AppButton {
                                required property var modelData
                                objectName: "onboarding/kind/" + modelData.kind
                                text: modelData.label
                                highlighted: dialog.kind === modelData.kind
                                onClicked: dialog.selectKind(modelData.kind)
                            }
                        }
                    }

                    Text {
                        Layout.topMargin: Theme.spaceSm
                        text: qsTr("服务地址")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    TextField {
                        id: baseUrlField
                        objectName: "onboarding/baseUrl"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("服务地址")
                        placeholderText: qsTr("https://api.example.com/v1")
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary
                        selectedTextColor: Theme.selectedTextColor
                        selectByMouse: true
                        font.pixelSize: Theme.fontSizeBody
                        onTextEdited: dialog.touched = true
                        background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: baseUrlField.activeFocus ? 2 : 1; border.color: baseUrlField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    }

                    Text {
                        Layout.topMargin: Theme.spaceSm
                        text: qsTr("API Key")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    TextField {
                        id: keyField
                        objectName: "onboarding/apiKey"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("API Key")
                        echoMode: TextInput.Password
                        placeholderText: qsTr("留空则不修改；本地服务可以留空")
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary
                        selectedTextColor: Theme.selectedTextColor
                        selectByMouse: true
                        font.pixelSize: Theme.fontSizeBody
                        onTextEdited: dialog.touched = true
                        background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: keyField.activeFocus ? 2 : 1; border.color: keyField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    }

                    Text {
                        Layout.topMargin: Theme.spaceSm
                        text: qsTr("模型名")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                    }

                    TextField {
                        id: modelField
                        objectName: "onboarding/model"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("模型名")
                        placeholderText: qsTr("例如 gpt-4o-mini")
                        placeholderTextColor: Theme.textSecondary
                        color: Theme.textPrimary
                        selectionColor: Theme.brandPrimary
                        selectedTextColor: Theme.selectedTextColor
                        selectByMouse: true
                        font.pixelSize: Theme.fontSizeBody
                        onTextEdited: dialog.touched = true
                        background: Rectangle { radius: Theme.radiusField; color: Theme.surfaceHover; border.width: modelField.activeFocus ? 2 : 1; border.color: modelField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        spacing: Theme.spaceSm

                        AppButton {
                            objectName: "onboarding/test"
                            text: qsTr("测试连接")
                            enabled: dialog.profileUsable && !dialog.testing
                            onClicked: dialog.testConnection()
                        }

                        Item { Layout.fillWidth: true }
                    }

                    StatusLine {
                        objectName: "onboarding/testStatus"
                        text: dialog.testText
                        state: dialog.testState
                    }
                }

                // ── 第 3 步：导入旧设置（可选）──────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm
                    visible: dialog.step === 2

                    AppButton {
                        objectName: "onboarding/chooseImport"
                        text: qsTr("选择设置备份…")
                        enabled: !dialog.importDone
                        onClicked: importFileDialog.open()
                    }

                    Text {
                        objectName: "onboarding/importPath"
                        Layout.fillWidth: true
                        visible: dialog.importPath.length > 0
                        text: dialog.importPath
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontMono
                        elide: Text.ElideMiddle
                    }

                    Text {
                        objectName: "onboarding/importSummary"
                        Layout.fillWidth: true
                        visible: dialog.importPath.length > 0
                        text: {
                            const preview = dialog.importPreview || {}
                            return (preview.sections || []).join("\n")
                        }
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: dialog.importPath.length > 0
                        text: (dialog.importPreview.warnings || []).join("\n")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontSans
                        wrapMode: Text.WordWrap
                    }

                    AppButton {
                        objectName: "onboarding/import"
                        text: qsTr("导入设置")
                        highlighted: true
                        visible: dialog.importPath.length > 0 && !dialog.importDone
                        onClicked: dialog.importSettings()
                    }

                    StatusLine {
                        objectName: "onboarding/importStatus"
                        text: dialog.importMessage
                        state: dialog.importFailed ? "danger" : "success"
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.topMargin: Theme.spaceMd
            Layout.bottomMargin: Theme.spaceXl
            spacing: Theme.spaceSm

            AppButton {
                objectName: "onboarding/skip"
                text: qsTr("跳过向导")
                buttonColor: "transparent"
                onClicked: dialog.skip()
            }

            Item { Layout.fillWidth: true }

            AppButton {
                objectName: "onboarding/back"
                text: qsTr("上一步")
                visible: dialog.step > 0
                onClicked: dialog.back()
            }

            AppButton {
                objectName: "onboarding/next"
                text: qsTr("下一步")
                highlighted: true
                visible: dialog.step < 2
                onClicked: dialog.next()
            }

            AppButton {
                objectName: "onboarding/finish"
                text: qsTr("完成")
                highlighted: true
                visible: dialog.step === 2
                enabled: dialog.canFinish
                onClicked: dialog.finish()
            }
        }

        Text {
            objectName: "onboarding/message"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXl
            Layout.rightMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceSm
            visible: dialog.message.length > 0
            text: dialog.message
            color: dialog.messageIsError ? Theme.statusDanger : Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.WordWrap
        }
    }
}
