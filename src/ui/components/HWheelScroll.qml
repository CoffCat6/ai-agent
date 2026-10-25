import QtQuick

// Shift + mouse wheel → horizontal scroll of the enclosing Flickable.
//
// Qt Quick does not map Shift+wheel to horizontal scrolling natively:
// QQuickFlickable::wheelEvent distributes by angleDelta axis only, with no
// keyboard-modifier handling (verified against the Qt 6.9 source), so the
// only built-in ways to move a horizontally-overflowing view are tilt/-
// trackpad horizontal deltas and dragging the horizontal scrollbar.
//
// Why MouseArea and not WheelHandler: WheelHandler with
// `orientation: Qt.Horizontal` only reacts to devices that emit a
// horizontal axis themselves, and redirecting a vertical delta via
// `property: "contentX"` accumulates `rotation` without ever writing the
// property (both probe-verified on Qt 6.9). MouseArea.onWheel gives full
// control over direction and acceptance.
//
// Why `flickTarget` needs resolving: an Item declared inside a ScrollView
// keeps the ScrollView as its visual parent - Controls do NOT reparent it
// into the inner Flickable (structure-probe-verified), and the ScrollView
// is a Pane WITHOUT a contentX property. Writing `parent.contentX` there
// silently creates a JS expando and does nothing - the exact bug the first
// implementation shipped. Duck-typing on the contentX property finds the
// real Flickable in both hosts: a plain Flickable parent IS the target;
// a ScrollView parent exposes it as parent.contentItem.
//
// Place INSIDE the Flickable/ScrollView whose horizontal overflow you want
// to pan. anchors.fill: parent pins the overlay to the visible viewport in
// both hosts. acceptedButtons: NoButton keeps presses, dragging and text
// selection untouched; plain (Shift-less) wheels set accepted=false so the
// Flickable keeps its normal vertical scrolling. Only meaningful where
// contentWidth can exceed the viewport; a vertical-only list has nowhere
// to pan and the handler is a no-op there.
MouseArea {
    id: root

    acceptedButtons: Qt.NoButton
    anchors.fill: parent

    readonly property Flickable flickTarget: {
        // Climb the VISUAL parent chain (QML `parent` = parentItem) until an
        // object exposing contentX turns up. Two shapes occur:
        //   - plain Flickable host: this overlay's parentItem is the
        //     Flickable's contentItem (a plain Item WITHOUT contentX - only
        //     QObject::parent is the Flickable, which QML never sees), so
        //     the walk lands on the Flickable one level up;
        //   - ScrollView host: the parentItem stays the ScrollView (a Pane
        //     without contentX) and its contentItem IS the inner Flickable.
        let o = parent
        while (o) {
            if (o.contentX !== undefined) return o
            if (o.contentItem && o.contentItem.contentX !== undefined)
                return o.contentItem
            o = o.parent
        }
        return null
    }

    onWheel: (wheel) => {
        if (!flickTarget) {
            wheel.accepted = false
            return
        }
        if (wheel.modifiers & Qt.ShiftModifier) {
            // A drag/fling in progress (incl. the rubber-band rebound at
            // the bounds) keeps animating contentX and would stomp the pan
            // below every frame until something presses on the view -
            // stop it first.
            if (flickTarget.moving || flickTarget.flicking)
                flickTarget.cancelFlick()
            // Clamp the write OURSELVES: QQuickFlickable does NOT clamp
            // programmatic contentX assignments (only gestures get the
            // bounds treatment - log-proven: unclamped writes at the left
            // end produced negative contentX, which triggers the native
            // rubber-band rebound; the rebound then fights every further
            // pan until a click settles the view).
            const maxX = flickTarget.contentWidth - flickTarget.width
            const x = flickTarget.contentX - wheel.angleDelta.y
            flickTarget.contentX = Math.max(0, Math.min(maxX, x))
            // Wheel-down pans right (the usual Shift+wheel convention);
            // accepting the event stops the vertical handling - also at
            // the limits, so the gesture never chains into vertical
            // scrolling ("到头即停").
            wheel.accepted = true
        } else {
            wheel.accepted = false
        }
    }
}
