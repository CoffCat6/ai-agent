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
#include "core/utils/SlashCommand.h"
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

// docs/25: 批量导入测试助手（文件作用域——moc 对类作用域 lambda 报
// "Not a signal or slot declaration"，且 helper 无捕获需求）。
static QString batchWrite(const QTemporaryDir& dir, const QString& name,
                          const QString& content) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
    f.write(content.toUtf8());
    f.close();
    return path;
}

static void setupEmbedder(RagService& rag, bool slow) {
    rag.setEmbedder([](const QString&) { return QVector<float>(8, 0.25f); });
    rag.setBatchEmbedder([slow](const QStringList& texts) {
        if (slow) QTest::qSleep(300);   // hold the file in-flight
        QVector<QVector<float>> out;
        out.reserve(texts.size());
        for (int i = 0; i < texts.size(); ++i)
            out.append(QVector<float>(8, 0.25f));
        return out;
    });
}

} // namespace

// docs/21：会话是线性的一串消息，测试夹具按顺序追加即可。
qint64 appendMessage(SqliteMemory& memory, const QString& sessionId,
                     const QString& role, const QString& content) {
    return memory.saveMessage(sessionId, QJsonObject{{QStringLiteral("role"), role},
                                                     {QStringLiteral("content"), content}});
}

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
        appendMessage(memory, "s-edit", QStringLiteral("user"), QStringLiteral("旧问题"));
        appendMessage(memory, "s-edit", QStringLiteral("assistant"), QStringLiteral("旧回答"));

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
        appendMessage(memory, "s1", QStringLiteral("user"), QStringLiteral("第一问：什么是 FTS5"));
        appendMessage(memory, "s1", QStringLiteral("assistant"), QStringLiteral("FTS5 是 SQLite 的全文检索扩展模块"));

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
        appendMessage(memory, "s1", QStringLiteral("user"), "q");
        appendMessage(memory, "s1", QStringLiteral("assistant"), "a");
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
        appendMessage(memory, "s1", QStringLiteral("user"), "list the dir");
        appendMessage(memory, "s1", QStringLiteral("assistant"), "done");
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
        appendMessage(memory, "s1", QStringLiteral("user"), "q");
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
        appendMessage(memory, "s1", QStringLiteral("user"), "q");
        appendMessage(memory, "s1", QStringLiteral("assistant"), "a");

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

    // 提示词预设是**会话级**决定：套用/清除/切标签互不干扰，且全局系统
    // 提示只在未套用的标签上下发（否则改一次全局设置会把预设冲掉）。
    void promptPresetIsPerTab() {
        AgentViewModel vm(nullptr);
        AIAgent agent(&vm);
        vm.setAgent(&agent);
        // 不新建标签：模板 agent 就挂在 tab 0 上，预设要落在有 agent 的标签。
        vm.applyGlobalSystemPrompt(QStringLiteral("GLOBAL"));
        QCOMPARE(agent.effectiveSystemPrompt(), QStringLiteral("GLOBAL"));

        vm.applyPromptPreset(QStringLiteral("preset-a"), QStringLiteral("评审"),
                             QStringLiteral("PRESET A"));
        QCOMPARE(vm.activePromptPresetId(), QStringLiteral("preset-a"));
        QCOMPARE(vm.activePromptPresetName(), QStringLiteral("评审"));
        QCOMPARE(agent.effectiveSystemPrompt(), QStringLiteral("PRESET A"));

        // 全局设置变更不能顶掉已套用的预设，但要记住新值供清除时回落。
        vm.applyGlobalSystemPrompt(QStringLiteral("GLOBAL 2"));
        QCOMPARE(agent.effectiveSystemPrompt(), QStringLiteral("PRESET A"));

        vm.clearPromptPreset();
        QCOMPARE(vm.activePromptPresetId(), QString());
        QCOMPARE(agent.effectiveSystemPrompt(), QStringLiteral("GLOBAL 2"));
    }

    // 新标签不继承上一个标签的预设；切回去仍然保持。
    void promptPresetFollowsActiveTab() {
        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        QVector<AIAgent*> created;
        vm.setAgentFactory([&created, &vm]() -> AIAgent* {
            auto* agent = new AIAgent(&vm);
            created.append(agent);
            return agent;
        });
        vm.setAgent(&templateAgent);

        vm.applyPromptPreset(QStringLiteral("preset-a"), QStringLiteral("Reviewer"),
                             QStringLiteral("PRESET A"));
        QCOMPARE(vm.activePromptPresetId(), QStringLiteral("preset-a"));

        vm.openTab();                       // 新标签没有预设
        QCOMPARE(vm.activePromptPresetId(), QString());
        QCOMPARE(vm.activePromptPresetName(), QString());

        vm.applyPromptPreset(QStringLiteral("preset-b"), QStringLiteral("Mentor"),
                             QStringLiteral("PRESET B"));
        QCOMPARE(vm.activePromptPresetId(), QStringLiteral("preset-b"));

        vm.activateTab(0);
        QCOMPARE(vm.activePromptPresetId(), QStringLiteral("preset-a"));
    }

    // 预设被改名/改正文要推到已套用的标签；被删除时保持原提示不变
    // （设置页承诺「删除预设不影响已套用的会话」）。
    void promptPresetSyncsEditsButNotDeletes() {
        AgentViewModel vm(nullptr);
        AIAgent agent(&vm);
        vm.setAgent(&agent);

        vm.applyPromptPreset(QStringLiteral("preset-a"), QStringLiteral("Reviewer"),
                             QStringLiteral("PRESET A"));
        QCOMPARE(agent.effectiveSystemPrompt(), QStringLiteral("PRESET A"));

        vm.syncPromptPresets({QVariantMap{
            {QStringLiteral("id"), QStringLiteral("preset-a")},
            {QStringLiteral("name"), QStringLiteral("Reviewer v2")},
            {QStringLiteral("prompt"), QStringLiteral("PRESET A v2")}}});
        QCOMPARE(vm.activePromptPresetName(), QStringLiteral("Reviewer v2"));
        QCOMPARE(agent.effectiveSystemPrompt(), QStringLiteral("PRESET A v2"));

        vm.syncPromptPresets({});
        QCOMPARE(vm.activePromptPresetId(), QStringLiteral("preset-a"));
        QCOMPARE(agent.effectiveSystemPrompt(), QStringLiteral("PRESET A v2"));
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

    // ── docs/30 单对话视图 ────────────────────────────────
    // 侧栏点一条会话的语义收在 openConversation 里。核心断言是它与标签栏
    // 的差别：切换不新增 controller（每个都是一条 SQLite 连接），且
    // sessionList 上的运行时键（open/active/busy/hasDraft/unread）跟着变。
    void openConversationSwitchesWithoutSpawningControllers() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("sess-b"), QStringLiteral("会话 B"));

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

        const int tabsBefore = vm.tabModel().size();

        vm.openConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));
        // 空闲时就地换内容：不新增 controller。
        QCOMPARE(vm.tabModel().size(), tabsBefore);

        auto sessionEntry = [&vm](const QString& id) {
            for (const QVariant& entry : vm.sessionList())
                if (entry.toMap().value("id").toString() == id) return entry.toMap();
            return QVariantMap{};
        };
        QCOMPARE(sessionEntry(QStringLiteral("sess-a")).value("active").toBool(), true);
        QCOMPARE(sessionEntry(QStringLiteral("sess-a")).value("open").toBool(), true);
        QCOMPARE(sessionEntry(QStringLiteral("sess-b")).value("active").toBool(), false);
        QCOMPARE(sessionEntry(QStringLiteral("sess-b")).value("open").toBool(), false);

        // 切到另一条：仍只有一个 controller，活动标记整体搬家。
        vm.openConversation(QStringLiteral("sess-b"));
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-b"));
        QCOMPARE(vm.tabModel().size(), tabsBefore);
        QCOMPARE(sessionEntry(QStringLiteral("sess-a")).value("active").toBool(), false);
        QCOMPARE(sessionEntry(QStringLiteral("sess-a")).value("open").toBool(), false);
        QCOMPARE(sessionEntry(QStringLiteral("sess-b")).value("active").toBool(), true);
        QCOMPARE(sessionEntry(QStringLiteral("sess-b")).value("open").toBool(), true);

        // 幂等：再点一次当前会话不产生任何副作用。
        vm.openConversation(QStringLiteral("sess-b"));
        QCOMPARE(vm.tabModel().size(), tabsBefore);
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-b"));

        // 未知 id（侧栏列表陈旧）不制造空会话，也不崩。
        vm.openConversation(QStringLiteral("no-such-session"));
        QCOMPARE(vm.tabModel().size(), tabsBefore);
        vm.openConversation(QString());
        QCOMPARE(vm.tabModel().size(), tabsBefore);
    }

    // 已经打开的会话被点回来时**不得**新建 controller，也不得打断它的任务：
    // 这是标签栏时代"切走不中断"的等价断言（activateTab 只换索引）。
    // docs/30 §4.4：打开集合有上界，且"钉住"（有草稿/在跑）的会话绝不淘汰。
    // 上界的理由不是美观：每个 controller 一条 SQLite 连接 + 最多 800 条
    // trace 节点恢复，标签时代那个"用户懒得点 +"的隐含限制已不存在。
    void openConversationsAreBoundedAndRefuseWhenAllPinned() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        const int count = AgentViewModel::kMaxOpenConversations + 3;
        QStringList ids;
        for (int i = 0; i < count; ++i) {
            const QString id = QStringLiteral("sess-%1").arg(i);
            memory.beginSession(id, QStringLiteral("会话 %1").arg(i));
            ids.append(id);
        }

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

        // 空闲时反复切换：就地复用，controller 数恒定（浏览历史不该堆连接）。
        for (const QString& id : ids) vm.openConversation(id);
        QCOMPARE(vm.currentSessionId(), ids.last());
        QCOMPARE(vm.tabModel().size(), 1);

        // 每条都留下草稿 → 全都钉住，切换只能新开 controller，直到上限。
        QSignalSpy notice(&vm, &AgentViewModel::noticeOccurred);
        for (const QString& id : ids) {
            vm.openConversation(id);
            vm.setDraftText(QStringLiteral("草稿 %1").arg(id));
            if (vm.tabModel().size() >= AgentViewModel::kMaxOpenConversations)
                break;
        }
        QCOMPARE(vm.tabModel().size(), AgentViewModel::kMaxOpenConversations);

        // 所有打开的会话都还带着自己的草稿（没被淘汰牺牲）。
        int openWithDraft = 0;
        for (const QVariant& entry : vm.sessionList()) {
            const QVariantMap session = entry.toMap();
            if (!session.value("open").toBool()) continue;
            if (session.value("hasDraft").toBool()) ++openWithDraft;
        }
        QCOMPARE(openWithDraft, AgentViewModel::kMaxOpenConversations);

        // 达上限且全部钉住 → 明确拒绝（提示用户），不静默失败、不越界。
        QString unopened;
        for (const QString& id : ids) {
            bool isOpen = false;
            for (const QVariant& entry : vm.sessionList()) {
                const QVariantMap s = entry.toMap();
                if (s.value("id").toString() == id && s.value("open").toBool())
                    isOpen = true;
            }
            if (!isOpen) { unopened = id; break; }
        }
        if (!unopened.isEmpty()) {
            const int before = vm.tabModel().size();
            vm.openConversation(unopened);
            QCOMPARE(vm.tabModel().size(), before);
            QCOMPARE(notice.count(), 1);
            // 被拒绝的会话没有被打开。
            QVERIFY(vm.currentSessionId() != unopened);
        }

        // 打开集合里没有任何会话被误标为 open=false 之外的怪状态。
        for (const QVariant& entry : vm.sessionList()) {
            const QVariantMap session = entry.toMap();
            if (!session.value("active").toBool()) continue;
            QVERIFY(session.value("open").toBool());
        }
    }

    void openConversationReusesExistingController() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));

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

        vm.openConversation(QStringLiteral("sess-a"));
        const int tabsWithA = vm.tabModel().size();

        // 转到一个空白新对话 —— A 的 controller 保留在打开集合里。
        vm.newLocalChat();
        const int tabsAfterNew = vm.tabModel().size();
        QCOMPARE(tabsAfterNew, tabsWithA + 1);

        // 点回 A：复用已有 controller，不新增。
        vm.openConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.tabModel().size(), tabsAfterNew);
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));
    }

    // 草稿是"不能淘汰"的硬条件：用户切走再切回必须还在（docs/30 §7-1）。
    void conversationDraftSurvivesSwitching() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("sess-b"), QStringLiteral("会话 B"));

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

        vm.openConversation(QStringLiteral("sess-a"));
        vm.setDraftText(QStringLiteral("未发送的想法"));
        vm.openConversation(QStringLiteral("sess-b"));
        QCOMPARE(vm.draftText(), QString());

        // sess-a 仍被 controller 承载（有草稿 → 不可淘汰）。
        bool foundDraftOwner = false;
        for (const QVariant& entry : vm.sessionList()) {
            const QVariantMap session = entry.toMap();
            if (session.value("id").toString() != QStringLiteral("sess-a")) continue;
            foundDraftOwner = session.value("open").toBool()
                && session.value("hasDraft").toBool();
        }
        QVERIFY(foundDraftOwner);

        vm.openConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.draftText(), QStringLiteral("未发送的想法"));
    }

    // "完成未读"是 QML 依赖的运行时键：写入/清除都要安全，且活动会话永远
    // 不带未读点（active && unread 是自相矛盾的呈现）。
    void unreadStateIsConsistentWithActiveConversation() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("sess-b"), QStringLiteral("会话 B"));

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

        auto entryOf = [&vm](const QString& id) {
            for (const QVariant& e : vm.sessionList())
                if (e.toMap().value("id").toString() == id) return e.toMap();
            return QVariantMap{};
        };

        vm.openConversation(QStringLiteral("sess-a"));
        // 未知 id / 空串是安全 no-op。
        vm.clearConversationUnread(QString());
        vm.clearConversationUnread(QStringLiteral("no-such-session"));
        QVERIFY(!entryOf(QStringLiteral("sess-a")).value("unread").toBool());

        // 切走再切回，活动会话始终不带未读。
        vm.openConversation(QStringLiteral("sess-b"));
        QVERIFY(!entryOf(vm.currentSessionId()).value("unread").toBool());
        vm.openConversation(QStringLiteral("sess-a"));
        QVERIFY(!entryOf(QStringLiteral("sess-a")).value("unread").toBool());
    }

    // closeConversation 与"删除会话"是两件事：结束视图不删数据，且结束后
    // 会话仍在 sessionList 里（open=false），点它可重新打开。
    void closeConversationReleasesViewButKeepsHistory() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("sess-b"), QStringLiteral("会话 B"));

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

        // 两条会话同时被打开的唯一日常路径：A 里存着草稿（钉住不能就地换
        // 内容），此时再打开 B 就只能新起一个 controller。没有草稿的话，
        // 打开 B 会就地复用 A 的 controller —— 那才是单窗口语义。
        vm.openConversation(QStringLiteral("sess-a"));
        vm.setDraftText(QStringLiteral("A 的草稿"));
        vm.openConversation(QStringLiteral("sess-b"));
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-b"));
        QCOMPARE(vm.tabModel().size(), 2);

        vm.closeConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.tabModel().size(), 1);
        // 数据还在：会话仍在历史列表里，只是不再被打开。
        bool stillListed = false;
        for (const QVariant& entry : vm.sessionList()) {
            const QVariantMap session = entry.toMap();
            if (session.value("id").toString() != QStringLiteral("sess-a")) continue;
            stillListed = true;
            QVERIFY(!session.value("open").toBool());
            QVERIFY(!session.value("active").toBool());
        }
        QVERIFY(stillListed);
        QCOMPARE(memory.listSessions().size(), 2);
        // 关的那条是后台会话，活动会话不受影响。
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-b"));

        // 再点开它 → 重新承载（载入的是它的历史，不是 B 的）。
        vm.openConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));
        QCOMPARE(vm.draftText(), QString());
    }

    // 关掉最后一个打开会话时**结束**而不是拒绝（docs/30 §4.5）：
    // 就地重置成一个空白对话，controller 数保持为 1。
    void closingLastConversationResetsToBlank() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));

        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        templateAgent.setMemory(&memory);
        vm.setMemory(&memory);
        vm.setAgent(&templateAgent);

        vm.openConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));

        vm.closeConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.tabModel().size(), 1);
        QCOMPARE(vm.currentSessionId(), QString());
        QCOMPARE(vm.draftText(), QString());
        QCOMPARE(vm.activeProjectId(), QString());
        // 数据没被删。
        QCOMPARE(memory.listSessions().size(), 1);

        // 不存在的 id 是安全的 no-op。
        vm.closeConversation(QStringLiteral("no-such-session"));
        QCOMPARE(vm.tabModel().size(), 1);
    }

    // docs/30 P1：Ctrl+W 的落点。有会话 → 结束视图；空白对话 → 就地重置。
    void closeActiveConversationHandlesBlankAndBound() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));

        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        templateAgent.setMemory(&memory);
        vm.setMemory(&memory);
        vm.setAgent(&templateAgent);

        // 空白对话：按下 Ctrl+W 不该崩，也不该多出 controller。
        QCOMPARE(vm.currentSessionId(), QString());
        vm.setDraftText(QStringLiteral("草稿"));
        const int tabsBefore = vm.tabModel().size();
        vm.closeActiveConversation();
        QCOMPARE(vm.tabModel().size(), tabsBefore);
        QCOMPARE(vm.draftText(), QString());
        QCOMPARE(vm.currentSessionId(), QString());

        // 有会话：等价于 closeConversation(活动会话)。
        vm.openConversation(QStringLiteral("sess-a"));
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));
        vm.closeActiveConversation();
        QCOMPARE(vm.currentSessionId(), QString());
        QCOMPARE(vm.tabModel().size(), 1);
        // 数据仍在。
        QCOMPARE(memory.listSessions().size(), 1);
    }

    // docs/30 P1：Ctrl+Tab / Ctrl+Shift+Tab 在**被承载**的会话间循环，
    // 跳过空白对话（它在侧栏没有对应行，切过去等于消失）。
    void cycleConversationSkipsBlankAndWraps() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("sess-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("sess-b"), QStringLiteral("会话 B"));

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

        // 只有一条会话时循环是 no-op（不会绕回自己、不会新建）。
        vm.openConversation(QStringLiteral("sess-a"));
        const int tabsOne = vm.tabModel().size();
        vm.cycleConversation(1);
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));
        QCOMPARE(vm.tabModel().size(), tabsOne);

        // 造第二个承载者：A 留草稿（钉住），再开 B。
        vm.setDraftText(QStringLiteral("A 的草稿"));
        vm.openConversation(QStringLiteral("sess-b"));
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-b"));
        QCOMPARE(vm.tabModel().size(), 2);

        // 正向循环到 A，再正向绕回 B。
        vm.cycleConversation(1);
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));
        vm.cycleConversation(1);
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-b"));
        // 反向回到 A。
        vm.cycleConversation(-1);
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));

        // delta 0 是 no-op。
        vm.cycleConversation(0);
        QCOMPARE(vm.currentSessionId(), QStringLiteral("sess-a"));
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

    // docs/22 F2: a per-tab memory adopted via setOwnedMemory must be
    // released with the agent — closing a tab no longer leaks the SQLite
    // connection (registered QSqlDatabase + file handle) until process exit.
    void ownedMemoryReleasedWithAgent() {
        const int baseline = QSqlDatabase::connectionNames().size();

        // Direct mechanism: the adopted memory dies with the agent.
        {
            AIAgent agent(nullptr);
            agent.setOwnedMemory(
                std::make_unique<SqliteMemory>(QStringLiteral(":memory:")));
            QCOMPARE(QSqlDatabase::connectionNames().size(), baseline + 1);
        }
        QTRY_COMPARE(QSqlDatabase::connectionNames().size(), baseline);

        // End-to-end through the tab lifecycle (main.cpp wires its factory
        // with setOwnedMemory): open → close → connection released.
        AgentViewModel vm(nullptr);
        AIAgent templateAgent(&vm);
        vm.setAgent(&templateAgent);
        vm.setAgentFactory([&vm]() -> AIAgent* {
            auto* agent = new AIAgent(&vm);
            agent->setOwnedMemory(
                std::make_unique<SqliteMemory>(QStringLiteral(":memory:")));
            return agent;
        });
        vm.openTab();
        QCOMPARE(QSqlDatabase::connectionNames().size(), baseline + 1);
        vm.closeTab(1);   // the factory tab (same index the test above proves)
        QTRY_COMPARE(QSqlDatabase::connectionNames().size(), baseline);
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
        // setActiveProjectId 经 SettingsService 落盘（docs/25 A5）并改全局
        // 沙箱指向，用例结束后必须还原，避免污染用户配置与后续用例。
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

    // G3: importing a project seeds AGENTS.md once; re-import must not
    // overwrite whatever the user has since written.
    void importProjectSeedsRulesOnce() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SqliteMemory memory(QStringLiteral(":memory:"));
        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);

        const QString id = vm.importProject(dir.path());
        QVERIFY(!id.isEmpty());
        const QString rulesPath = dir.path() + "/AGENTS.md";
        QVERIFY(QFileInfo::exists(rulesPath));
        QFile seeded(rulesPath);
        QVERIFY(seeded.open(QIODevice::ReadOnly));
        QCOMPARE(seeded.readAll(), AIAgent::rulesSeedTemplate());
        seeded.close();
        // An untouched seed reads as "seed", not "active".
        QCOMPARE(vm.projectRulesState(dir.path()), QStringLiteral("seed"));

        // The user writes their own rules.
        QFile out(rulesPath);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("# Mine\n- run ctest\n");
        out.close();
        QCOMPARE(vm.projectRulesState(dir.path()), QStringLiteral("active"));

        // Re-importing the same folder reuses the record and leaves the file.
        QCOMPARE(vm.importProject(dir.path()), id);
        QFile after(rulesPath);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), QByteArray("# Mine\n- run ctest\n"));
    }

    // G3: seeding never clobbers something already at the AGENTS.md path.
    // A directory there makes the agent's isFile() probe fail, so it falls
    // back to .nexus/rules.md - the import must still succeed either way.
    void importProjectNeverClobbersExistingRulesPath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir("AGENTS.md"));
        SqliteMemory memory(QStringLiteral(":memory:"));
        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);

        const QString id = vm.importProject(dir.path());
        QVERIFY(!id.isEmpty());
        QCOMPARE(memory.listProjects().size(), 1);
        // The pre-existing entry is untouched and nothing was written into it.
        QVERIFY(QFileInfo(dir.path() + "/AGENTS.md").isDir());
        QCOMPARE(QDir(dir.path() + "/AGENTS.md").entryList(
                     QDir::Files | QDir::Hidden).size(), 0);
    }

    // G3: the settings page must see exactly what the agent would inject,
    // including the .nexus/rules.md fallback and non-UTF-8 content.
    void loadProjectRulesReadsWholeFileAndFallsBack() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SqliteMemory memory(QStringLiteral(":memory:"));
        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);

        QVERIFY(vm.loadProjectRules(dir.path()).isEmpty());
        QCOMPARE(vm.projectRulesState(dir.path()), QStringLiteral("missing"));
        // GBK-ish bytes: fromUtf8 yields U+FFFD, so the local-8bit fallback
        // must still return the WHOLE file. Reading the device a second time
        // (already at EOF) would return empty here.
        QFile legacy(dir.path() + "/AGENTS.md");
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(QByteArray("��ù���"));
        legacy.close();
        const QString decoded = vm.loadProjectRules(dir.path());
        QVERIFY(!decoded.isEmpty());
        QVERIFY(!decoded.contains(QChar::ReplacementCharacter));
        QFile::remove(dir.path() + "/AGENTS.md");


        // No AGENTS.md: the .nexus/rules.md fallback is what the agent reads,
        // so the page must show it too.
        QVERIFY(QDir(dir.path()).mkpath(".nexus"));
        QFile nested(dir.path() + "/.nexus/rules.md");
        QVERIFY(nested.open(QIODevice::WriteOnly));
        nested.write("- from nexus\n");
        nested.close();
        QCOMPARE(vm.loadProjectRules(dir.path()), QString("- from nexus\n"));
        // The badge must follow the same fallback the agent injects:
        // rules living only in .nexus/rules.md read as "active",
        // not the AGENTS.md-only "missing" the first iteration showed.
        QCOMPARE(vm.projectRulesState(dir.path()), QStringLiteral("active"));

        // AGENTS.md wins over the fallback.
        QFile root(dir.path() + "/AGENTS.md");
        QVERIFY(root.open(QIODevice::WriteOnly));
        root.write("- from agents\n");
        root.close();
        QCOMPARE(vm.loadProjectRules(dir.path()), QString("- from agents\n"));

        // A long file is returned in full here (the 8KB cap is the agent's
        // injection rule, not the editor's), but the state reports truncation.
        QVERIFY(root.open(QIODevice::WriteOnly));
        root.write(QByteArray(AIAgent::kProjectRulesMaxBytes + 1024, 'x'));
        root.close();
        QCOMPARE(vm.loadProjectRules(dir.path()).size(),
                 AIAgent::kProjectRulesMaxBytes + 1024);
        QCOMPARE(vm.projectRulesState(dir.path()),
                 QStringLiteral("truncated"));

        // Empty (and whitespace-only) files count as empty, not active.
        QVERIFY(root.open(QIODevice::WriteOnly));
        root.write("  \n\n");
        root.close();
        QCOMPARE(vm.projectRulesState(dir.path()), QStringLiteral("empty"));

        // A CRLF re-save of the untouched template still reads as "seed"
        // (same shared normalization as the injector).
        QVERIFY(root.open(QIODevice::WriteOnly));
        root.write(QByteArray(AIAgent::rulesSeedTemplate()).replace('\n', "\r\n"));
        root.close();
        QCOMPARE(vm.projectRulesState(dir.path()), QStringLiteral("seed"));
    }

    // R3: the watcher arms exactly the existing candidate files, re-arms
    // after our own write creates a missing AGENTS.md, and disarms on
    // unwatch. Async fileChanged DELIVERY is OS-latency dependent
    // (Windows AV can hold events for tens of seconds) and is not asserted
    // here: the QML handler tolerates late/duplicate events by comparing
    // disk content against the editor text.
    void projectRulesWatcherArmsAndDisarms() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SqliteMemory memory(QStringLiteral(":memory:"));
        AgentViewModel vm(nullptr);
        vm.setMemory(&memory);
        const QString native = QDir::toNativeSeparators(dir.path());

        // No rules files yet: nothing can be armed.
        vm.watchProjectRules(dir.path());
        QCOMPARE(vm.watchedRulesFiles().size(), 0);

        // Only AGENTS.md exists: exactly that file is armed.
        QFile f(dir.path() + "/AGENTS.md");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("- seed");
        f.close();
        vm.watchProjectRules(dir.path());
        QCOMPARE(vm.watchedRulesFiles(),
                 QStringList({ native + QDir::separator() + "AGENTS.md" }));

        // Both candidates exist: both are armed.
        QVERIFY(QDir(dir.path()).mkpath(".nexus"));
        QFile nested(dir.path() + "/.nexus/rules.md");
        QVERIFY(nested.open(QIODevice::WriteOnly));
        nested.close();
        vm.watchProjectRules(dir.path());
        QCOMPARE(vm.watchedRulesFiles().size(), 2);

        // Our own write to a WATCHED root re-arms - this covers the
        // first-save case where the page creates AGENTS.md itself.
        vm.unwatchProjectRules();
        QFile::remove(dir.path() + "/AGENTS.md");
        vm.watchProjectRules(dir.path());
        QCOMPARE(vm.watchedRulesFiles().size(), 1);   // only .nexus/rules.md
        QVERIFY(vm.writeProjectRules(dir.path(), "- mine"));
        QCOMPARE(vm.watchedRulesFiles().size(), 2);   // AGENTS.md re-armed

        // Switching away disarms everything.
        vm.unwatchProjectRules();
        QCOMPARE(vm.watchedRulesFiles().size(), 0);
    }

    // B2: 斜杠指令展开（core/utils/SlashCommand.h 纯函数）——匹配、
    // {text} 占位、无占位追加、大小写、未知命令与非斜杠原样返回。
    void slashCommandExpansion() {
        using namespace NexusAI::SlashCommand;
        const QVector<Command> cmds = {
            {QStringLiteral("review"), QStringLiteral("请审查：{text}")},
            {QStringLiteral("translate"), QStringLiteral("翻译成英文")},
        };
        QCOMPARE(expand(QStringLiteral("普通问题"), cmds),
                 QStringLiteral("普通问题"));
        QCOMPARE(expand(QStringLiteral("/etc/hosts 是什么"), cmds),
                 QStringLiteral("/etc/hosts 是什么"));
        QCOMPARE(expand(QStringLiteral("/review 这段代码"), cmds),
                 QStringLiteral("请审查：这段代码"));
        QCOMPARE(expand(QStringLiteral("/REVIEW 这段代码"), cmds),
                 QStringLiteral("请审查：这段代码"));
        QCOMPARE(expand(QStringLiteral("/translate"), cmds),
                 QStringLiteral("翻译成英文"));
        QCOMPARE(expand(QStringLiteral("/translate  你好 "), cmds),
                 QStringLiteral("翻译成英文\n\n你好"));
        QCOMPARE(expand(QStringLiteral("  /review 前导空白"), cmds),
                 QStringLiteral("请审查：前导空白"));
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
        appendMessage(memory, "s-guard", QStringLiteral("user"), QStringLiteral("既有消息"));

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

    // Happy path: each file gets a status; duplicate content is "duplicate",
    // empty content is "failed", and retry re-runs the failed file with its
    // CURRENT on-disk content.
    void batchImportStatusesAndRetry() {
        AgentViewModel vm(nullptr);
        RagService rag;
        setupEmbedder(rag, false);
        vm.setRagService(&rag);
        QTemporaryDir dir(QStandardPaths::writableLocation(
                              QStandardPaths::DocumentsLocation)
                          + QStringLiteral("/nexusai-tests-XXXXXX"));
        QVERIFY(dir.isValid());

        const QString pathA = batchWrite(dir, "a.txt", QStringLiteral("alpha 专属内容甲"));
        const QString pathB = batchWrite(dir, "b.txt", QStringLiteral("beta 专属内容乙"));
        const QString pathDup = batchWrite(dir, "a2.txt", QStringLiteral("alpha 专属内容甲"));  // same content as A
        const QString pathEmpty = batchWrite(dir, "empty.txt", QStringLiteral("   "));
        QVERIFY(!pathA.isEmpty() && !pathB.isEmpty() && !pathDup.isEmpty() && !pathEmpty.isEmpty());

        vm.importDocuments({pathA, pathB, pathDup, pathEmpty});
        QTRY_COMPARE_WITH_TIMEOUT(vm.batchImportRunning(), false, 30000);

        QCOMPARE(vm.batchImportTotal(), 4);
        QCOMPARE(vm.batchImportFailedCount(), 1);
        QCOMPARE(rag.documents().size(), 2);   // A 与 B 入库；空文件失败

        QVariantMap status;
        for (const QVariant& entry : vm.batchImportResults()) {
            const QVariantMap e = entry.toMap();
            status[e.value("name").toString()] = e.value("status").toString();
        }
        QCOMPARE(status.value("a.txt").toString(), QStringLiteral("ok"));
        QCOMPARE(status.value("b.txt").toString(), QStringLiteral("ok"));
        QCOMPARE(status.value("a2.txt").toString(), QStringLiteral("duplicate"));
        QCOMPARE(status.value("empty.txt").toString(), QStringLiteral("failed"));

        // Retry: fill the failed file with NEW content — it becomes "ok"
        // and the other entries are preserved (not re-imported).
        batchWrite(dir, "empty.txt", QStringLiteral("gamma 补充内容丙"));
        const int documentsBefore = rag.documents().size();
        vm.retryFailedBatchImports();
        QTRY_COMPARE_WITH_TIMEOUT(vm.batchImportRunning(), false, 30000);
        QCOMPARE(vm.batchImportFailedCount(), 0);
        QCOMPARE(rag.documents().size(), documentsBefore + 1);
        QCOMPARE(vm.batchImportResults().size(), 4);
    }

    // docs/25: cancel stops the queue — the in-flight file completes, the
    // not-yet-started ones are marked cancelled. The slow embedder holds
    // file 1 in flight for 300 ms while the cancel flag is set.
    void batchImportCancelStopsRemaining() {
        AgentViewModel vm(nullptr);
        RagService rag;
        setupEmbedder(rag, true);
        vm.setRagService(&rag);
        QTemporaryDir dir(QStandardPaths::writableLocation(
                              QStandardPaths::DocumentsLocation)
                          + QStringLiteral("/nexusai-tests-XXXXXX"));
        QVERIFY(dir.isValid());

        const QString p1 = batchWrite(dir, "f1.txt", QStringLiteral("first 第一"));
        const QString p2 = batchWrite(dir, "f2.txt", QStringLiteral("second 第二"));
        const QString p3 = batchWrite(dir, "f3.txt", QStringLiteral("third 第三"));
        QVERIFY(!p1.isEmpty() && !p2.isEmpty() && !p3.isEmpty());

        vm.importDocuments({p1, p2, p3});
        // File 1 is in flight: the cancel lands before file 2 is dispatched.
        vm.cancelBatchImport();
        QTRY_COMPARE_WITH_TIMEOUT(vm.batchImportRunning(), false, 30000);

        QCOMPARE(rag.documents().size(), 1);   // only the in-flight file
        QCOMPARE(vm.batchImportFailedCount(), 0);
        int cancelled = 0, ok = 0;
        for (const QVariant& entry : vm.batchImportResults()) {
            const QString s = entry.toMap().value("status").toString();
            if (s == QStringLiteral("cancelled")) ++cancelled;
            else if (s == QStringLiteral("ok")) ++ok;
        }
        QCOMPARE(ok, 1);
        QCOMPARE(cancelled, 2);
    }

    // docs/25: embedder unavailable → the document still ingests in pseudo
    // mode and the batch reports it as "degraded" (not failed).
    void batchImportReportsDegraded() {
        AgentViewModel vm(nullptr);
        RagService rag;
        setupEmbedder(rag, false);
        // Empty batch results → DocumentRetriever falls back to pseudo.
        rag.setBatchEmbedder([](const QStringList&) { return QVector<QVector<float>>{}; });
        vm.setRagService(&rag);
        QTemporaryDir dir(QStandardPaths::writableLocation(
                              QStandardPaths::DocumentsLocation)
                          + QStringLiteral("/nexusai-tests-XXXXXX"));
        QVERIFY(dir.isValid());

        const QString p1 = batchWrite(dir, "d1.txt", QStringLiteral("离线内容一"));
        const QString p2 = batchWrite(dir, "d2.txt", QStringLiteral("离线内容二"));
        QVERIFY(!p1.isEmpty() && !p2.isEmpty());

        vm.importDocuments({p1, p2});
        QTRY_COMPARE_WITH_TIMEOUT(vm.batchImportRunning(), false, 30000);

        QCOMPARE(vm.batchImportFailedCount(), 0);
        QCOMPARE(rag.documents().size(), 2);
        for (const QVariant& entry : vm.batchImportResults())
            QCOMPARE(entry.toMap().value("status").toString(), QStringLiteral("degraded"));
        QVERIFY(rag.usesPseudoEmbedding(QStringLiteral("d1.txt")));
    }
    // 入口守卫：拿不到持久层 / 活动会话 / 合法下标时直接拒绝，不造空会话。
    void branchFromAnswerGuards() {
        AgentViewModel vm(nullptr);
        QVERIFY(!vm.branchFromAnswer(1));           // 没有持久层
        SqliteMemory mem(QStringLiteral(":memory:"));
        vm.setMemory(&mem);
        QVERIFY(!vm.branchFromAnswer(1));            // 活动标签还没有会话
        QVERIFY(!vm.branchFromAnswer(-1));
        QCOMPARE(mem.listSessions().size(), 0);
    }

    // 完整链路：下标 → 第几条回答 → 副本。回归点：上一版界面读 messageId 而回答侧
    // 从没人写过这个键，按钮因此永不显示，旧用例又都手写主键绕过了这段换算。
    void branchFromAnswerForksByTraceIndex() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("src"), QStringLiteral("源会话"));
        memory.saveMessage(QStringLiteral("src"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                       {QStringLiteral("content"), QStringLiteral("第一问")}});
        memory.saveMessage(QStringLiteral("src"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                       {QStringLiteral("content"), QStringLiteral("第一答")}});
        memory.saveMessage(QStringLiteral("src"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                       {QStringLiteral("content"), QStringLiteral("第二问")}});
        memory.saveMessage(QStringLiteral("src"),
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                       {QStringLiteral("content"), QStringLiteral("第二答")}});
        struct Row { const char* id; const char* type; const char* title; };
        const Row rows[] = {
            {"nq1", "user_message", "第一问"},
            {"na1", "summary", "第一答"},
            {"nq2", "user_message", "第二问"},
            {"na2", "summary", "第二答"},
        };
        for (int i = 0; i < 4; ++i) {
            memory.saveTraceNode(QStringLiteral("src"),
                                 QJsonObject{{QStringLiteral("id"), QString::fromLatin1(rows[i].id)},
                                             {QStringLiteral("type"), QString::fromLatin1(rows[i].type)},
                                             {QStringLiteral("title"), QString::fromUtf8(rows[i].title)},
                                             {QStringLiteral("status"), QStringLiteral("completed")},
                                             {QStringLiteral("timestamp"), 1}}, i);
        }

        AgentViewModel vm(nullptr);
        AIAgent agent(&vm);
        agent.setMemory(&memory);
        vm.setMemory(&memory);
        vm.setAgent(&agent);
        vm.loadSession(QStringLiteral("src"));
        QCOMPARE(vm.traceCount(), 4);

        // 从第一条回答（下标 1）分叉：副本 = 第一问 + 第一答，源会话一字不动。
        QVERIFY(vm.branchFromAnswer(1));
        QString forked;
        for (const auto& entry : memory.listSessions()) {
            const QJsonObject session = entry.toObject();
            if (session.value(QStringLiteral("title")).toString()
                    == QStringLiteral("源会话（分支）"))
                forked = session.value(QStringLiteral("id")).toString();
        }
        QVERIFY(!forked.isEmpty());
        const QJsonArray copied = memory.loadMessages(forked, 50);
        QCOMPARE(copied.size(), 2);
        QCOMPARE(copied.at(1).toObject().value("content").toString(),
                 QStringLiteral("第一答"));
        QCOMPARE(memory.loadTraceNodes(forked).size(), 2);
        QCOMPARE(agent.currentSessionId(), forked);
        QCOMPARE(memory.loadMessages(QStringLiteral("src"), 50).size(), 4);

        vm.loadSession(QStringLiteral("src"));
        const int sessionsBefore = memory.listSessions().size();
        QVERIFY(!vm.branchFromAnswer(0));
        QVERIFY(!vm.branchFromAnswer(999));
        QCOMPARE(memory.listSessions().size(), sessionsBefore);

        // 从最后一条回答分叉 = 整段历史都带过去；第二次分叉标题要顺延编号。
        QVERIFY(vm.branchFromAnswer(3));
        const QString whole = agent.currentSessionId();
        QCOMPARE(memory.loadMessages(whole, 50).size(), 4);
        QCOMPARE(memory.loadTraceNodes(whole).size(), 4);
        for (const auto& entry : memory.listSessions()) {
            const QJsonObject session = entry.toObject();
            if (session.value(QStringLiteral("id")).toString() == whole)
                QCOMPARE(session.value(QStringLiteral("title")).toString(),
                         QStringLiteral("源会话（分支2）"));
        }
    }
};
QTEST_GUILESS_MAIN(TestAgentViewModel)
#include "test_viewmodel.moc"
