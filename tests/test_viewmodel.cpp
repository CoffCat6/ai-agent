// AgentViewModel UI-layout property coverage: sidebar / context panel width
// clamping, debounced QSettings persistence and cross-instance restore.
#include "viewmodels/AgentViewModel.h"
#include "core/agent/AIAgent.h"
#include "viewmodels/TraceModel.h"
#include "viewmodels/LogModel.h"
#include "viewmodels/I18nManager.h"
#include "viewmodels/McpManager.h"
#include "core/mcp/McpClient.h"
#include "core/mcp/McpConfig.h"
#include "core/mcp/McpToolAdapter.h"
#include "core/tools/ToolRegistry.h"
#include "core/memory/SqliteMemory.h"
#include "core/utils/Logger.h"
#include "core/rag/RagService.h"
#include "core/utils/Activity.h"

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

    // docs/20 §3.1/§3.2：lastIndexOfType 定位类型末行。finishTask 会在助手
    // 回复（summary）之后追加 metrics 节点，所以整表末行恒为 metrics ——
    // 消息级操作按钮必须用类型末行，而非整表末行。
    void traceModelLastIndexOfType() {
        TraceModel m;
        m.resetNodes({
            QVariantMap{{"id", "u1"}, {"type", "user_message"}},
            QVariantMap{{"id", "s1"}, {"type", "summary"}},
            QVariantMap{{"id", "m1"}, {"type", "metrics"}},
            QVariantMap{{"id", "u2"}, {"type", "user_message"}},
            QVariantMap{{"id", "s2"}, {"type", "summary"}},
            QVariantMap{{"id", "m2"}, {"type", "metrics"}},
        });
        // 类型末行： summary 在 4（非整表末行 5），user_message 在 3。
        QCOMPARE(m.lastIndexOfType(QStringLiteral("summary")), 4);
        QCOMPARE(m.lastIndexOfType(QStringLiteral("user_message")), 3);
        QCOMPARE(m.lastIndexOfType(QStringLiteral("metrics")), 5);
        QCOMPARE(m.lastIndexOfType(QStringLiteral("nope")), -1);
    }

    // docs/20 §3.3：输入历史环（进程内、相邻去重、↑ 前移 / ↓ 后移）；
    // 无 agent 时尝试发送也保留输入，便于配置模型后重试。
    void inputHistoryNavigateAndDedupe() {
        AgentViewModel vm(nullptr);

        // 空历史：两个方向都返回空串。
        QCOMPARE(vm.historyPrev(), QString());
        QCOMPARE(vm.historyNext(), QString());

        vm.startTask(QStringLiteral("第一条"));
        vm.stop();
        vm.startTask(QStringLiteral("第二条"));
        vm.stop();
        vm.startTask(QStringLiteral("第二条"));   // 相邻重复不入队
        vm.stop();

        // ↑ 回溯：第二条 → 第一条 → 已到最旧仍返回第一条。
        QCOMPARE(vm.historyPrev(), QStringLiteral("第二条"));
        QCOMPARE(vm.historyPrev(), QStringLiteral("第一条"));
        QCOMPARE(vm.historyPrev(), QStringLiteral("第一条"));

        // ↓ 后移：第二条 → 回到草稿态（空串）。
        QCOMPARE(vm.historyNext(), QStringLiteral("第二条"));
        QCOMPARE(vm.historyNext(), QString());
        QCOMPARE(vm.historyNext(), QString());

        // resetInputHistory 后再 ↑ 回到最新一条。
        vm.historyPrev();
        vm.resetInputHistory();
        QCOMPARE(vm.historyPrev(), QStringLiteral("第二条"));
    }

    // docs/20 §3.3：空白输入不入历史。
    void inputHistorySkipsBlank() {
        AgentViewModel vm(nullptr);
        vm.startTask(QStringLiteral("   "));
        vm.stop();
        vm.startTask(QString());
        vm.stop();
        QCOMPARE(vm.historyPrev(), QString());
    }

    // docs/20 §3.4（A4 拖拽附件）：窗口级拖放解析。文本文件 → file 附件
    // 描述符经 attachmentsDropped 发射；不支持的类型仅计数提示、不发射。
    void handleDroppedFilesClassifiesAndEmits() {
        AgentViewModel vm(nullptr);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString textPath = dir.filePath("note.txt");
        { QFile f(textPath); QVERIFY(f.open(QIODevice::WriteOnly));
          f.write("hello attachment"); }
        const QString binPath = dir.filePath("archive.zip");
        { QFile f(binPath); QVERIFY(f.open(QIODevice::WriteOnly));
          f.write("PK"); }

        QSignalSpy dropped(&vm, &AgentViewModel::attachmentsDropped);
        QSignalSpy notice(&vm, &AgentViewModel::noticeOccurred);

        vm.handleDroppedFiles({QUrl::fromLocalFile(textPath),
                               QUrl::fromLocalFile(binPath)});
        QCOMPARE(dropped.count(), 1);
        const QVariantList atts = dropped.at(0).at(0).toList();
        QCOMPARE(atts.size(), 1);
        const QVariantMap file = atts.at(0).toMap();
        QCOMPARE(file.value("kind").toString(), QStringLiteral("file"));
        QCOMPARE(file.value("name").toString(), QStringLiteral("note.txt"));
        QCOMPARE(file.value("content").toString(), QStringLiteral("hello attachment"));
        // zip 不支持：计数提示但不进附件列表。
        QCOMPARE(notice.count(), 1);

        // 全为不支持类型：不发射 attachmentsDropped，仅提示。
        dropped.clear();
        notice.clear();
        vm.handleDroppedFiles({QUrl::fromLocalFile(binPath)});
        QCOMPARE(dropped.count(), 0);
        QCOMPARE(notice.count(), 1);

        // 空列表：无任何信号。
        dropped.clear();
        notice.clear();
        vm.handleDroppedFiles({});
        QCOMPARE(dropped.count(), 0);
        QCOMPARE(notice.count(), 0);
    }

    // docs/20 §3.4（A4）回归：QML 的 url 对象没有 toLocalFile()，路径转换
    // 必须在 C++ 用 QUrl 完成。这里锁定三条边界：
    //  1) 含空格/中文/URL 保留字符（# %）的真实文件必须能还原原名并读盘，
    //     否则拖拽会静默丢附件或读到错误路径；
    //  2) 键为 toEncoded()（percent-encoded）的 URL 也要还原——QML 侧若
    //     用 toString() 取路径再传字符串就会踩到未解码的坑；
    //  3) 非 file: URL（http 等）不得被当成文件读取，计为不支持。
    void handleDroppedFilesDecodesLocalUrls() {
        AgentViewModel vm(nullptr);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 空格 + 中文 + '#'/'%'（两者在 URL 里必须转义，否则路径被截断）。
        const QString tricky = dir.filePath(QStringLiteral("my note 报告 #1%.txt"));
        { QFile f(tricky); QVERIFY(f.open(QIODevice::WriteOnly));
          f.write("tricky content"); }
        QVERIFY(QFileInfo::exists(tricky));

        const QUrl encoded = QUrl::fromLocalFile(tricky);
        // toString() 是「漂亮」形式，保留字符才转义；toEncoded() 才是全转义。
        QVERIFY(encoded.toEncoded().contains('%'));

        QSignalSpy dropped(&vm, &AgentViewModel::attachmentsDropped);
        QSignalSpy notice(&vm, &AgentViewModel::noticeOccurred);

        vm.handleDroppedFiles({encoded});
        QCOMPARE(dropped.count(), 1);
        const QVariantMap file = dropped.at(0).at(0).toList().at(0).toMap();
        QCOMPARE(file.value("kind").toString(), QStringLiteral("file"));
        QCOMPARE(file.value("name").toString(),
                 QStringLiteral("my note 报告 #1%.txt"));
        QCOMPARE(file.value("content").toString(), QStringLiteral("tricky content"));
        QCOMPARE(notice.count(), 0);

        // 用 toEncoded() 构造（等价于从 percent-encoded 字符串重建）仍要正确。
        dropped.clear();
        notice.clear();
        vm.handleDroppedFiles({QUrl::fromEncoded(encoded.toEncoded())});
        QCOMPARE(dropped.count(), 1);
        const QVariantMap again = dropped.at(0).at(0).toList().at(0).toMap();
        QCOMPARE(again.value("name").toString(),
                 QStringLiteral("my note 报告 #1%.txt"));
        QCOMPARE(again.value("content").toString(), QStringLiteral("tricky content"));

        // 非本地 URL：不读盘，计为不支持并提示。
        dropped.clear();
        notice.clear();
        vm.handleDroppedFiles({QUrl(QStringLiteral("https://example.com/a.txt"))});
        QCOMPARE(dropped.count(), 0);
        QCOMPARE(notice.count(), 1);
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

    // 等级筛选回归：filterLevel 是 severity rank（0=Debug..4=Fatal），不是
    // QtMsgType 原始值（Qt 6 中 QtInfoMsg=4 > QtFatalMsg=3）。语义为"仅显示
    // 该级别"（精确匹配 rank），m_filterLevel=-1 为全部。
    void logModelLevelFilter() {
        Logger::install();
        Logger::setMinLevel(QtDebugMsg);   // 让 Debug 进 ring

        const QString tag = QStringLiteral("lvlfilt-%1")
            .arg(QDateTime::currentMSecsSinceEpoch());
        qDebug()    << (tag + "-debug");
        qInfo()     << (tag + "-info");
        qWarning()  << (tag + "-warn");
        qCritical() << (tag + "-crit");

        LogModel model;
        model.setFilterCategory(QString());
        model.setFilterText(tag);          // 只看本次发的 4 行
        model.setPolling(true);
        QTest::qWait(300);
        model.setPolling(false);

        struct Case { int rank; QStringList expected; };
        const Case cases[] = {
            {0, QStringList{"DEBUG"}},
            {1, QStringList{"INFO"}},
            {2, QStringList{"WARN"}},
            {3, QStringList{"CRIT"}},
        };
        for (const auto& c : cases) {
            model.setFilterLevel(c.rank);
            QTest::qWait(150);
            QStringList levels;
            for (int i = 0; i < model.count(); ++i)
                levels << model.data(model.index(i, 0),
                                     LogModel::LevelRole).toString();
            levels.sort();
            QStringList expected = c.expected;
            expected.sort();          // order-independent compare
            QVERIFY2(levels == expected,
                     qPrintable(QStringLiteral("rank=%1 got [%2] want [%3]")
                        .arg(c.rank)
                        .arg(levels.join(","))
                        .arg(expected.join(","))));
        }

        // ALL: 全部 4 行可见
        model.setFilterLevel(-1);
        QTest::qWait(150);
        QVERIFY2(model.count() >= 4,
                 qPrintable(QStringLiteral("ALL filter lost rows: %1")
                    .arg(model.count())));

        Logger::setMinLevel(QtInfoMsg);    // 恢复全局默认
    }

    // 日期筛选（面板按日过滤）：本进程写下的条目都属"今天"，因此可确定性断言
    // ——今天保留全部、不存在的日期为空、列表含今天且最新在前；并且被选中但
    // 已无行的日期仍留在列表里（否则下拉框会出现空白选中项）。
    void logModelDateFilter() {
        Logger::install();
        Logger::setMinLevel(QtInfoMsg);

        const QString tag = QStringLiteral("datefilt-%1")
            .arg(QDateTime::currentMSecsSinceEpoch());
        const QString today = QDate::currentDate()
            .toString(QStringLiteral("yyyy-MM-dd"));

        qInfo() << (tag + "-a");
        qInfo() << (tag + "-b");

        LogModel model;
        model.setFilterCategory(QString());
        model.setFilterLevel(-1);
        model.setFilterText(tag);          // 只看本次发的两行
        model.setPolling(true);
        QTest::qWait(300);
        model.setPolling(false);

        const int allRows = model.count();
        QVERIFY2(allRows >= 2, qPrintable(QStringLiteral("seeded rows missing: %1")
            .arg(allRows)));

        // 今天：全部保留
        model.setFilterDate(today);
        QCOMPARE(model.count(), allRows);

        // 不存在的日期：清空，但仍可被选中
        const QString missing = QStringLiteral("1970-01-01");
        model.setFilterDate(missing);
        QCOMPARE(model.count(), 0);
        QVERIFY2(model.availableDates().contains(missing),
                 "selected day dropped from the picker list");

        // 回到全部
        model.setFilterDate(QString());
        QCOMPARE(model.count(), allRows);

        // 列表含今天，且最新在前
        const QVariantList days = model.availableDates();
        QVERIFY(!days.isEmpty());
        QCOMPARE(days.first().toString(), today);

        // 日期与其余筛选是 AND 关系：级别过滤后按今天仍全中
        model.setFilterLevel(1);           // INFO
        model.setFilterDate(today);
        QVERIFY2(model.count() >= 2,
                 "date + level filters did not compose");
        model.setFilterLevel(-1);
        model.setFilterDate(QString());

        Logger::setMinLevel(QtInfoMsg);    // 恢复全局默认
    }

    // ── Batch 18: session export (FEAT-1801) ──────────

    // docs/20 §3.2（FORK-701）：编辑重发 —— 删除持久层最后一条 user 行及
    // 之后全部消息，以新文本重新发起。
    void editAndResendWithoutProviderKeepsLastTurn() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession("s-edit", "Edit");
        memory.saveMessage("s-edit", QJsonObject{{"role", "user"},
            {"content", QStringLiteral("旧问题")}});
        memory.saveMessage("s-edit", QJsonObject{{"role", "assistant"},
            {"content", QStringLiteral("旧回答")}});

        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        AIAgent agent;
        vm.setAgent(&agent);
        agent.setMemory(&memory);

        agent.resumeSession("s-edit");
        QVERIFY(!vm.currentSessionId().isEmpty());

        QVERIFY(!vm.editAndResend(QStringLiteral("新问题")));

        // 未配置模型时不能破坏已有对话；成功替换由 test_chat_workflows
        // 验证请求、界面轨迹和持久层三者一致。
        const QJsonArray loaded = memory.loadMessages(vm.currentSessionId(), 200);
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).toObject().value("role").toString(),
                 QStringLiteral("user"));
        QCOMPARE(loaded.at(0).toObject().value("content").toString(),
                  QStringLiteral("旧问题"));

        vm.setAgent(nullptr);
    }

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

    // Parallel tab state remains independent even before an agent is attached.
    void parallelTabsStayIndependent() {
        AgentViewModel vm(nullptr);
        QCOMPARE(vm.tabModel().size(), 1);
        QCOMPARE(vm.activeTabIndex(), 0);

        vm.setDraftText(QStringLiteral("draft A"));
        vm.openTab();
        QCOMPARE(vm.activeTabIndex(), 1);
        QCOMPARE(vm.tabModel().size(), 2);
        QCOMPARE(vm.draftText(), QString());
        vm.setDraftText(QStringLiteral("draft B"));

        vm.activateTab(0);
        QCOMPARE(vm.draftText(), QStringLiteral("draft A"));
        vm.activateTab(1);
        QCOMPARE(vm.draftText(), QStringLiteral("draft B"));

        const QVariantList tabs = vm.tabModel();
        QCOMPARE(tabs.size(), 2);
        QVERIFY(!tabs.at(0).toMap().value("busy").toBool());
        QVERIFY(!tabs.at(1).toMap().value("busy").toBool());
    }

    void agentlessTaskIsRejected() {
        AgentViewModel vm(nullptr);
        QSignalSpy errors(&vm, &AgentViewModel::errorOccurred);
        vm.startTask(QStringLiteral("task without model"));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!vm.stateBusy());
        QCOMPARE(vm.traceCount(), 0);
    }

    // Closing a tab drops it from the model; the last remaining tab is guarded.
    void closingTabDropsItFromModel() {
        AgentViewModel vm(nullptr);
        vm.openTab();
        vm.closeTab(1);
        QCOMPARE(vm.tabModel().size(), 1);
        QCOMPARE(vm.activeTabIndex(), 0);

        vm.closeTab(0);
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

    // Loading a session without an attached agent is a no-op and does not
    // silently create a demo conversation.
    void loadSessionWithoutAgentIsNoOp() {
        AgentViewModel vm(nullptr);
        const int tabsBefore = vm.tabModel().size();
        vm.loadSession(QStringLiteral("session-x"));
        QCOMPARE(vm.tabModel().size(), tabsBefore);
        QCOMPARE(vm.activeTabIndex(), 0);
        QVERIFY(!vm.stateBusy());
    }

    // docs/17 P0-3: 会话与项目联动必须**双向对称**。回归点：此前只处理
    // "激活项目"分支，点开本地会话时活动项目保持不变，于是 TopBar 项目徽标、
    // 沙箱指向、侧栏高亮会停在上一个项目上，与刚打开的会话内容不一致。
    void loadSessionSyncsActiveProjectBothWays() {
        // setActiveProjectId 直写 QSettings（M4 先例）并改全局沙箱指向，
        // 用例结束后必须还原，避免污染用户配置与后续用例。
        QSettings settings("NexusAI", "NexusAI");
        const QVariant savedActive = settings.value("projects/activeId");

        SqliteMemory memory(QStringLiteral(":memory:"));
        const QString projectId = memory.createProject(QStringLiteral("demo"),
                                                       QStringLiteral("D:/work/demo"));
        QVERIFY(!projectId.isEmpty());
        memory.beginSession(QStringLiteral("proj-session"), QStringLiteral("项目会话"));
        memory.setSessionProject(QStringLiteral("proj-session"), projectId);
        memory.beginSession(QStringLiteral("local-session"), QStringLiteral("本地会话"));

        AgentViewModel vm(nullptr);
        AIAgent agent(&vm);
        agent.setMemory(&memory);
        vm.setMemory(&memory);
        vm.setAgent(&agent);
        // 会话列表来自 agent 的记忆后端，先确认两条会话都在（否则下面的
        // 归属查找会静默落空、用例变成假绿）。
        QCOMPARE(vm.sessionList().size(), 2);

        // 项目会话 → 激活其项目
        vm.loadSession(QStringLiteral("proj-session"));
        QCOMPARE(vm.activeProjectId(), projectId);

        // 本地会话 → 回到本地（空）：本次修复的核心断言
        vm.loadSession(QStringLiteral("local-session"));
        QCOMPARE(vm.activeProjectId(), QString());

        // 再点回项目会话，双向都要生效
        vm.loadSession(QStringLiteral("proj-session"));
        QCOMPARE(vm.activeProjectId(), projectId);

        vm.setActiveProjectId(QString());   // 复位全局沙箱指向
        if (savedActive.isValid()) settings.setValue("projects/activeId", savedActive);
        else settings.remove("projects/activeId");
    }

    // docs/17: 新建会话的默认归属 = 普通对话（本地）。只有项目行的「+」
    // （newProjectChat）才创建项目会话——顶部「新会话」与标签栏「+」都走
    // newLocalChat。回归点：这两个入口此前直接调 openTab()，新 tab 会继承
    // 当前活动项目，于是"在项目里点新会话"拿到的是项目会话而非普通对话。
    void newChatDefaultsToLocalUnlessProjectRequested() {
        QSettings settings("NexusAI", "NexusAI");
        const QVariant savedActive = settings.value("projects/activeId");

        SqliteMemory memory(QStringLiteral(":memory:"));
        const QString projectId = memory.createProject(QStringLiteral("demo"),
                                                       QStringLiteral("D:/work/demo"));
        QVERIFY(!projectId.isEmpty());

        AgentViewModel vm(nullptr);
        AIAgent agent(&vm);
        agent.setMemory(&memory);
        vm.setMemory(&memory);
        // docs/18: 归属按标签持有，新标签必须有自己的 agent 才能承接归属
        // （无工厂时新标签还没有 agent，归属无处可推）。
        vm.setAgentFactory([&vm, &memory]() -> AIAgent* {
            auto* a = new AIAgent(&vm);
            a->setMemory(&memory);
            return a;
        });
        vm.setAgent(&agent);

        // 起点：已经在一个项目里
        vm.setActiveProjectId(projectId);
        QCOMPARE(vm.activeProjectId(), projectId);

        // 顶部「新会话」/ 标签栏「+」→ 普通对话：必须回到本地
        vm.newLocalChat();
        QCOMPARE(vm.activeProjectId(), QString());

        // 项目行的「+」→ 项目会话
        vm.newProjectChat(projectId);
        QCOMPARE(vm.activeProjectId(), projectId);

        // 空 id 退化为普通对话
        vm.newProjectChat(QString());
        QCOMPARE(vm.activeProjectId(), QString());

        // 已是本地时再点「新会话」仍是本地（setActiveProjectId 的 no-op 分支）
        vm.newLocalChat();
        QCOMPARE(vm.activeProjectId(), QString());

        vm.setActiveProjectId(QString());   // 复位全局沙箱指向
        if (savedActive.isValid()) settings.setValue("projects/activeId", savedActive);
        else settings.remove("projects/activeId");
    }

    // docs/18 T1: activeProjectId 是**派生值**（活动标签的归属）——切标签即变。
    // 旧模型下它是全局值，切标签不会变，这正是"徽标撒谎"的根因。
    void activeProjectIdDerivesFromActiveTab() {
        QSettings settings("NexusAI", "NexusAI");
        const QVariant savedActive = settings.value("projects/activeId");

        SqliteMemory memory(QStringLiteral(":memory:"));
        const QString pA = memory.createProject(QStringLiteral("A"), QStringLiteral("D:/work/a"));
        const QString pB = memory.createProject(QStringLiteral("B"), QStringLiteral("D:/work/b"));
        QVERIFY(!pA.isEmpty() && !pB.isEmpty());

        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        templateAgent.setMemory(&memory);
        vm.setMemory(&memory);
        vm.setAgentFactory([&vm, &memory]() -> AIAgent* {
            auto* a = new AIAgent(&vm);
            a->setMemory(&memory);
            return a;
        });
        vm.setAgent(&templateAgent);

        vm.newProjectChat(pA);                 // 标签 1 → 项目 A
        const int aTab = vm.activeTabIndex();
        QCOMPARE(vm.activeProjectId(), pA);
        vm.newProjectChat(pB);                 // 标签 2 → 项目 B
        const int bTab = vm.activeTabIndex();
        QCOMPARE(vm.activeProjectId(), pB);

        // 切标签 → 派生值随活动标签变化（旧模型下始终是最后写入的全局值）
        vm.activateTab(aTab);
        QCOMPARE(vm.activeProjectId(), pA);
        vm.activateTab(bTab);
        QCOMPARE(vm.activeProjectId(), pB);

        vm.setActiveProjectId(QString());
        if (savedActive.isValid()) settings.setValue("projects/activeId", savedActive);
        else settings.remove("projects/activeId");
    }

    // docs/18 T3+T4: 归属按标签隔离——新建本地对话、或打开一条会话，都只影响
    // 当前标签；其他标签的项目归属不被广播式清空/覆盖（旧模型的核心缺陷）。
    void tabProjectOwnershipIsIsolated() {
        QSettings settings("NexusAI", "NexusAI");
        const QVariant savedActive = settings.value("projects/activeId");

        SqliteMemory memory(QStringLiteral(":memory:"));
        const QString pA = memory.createProject(QStringLiteral("A"), QStringLiteral("D:/work/a"));
        const QString pB = memory.createProject(QStringLiteral("B"), QStringLiteral("D:/work/b"));
        QVERIFY(!pA.isEmpty() && !pB.isEmpty());
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("A 的会话"));
        memory.setSessionProject(QStringLiteral("sess-a"), pA);

        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        templateAgent.setMemory(&memory);
        vm.setMemory(&memory);
        vm.setAgentFactory([&vm, &memory]() -> AIAgent* {
            auto* a = new AIAgent(&vm);
            a->setMemory(&memory);
            return a;
        });
        vm.setAgent(&templateAgent);

        // 各标签的归属（applyToAgents 按 m_tabs 顺序遍历）
        auto tabProjects = [&vm]() {
            QStringList out;
            vm.applyToAgents([&out](AIAgent* a) { out.append(a->activeProjectId()); });
            return out;
        };

        vm.newProjectChat(pB);                 // 标签 1 → 项目 B
        const int bTab = vm.activeTabIndex();
        QCOMPARE(vm.activeProjectId(), pB);

        // T3：新建本地对话 → 只有新标签是本地，标签 1 仍是 B
        vm.newLocalChat();
        const int localTab = vm.activeTabIndex();
        QCOMPARE(vm.activeProjectId(), QString());
        QStringList projects = tabProjects();
        QCOMPARE(projects.size(), 3);
        QCOMPARE(projects.at(bTab), pB);       // 旧模型这里会被广播清空
        QCOMPARE(projects.at(localTab), QString());

        // T4：在本地标签里打开项目 A 的会话 → 只有该标签变成 A，标签 1 仍是 B
        vm.loadSession(QStringLiteral("sess-a"));
        QCOMPARE(vm.activeProjectId(), pA);
        projects = tabProjects();
        QCOMPARE(projects.at(localTab), pA);
        QCOMPARE(projects.at(bTab), pB);       // 旧模型这里会被改成 A

        vm.setActiveProjectId(QString());
        if (savedActive.isValid()) settings.setValue("projects/activeId", savedActive);
        else settings.remove("projects/activeId");
    }

    // docs/18 期 3: 启动**不再把"上次的项目"回灌到启动标签上**——归属按标签
    // 持有，空标签默认本地（普通对话）。旧实现会把它暂存并在 setAgent 时套用，
    // 于是"刚打开应用的新标签"莫名带上了一个项目。侧栏的浏览位置改由 QML 读
    // Settings.activeProjectId 恢复展开态，与归属无关。
    void freshTabDefaultsToLocalOnStartup() {
        QSettings settings("NexusAI", "NexusAI");
        const QVariant savedActive = settings.value("projects/activeId");

        AgentViewModel vm(nullptr);
        // 复现启动序列：挂接 agent 之前先设过项目
        vm.setActiveProjectId(QStringLiteral("stale-project"));

        AIAgent agent(&vm);
        vm.setAgent(&agent);
        QCOMPARE(vm.activeProjectId(), QString());   // 启动标签仍是本地

        vm.setActiveProjectId(QString());
        if (savedActive.isValid()) settings.setValue("projects/activeId", savedActive);
        else settings.remove("projects/activeId");
    }

    // docs/18 G5: 归属落定（活动标签已有会话）后为只读。导入项目属于"非归属
    // 动作"，不得把已承载会话的标签改写到新项目上——旧行为会连带切换顶栏徽标，
    // 并把该标签的沙箱与系统提示指向新项目，而它显示的仍是旧会话。
    void importProjectLeavesBoundTabUntouched() {
        QSettings settings("NexusAI", "NexusAI");
        const QVariant savedActive = settings.value("projects/activeId");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        SqliteMemory memory(QStringLiteral(":memory:"));
        const QString pA = memory.createProject(QStringLiteral("A"),
                                                QStringLiteral("D:/work/a"));
        QVERIFY(!pA.isEmpty());
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("A 的会话"));
        memory.setSessionProject(QStringLiteral("sess-a"), pA);

        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        templateAgent.setMemory(&memory);
        vm.setMemory(&memory);
        vm.setAgentFactory([&vm, &memory]() -> AIAgent* {
            auto* a = new AIAgent(&vm);
            a->setMemory(&memory);
            return a;
        });
        vm.setAgent(&templateAgent);

        // 活动标签：归属 A，且已承载一条会话 → 归属落定
        vm.newProjectChat(pA);
        vm.loadSession(QStringLiteral("sess-a"));
        QCOMPARE(vm.activeProjectId(), pA);
        QVERIFY(!vm.currentSessionId().isEmpty());

        // 导入全新目录：项目记录建立，但已落定的归属不得被改写
        const QString imported = vm.importProject(dir.path());
        QVERIFY(!imported.isEmpty());
        QCOMPARE(vm.activeProjectId(), pA);      // 旧行为这里会变成 imported

        // 归属未定（空标签）时，导入才顺带把它设为该标签的目标
        vm.newLocalChat();
        QCOMPARE(vm.activeProjectId(), QString());
        QCOMPARE(vm.importProject(dir.path()), imported);   // 同路径 → 复用记录
        QCOMPARE(vm.activeProjectId(), imported);

        vm.setActiveProjectId(QString());
        if (savedActive.isValid()) settings.setValue("projects/activeId", savedActive);
        else settings.remove("projects/activeId");
    }

    // Batch 31b: McpManager add/remove round trip. The user's real
    // mcp_servers.json is backed up and restored around the test.
    void mcpManagerAddRemoveRoundTrip() {
        const QString configPath = McpConfig::configFilePath();
        QFile configFile(configPath);
        const bool hadConfig = configFile.exists();
        QByteArray originalConfig;
        if (hadConfig && configFile.open(QIODevice::ReadOnly)) {
            originalConfig = configFile.readAll();
            configFile.close();
        }
        auto restore = [&]() {
            if (hadConfig) {
                QFile out(configPath);
                out.open(QIODevice::WriteOnly);
                out.write(originalConfig);
            } else {
                QFile::remove(configPath);
            }
        };
        // Clean slate for the test.
        QFile::remove(configPath);

        auto* manager = McpManager::instance();
        const QString fakeExe = QCoreApplication::applicationDirPath()
            + QStringLiteral("/fake_mcp_server");

        manager->addServer(QVariantMap{
            {"name", QStringLiteral("t-fake")},
            {"transport", QStringLiteral("stdio")},
            {"command", fakeExe},
            {"enabled", true}});

        // Tools register on connect (name via the real normalization rule:
        // '-' is a legal character and is kept).
        const QString toolName =
            McpToolAdapter::registryNameFor(QStringLiteral("t-fake"),
                                            QStringLiteral("echo"));
        QTRY_VERIFY(ToolRegistry::instance().find(toolName) != nullptr);

        const QVariantList servers = manager->servers();
        QCOMPARE(servers.size(), 1);
        QCOMPARE(servers.at(0).toMap().value("name").toString(),
                 QStringLiteral("t-fake"));
        QCOMPARE(servers.at(0).toMap().value("state").toInt(),
                 static_cast<int>(McpClient::SessionState::Connected));
        QCOMPARE(servers.at(0).toMap().value("toolCount").toInt(), 1);

        // Persisted to mcp_servers.json.
        QVERIFY(QFile::exists(configPath));

        manager->removeServer(QStringLiteral("t-fake"));
        QTRY_VERIFY(ToolRegistry::instance().find(toolName) == nullptr);
        QCOMPARE(manager->servers().size(), 0);

        restore();
    }

    // docs/20 §3.2：编辑重发守卫 —— 空文本 / 纯空白 / 无 agent 都是 no-op。
    void editAndResendGuards() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession("s-guard", "Guard");
        memory.saveMessage("s-guard", QJsonObject{{"role", "user"},
            {"content", QStringLiteral("既有消息")}});

        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);

        // 无 agent：无持久层可修改，不伪造。
        vm.editAndResend(QStringLiteral("新问题"));
        QCOMPARE(memory.loadMessages("s-guard", 200).size(), 1);

        // 空文本 / 纯空白：无 agent 时本来就走不到删除，这里验证不崩溃。
        AIAgent agent;
        vm.setAgent(&agent);
        agent.setMemory(&memory);
        agent.startTask(QStringLiteral("既有消息"));
        const int before = memory.loadMessages("s-guard", 200).size();
        vm.editAndResend(QString());
        vm.editAndResend(QStringLiteral("   "));
        QCOMPARE(memory.loadMessages("s-guard", 200).size(), before);

        vm.setAgent(nullptr);
    }
};

QTEST_GUILESS_MAIN(TestAgentViewModel)
#include "test_viewmodel.moc"
