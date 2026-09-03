import QtQuick
import NexusAI
import NexusAI.ViewModels

// Shared dialog chrome: secondary-background panel with a subtle border.
// Use as `background: DialogBackground {}`; child items (e.g. stripes)
// attach through the default data property as with a plain Rectangle.
Rectangle {
    color: Theme.bgSecondary
    radius: 8
    border.width: 1
    border.color: Theme.borderSubtle
}
