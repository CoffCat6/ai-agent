// AgentViewModel UI-layout property coverage: sidebar / context panel width
// clamping, debounced QSettings persistence and cross-instance restore.
#include "viewmodels/AgentViewModel.h"
#include "viewmodels/TraceModel.h"
#include "viewmodels/LogModel.h"
#include "core/utils/Logger.h"
#include "core/rag/RagService.h"
#include "core/utils/Activity.h"

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace NexusAI;

namespace {

// Restores the user's real ui/sidebarWidth & ui/panelWidth afterwards;
// the test shares the app's QSettings location.
class WidthSettingsGuard {
public:
    WidthSettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        sidebar = s.value("ui/sidebarWidth");
        panel = s.value("ui/panelWidth");
    }
    ~WidthSettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        if (sidebar.isValid()) s.setValue("ui/sidebarWidth", sidebar);
        else s.remove("ui/sidebarWidth");
        if (panel.isValid()) s.setValue("ui/panelWidth", panel);
        else s.remove("ui/panelWidth");
    }
    QVariant sidebar;
    QVariant panel;
};

} // namespace

class TestAgentViewModel : public QObject {
    Q_OBJECT

private slots:
    // TraceModel diffing: trailing appends -> rowsInserted, in-place status
    // changes -> dataChanged, structural changes -> modelReset.
    void traceModelDelta() {
        TraceModel m;
        QSignalSpy insertSpy(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy changeSpy(&m, &QAbstractItemModel::dataChanged);
        QSignalSpy resetSpy(&m, &QAbstractItemModel::modelReset);

        QVariantList nodes;
        nodes.append(QVariantMap{{"id", "a"}, {"type", "user_message"}, {"title", "hi"}});
        nodes.append(QVariantMap{{"id", "b"}, {"type", "thinking"},
                                 {"title", "t"}, {"status", "running"}});
        m.applyDelta(nodes);
        QCOMPARE(m.count(), 2);
        QCOMPARE(insertSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(m.intermediateCount(), 1);
        QCOMPARE(m.get(0).value("type").toString(), QStringLiteral("user_message"));

        // Status update: dataChanged only, no insert/reset, no content copies
        nodes[1] = QVariantMap{{"id", "b"}, {"type", "thinking"},
                               {"title", "t"}, {"status", "success"}};
        m.applyDelta(nodes);
        QCOMPARE(m.count(), 2);
        QCOMPARE(insertSpy.count(), 1);
        QCOMPARE(changeSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(m.get(1).value("status").toString(), QStringLiteral("success"));

        // Identical re-sync: nothing signaled
        m.applyDelta(nodes);
        QCOMPARE(insertSpy.count(), 1);
        QCOMPARE(changeSpy.count(), 1);

        // Append mixed with updates: append lands as one rowsInserted
        nodes.append(QVariantMap{{"id", "c"}, {"type", "summary"},
                                 {"title", "done"}, {"status", "completed"}});
        nodes[0] = QVariantMap{{"id", "a"}, {"type", "user_message"}, {"title", "hi!"}};
        m.applyDelta(nodes);
        QCOMPARE(m.count(), 3);
        QCOMPARE(insertSpy.count(), 2);
        QCOMPARE(m.get(0).value("title").toString(), QStringLiteral("hi!"));
        QCOMPARE(m.intermediateCount(), 1);

        // Removal (session switch with fewer nodes) -> full reset, still correct
        m.applyDelta(QVariantList{nodes.at(2)});
        QCOMPARE(m.count(), 1);
        QCOMPARE(resetSpy.count(), 1);
        QCOMPARE(m.get(0).value("id").toString(), QStringLiteral("c"));

        // rowOfId / nodeAt / clear
        QCOMPARE(m.rowOfId(QStringLiteral("c")), 0);
        QCOMPARE(m.rowOfId(QStringLiteral("zz")), -1);
        m.appendNode(QVariantMap{{"id", "d"}, {"type", "thinking"}, {"status", "running"}});
        QCOMPARE(m.intermediateCount(), 1);
        m.updateLastNode({{"status", "completed"}});
        QCOMPARE(m.get(1).value("status").toString(), QStringLiteral("completed"));
        QCOMPARE(m.nodeAt(1).value("id").toString(), QStringLiteral("d"));
        m.clearNodes();
        QCOMPARE(m.count(), 0);
    }

    // Named AgentState constants exposed to QML must mirror the core enum
    // (QML compares AgentVM.state against them instead of magic numbers).
    void namedStateConstants() {
        AgentViewModel vm(nullptr);
        QCOMPARE(vm.stateIdle(), 0);
        QCOMPARE(vm.stateThinking(), 1);
        QCOMPARE(vm.stateAwaitingToolConfirm(), 2);
        QCOMPARE(vm.stateExecutingTool(), 3);
        QCOMPARE(vm.stateSummarizing(), 4);
        QCOMPARE(vm.stateCompleted(), 5);
        QCOMPARE(vm.stateError(), 6);

        // Fresh VM: Idle is not busy; the busy window covers states 1..4
        QCOMPARE(vm.stateAsInt(), vm.stateIdle());
        QVERIFY(!vm.stateBusy());
    }

    void widthDefaultsClampingAndSignals() {
        WidthSettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("ui/sidebarWidth");
            s.remove("ui/panelWidth");
        }

        AgentViewModel vm(nullptr);
        QCOMPARE(vm.sidebarWidth(), 260);
        QCOMPARE(vm.panelWidth(), 300);

        QSignalSpy sidebarSpy(&vm, &AgentViewModel::sidebarWidthChanged);
        QSignalSpy panelSpy(&vm, &AgentViewModel::panelWidthChanged);

        vm.setSidebarWidth(340);
        QCOMPARE(vm.sidebarWidth(), 340);
        QCOMPARE(sidebarSpy.count(), 1);

        // No-op write must not emit
        vm.setSidebarWidth(340);
        QCOMPARE(sidebarSpy.count(), 1);

        // Hard clamps applied in the setter (the QML drag handler applies
        // the finer window-aware limit on top of these)
        vm.setSidebarWidth(0);
        QCOMPARE(vm.sidebarWidth(), 180);
        vm.setSidebarWidth(9999);
        QCOMPARE(vm.sidebarWidth(), 560);
        vm.setPanelWidth(0);
        QCOMPARE(vm.panelWidth(), 220);
        vm.setPanelWidth(9999);
        QCOMPARE(vm.panelWidth(), 640);
        QVERIFY(sidebarSpy.count() >= 3);
        QVERIFY(panelSpy.count() >= 2);
    }

    void widthPersistenceDebouncedAndRestored() {
        WidthSettingsGuard guard;
        {
            QSettings s("NexusAI", "NexusAI");
            s.remove("ui/sidebarWidth");
            s.remove("ui/panelWidth");
        }

        AgentViewModel vm(nullptr);
        vm.setSidebarWidth(420);
        vm.setPanelWidth(480);

        // Writes are coalesced on a 400 ms single-shot timer; let it fire
        QTest::qWait(600);

        // A fresh instance restores both widths from QSettings
        AgentViewModel restored(nullptr);
        QCOMPARE(restored.sidebarWidth(), 420);
        QCOMPARE(restored.panelWidth(), 480);
    }

    // ── Batch 8: import dedupe / same-name coexistence (FEAT-802) ──
    // Same content must be rejected with an explicit notice (not silently
    // re-embedded), and same-name-different-content files must coexist under
    // a deduplicated display name instead of overwriting each other.
    void importDedupesAndCoexists() {
        AgentViewModel vm(nullptr);
        RagService rag;
        vm.setRagService(&rag);

        const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QTemporaryDir dir(base + QStringLiteral("/nexusai-tests-XXXXXX"));
        QVERIFY(dir.isValid());

        auto writeFile = [&dir](const QString& name, const QString& content) {
            const QString path = dir.filePath(name);
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qWarning() << "test file open failed:" << path << f.errorString();
                return QString();
            }
            f.write(content.toUtf8());
            f.close();
            return path;
        };

        const QString longContent = QStringLiteral("alpha beta gamma delta epsilon. ").repeated(40);
        const QString pathA = writeFile("doc.txt", longContent);
        QVERIFY(!pathA.isEmpty());
        const QString pathB = writeFile("doc-dup.txt", longContent);              // same content
        QVERIFY(!pathB.isEmpty());

        QSignalSpy notices(&vm, &AgentViewModel::noticeOccurred);
        QSignalSpy errors(&vm, &AgentViewModel::errorOccurred);

        vm.importDocument(pathA);
        QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 1, 20000);
        QCOMPARE(errors.count(), 0);
        QTRY_COMPARE(rag.documents().size(), 1);

        // Same content, different file name → rejected, no second document
        vm.importDocument(pathB);
        QTRY_VERIFY_WITH_TIMEOUT(notices.count() >= 2, 20000);
        QVERIFY2(notices.last().first().toString().contains(QStringLiteral("已存在")),
                 qPrintable(QStringLiteral("expected a duplicate notice, got: ")
                                + notices.last().first().toString()));
        QCOMPARE(rag.documents().size(), 1);

        // Same name, different content → coexists under a deduplicated name
        const QString pathC = writeFile("doc.txt",
            QStringLiteral("zzz different payload. ").repeated(40));
        vm.importDocument(pathC);
        QTRY_COMPARE_WITH_TIMEOUT(rag.documents().size(), 2, 20000);
        const QJsonArray docs = rag.documents();
        QVERIFY(docs.at(0).toObject().value("name").toString()
                    != docs.at(1).toObject().value("name").toString());
        bool sawBase = false, sawVariant = false;
        for (const auto& d : docs) {
            const QString n = d.toObject().value("name").toString();
            if (n == QStringLiteral("doc.txt")) sawBase = true;
            if (n.startsWith(QStringLiteral("doc ("))) sawVariant = true;
        }
        QVERIFY2(sawBase && sawVariant, "deduplicated display name missing");
    }

