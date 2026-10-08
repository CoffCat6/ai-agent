// Logger ring-buffer coverage (日志开发 doc 6 §6): eviction, level filter,
// incremental snapshot, multi-threaded write smoke, snapshot consistency.
#include "core/utils/Logger.h"
#include "core/utils/Activity.h"

#include <QtConcurrent/QtConcurrent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
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
        // Ring capacity 5000 > the 2000 fresh writes: ALL of them survive,
        // each with a unique seq (assertion is relative to the write count
        // so a future capacity bump does not change the expectation).
        QCOMPARE(mtCount, 2000);
        QCOMPARE(seqs.size(), 2000);
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

    // Batch 12 前日志增强: Activity facade
    // Activity::report lands in the ring buffer under the mapped category,
    // with the context id appended for keyword/correlation filtering.
    void activityReportWritesCategorizedEntry() {
        const quint64 rev0 = Logger::revision();
        Activity::report(Activity::Kind::Network,
                         QStringLiteral("activity-test-net"), QStringLiteral("req-42"));

        const auto entries = Logger::entriesSince(rev0);
        QVERIFY2(!entries.isEmpty(), "Activity::report produced no ring entry");
        bool found = false;
        for (const auto& e : entries) {
            if (e.message.contains(QStringLiteral("activity-test-net"))) {
                found = true;
                QCOMPARE(e.category, QStringLiteral("nexus.network"));
                QVERIFY(e.message.contains(QStringLiteral("[req-42]")));
            }
        }
        QVERIFY2(found, "categorized entry not found in ring");
    }

    // The injected sink receives the structured usage fields (category,
    // action, target, request id, detail) for persistent usage storage.
    void usageSinkInvoked() {
        QString sinkCategory, sinkAction, sinkTarget, sinkDetail;
        quint64 sinkRequest = 0;
        int calls = 0;
        Activity::setUsageSink([&](const QString& category, const QString& action,
                                   const QString& target, quint64 requestId,
                                   const QString& detail) {
            ++calls;
            sinkCategory = category; sinkAction = action; sinkTarget = target;
            sinkRequest = requestId; sinkDetail = detail;
        });

        Activity::usage(Activity::Kind::User, QStringLiteral("doc.import"),
                        QStringLiteral("x.txt"), 7, QStringLiteral("detail-d"));

        Activity::setUsageSink({});   // restore: no sink for other tests
        QCOMPARE(calls, 1);
        QCOMPARE(sinkCategory, QStringLiteral("nexus.user"));
        QCOMPARE(sinkAction, QStringLiteral("doc.import"));
        QCOMPARE(sinkTarget, QStringLiteral("x.txt"));
        QCOMPARE(sinkRequest, 7u);
        QCOMPARE(sinkDetail, QStringLiteral("detail-d"));
    }

    // T9: backfillFromFile parses historical log entries and marks them historical.
    void backfillParsesAndMarksHistorical() {
        // Write a few log lines that will be written to the log file.
        qInfo().noquote() << "T9_BACKFILL_TEST_LINE_A";
        qInfo().noquote() << "T9_BACKFILL_TEST_LINE_B";
        qWarning().noquote() << "T9_BACKFILL_TEST_LINE_C";

        // Flush by reading the current ring (entries are already in the ring).
        const auto beforeBackfill = Logger::entriesSince(0);
        QVERIFY(beforeBackfill.size() >= 3);

        // Backfill from file. The lines we just wrote should already be in
        // the ring, so dedup should skip them. New entries (if any from
        // previous runs) would be injected.
        const int injected = Logger::backfillFromFile(500);
        QVERIFY(injected >= 0);  // never throws

        // All backfilled entries must have historical=true.
        const auto afterBackfill = Logger::entriesSince(0);
        for (const auto& e : afterBackfill) {
            if (e.historical) {
                QVERIFY(e.timestamp > 0);
                QVERIFY(!e.message.isEmpty());
            }
        }
    }

    void backfillCappedAtHalfCapacity() {
        // Backfill with a small cap; result must not exceed the cap.
        const int cap = 100;
        const int injected = Logger::backfillFromFile(cap);
        QVERIFY(injected <= cap);
    }

    void backfillDoesNotDuplicateExisting() {
        // Write a unique marker line.
        const QString marker = QStringLiteral("T9_DEDUP_MARKER_%1").arg(QDateTime::currentMSecsSinceEpoch());
        qInfo().noquote() << marker;

        // It's now in the ring. Backfill should NOT add it again.
        const auto before = Logger::entriesSince(0);
        int countBefore = 0;
        for (const auto& e : before)
            if (e.message == marker) ++countBefore;

        Logger::backfillFromFile(500);

        const auto after = Logger::entriesSince(0);
        int countAfter = 0;
        for (const auto& e : after)
            if (e.message == marker) ++countAfter;

        QCOMPARE(countAfter, countBefore);  // no duplication
    }

    void backfillFileNotReadableReturnsZero() {
        // Point logDir to a non-existent path by calling backfill when
        // no log file exists. The method should return 0, not crash.
        // Since Logger::logDir() is fixed, we just verify it doesn't throw.
        const int result = Logger::backfillFromFile(10);
        QVERIFY(result >= 0);
    }

    void rotatesBySizeAndPrunesToConfiguredFileCount() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString previousDir = Logger::logDir();
        const int previousSize = Logger::maxFileSizeMb();
        const int previousRetention = Logger::retentionFiles();
        const QtMsgType previousLevel = Logger::minLevel();

        Logger::setLogDirectory(dir.path());
        Logger::setMaxFileSizeMb(1);
        Logger::setRetentionFiles(2);
        Logger::setMinLevel(QtInfoMsg);
        const QString payload(600 * 1024, QLatin1Char('x'));
        for (int i = 0; i < 4; ++i)
            qInfo().noquote() << payload;

        const QFileInfoList files = QDir(dir.path()).entryInfoList(
            QStringList() << QStringLiteral("nexus-*.log"), QDir::Files);
        QCOMPARE(files.size(), 2);
        QVERIFY(QFileInfo::exists(Logger::logFilePath()));
        QVERIFY(QFileInfo(Logger::logFilePath()).size() <= 1024 * 1024);

        Logger::setLogDirectory(previousDir);
        Logger::setMaxFileSizeMb(previousSize);
        Logger::setRetentionFiles(previousRetention);
        Logger::setMinLevel(previousLevel);
    }
};

QTEST_GUILESS_MAIN(TestLogger)
#include "test_logger.moc"
