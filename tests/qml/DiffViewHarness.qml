import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// Test harness for DiffView's split-column geometry.
//
// DiffView is normally reached through the review dialog, but the split
// behaviour under test (content-aware default, draggable seam, clamping) is
// entirely local to the component, so hosting it directly keeps the cases
// short and independent of checkpoint plumbing.
Window {
    id: win
    width: 900
    height: 600
    visible: true
    color: Theme.bgSecondary

    DiffView {
        id: view
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 420
        viewMode: "split"
    }

    function setDiff(text) { view.diffText = text }
    function setMode(m) { view.viewMode = m }
    function setRatio(r) { view.splitRatio = r }
    function setTouched(t) { view.splitTouched = t }
    function dragSplit(delta) { view.dragSplit(delta) }
    function resetToAuto() {
        view.splitTouched = false
        view.splitRatio = view.autoSplitRatio
    }

    function ratio() { return view.splitRatio }
    function autoRatio() { return view.autoSplitRatio }
    function touched() { return view.splitTouched }
    function showGutters() { return view.showGutters }
    function hasDiff() { return view.hasDiff }
    function renderedCount() { return view.renderedCount }
    function rowCount() { return view.splitRows.length }
    function viewMode() { return view.viewMode }
    // Seam x in the view's own coordinates, for the same row width the
    // delegate uses.
    function seamX() { return view.splitXFor(view.width) }
    // Seam for an arbitrary row width: a delegate computes its own seam from
    // the row it is laid out in, which is what a row-geometry assertion has
    // to compare against.
    function seamForRowWidth(w) { return view.splitXFor(w) }
    function viewWidth() { return view.width }
    // Geometry contract: the test asserts against the component's own
    // limits instead of hard-coding the pixel values, so a change to the
    // gutter/inset cannot leave the test and the component disagreeing.
    function minSeamX() { return view.minSeamXFor() }
    function maxSeamX() { return view.maxSeamXFor(view.width) }
    function minColumnText() { return view.minColumnText }
    function textSpan() { return view.textSpanFor(view.width) }
    function oldBodyWidth(seamX) { return view.oldBodyWidthFor(view.width, seamX) }
    function newBodyWidth(seamX) { return view.newBodyWidthFor(view.width, seamX) }
}
