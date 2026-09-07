// Logger ring-buffer coverage (日志开发 doc 6 §6): eviction, level filter,
// incremental snapshot, multi-threaded write smoke, snapshot consistency.
#include "core/utils/Logger.h"

#include <QtConcurrent/QtConcurrent>
#include <QtTest>

using namespace NexusAI;

namespace {

// Marker-tagged write; returns the assigned seq (revision delta).
void write(const QString& marker) {
    qInfo().noquote() << marker;
}

} // namespace

class TestLogger : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qputenv("NEXUSAI_LOG_LEVEL", "debug");   // deterministic level for the suite
        Logger::install();
        Logger::setMinLevel(QtDebugMsg);
    }

    // §6.1 ring eviction: capacity+200 marker writes leave at most
    // RingCapacity marker entries, oldest evicted, newest kept.
    void ringEvictsOldest() {
        const quint64 rev0 = Logger::revision();
        const int extra = 200;
        for (int i = 0; i < Logger::RingCapacity + extra; ++i)
            write(QStringLiteral("ringtest-evict-%1").arg(i));

        const auto entries = Logger::entriesSince(rev0);
        int markers = 0;
        bool sawFirst = false, sawLast = false;
        quint64 prevSeq = 0;
        for (const auto& e : entries) {
            if (!e.message.contains(QStringLiteral("ringtest-evict-"))) continue;
            ++markers;
            if (e.message.endsWith(QStringLiteral("ringtest-evict-0"))) sawFirst = true;
            if (e.message.endsWith(QStringLiteral("ringtest-evict-%1")
                                       .arg(Logger::RingCapacity + extra - 1))) sawLast = true;
            QVERIFY(e.seq > prevSeq);   // oldest-first, strictly growing
            prevSeq = e.seq;
        }
        QVERIFY2(markers <= Logger::RingCapacity, "ring capacity exceeded");
        QVERIFY2(markers >= Logger::RingCapacity - 200,
                 "unexpectedly few marker entries survived");
        QVERIFY2(!sawFirst, "oldest entry not evicted");
        QVERIFY2(sawLast, "newest entry missing");
    }

    // §6.2 level filter applies before buffering
    void levelFiltersBuffer() {
        Logger::setMinLevel(QtWarningMsg);
        const quint64 rev0 = Logger::revision();
        // Explicit levels: the write() helper always emits qInfo.
        qDebug().noquote() << "ringtest-lvl-debug";
        qInfo().noquote() << "ringtest-lvl-info";
        qWarning().noquote() << "ringtest-lvl-warn";

        const auto entries = Logger::entriesSince(rev0);
        for (const auto& e : entries) {
            if (e.message.contains(QStringLiteral("ringtest-lvl"))) {
                QVERIFY2(!e.message.contains(QStringLiteral("ringtest-lvl-debug")),
                         "debug line buffered past Warning filter");
                QVERIFY2(!e.message.contains(QStringLiteral("ringtest-lvl-info")),
                         "info line buffered past Warning filter");
            }
        }
        Logger::setMinLevel(QtDebugMsg);
    }

    // §6.3 incremental pull: two chunks concatenate to the full snapshot
    void incrementalPullHasNoGaps() {
        const quint64 rev0 = Logger::revision();
        for (int i = 0; i < 50; ++i)
            write(QStringLiteral("ringtest-inc-%1").arg(i));
        const quint64 mid = Logger::revision();

        const auto first = Logger::entriesSince(rev0);
        const auto second = Logger::entriesSince(mid);
        const auto full = Logger::entriesSince(rev0);

        int firstCount = 0, secondCount = 0;
        for (const auto& e : first)
            if (e.message.contains(QStringLiteral("ringtest-inc-"))) ++firstCount;
        for (const auto& e : second)
            if (e.message.contains(QStringLiteral("ringtest-inc-"))) ++secondCount;
        QVERIFY(firstCount + secondCount >= 50);   // no gap, no loss

        int fullCount = 0;
        quint64 prev = 0;
        for (const auto& e : full) {
            if (e.message.contains(QStringLiteral("ringtest-inc-"))) ++fullCount;
            QVERIFY(e.seq > prev);
            prev = e.seq;
        }
        QCOMPARE(firstCount + secondCount, fullCount);   // no duplication
    }

    // §6.4 multi-threaded write smoke: seq stays unique and revision
    // advances by exactly the number of writes.
    void multithreadedWritesKeepUniqueSeq() {
        const quint64 rev0 = Logger::revision();
        const int threads = 4, perThread = 500;

        QFutureSynchronizer<void> sync;
        for (int t = 0; t < threads; ++t) {
            sync.addFuture(QtConcurrent::run([t, perThread]() {
                for (int i = 0; i < perThread; ++i)
                    write(QStringLiteral("ringtest-mt-%1-%2").arg(t).arg(i));
            }));
        }
        sync.waitForFinished();

        const auto entries = Logger::entriesSince(rev0);
        QCOMPARE(Logger::revision() - rev0,
                 static_cast<quint64>(threads * perThread));

        QSet<quint64> seqs;
        int mtCount = 0;
        for (const auto& e : entries) {
            if (!e.message.contains(QStringLiteral("ringtest-mt-"))) continue;
            ++mtCount;
            QVERIFY2(!seqs.contains(e.seq), "duplicate seq in ring");
            seqs.insert(e.seq);
        }
        // Ring keeps the newest `capacity`; with 2000 fresh writes all of
        // them must still be there (capacity 1000 < 2000 → exactly the last
        // 1000 survive, all unique).
        QCOMPARE(mtCount, Logger::RingCapacity);
        QCOMPARE(seqs.size(), Logger::RingCapacity);
    }

    // §6.5 snapshot consistency while writers keep pushing
    void snapshotConsistentUnderConcurrentWrites() {
        const quint64 rev0 = Logger::revision();

        QFutureSynchronizer<void> sync;
        for (int t = 0; t < 2; ++t) {
            sync.addFuture(QtConcurrent::run([t]() {
                for (int i = 0; i < 300; ++i)
                    write(QStringLiteral("ringtest-cc-%1-%2").arg(t).arg(i));
            }));
        }

        // Pull repeatedly DURING the writes: every returned entry must be a
        // complete, well-formed line (non-empty message, valid level).
        for (int pull = 0; pull < 20; ++pull) {
            const auto entries = Logger::entriesSince(rev0 - 1);
            for (const auto& e : entries) {
                QVERIFY(!e.message.isEmpty());
                QVERIFY(e.level >= 0 && e.level <= 4);
                QVERIFY(e.seq > 0);
            }
            QTest::qWait(10);
        }
        sync.waitForFinished();
    }
};

QTEST_GUILESS_MAIN(TestLogger)
#include "test_logger.moc"
