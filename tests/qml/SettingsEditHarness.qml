// Fixture for test_settings_edit_lifecycle: reproduces the SettingsCenterDialog
// Loader lifecycle (category switch destroys the hosted page) for a settings
// text field, comparing the shipped fix (debounce + destruction commit)
// against the original editingFinished-only hook.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 600
    height: 400
    visible: true

    // Committed value seen by the "sink" (stands in for SettingsService).
    property string committed: ""
    // How often the editingFinished-only field reported a save.
    property int editingFinishedCount: 0

    function readCommitted() { return root.committed }
    function readEditingFinishedCount() { return root.editingFinishedCount }

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
        }
    }
}
