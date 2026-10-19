import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for the '@' completion interaction. State is injected from
// C++ (see test_mention_interaction.cpp) so the test can drive real key and
// mouse input without a project or an LLM.
Window {
    id: win
    width: 640
    height: 240
    visible: true
    color: Theme.bgSecondary

    InputConsole {
        id: consoleRoot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
    }

    // Injecting the completion list from C++ through a var property is not
    // enough: a QVariantList assigned via setProperty is not visible to JS as
    // an array (length undefined), which is exactly what the completion
    // handlers check. Passing it through a QML function gives a real JS array.
    function setMentionResults(items, index) {
        consoleRoot.mentionIndex = index
        consoleRoot.mentionResults = items
        consoleRoot.mentionOpen = true
    }
}
