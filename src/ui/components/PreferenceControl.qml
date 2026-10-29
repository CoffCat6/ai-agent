import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    id: control

    property string prefKey: ""
    property string displayName: ""
    // SettingsCatalog 的搜索跳转按 id 查找目标；偏好条目 id 就是键名。
    objectName: prefKey

    // Validation error from the last failed commit for exactly this key
    // (EditBuffer clears it on the next edit).
    readonly property string errorMessage: EditBuffer.firstInvalidKey === prefKey
                                           ? EditBuffer.validationError : ""

    // Registry metadata for prefKey; null when the key is not registered
    // (the control then falls back to a plain text field).
    readonly property var meta: {
        const m = PreferenceRegistry.preferenceMeta(prefKey)
        return m && m.type !== undefined ? m : null
    }

    implicitHeight: layout.implicitHeight

    Accessible.name: control.displayName

    function stage(value) {
        EditBuffer.stage(prefKey, value)
    }

    // Reads the staged map property so bindings refresh on rollback/reset.
    function stagedValue() {
        const stagedEdit = EditBuffer.staged[prefKey]
        return stagedEdit !== undefined ? stagedEdit : PreferenceStore.load(prefKey)
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: Theme.spaceXs

        Text {
            text: control.displayName
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        // docs/25 D3: 内部控件各自补读屏命名——根 Item 的 Accessible.name
        // 不会自动传递给真正接收焦点的子控件。
        Switch {
            visible: control.meta && control.meta.type === "Bool"
            Accessible.name: control.displayName
            checked: control.stagedValue() === true
            onToggled: control.stage(checked)
        }

        ComboBox {
            visible: control.meta && control.meta.type === "Enum"
                     && control.meta.options && control.meta.options.length > 2
            Accessible.name: control.displayName
            Layout.fillWidth: true
            model: control.meta ? control.meta.options : []
            currentIndex: {
                if (!control.meta) return -1
                const value = control.stagedValue()
                return control.meta.options ? control.meta.options.indexOf(value) : -1
            }
            onActivated: {
                // Index into the array by position; by text would yield
                // undefined for any non-numeric key.
                if (control.meta && control.meta.options)
                    control.stage(control.meta.options[currentIndex])
            }
        }

        RowLayout {
            visible: control.meta && control.meta.type === "Int"
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            readonly property var range: control.meta ? control.meta.constraint : null

            Slider {
                id: prefSlider
                Layout.fillWidth: true
                Accessible.name: control.displayName
                from: parent.range && parent.range.hasRange ? parent.range.min : 0
                to: parent.range && parent.range.hasRange ? parent.range.max : 100
                value: control.stagedValue()
                onMoved: control.stage(Math.round(value))
            }
            SpinBox {
                // 128 leaves ~48 px of text area after the 40 px indicators.
                id: prefSpin
                Layout.preferredWidth: 128
                Accessible.name: control.displayName
                editable: true
                from: parent.range && parent.range.hasRange ? parent.range.min : 0
                to: parent.range && parent.range.hasRange ? parent.range.max : 100
                value: control.stagedValue()
                onValueModified: control.stage(value)
            }
        }

        TextField {
            id: stringField
            visible: !control.meta || control.meta.type === "String"
            Accessible.name: control.displayName
            Layout.fillWidth: true
            text: {
                const value = control.stagedValue()
                return value === undefined || value === null ? "" : String(value)
            }
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            background: Rectangle {
                radius: Theme.radiusField
                color: Theme.surfaceHover
                border.width: stringField.activeFocus ? 2 : 1
                border.color: stringField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
            }
            onEditingFinished: control.stage(text)
        }

        Text {
            visible: control.errorMessage.length > 0
            text: control.errorMessage
            color: Theme.statusDanger
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontSans
            Layout.fillWidth: true
        }
    }
}
