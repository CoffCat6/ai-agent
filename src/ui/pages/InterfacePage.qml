import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

SettingsPage {
    id: page

    Accessible.name: qsTr("界面设置页")

    title: qsTr("界面设置")

    // Current value of a preference = staged edit if any, else stored value.
    // Reads the `staged` map property (NOTIFY stagedChanged) so bindings
    // refresh on stage/rollback/reset - a Q_INVOKABLE-only binding would
    // evaluate exactly once and never update.
    function pref(key) {
        const stagedValue = EditBuffer.staged[key]
        return stagedValue !== undefined ? stagedValue : PreferenceStore.load(key)
    }

    // Ranges come from the registry metadata - the single source of truth,
    // shared with the C++ clamps and validation.
    readonly property var fontSizeRange: PreferenceRegistry.preferenceMeta("ui.baseFontSize").constraint
    readonly property var sidebarRange: PreferenceRegistry.preferenceMeta("ui.defaultSidebarWidth").constraint
    readonly property var panelRange: PreferenceRegistry.preferenceMeta("ui.defaultPanelWidth").constraint

    // docs/31 O9：字体列表动辄上百项，先按名称过滤再给下拉。
    // 等宽下拉只列真正等宽的字体；旧配置里手选的非等宽值仍保留在列表
    // 顶部，避免下拉显示空白。
    property string fontFilter: ""

    function fontMatches(family) {
        if (page.fontFilter === "") return true
        return family.toLowerCase().indexOf(page.fontFilter.toLowerCase()) >= 0
    }

    readonly property var allFontFamilies: {
        const families = FontProbe.availableFamilies()
        families.sort()
        return families
    }

    readonly property var allMonoFamilies: {
        const families = FontProbe.fixedPitchFamilies()
        const current = page.pref("ui.fontMono")
        if (current && families.indexOf(current) < 0)
            families.unshift(current)
        return families
    }

    readonly property var filteredFontFamilies: page.allFontFamilies.filter(page.fontMatches)
    readonly property var filteredMonoFamilies: page.allMonoFamilies.filter(page.fontMatches)

    // Enum values are stable tokens; the theme combo shows a localized label.
    function themeModeLabel(value) {
        switch (value) {
        case "auto":  return qsTr("跟随系统")
        case "dark":  return qsTr("深色")
        case "light": return qsTr("浅色")
        default:      return value
        }
    }

    // Options come from the registry metadata (single source of truth), paired
    // with localized labels - the enum values stay stable tokens.
    readonly property var themeModeOptions: PreferenceRegistry.preferenceMeta("ui.themeMode").options.map(
        function(v) { return { value: v, label: page.themeModeLabel(v) } })

    SettingsSection {
        title: qsTr("外观")

        SettingRow {
            objectName: "ui.themeMode"
            label: qsTr("主题模式")

            ComboBox {
                id: themeModeCombo
                Layout.preferredWidth: Theme.controlWidthMd
                // docs/25 D3: 控件补读屏命名（对齐 SettingsField 做法）
                Accessible.name: qsTr("主题模式")
                Accessible.description: qsTr("当前：%1").arg(currentText)
                model: page.themeModeOptions
                textRole: "label"
                valueRole: "value"
                currentIndex: {
                    const cur = page.pref("ui.themeMode")
                    for (let i = 0; i < model.length; ++i)
                        if (model[i].value === cur) return i
                    return 0
                }
                onActivated: EditBuffer.stage("ui.themeMode", currentValue)
            }
        }
    }

    SettingsSection {
        title: qsTr("字体")

        SettingRow {
            objectName: "ui.fontSearch"
            label: qsTr("搜索字体")

            TextField {
                objectName: "ui/fontSearchField"
                Layout.preferredWidth: Theme.controlWidthMd
                placeholderText: qsTr("按名称筛选字体")
                selectByMouse: true
                onTextChanged: page.fontFilter = text.trim()
                Accessible.name: qsTr("搜索字体")
                Accessible.description: qsTr("同时筛选正文字体与等宽字体")
            }
        }

        Text {
            objectName: "ui/fontSearchEmpty"
            visible: page.filteredFontFamilies.length === 0
                     && page.filteredMonoFamilies.length === 0
            Layout.fillWidth: true
            text: qsTr("没有匹配「%1」的字体。").arg(page.fontFilter)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.WrapAnywhere
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        SettingRow {
            objectName: "ui.fontSans"
            label: qsTr("正文字体")

            ComboBox {
                id: fontSansCombo
                objectName: "ui/fontSansCombo"
                Layout.preferredWidth: Theme.controlWidthMd
                Accessible.name: qsTr("正文字体")
                model: page.filteredFontFamilies
                currentIndex: page.filteredFontFamilies.indexOf(page.pref("ui.fontSans"))
                onActivated: EditBuffer.stage("ui.fontSans", currentText)
            }
        }

        SettingRow {
            objectName: "ui.fontMono"
            label: qsTr("等宽字体")

            ComboBox {
                id: fontMonoCombo
                objectName: "ui/fontMonoCombo"
                Layout.preferredWidth: Theme.controlWidthMd
                Accessible.name: qsTr("等宽字体")
                model: page.filteredMonoFamilies
                currentIndex: page.filteredMonoFamilies.indexOf(page.pref("ui.fontMono"))
                onActivated: EditBuffer.stage("ui.fontMono", currentText)
            }
        }

        SettingRow {
            objectName: "ui.baseFontSize"
            label: qsTr("基础字号")

            RowLayout {
                spacing: Theme.spaceSm
                Slider {
                    id: fontSizeSlider
                    Layout.preferredWidth: Theme.controlWidthXs
                    Accessible.name: qsTr("基础字号")
                    from: page.fontSizeRange.min
                    to: page.fontSizeRange.max
                    value: page.pref("ui.baseFontSize")
                    onMoved: EditBuffer.stage("ui.baseFontSize", Math.round(value))
                }
                SpinBox {
                    // Basic style indicators take 40 px per side; the
                    // width must leave room for the text between them.
                    id: fontSizeSpin
                    Layout.preferredWidth: 128
                    Accessible.name: qsTr("基础字号")
                    editable: true
                    from: page.fontSizeRange.min
                    to: page.fontSizeRange.max
                    value: page.pref("ui.baseFontSize")
                    onValueModified: EditBuffer.stage("ui.baseFontSize", value)
                }
            }
        }

        // Live preview: renders with the staged (not yet saved) font
        // settings so the user sees the effect before committing.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: previewCol.implicitHeight + Theme.spaceXl
            radius: Theme.radiusMd
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: previewCol
                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                spacing: Theme.spaceXs

                Text {
                    text: qsTr("预览（保存前生效于界面）")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                    font.family: Theme.fontMono
                }
                Text {
                    text: qsTr("正文预览：智能体正在思考多行文本 Aa Bb 0123")
                    color: Theme.textPrimary
                    font.pixelSize: page.pref("ui.baseFontSize")
                    font.family: page.pref("ui.fontSans")
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: "const answer = agent.ask(\"hi\"); // 0123"
                    color: Theme.textSecondary
                    font.pixelSize: page.pref("ui.baseFontSize")
                    font.family: page.pref("ui.fontMono")
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }

    SettingsSection {
        title: qsTr("语言")

        SettingRow {
            objectName: "ui.locale"
            label: qsTr("界面语言")

            ComboBox {
                id: localeCombo
                Layout.preferredWidth: Theme.controlWidthMd
                Accessible.name: qsTr("界面语言")
                Accessible.description: qsTr("当前：%1").arg(currentText)
                textRole: "label"

                // First entry is "auto" = follow the system language.
                property var localeEntries: {
                    const entries = [{ value: "auto", label: qsTr("跟随系统") }]
                    const locales = I18n.availableLocales
                    for (let i = 0; i < locales.length; ++i) {
                        const l = locales[i]
                        if (l === "zh-CN") entries.push({ value: l, label: qsTr("简体中文") })
                        else if (l === "en") entries.push({ value: l, label: qsTr("English") })
                        else entries.push({ value: l, label: l })
                    }
                    return entries
                }
                model: localeEntries

                // Unknown stored values fall back to "auto" instead of a
                // blank combo (e.g. values written by an older build).
                currentIndex: {
                    const v = page.pref("ui.locale")
                    for (let i = 0; i < localeEntries.length; ++i) {
                        if (localeEntries[i].value === v) return i
                    }
                    return 0
                }
                onActivated: EditBuffer.stage("ui.locale", localeEntries[currentIndex].value)
            }
        }
    }

    SettingsSection {
        Layout.bottomMargin: Theme.spaceXs
        title: qsTr("窗口布局")

        SettingRow {
            objectName: "ui.defaultSidebarWidth"
            label: qsTr("侧边栏默认宽度")

            SpinBox {
                // 160 leaves ~80 px of text area after the 40 px
                // indicators, enough for "560 px".
                id: sidebarWidthSpin
                Layout.preferredWidth: Theme.controlWidthSm
                Accessible.name: qsTr("侧边栏默认宽度")
                from: page.sidebarRange.min
                to: page.sidebarRange.max
                value: page.pref("ui.defaultSidebarWidth")
                onValueModified: EditBuffer.stage("ui.defaultSidebarWidth", value)
                textFromValue: function(v) { return v + " px" }
            }
        }

        SettingRow {
            objectName: "ui.defaultPanelWidth"
            label: qsTr("面板默认宽度")

            SpinBox {
                id: panelWidthSpin
                Layout.preferredWidth: Theme.controlWidthSm
                Accessible.name: qsTr("面板默认宽度")
                from: page.panelRange.min
                to: page.panelRange.max
                value: page.pref("ui.defaultPanelWidth")
                onValueModified: EditBuffer.stage("ui.defaultPanelWidth", value)
                textFromValue: function(v) { return v + " px" }
            }
        }

        SettingRow {
            objectName: "ui.defaultSidebarExpanded"
            label: qsTr("侧边栏默认展开")

            Switch {
                id: sidebarExpandedSwitch
                Accessible.name: qsTr("侧边栏默认展开")
                checked: page.pref("ui.defaultSidebarExpanded") === true
                onToggled: EditBuffer.stage("ui.defaultSidebarExpanded", checked)
            }
        }

        SettingRow {
            objectName: "ui.defaultPanelVisible"
            label: qsTr("面板默认展开")

            Switch {
                id: panelVisibleSwitch
                Accessible.name: qsTr("面板默认展开")
                checked: page.pref("ui.defaultPanelVisible") === true
                onToggled: EditBuffer.stage("ui.defaultPanelVisible", checked)
            }
        }

        SettingRow {
            objectName: "ui.restoreWindowOnStartup"
            label: qsTr("启动时恢复窗口")

            Switch {
                id: restoreWindowSwitch
                Accessible.name: qsTr("启动时恢复窗口")
                checked: page.pref("ui.restoreWindowOnStartup") === true
                onToggled: EditBuffer.stage("ui.restoreWindowOnStartup", checked)
            }
        }

        SettingRow {
            // docs/31 S4.7 O10：无障碍「减少动效」。开关经 EditBuffer 落盘后
            // 由 ChangeBus 通知 ThemeManager，Theme.motion* 三令牌即时归零。
            objectName: "ui.reduceMotion"
            label: qsTr("减少动效")

            Switch {
                id: reduceMotionSwitch
                Accessible.name: qsTr("减少动效")
                checked: page.pref("ui.reduceMotion") === true
                onToggled: EditBuffer.stage("ui.reduceMotion", checked)
            }
        }

        Text {
            // docs/17 B7: 这些键只影响下次启动的初始布局，当前窗口
            // 以拖拽为准（运行时拖拽写入 ui/sidebarWidth 等覆盖键）。
            text: qsTr("以上默认值在下次启动时生效；当前窗口布局以手动拖拽为准。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