    // ── 日志增强: user actions + category filter ──
    // VM entry points report user actions through the Activity facade into
    // the injected sink, and the LogPanel model can filter by category.
    void userActionsFeedUsageSink() {
        AgentViewModel vm(nullptr);
        RagService rag;
        vm.setRagService(&rag);

        QVector<QString> actions;
        Activity::setUsageSink([&](const QString& category, const QString& action,
                                   const QString& target, quint64 requestId,
                                   const QString& detail) {
            Q_UNUSED(category); Q_UNUSED(target); Q_UNUSED(requestId); Q_UNUSED(detail);
            actions.append(action);
        });

        vm.newChat();                     // session.new
        vm.removeDocument(QStringLiteral("nope.txt"));   // doc.remove

        Activity::setUsageSink({});
        QVERIFY2(actions.contains(QStringLiteral("session.new")),
                 "session.new not reported");
        QVERIFY2(actions.contains(QStringLiteral("doc.remove")),
                 "doc.remove not reported");
    }

    // 日志增强二轮: the panel view is an append-only session record —
    // entries logged while the panel is CLOSED must still be there when it
    // reopens, and ring evictions must NOT remove rows from the view
    // (both were broken by the old eviction-mirroring pull).
    void logViewPersistsAcrossPanelToggle() {
        Logger::install();
        Logger::setMinLevel(QtInfoMsg);

        LogModel model;
        model.setFilterCategory(QString());
        model.setFilterLevel(-1);            // ALL
        model.setPolling(true);              // panel "open"
        QTest::qWait(250);
        const int openRows = model.count();
        model.setPolling(false);             // panel "closed"

        // Logs happen while the panel is closed (this is the old data-loss
        // window: the fast poller was stopped and ring eviction ate rows).
        const quint64 revAtClose = Logger::revision();
        for (int i = 0; i < 12; ++i)
            Activity::usage(Activity::Kind::User,
                            QStringLiteral("closed-window-%1").arg(i));
        QVERIFY(Logger::revision() > revAtClose);

        model.setPolling(true);              // panel reopened
        QTest::qWait(250);
        QVERIFY2(model.count() > openRows,
                 "entries logged while the panel was closed were lost");

        // The earliest closed-window entry must still be in the view.
        bool sawEarliest = false;
        for (int i = 0; i < model.count(); ++i) {
            const QString msg = model.data(model.index(i, 0),
                                           LogModel::MessageRole).toString();
            if (msg.contains(QStringLiteral("closed-window-0"))) { sawEarliest = true; break; }
        }
        QVERIFY2(sawEarliest, "the first closed-window entry vanished");
    }

