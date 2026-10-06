pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import "../../src/ui/components" as UI

Item {
    width: 800
    height: 600

    Component {
        id: viewportComponent
        UI.TranscriptViewport {
            id: viewport
            width: 640
            height: 480
            property bool wrapRows: false
            model: ListModel { id: rows }
            delegate: Rectangle {
                required property real rowHeight
                required property int index
                width: viewport.availableWidth
                height: viewport.wrapRows
                    ? rowHeight * 626 / Math.max(1, width) : rowHeight
            }

            function populate() {
                for (let i = 0; i < 180; ++i)
                    rows.append({rowHeight: i % 7 === 0 ? 4000 : (i % 3 === 0 ? 500 : 28)})
            }
            function appendRow(height) { rows.append({rowHeight: height}) }
            function resizeRow(index, height) { rows.setProperty(index, "rowHeight", height) }
            function collapseRows() {
                for (let i = 0; i < rows.count; ++i)
                    rows.setProperty(i, "rowHeight", 20)
            }
            function resetRows() {
                rows.clear()
                for (let i = 0; i < 50; ++i)
                    rows.append({rowHeight: 100 + i * 10})
            }
        }
    }

    TestCase {
        name: "TranscriptViewport"
        when: windowShown
        property var viewport

        function init() {
            viewport = createTemporaryObject(viewportComponent, parent)
            verify(viewport !== null)
            viewport.populate()
            tryCompare(viewport, "count", 180)
            tryVerify(function() { return viewport.contentHeight > 100000 })
            atBottom()
        }

        function atBottom() {
            // Column positioning is polished after model/delegate mutations.
            wait(20)
            tryVerify(function() {
                return Math.abs(viewport.maximumOffset - viewport.scrollOffset) < 0.1
            })
            compare(viewport.pinnedToBottom, true)
        }

        function test_exactLongTranscriptExtent() {
            let total = 0
            for (let i = 0; i < 180; ++i)
                total += i % 7 === 0 ? 4000 : (i % 3 === 0 ? 500 : 28)
            tryCompare(viewport, "contentHeight", total)
            compare(viewport.maximumOffset, total - viewport.height)
            viewport.scrollBy(-viewport.maximumOffset)
            compare(viewport.scrollOffset, 0)
            compare(viewport.pinnedToBottom, false)
            viewport.jumpToBottom()
            atBottom()
        }

        function test_thousandRowsKeepExactEndpoints() {
            for (let i = 180; i < 1200; ++i)
                viewport.appendRow(i % 5 === 0 ? 6000 : 36)
            tryCompare(viewport, "count", 1200)
            atBottom()
            for (let i = 0; i < 3; ++i) {
                viewport.scrollBy(-viewport.maximumOffset)
                compare(viewport.scrollOffset, 0)
                viewport.jumpToBottom()
                atBottom()
            }
        }

        function test_streamingFollowsMeasuredHeight() {
            viewport.appendRow(4000)
            atBottom()
            viewport.resizeRow(180, 9000)
            atBottom()
            viewport.resizeRow(180, 80)
            atBottom()
        }

        function test_readingHistoryDoesNotFollowStreaming() {
            viewport.scrollBy(-viewport.maximumOffset / 2)
            const offset = viewport.scrollOffset
            const oldHeight = viewport.contentHeight
            viewport.appendRow(8000)
            viewport.resizeRow(179, 12000)
            tryVerify(function() { return viewport.contentHeight > oldHeight + 8000 })
            wait(50)
            compare(viewport.scrollOffset, offset)
            compare(viewport.pinnedToBottom, false)
            viewport.jumpToBottom()
            atBottom()
        }

        function test_wheelAndReturnToBottom() {
            mouseWheel(viewport, 320, 240, 0, 120)
            tryVerify(function() { return viewport.maximumOffset - viewport.scrollOffset > 2 })
            compare(viewport.pinnedToBottom, false)
            mouseWheel(viewport, 320, 240, 0, -120)
            atBottom()
        }

        function test_scrollbarOwnsOffsetWhileStreaming() {
            const x = viewport.width - 7
            mousePress(viewport, x, 2)
            compare(viewport.userInteracting, true)
            compare(viewport.pinnedToBottom, false)
            viewport.appendRow(16000)
            wait(50)
            verify(viewport.scrollOffset < viewport.maximumOffset)
            mouseMove(viewport, x, viewport.height - 1)
            mouseRelease(viewport, x, viewport.height - 1)
            atBottom()
        }

        function test_collapseClampsWithoutRebuildingModel() {
            viewport.scrollBy(-500)
            viewport.collapseRows()
            tryCompare(viewport, "contentHeight", 3600)
            tryVerify(function() { return viewport.scrollOffset <= viewport.maximumOffset })
            verify(viewport.scrollOffset >= 0)
            compare(viewport.pinnedToBottom, false)
            compare(viewport.count, 180)
            viewport.jumpToBottom()
            atBottom()
        }

        function test_resizeAndRewrapStayAtBottom() {
            viewport.wrapRows = true
            viewport.width = 420
            atBottom()
            viewport.height = 280
            atBottom()
            viewport.width = 800
            atBottom()
        }

        function test_sessionResetAndImmediateJump() {
            viewport.scrollBy(-viewport.maximumOffset)
            viewport.resetRows()
            viewport.jumpToBottom()
            tryCompare(viewport, "count", 50)
            tryCompare(viewport, "contentHeight", 17250)
            atBottom()
        }
    }
}
