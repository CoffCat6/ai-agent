import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    id: page

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

    readonly property var fontFamilies: {
        const families = FontProbe.availableFamilies()
        families.sort()
        return families
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 24

        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: contentCol
            width: flick.width
            spacing: 20

            Item { Layout.preferredHeight: 4 }

            Text {
                text: qsTr("界面设置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeH2
                font.bold: true
                font.family: Theme.fontSans
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Text {
                text: qsTr("外观")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            RowLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: qsTr("深色模式")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    Layout.fillWidth: true
                }
                Switch {
                    checked: page.pref("ui.themeMode") === "dark"
                    onToggled: EditBuffer.stage("ui.themeMode", checked ? "dark" : "light")
                }
            }

            Text {
                text: qsTr("字体")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("正文字体")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        Layout.preferredWidth: 200
                        model: page.fontFamilies
                        currentIndex: page.fontFamilies.indexOf(page.pref("ui.fontSans"))
                        onActivated: EditBuffer.stage("ui.fontSans", currentText)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("等宽字体")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        Layout.preferredWidth: 200
                        model: page.fontFamilies
                        currentIndex: page.fontFamilies.indexOf(page.pref("ui.fontMono"))
                        onActivated: EditBuffer.stage("ui.fontMono", currentText)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("基础字号")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        spacing: 8
                        Slider {
                            Layout.preferredWidth: 120
                            from: page.fontSizeRange.min
                            to: page.fontSizeRange.max
                            value: page.pref("ui.baseFontSize")
                            onMoved: EditBuffer.stage("ui.baseFontSize", Math.round(value))
                        }
                        SpinBox {
                            // Basic style indicators take 40 px per side; the
                            // width must leave room for the text between them.
                            Layout.preferredWidth: 128
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
                    implicitHeight: previewCol.implicitHeight + 20
                    radius: 8
                    color: Theme.bgSecondary
                    border.width: 1
                    border.color: Theme.borderSubtle

                    ColumnLayout {
                        id: previewCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

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

            Text {
                text: qsTr("语言")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            RowLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("界面语言")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontSans
                    Layout.fillWidth: true
                }
                ComboBox {
                    id: localeCombo
                    Layout.preferredWidth: 200
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
                        for (let i = 0; i < localeEntries.length; ++i)
                            if (localeEntries[i].value === v) return i
                        return 0
                    }
                    onActivated: EditBuffer.stage("ui.locale", localeEntries[currentIndex].value)
                }
            }

            Text {
                text: qsTr("窗口布局")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
                font.family: Theme.fontMono
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            ColumnLayout {
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 16
                Layout.fillWidth: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: qsTr("侧边栏默认宽度")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    SpinBox {
                        // 160 leaves ~80 px of text area after the 40 px
                        // indicators, enough for "560 px".
                        Layout.preferredWidth: 160
                        from: page.sidebarRange.min
                        to: page.sidebarRange.max
                        value: page.pref("ui.defaultSidebarWidth")
                        onValueModified: EditBuffer.stage("ui.defaultSidebarWidth", value)
                        textFromValue: function(v) { return v + " px" }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: qsTr("面板默认宽度")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    SpinBox {
                        Layout.preferredWidth: 160
                        from: page.panelRange.min
                        to: page.panelRange.max
                        value: page.pref("ui.defaultPanelWidth")
                        onValueModified: EditBuffer.stage("ui.defaultPanelWidth", value)
                        textFromValue: function(v) { return v + " px" }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: qsTr("侧边栏默认展开")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    Switch {
                        checked: page.pref("ui.defaultSidebarExpanded") === true
                        onToggled: EditBuffer.stage("ui.defaultSidebarExpanded", checked)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: qsTr("面板默认展开")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    Switch {
                        checked: page.pref("ui.defaultPanelVisible") === true
                        onToggled: EditBuffer.stage("ui.defaultPanelVisible", checked)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: qsTr("启动时恢复窗口")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontSans
                        Layout.fillWidth: true
                    }
                    Switch {
                        checked: page.pref("ui.restoreWindowOnStartup") === true
                        onToggled: EditBuffer.stage("ui.restoreWindowOnStartup", checked)
                    }
                }
            }
        }
    }
}
