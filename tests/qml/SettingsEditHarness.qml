// Fixture for test_settings_edit_lifecycle: reproduces the SettingsCenterDialog
// Loader lifecycle (category switch destroys the hosted page) for settings
// text fields.
//
// The row mirrors src/ui/components/SettingsField.qml's submit timing: a
// reusable row that commits on destruction only when its text differs from the
// baseline recorded after the last sync (an untouched row must NOT resubmit —
// otherwise every page rebuild rewrites stored values). It is instantiated
// directly inside the page, exactly like SettingsField is in ModelConfigPage.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 600
    height: 400
    visible: true

    // Sink for the plain field (stands in for SettingsService).
    property string committed: ""
    // How often the editingFinished-only field reported a save.
    property int editingFinishedCount: 0
    // Sink + submit counter for the SettingsField-style row.
    property string rowSink: ""
    property int rowSubmitCount: 0
    // Sink for the secret row.
    property string secretSink: ""

    function readCommitted() { return root.committed }
    function readEditingFinishedCount() { return root.editingFinishedCount }
    function readRowSink() { return root.rowSink }
    function readRowSubmitCount() { return root.rowSubmitCount }
    function readSecretSink() { return root.secretSink }

    property alias pageLoader: loader

    Loader {
        id: loader
        objectName: "pageLoader"
        anchors.fill: parent
        active: true
        sourceComponent: pageComp
    }

    Component {
        id: pageComp
        ColumnLayout {
            spacing: 8

            // Mirrors the shipped GeneralPage field: debounce + destruction commit.
            TextField {
                id: editField
                objectName: "editField"
                text: ""
                onTextEdited: debounce.restart()
                onEditingFinished: root.committed = text
                Component.onDestruction: root.committed = text

                Timer {
                    id: debounce
                    interval: 400
                    onTriggered: root.committed = editField.text
                }
            }

            // Mirrors the pre-fix wiring: editingFinished is the only save path.
            TextField {
                id: editingFinishedOnly
                objectName: "editingFinishedOnlyField"
                text: ""
                onEditingFinished: root.editingFinishedCount += 1
            }

            // Mirrors SettingsField: reusable row, parent owns the handler.
            RowLayout {
                id: row
                objectName: "settingsRow"
                spacing: 10

                // Commits on destruction only when the text differs from the
                // last known baseline (an untouched row must stay silent).
                property bool commitOnDestroy: true
                property string lastKnownValue: ""
                signal submitted(string value)

                function noteValue(v) {
                    row.lastKnownValue = (v === undefined || v === null) ? "" : String(v)
                }
                function commitIfDirty() {
                    // text, never displayText: Password's displayText is the mask.
                    if (rowInput.text === row.lastKnownValue) return
                    row.submitted(rowInput.text)
                    row.noteValue(rowInput.text)
                }

                TextField {
                    id: rowInput
                    objectName: "settingsRowInput"
                    Layout.fillWidth: true
                    text: ""
                    onEditingFinished: {
                        row.submitted(text)
                        row.noteValue(text)
                    }
                    Component.onDestruction: if (row.commitOnDestroy) row.commitIfDirty()
                }
            }

            // Same row with echoMode Password — guards against committing the
            // masked echo instead of the real text.
            RowLayout {
                id: secretRow
                objectName: "secretRow"
                spacing: 10

                property bool commitOnDestroy: true
                property string lastKnownValue: ""
                signal submitted(string value)

                function noteValue(v) {
                    secretRow.lastKnownValue = (v === undefined || v === null) ? "" : String(v)
                }
                function commitIfDirty() {
                    if (secretInput.text === secretRow.lastKnownValue) return
                    secretRow.submitted(secretInput.text)
                    secretRow.noteValue(secretInput.text)
                }

                TextField {
                    id: secretInput
                    objectName: "secretRowInput"
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    text: ""
                    onEditingFinished: {
                        secretRow.submitted(text)
                        secretRow.noteValue(text)
                    }
                    Component.onDestruction: if (secretRow.commitOnDestroy) secretRow.commitIfDirty()
                }
            }

            // Parent-side handlers (what a settings page attaches to the row).
            Connections {
                target: row
                function onSubmitted(value) {
                    root.rowSubmitCount += 1
                    root.rowSink = value
                }
            }
            Connections {
                target: secretRow
                function onSubmitted(value) { root.secretSink = value }
            }
        }
    }
}
