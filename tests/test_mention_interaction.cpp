// G2: '@' 补全的交互回归——真实按键/鼠标事件。
//
// 用户反馈：弹窗能列出文件，但 Enter/Tab/鼠标都选不中。此前的单测只覆盖
// C++ 解析与组件渲染，从未驱动过键鼠事件。本用例加载真实的 InputConsole，
// 注入 mention 状态后分别用 QTest 发送 Enter、Tab、Down+Enter 和鼠标点击，
// 断言文本被替换为 `@路径 `（带空格路径加引号），弹窗关闭。

#include <QtTest>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <QVariantList>

#include <memory>

#include "TestSettingsIsolation.h"
#include "viewmodels/AgentViewModel.h"
#include "viewmodels/McpManager.h"
#include "viewmodels/SettingsService.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h）：
// 预设指令用例会经 Settings.addPromptPreset 落库，没有隔离会写进用户
// 真实注册表。
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;

using namespace NexusAI;

namespace {

class NexusQrcFallback : public QQmlAbstractUrlInterceptor {
public:
    explicit NexusQrcFallback(QString baseDir) : m_baseDir(std::move(baseDir)) {}

    QUrl intercept(const QUrl& url, DataType) override {
        static const QString prefix = QStringLiteral("/qt/qml/NexusAI");
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString path = url.path();
            if (path.startsWith(prefix)) {
                const QString fsPath = m_baseDir + path.mid(prefix.length());
                if (QFile::exists(fsPath))
                    return QUrl::fromLocalFile(fsPath);
            }
        }
        return url;
    }

private:
    QString m_baseDir;
};

void collectVisualItems(QQuickItem* parent, const QString& objectName,
                        QList<QQuickItem*>* out) {
    const auto children = parent->childItems();
    for (QQuickItem* child : children) {
        if (child->objectName() == objectName)
            out->append(child);
        collectVisualItems(child, objectName, out);
    }
}

class Harness {
public:
    NexusQrcFallback fallback{QStringLiteral(NEXUS_BUILD_DIR "/NexusAI")};
    QQmlApplicationEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window{nullptr};
    QQuickItem* input{nullptr};
    QQuickItem* consoleItem{nullptr};
    QObject* popup{nullptr};
    QString error;

    Harness() {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/InputConsoleHarness.qml")));
        if (!component.isReady()) {
            error = component.errorString();
            return;
        }
        root.reset(component.create());
        if (!root) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow*>(root.get());
        if (!window) {
            error = QStringLiteral("harness root is not a QQuickWindow");
            return;
        }
        window->show();
        if (!QTest::qWaitForWindowExposed(window)) {
            error = QStringLiteral("window was not exposed");
            return;
        }
        window->requestActivate();
        QTest::qWait(200);
        input = window->findChild<QQuickItem*>(QStringLiteral("messageInput"));
        consoleItem = window->findChild<QQuickItem*>(QStringLiteral("inputConsole"));
        if (consoleItem)
            popup = consoleItem->findChild<QObject*>(QStringLiteral("mentionPopup"));
        if (!input)
            error = QStringLiteral("messageInput not found");
        else if (!consoleItem)
            error = QStringLiteral("inputConsole not found");
        else if (!popup)
            error = QStringLiteral("mentionPopup not found");
    }

    // Text stays in the editor; only the completion state is injected.
    void prepare(const QString& text, const QVariantList& results,
                 int selected = 0) {
        input->forceActiveFocus();
        QTest::qWait(80);
        input->setProperty("text", text);
        input->setProperty("cursorPosition", text.size());
        // Let the control settle its own cursor notifications first: they
        // re-run updateMention(), which (without a project) clears results.
        QTest::qWait(250);
        consoleItem->setProperty("mentionStart", 0);
        consoleItem->setProperty("mentionQuery", QString());
        QVERIFY(QMetaObject::invokeMethod(root.get(), "setMentionResults",
                                          Q_ARG(QVariant, results),
                                          Q_ARG(QVariant, QVariant::fromValue(selected))));
        QMetaObject::invokeMethod(popup, "open");
        QTest::qWait(150);
        (void)window->grabWindow();   // flush delegate incubation
        QTest::qWait(100);
    }
};

QVariantList sampleResults() {
    return {QStringLiteral("src/main.cpp"), QStringLiteral("README.md"),
            QStringLiteral("docs/with space.md")};
}

} // namespace