    // 分类往返契约锁：user 过滤只见 user 行，切回“全部”行数恢复且含 app 行
    void logModelCategoryRoundTrip() {
        Logger::install();
        Logger::setMinLevel(QtInfoMsg);

        Activity::usage(Activity::Kind::User, QStringLiteral("rt.user"));
        Activity::report(Activity::Kind::App, QStringLiteral("rt.app"));

        LogModel model;
        model.setFilterCategory(QStringLiteral("nexus.user"));
        model.setPolling(true);
        QTest::qWait(300);
        model.setPolling(false);
        QVERIFY(model.count() > 0);
        for (int i = 0; i < model.count(); ++i) {
            const QString cat = model.data(model.index(i, 0),
                                           LogModel::CategoryRole).toString();
            QVERIFY2(cat.startsWith(QStringLiteral("nexus.user")),
                     qPrintable(QStringLiteral("user filter leaked: %1").arg(cat)));
        }
        const int userRows = model.count();

        model.setFilterCategory(QString());   // back to All
        model.setPolling(true);
        QTest::qWait(200);
        model.setPolling(false);
        QVERIFY2(model.count() > userRows,
                 "returning to All did not restore the other categories");
    }

    void logModelCategoryFilter() {
        // The message handler feeds the ring buffer — install it (the
        // harness binaries each manage their own Logger lifetime).
        Logger::install();
        Logger::setMinLevel(QtInfoMsg);

        // Seed the ring with user + app events, then check the filter
        Activity::usage(Activity::Kind::User, QStringLiteral("filter.user"));
        Activity::report(Activity::Kind::App, QStringLiteral("filter.app"));

        LogModel model;
        model.setFilterCategory(QStringLiteral("nexus.user"));
        model.setPolling(true);
        QTest::qWait(300);               // let the 100 ms poller catch up
        model.setPolling(false);

        QVERIFY(model.count() > 0);
        for (int i = 0; i < model.count(); ++i) {
            const QModelIndex idx = model.index(i, 0);
            const QString cat = model.data(idx, LogModel::CategoryRole).toString();
            QVERIFY2(cat.startsWith(QStringLiteral("nexus.user")),
                     qPrintable(QStringLiteral("non-user row in filtered view: %1").arg(cat)));
        }

        // Switch to "all" → rows of other categories become visible
        model.setFilterCategory(QString());
        model.setPolling(true);
        QTest::qWait(150);
        model.setPolling(false);
        bool sawApp = false;
        for (int i = 0; i < model.count(); ++i) {
            const QModelIndex idx = model.index(i, 0);
            const QString cat = model.data(idx, LogModel::CategoryRole).toString();
            if (cat == QStringLiteral("nexus.app") || cat.isEmpty()) sawApp = true;
        }
        QVERIFY2(sawApp, "app rows missing in unfiltered view");
    }
};

QTEST_GUILESS_MAIN(TestAgentViewModel)
#include "test_viewmodel.moc"
