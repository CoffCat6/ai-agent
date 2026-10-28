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

        // 导轨定位的关键前提：折叠中间节点只是把它们的 delegate 高度
        // 压到 0，delegate 仍在 Repeater 里，所以 itemAt(idx) 依然有效、
        // 行号不会因为折叠而失效。这里把这条前提钉住——如果哪天有人把
        // 折叠改成从模型里删行，导轨的 row 就会整体错位。
        function test_collapsedRowsKeepIndicesResolvable() {
            var yBefore = viewport.rowY(60)
            verify(yBefore > 0)

            viewport.collapseRows()
            wait(30)
            // 计数不变（折叠不删行）。
            compare(viewport.count, 180)
            // 行号仍能解析出行高与 y（y 会变小，因为前面的行矮了）。
            var yAfter = viewport.rowY(60)
            verify(yAfter >= 0)
            verify(yAfter < yBefore)
            // 越界与负数不生成畸形坐标。
            compare(viewport.rowY(-1), -1)
            compare(viewport.rowY(10000), -1)
        }

        // activeIndexIn：视口顶落在第几条用户消息。判据是"这条的顶边
        // 已滚到视口顶之上"，返回 -1 表示还没滚过第一条。
        function test_activeIndexInFollowsScrollOffset() {
            viewport.resetRows()
            viewport.jumpToBottom()
            tryCompare(viewport, "count", 50)
            wait(30)

            var rows = [0, 5, 12, 30, 49]
            // 顶部时：第一条的顶边就在视口顶（y=0 <= top），所以是 0。
            viewport.scrollBy(-viewport.maximumOffset)
            wait(30)
            compare(viewport.activeIndexIn(rows), 0)

            // 滚到底部：所有行的顶边都已在视口顶之上 → 最后一条。
            viewport.jumpToBottom()
            wait(30)
            compare(viewport.activeIndexIn(rows), rows.length - 1)

            // 空数组与 null 都返回 -1，不崩。
            compare(viewport.activeIndexIn([]), -1)
            compare(viewport.activeIndexIn(null), -1)
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
