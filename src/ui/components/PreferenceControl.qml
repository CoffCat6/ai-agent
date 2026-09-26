import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import NexusAI
import NexusAI.ViewModels

Item {
    id: control

    property string prefKey: ""
    property string displayName: ""

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
        spacing: 4

        Text {
            text: control.displayName
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.bold: true
            font.family: Theme.fontMono
            Layout.fillWidth: true
        }

        Switch {
            visible: control.meta && control.meta.type === "Bool"
            checked: control.stagedValue() === true
            onToggled: control.stage(checked)
        }

        ComboBox {
            visible: control.meta && control.meta.type === "Enum"
                     && control.meta.options && control.meta.options.length > 2
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
            spacing: 8

            readonly property var range: control.meta ? control.meta.constraint : null

            Slider {
                Layout.fillWidth: true
                from: parent.range && parent.range.hasRange ? parent.range.min : 0
                to: parent.range && parent.range.hasRange ? parent.range.max : 100
                value: control.stagedValue()
                onMoved: control.stage(Math.round(value))
            }
            SpinBox {
                // 128 leaves ~48 px of text area after the 40 px indicators.
                Layout.preferredWidth: 128
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
            Layout.fillWidth: true
            text: {
                const value = control.stagedValue()
                return value === undefined || value === null ? "" : String(value)
            }
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontSans
            background: Rectangle {
                radius: 6
                color: Theme.bgPrimary
                border.width: stringField.activeFocus ? 2 : 1
                border.color: stringField.activeFocus ? Theme.brandPrimary : Theme.borderSubtle
            }
            onEditingFinished: control.stage(text)
        }

        Text {
            visible: control.errorMessage.length > 0
            text: control.errorMessage
            color: Theme.statusDanger
            font.pixelSize: 10
            font.family: Theme.fontSans
            Layout.fillWidth: true
        }
    }
}
