import QtQuick
import QtQuick.Controls
import NexusAI

Item {
    id: compat

    signal settingsRequested
    signal openRequested(string category)

    function open() {
        openRequested("model")
    }
}
