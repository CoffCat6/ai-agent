import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for ReferenceChipRow: lets the C++ case inject
// inspectFileReferences-shaped data without going through AgentVM.
Window {
    id: win
    width: 520
    height: 80
    visible: true
    color: Theme.bgSecondary

    ReferenceChipRow {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    function setChecks(items) { row.checks = items }
    function chipCount() { return row.chipCount }
    function rowVisible() { return row.visible }
    function rowHeight() { return row.implicitHeight }
    function backgroundColor() { return Theme.bgSecondary.toString() }
}