class TestMentionInteraction : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    void applyMentionReplacesTokenAtomically() {
        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        h.prepare(QStringLiteral("@ma"), sampleResults());
        QVERIFY(QMetaObject::invokeMethod(
            h.consoleItem, "applyMention",
            Q_ARG(QVariant, QStringLiteral("src/main.cpp"))));

        // remove()+insert() must be atomic from updateMention()'s point of
        // view; a re-entrant update used to reset mentionStart to -1 and lose
        // the completion.
        QCOMPARE(h.input->property("text").toString(),
                 QStringLiteral("@src/main.cpp "));
        QCOMPARE(h.input->property("cursorPosition").toInt(),
                 QStringLiteral("@src/main.cpp ").size());
        QCOMPARE(h.consoleItem->property("mentionOpen").toBool(), false);
        QCOMPARE(h.consoleItem->property("mentionStart").toInt(), -1);
    }

    void enterCompletesHighlightedFile() {
        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        h.prepare(QStringLiteral("@ma"), sampleResults());
        QVERIFY(h.input->hasActiveFocus());
        QCOMPARE(h.consoleItem->property("mentionOpen").toBool(), true);

        QTest::keyClick(h.window, Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(h.input->property("text").toString(),
                 QStringLiteral("@src/main.cpp "));
        QCOMPARE(h.consoleItem->property("mentionOpen").toBool(), false);
    }

    void tabCompletesHighlightedFile() {
        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        h.prepare(QStringLiteral("@ma"), sampleResults());

        QTest::keyClick(h.window, Qt::Key_Tab);
        QTest::qWait(80);

        QCOMPARE(h.input->property("text").toString(),
                 QStringLiteral("@src/main.cpp "));
        QCOMPARE(h.consoleItem->property("mentionOpen").toBool(), false);
    }

    void downThenEnterPicksSecondRow() {
        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        h.prepare(QStringLiteral("@ma"), sampleResults());

        QTest::keyClick(h.window, Qt::Key_Down);
        QTest::qWait(40);
        QCOMPARE(h.consoleItem->property("mentionIndex").toInt(), 1);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(h.input->property("text").toString(),
                 QStringLiteral("@README.md "));
    }

    void pathsWithSpacesAreQuoted() {
        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        h.prepare(QStringLiteral("@do"), sampleResults(), 2);

        QTest::keyClick(h.window, Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(h.input->property("text").toString(),
                 QStringLiteral("@\"docs/with space.md\" "));
    }

    void mouseClickCompletesRow() {
        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        h.prepare(QStringLiteral("@ma"), sampleResults());

        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(), QStringLiteral("mentionRow"), &rows);
        QVERIFY2(rows.size() >= 2,
                 qPrintable(QStringLiteral("mention rows not created: %1").arg(rows.size())));

        auto* second = rows.at(1);
        const QPointF scenePos = second->mapToScene(
            QPointF(second->width() / 2, second->height() / 2));
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier,
                          scenePos.toPoint());
        QTest::qWait(120);

        QCOMPARE(h.input->property("text").toString(),
                 QStringLiteral("@README.md "));
        QCOMPARE(h.consoleItem->property("mentionOpen").toBool(), false);
    }

    // ── 提示词预设的 / 指令化（原输入框下方选择器迁移）──────────────
    // "/" 补全面板混入预设条目：派发式语义——选中即套用为会话系统提示，
    // "/名称" token 从草稿移除；键盘 Enter 与鼠标点击两条路径都必须派发
    // （鼠标路径此前把 name 字符串当 entry 传给 applySlash，对派发式条目
    // 是坏的——MCP 条目同雷）。

    // slash 面板准备：直接设文本 + 光标，让 updateCompletions 自然打开
    // "/" 补全（不经 prepare() 的 mention 状态注入）。
    void typeSlash(Harness& h, const QString& token) {
        h.input->forceActiveFocus();
        QTest::qWait(60);
        h.input->setProperty("text", token);
        h.input->setProperty("cursorPosition", token.size());
        QTest::qWait(250);
    }

    void cleanupPresets(AgentViewModel* vm, SettingsService* settings) {
        const QVariantList presets = settings->promptPresets();
        for (const auto& p : presets)
            settings->removePromptPreset(
                p.toMap().value(QStringLiteral("id")).toString());
        vm->clearPromptPreset();
    }

    void presetSlashEntryAppliesSessionPreset() {
        auto* vm = AgentViewModel::instance();
        auto* settings = SettingsService::instance();
        cleanupPresets(vm, settings);
        const QString presetId = settings->addPromptPreset(
            QStringLiteral("技术评审"),
            QStringLiteral("你是严谨的技术评审专家，逐条给出修改意见。"));

        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        QSignalSpy noticeSpy(vm, &AgentViewModel::noticeOccurred);
        typeSlash(h, QStringLiteral("/技术评审"));

        // 补全面板自然打开，候选里出现带 preset 标志的预设条目。
        QCOMPARE(h.consoleItem->property("slashOpen").toBool(), true);
        const QVariantList results =
            h.consoleItem->property("slashResults").toList();
        bool found = false;
        for (const auto& r : results) {
            const QVariantMap m = r.toMap();
            if (m.value(QStringLiteral("name")).toString()
                    != QStringLiteral("技术评审")) continue;
            found = true;
            QCOMPARE(m.value(QStringLiteral("preset")).toBool(), true);
            QCOMPARE(m.value(QStringLiteral("presetId")).toString(), presetId);
            QVERIFY(!m.value(QStringLiteral("promptText")).toString().isEmpty());
        }
        QVERIFY2(found, "preset entry missing from slash results");

        // 键盘 Enter：选中即派发套用（会话临时值落点 = activePromptPresetId）。
        QTest::keyClick(h.window, Qt::Key_Return);
        QTest::qWait(120);
        QCOMPARE(vm->activePromptPresetId(), presetId);
        QCOMPARE(vm->activePromptPresetName(), QStringLiteral("技术评审"));
        // "/名称" token 从草稿移除（不是要发送的文本）。
        QCOMPARE(h.input->property("text").toString(), QString());
        QCOMPARE(h.consoleItem->property("slashOpen").toBool(), false);
        // toast 反馈经 noticeOccurred 发出（无常驻回显位后的唯一即时反馈）。
        QCOMPARE(noticeSpy.count(), 1);
        QVERIFY(noticeSpy.first().first().toString()
                    .contains(QStringLiteral("技术评审")));

        // 渲染 smoke：面板 + 预设条目真的画出来了（非空图 + 非 0 字节文件）。
        typeSlash(h, QStringLiteral("/技术评审"));
        const QImage shot = h.window->grabWindow();
        QVERIFY(!shot.isNull());
        QTemporaryDir snapDir;
        const QString snapPath =
            snapDir.filePath(QStringLiteral("slash-preset-menu.png"));
        QVERIFY(shot.save(snapPath));
        QVERIFY(QFileInfo(snapPath).size() > 0);
        // 本地目视辅助：设 NEXUS_TEST_SNAPSHOT_DIR 时把抓图镜像到该目录。
        const QString mirrorDir = qEnvironmentVariable("NEXUS_TEST_SNAPSHOT_DIR");
        if (!mirrorDir.isEmpty())
            shot.save(mirrorDir + QStringLiteral("/slash-preset-menu.png"));

        // 鼠标点击路径：选中即派发（回归鼠标传参 bug——此前传 name 字符串，
        // 派发式条目会走进字面展开分支）。
        typeSlash(h, QStringLiteral("/技术"));
        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(), QStringLiteral("slashRow"), &rows);
        QQuickItem* presetRow = nullptr;
        for (QQuickItem* row : rows) {
            const QVariantMap m = row->property("modelData").toMap();
            if (m.value(QStringLiteral("preset")).toBool()) {
                presetRow = row;
                break;
            }
        }
        QVERIFY2(presetRow, "preset slashRow not created");
        const QPointF scenePos = presetRow->mapToScene(
            QPointF(presetRow->width() / 2, presetRow->height() / 2));
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier,
                          scenePos.toPoint());
        QTest::qWait(120);
        QCOMPARE(vm->activePromptPresetId(), presetId);
        QCOMPARE(h.input->property("text").toString(), QString());

        cleanupPresets(vm, settings);
        noticeSpy.clear();
    }

    void presetClearEntryRevertsToGlobal() {
        auto* vm = AgentViewModel::instance();
        auto* settings = SettingsService::instance();
        cleanupPresets(vm, settings);
        const QString presetId = settings->addPromptPreset(
            QStringLiteral("架构师"), QStringLiteral("你是资深架构师。"));
        vm->applyPromptPreset(presetId, QStringLiteral("架构师"),
                              QStringLiteral("你是资深架构师。"));

        Harness h;
        QVERIFY2(h.input, qPrintable(h.error));
        QSignalSpy noticeSpy(vm, &AgentViewModel::noticeOccurred);
        QSignalSpy manageSpy(h.consoleItem,
                             SIGNAL(managePresetsRequested()));
        QVERIFY(manageSpy.isValid());

        // 有套用中的预设 → "/清除" 匹配清除条目；Enter 清除回落全局。
        typeSlash(h, QStringLiteral("/清除"));
        QCOMPARE(h.consoleItem->property("slashOpen").toBool(), true);
        const QVariantList results =
            h.consoleItem->property("slashResults").toList();
        bool found = false;
        for (const auto& r : results) {
            const QVariantMap m = r.toMap();
            if (m.value(QStringLiteral("name")).toString()
                    == QStringLiteral("清除预设")) {
                found = true;
                QCOMPARE(m.value(QStringLiteral("presetClear")).toBool(), true);
            }
        }
        QVERIFY2(found, "clear entry missing while a preset is applied");
        QTest::keyClick(h.window, Qt::Key_Return);
        QTest::qWait(120);
        QCOMPARE(vm->activePromptPresetId(), QString());
        QCOMPARE(h.input->property("text").toString(), QString());
        QVERIFY(noticeSpy.count() >= 1);

        // 清除后 "/清除" 不再出候选（无东西可清）。
        typeSlash(h, QStringLiteral("/清除"));
        QTest::qWait(120);
        const QVariantList afterClear =
            h.consoleItem->property("slashResults").toList();
        QVERIFY(afterClear.isEmpty());

        // "/管理预设" → 派发 managePresetsRequested（设置中心 prompts 分类
        // 的接线由 AgentChatPage 完成，这里断言信号本身）。
        typeSlash(h, QStringLiteral("/管理预设"));
        QTest::keyClick(h.window, Qt::Key_Return);
        QTest::qWait(120);
        QCOMPARE(manageSpy.count(), 1);
        QCOMPARE(h.input->property("text").toString(), QString());

        cleanupPresets(vm, settings);
    }

    // docs/48 回归：MCP 资源列表必须随 serversChanged 重算。McpResourceDialog
    // 常驻在 InputConsole 里（只创建一次，openFor() 不重建），而
    // McpManager.listResources() 是 Q_INVOKABLE、自己没有变更通知——原实现
    // `readonly property var allResources: McpManager.listResources()` 只在
    // 创建时求值一次，资源在服务器连接完成后才到达时，对话框整个生命周期都
    // 停在空列表上。
    void mcpResourceListFollowsServersChanged() {
        Harness h;
        QVERIFY2(h.error.isEmpty(), qPrintable(h.error));

        QObject* dialog = h.window->findChild<QObject*>(
            QStringLiteral("mcpResourceDialog"));
        QVERIFY2(dialog, "McpResourceDialog 必须在 InputConsole 里（objectName 定位）");
        QVERIFY(dialog->property("allResources").toList().isEmpty());

        // serversChanged 是"资源可能已变"的统一信号
        // （McpClient::resourcesChanged → refreshServers → serversChanged）。
        // 以 allResources 的变更通知为证据：重算会重新赋值（即使值仍为空），
        // 绑定实现下这里不会有任何写入 → 该断言正是原缺陷的护栏。
        auto* manager = McpManager::instance();
        QSignalSpy writeSpy(dialog, SIGNAL(allResourcesChanged()));
        emit manager->serversChanged();
        QTRY_VERIFY(writeSpy.count() >= 1);

        // 菜单项「MCP 资源…」的判据是 root.mcpResourceEntryVisible，它读的是
        // dialog.allResources（唯一数据源，由 serversChanged 命令式重算）。
        // 直接断言这个派生属性：MenuItem.visible 在父 Menu 未弹出时不求值，
        // 拿它做断言会变成"永远 false"的假绿。
        QQuickItem* console = h.consoleItem;
        QVERIFY(console);
        QVERIFY(!console->property("mcpResourceEntryVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setMcpResources",
                                          Q_ARG(QVariant, QVariant::fromValue(
                                              static_cast<QObject*>(dialog))),
                                          Q_ARG(QVariant, 2)));
        QTRY_VERIFY(console->property("mcpResourceEntryVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "setMcpResources",
                                          Q_ARG(QVariant, QVariant::fromValue(
                                              static_cast<QObject*>(dialog))),
                                          Q_ARG(QVariant, 0)));
        QTRY_VERIFY(!console->property("mcpResourceEntryVisible").toBool());
    }
};

QTEST_MAIN(TestMentionInteraction)
#include "test_mention_interaction.moc"
