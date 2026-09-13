// AgentViewModel UI-layout property coverage: sidebar / context panel width
// clamping, debounced QSettings persistence and cross-instance restore.
#include "viewmodels/AgentViewModel.h"
#include "core/agent/AIAgent.h"
#include "viewmodels/TraceModel.h"
#include "viewmodels/LogModel.h"
#include "viewmodels/I18nManager.h"
#include "core/memory/SqliteMemory.h"
#include "core/utils/Logger.h"
#include "core/rag/RagService.h"
#include "core/utils/Activity.h"

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QTranslator>
#include <QUrl>

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
        // Layout defaults now seed the panel restore fallback (General
        // Settings); without clearing them a user-saved default would leak
        // into the fresh-VM expectations below.
        defSidebar = s.value("prefs/v1/ui.defaultSidebarWidth");
        defPanel = s.value("prefs/v1/ui.defaultPanelWidth");
        s.remove("prefs/v1/ui.defaultSidebarWidth");
        s.remove("prefs/v1/ui.defaultPanelWidth");
    }
    ~WidthSettingsGuard() {
        QSettings s("NexusAI", "NexusAI");
        if (sidebar.isValid()) s.setValue("ui/sidebarWidth", sidebar);
        else s.remove("ui/sidebarWidth");
        if (panel.isValid()) s.setValue("ui/panelWidth", panel);
        else s.remove("ui/panelWidth");
        if (defSidebar.isValid()) s.setValue("prefs/v1/ui.defaultSidebarWidth", defSidebar);
        else s.remove("prefs/v1/ui.defaultSidebarWidth");
        if (defPanel.isValid()) s.setValue("prefs/v1/ui.defaultPanelWidth", defPanel);
        else s.remove("prefs/v1/ui.defaultPanelWidth");
    }
    QVariant sidebar;
    QVariant panel;
    QVariant defSidebar;
    QVariant defPanel;
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
    // ── Batch 18: session export (FEAT-1801) ──────────

    void exportSessionMarkdownRoundtrip() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession("s1", "Exported");
        memory.saveMessage("s1", QJsonObject{{"role", "user"},
            {"content", QStringLiteral("第一问：什么是 FTS5")}});
        memory.saveMessage("s1", QJsonObject{{"role", "assistant"},
            {"content", QStringLiteral("FTS5 是 SQLite 的全文检索扩展模块")}});

        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("export.md");

        QSignalSpy notice(&vm, &AgentViewModel::noticeOccurred);
        QSignalSpy error(&vm, &AgentViewModel::errorOccurred);
        QVERIFY(vm.exportSession("s1", path, QStringLiteral("md")));
        QCOMPARE(notice.count(), 1);
        QCOMPARE(error.count(), 0);

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains(QStringLiteral("# NexusAI 会话导出")), "missing export header");
        QVERIFY2(text.contains(QStringLiteral("- 会话 ID：s1")), "missing session id");
        QVERIFY2(text.contains(QStringLiteral("## 用户 ·")), "missing user role header");
        QVERIFY2(text.contains(QStringLiteral("第一问：什么是 FTS5")), "missing user content");
        QVERIFY2(text.contains(QStringLiteral("## 助手 ·")), "missing assistant role header");
        QVERIFY2(text.contains(QStringLiteral("FTS5 是 SQLite 的全文检索扩展模块")), "missing assistant content");
        // No trace nodes stored → no trace summary section
        QVERIFY2(!text.contains(QStringLiteral("## 过程摘要")), "trace section must be absent");
    }

    void exportSessionJsonRoundtrip() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession("s1", "Json");
        memory.saveMessage("s1", QJsonObject{{"role", "user"}, {"content", "q"}});
        memory.saveMessage("s1", QJsonObject{{"role", "assistant"}, {"content", "a"}});
        QJsonObject node;
        node["id"] = "n1";   // saveTraceNode binds the JSON key "id" (column node_id, NOT NULL)
        node["type"] = "tool_call";
        node["title"] = "read_file";
        node["status"] = "success";
        memory.saveTraceNode("s1", node, 0);

        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("export.json");
        QVERIFY(vm.exportSession("s1", path, QStringLiteral("json")));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value("sessionId").toString(), QStringLiteral("s1"));
        QVERIFY(!doc.object().value("exportedAt").toString().isEmpty());
        const QJsonArray messages = doc.object().value("messages").toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value("content").toString(), QStringLiteral("q"));
        QVERIFY(messages.at(0).toObject().contains("timestamp"));   // Batch 18 loadMessages enrichment
        const QJsonArray traceNodes = doc.object().value("traceNodes").toArray();
        QCOMPARE(traceNodes.size(), 1);
        QCOMPARE(traceNodes.at(0).toObject().value("title").toString(), QStringLiteral("read_file"));
    }

    // Empty session: no crash, a valid skeleton in both formats; unsupported
    // format and a missing memory backend fail with an error signal.
    void exportSessionEmptySession() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString jsonPath = dir.filePath("empty.json");
        QVERIFY(vm.exportSession("empty", jsonPath, QStringLiteral("json")));
        QFile f(jsonPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value("messages").toArray().size(), 0);
        QCOMPARE(doc.object().value("traceNodes").toArray().size(), 0);

        const QString mdPath = dir.filePath("empty.md");
        QVERIFY(vm.exportSession("empty", mdPath, QStringLiteral("md")));
        QVERIFY(QFile(mdPath).exists());

        QSignalSpy error(&vm, &AgentViewModel::errorOccurred);
        QVERIFY(!vm.exportSession("empty", dir.filePath("x.xml"), QStringLiteral("xml")));
        QCOMPARE(error.count(), 1);
        AgentViewModel noBackend(nullptr);
        QSignalSpy error2(&noBackend, &AgentViewModel::errorOccurred);
        QVERIFY(!noBackend.exportSession("empty", dir.filePath("y.json"), QStringLiteral("json")));
        QCOMPARE(error2.count(), 1);
    }

    // Tool evidence lives only in trace_nodes (MEM-701 contract): the trace
    // summary section appears in Markdown exports when nodes exist.
    void exportSessionWithToolRounds() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession("s1", "With tools");
        memory.saveMessage("s1", QJsonObject{{"role", "user"}, {"content", "list the dir"}});
        memory.saveMessage("s1", QJsonObject{{"role", "assistant"}, {"content", "done"}});
        QJsonObject node;
        node["id"] = "n1";   // saveTraceNode binds the JSON key "id" (column node_id, NOT NULL)
        node["type"] = "tool_call";
        node["title"] = "list_dir";
        node["status"] = "success";
        memory.saveTraceNode("s1", node, 0);

        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("tools.md");
        QVERIFY(vm.exportSession("s1", path, QStringLiteral("md")));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains(QStringLiteral("## 过程摘要")), "missing trace summary section");
        QVERIFY2(text.contains(QStringLiteral("[success] tool_call · list_dir")),
                 "trace node line missing from the summary");
    }

    // Batch 21 (C2): embedded CR/LF in a trace node's status/type/title must
    // be flattened onto the single bullet line, or the Markdown list breaks.
    void exportSessionFlattensTraceNewlines() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession("s1", "Newlines");
        memory.saveMessage("s1", QJsonObject{{"role", "user"}, {"content", "q"}});
        QJsonObject node;
        node["id"] = "n1";
        node["type"] = "tool_call";
        // Embedded LF (incl. CRLF) must not leak a second list line.
        node["title"] = QStringLiteral("read_file\r\n/evil\npath");
        node["status"] = QStringLiteral("failed\nbroken");
        memory.saveTraceNode("s1", node, 0);

        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("newlines.md");
        QVERIFY(vm.exportSession("s1", path, QStringLiteral("md")));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
        int bulletLines = 0;
        QString bullet;
        for (const QString& line : lines) {
            if (line.startsWith(QStringLiteral("- ["))) {
                ++bulletLines;
                bullet = line;
                QVERIFY2(!line.contains('\r'), "CR must be stripped from trace bullet");
            }
        }
        QCOMPARE(bulletLines, 1);   // one node -> exactly one bullet line
        // LF (also the LF of CRLF after CR removal) becomes a single space.
        QCOMPARE(bullet,
                 QStringLiteral("- [failed broken] tool_call · read_file /evil path"));
    }

    // QML FileDialog hands selectedFile over as a file:// URL (percent-encoded
    // for spaces / non-ASCII): exercise the isLocalFile() -> toLocalFile()
    // normalization branch with a non-ASCII, space-containing target path.
    void exportSessionUrlPath() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession("s1", "Url export");
        memory.saveMessage("s1", QJsonObject{{"role", "user"}, {"content", "q"}});
        memory.saveMessage("s1", QJsonObject{{"role", "assistant"}, {"content", "a"}});

        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString subDir = dir.filePath(QStringLiteral("导出 测试"));
        QVERIFY(QDir().mkpath(subDir));
        const QString mdUrl = QUrl::fromLocalFile(
            QDir(subDir).filePath(QStringLiteral("会话 导出.md"))).toString();
        const QString jsonUrl = QUrl::fromLocalFile(
            QDir(subDir).filePath(QStringLiteral("会话 导出.json"))).toString();

        QSignalSpy notice(&vm, &AgentViewModel::noticeOccurred);
        QSignalSpy error(&vm, &AgentViewModel::errorOccurred);
        QVERIFY(vm.exportSession("s1", mdUrl, QStringLiteral("md")));
        QCOMPARE(notice.count(), 1);
        QCOMPARE(error.count(), 0);
        QVERIFY(vm.exportSession("s1", jsonUrl, QStringLiteral("json")));
        QCOMPARE(notice.count(), 2);
        QCOMPARE(error.count(), 0);

        QFile mdFile(QUrl(mdUrl).toLocalFile());
        QVERIFY(mdFile.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(mdFile.readAll());
        QVERIFY2(text.contains(QStringLiteral("# NexusAI 会话导出")), "missing export header");
        QVERIFY2(text.contains(QStringLiteral("- 会话 ID：s1")), "missing session id");

        QFile jsonFile(QUrl(jsonUrl).toLocalFile());
        QVERIFY(jsonFile.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value("messages").toArray().size(), 2);
    }

    // Batch 24 (G9): the embedded English .qm carries real translations for
    // known sources (the .qm files are also linked into this test binary via
    // qt_add_resources in tests/CMakeLists.txt).
    void englishQmTranslatesKnownStrings() {
        QTranslator translator;
        QVERIFY(translator.load(QLocale(QLocale::English), QStringLiteral("nexus_ai"),
                                QStringLiteral("_"), QStringLiteral(":/i18n")));
        QCOMPARE(translator.translate("AgentChatPage", "折叠过程"),
                 QStringLiteral("Collapse process"));
        QCOMPARE(translator.translate("InterfacePage", "跟随系统"),
                 QStringLiteral("Follow System"));
        QCOMPARE(translator.translate("SqliteMemory", "刚刚"),
                 QStringLiteral("Just now"));
        QCOMPARE(translator.translate("SqliteMemory", "%1 分钟前"),
                 QStringLiteral("%1 minutes ago"));
        // Language entries stay untranslated by design (native names).
        QCOMPARE(translator.translate("InterfacePage", "简体中文"),
                 QStringLiteral("简体中文"));
    }

    // The switch path: en now loads a translator instead of skipping it, and
    // switching back / to "auto" keeps currentLocale in sync with the signal.
    void applyLocaleSwitchesBothWays() {
        I18nManager* i18n = I18nManager::instance();
        const QString original = i18n->currentLocale();
        QSignalSpy spy(i18n, &I18nManager::localeChanged);

        i18n->applyLocale(QStringLiteral("en"));
        QCOMPARE(i18n->currentLocale(), QStringLiteral("en"));
        QCOMPARE(spy.count(), original == QStringLiteral("en") ? 0 : 1);

        i18n->applyLocale(QStringLiteral("zh-CN"));
        QCOMPARE(i18n->currentLocale(), QStringLiteral("zh-CN"));

        i18n->applyLocale(original);   // restore for the rest of the suite
        QCOMPARE(i18n->currentLocale(), original);
    }

    // Batch 29 (F7): parallel session tabs. Simulation path (no agent) —
    // the timers are intentionally not driven to completion; the point is
    // that starting a task in another tab never touches the first one.
    void parallelTabsStayIndependent() {
        AgentViewModel vm(nullptr);
        QCOMPARE(vm.tabModel().size(), 1);
        QCOMPARE(vm.activeTabIndex(), 0);

        vm.startTask(QStringLiteral("task A"));   // tab 0 begins (simulation)
        QVERIFY(vm.stateBusy());

        vm.openTab();                             // tab 1 + activate
        QCOMPARE(vm.activeTabIndex(), 1);
        QCOMPARE(vm.tabModel().size(), 2);
        QVERIFY(!vm.stateBusy());                 // fresh tab is idle

        vm.startTask(QStringLiteral("task B"));   // tab 1 begins
        QVERIFY(vm.stateBusy());

        // Switching back: tab 0 is still running its own task, with its own
        // trace — the two tabs share nothing.
        vm.activateTab(0);
        QVERIFY(vm.stateBusy());
        QCOMPARE(vm.traceCount(), 1);             // "task A" user message only
        vm.activateTab(1);
        QCOMPARE(vm.traceCount(), 1);             // "task B" user message only

        // Stopping tab 1 does not touch tab 0's task.
        vm.stop();
        vm.activateTab(0);
        QVERIFY(vm.stateBusy());

        // Tab titles start as "新会话" until a session id exists.
        const QVariantList tabs = vm.tabModel();
        QCOMPARE(tabs.size(), 2);
        QVERIFY(tabs.at(0).toMap().value("busy").toBool());
    }

    // Closing a running tab stops its task and drops it from the model; the
    // last remaining tab cannot be closed.
    void closeRunningTabStopsItsTask() {
        AgentViewModel vm(nullptr);
        vm.startTask(QStringLiteral("task A"));   // tab 0 busy
        vm.openTab();
        vm.activateTab(1);
        vm.startTask(QStringLiteral("task B"));   // tab 1 busy

        vm.closeTab(1);
        QCOMPARE(vm.tabModel().size(), 1);
        QCOMPARE(vm.activeTabIndex(), 0);
        QVERIFY(vm.stateBusy());                  // tab 0 unaffected

        vm.closeTab(0);                           // guard: last tab stays
        QCOMPARE(vm.tabModel().size(), 1);
    }

    // Batch 29 fix: each tab's factory agent is a separate instance with a
    // separate LLM service — two tabs sharing one service instance abort
    // each other's streams (LLMServiceBase keeps a single in-flight
    // context) and broadcast streaming chunks into the sibling trace.
    void factoryCreatesIndependentAgents() {
        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        QVector<AIAgent*> created;
        vm.setAgentFactory([&created, &vm]() -> AIAgent* {
            auto* agent = new AIAgent(&vm);
            created.append(agent);
            return agent;
        });
        vm.setAgent(&templateAgent);

        vm.openTab();                         // factory agent #1
        vm.openTab();                         // factory agent #2
        QCOMPARE(created.size(), 2);
        QVERIFY(created[0] != created[1]);

        QVector<AIAgent*> seen;
        vm.applyToAgents([&seen](AIAgent* a) { seen.append(a); });
        QCOMPARE(seen.size(), 3);             // template + two factory agents
        QVERIFY(seen.contains(&templateAgent));
        QVERIFY(seen.contains(created[0]));
        QVERIFY(seen.contains(created[1]));

        QPointer<AIAgent> firstGuard = created[0];
        vm.closeTab(1);                       // destroys tab agent #1
        QTRY_VERIFY(firstGuard.isNull());

        seen.clear();
        vm.applyToAgents([&seen](AIAgent* a) { seen.append(a); });
        QCOMPARE(seen.size(), 2);             // template + surviving agent
    }

    // Loading a session while the active tab is busy opens a new tab for it
    // instead of interrupting (or silently ignoring) the request.
    void busyTabLoadSessionOpensNewTab() {
        AgentViewModel vm(nullptr);
        vm.startTask(QStringLiteral("task A"));   // tab 0 busy
        const int tabsBefore = vm.tabModel().size();

        vm.loadSession(QStringLiteral("session-x"));
        QCOMPARE(vm.tabModel().size(), tabsBefore + 1);
        QCOMPARE(vm.activeTabIndex(), tabsBefore);
        QVERIFY(!vm.stateBusy());                 // the fresh tab is idle

        // An idle active tab loads in place.
        vm.loadSession(QStringLiteral("session-y"));
        QCOMPARE(vm.tabModel().size(), tabsBefore + 1);
    }
};

QTEST_GUILESS_MAIN(TestAgentViewModel)
#include "test_viewmodel.moc"
