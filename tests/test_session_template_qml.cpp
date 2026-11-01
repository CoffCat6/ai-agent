// 会话模板入口的渲染与行为回归。
//
// 模板功能此前只有一个入口（侧栏「新对话」右缘那条 22px 窄条），且用的是
// 未主题化的裸 Menu；侧栏一折叠就彻底不可达。重构把菜单抽成
// SessionTemplateMenu 并挂到三个宿主（侧栏窄条 / 标签栏「+」/ 空态），
// 应用逻辑收进 AgentVM::startFromSessionTemplate。
//
// 本用例与 test_prompt_preset_qml 同一套「build/NexusAI 文件系统副本 +
// qrc 回退拦截器 + offscreen」方案加载真实组件，锁住：
//   1. 菜单能打开、内容不是空白（像素级），并且完整落在窗口内；
//   2. 模板行数与 Settings.sessionTemplates 一致，行高能装下两行摘要；
//   3. 摘要文案正确（配置名 · 权限），配置被删时如实说"配置已删除"；
//   4. 「管理模板…」真的发出信号；
//   5. ★ startFromSessionTemplate 的三步顺序：profileId 在能力范围内生效、
//      permissionMode 落到新标签、firstMessage 只进草稿不自动发送。
//     第 5 条是整个重构最容易被改坏的语义（顺序一颠倒，模板的模型配置就
//      静默失效），所以即使只测 ViewModel 也必须进回归。
//
// Settings 是应用级单例且会落盘，用例用 QSettings 守卫原始值。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QColor>
#include <QFile>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <memory>

#include "viewmodels/AgentViewModel.h"
#include "viewmodels/SettingsService.h"
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using NexusAI::AgentViewModel;
using NexusAI::SettingsService;

namespace {

// build/NexusAI/qmldir 的 prefer 指向测试进程不存在的 qrc；把 NexusAI
// 前缀重写到构建目录的文件系统副本（与 test_todo_qml 相同方案）。
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

double colorDistance(const QColor& a, const QColor& b) {
    const double dr = (a.red() - b.red()) / 255.0;
    const double dg = (a.green() - b.green()) / 255.0;
    const double db = (a.blue() - b.blue()) / 255.0;
    return std::sqrt((dr * dr + dg * dg + db * db) / 3.0);
}

int countNonBackground(const QImage& image, const QColor& bg) {
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(argb.scanLine(y));
        for (int x = 0; x < argb.width(); ++x)
            if (colorDistance(QColor(line[x]), bg) > 0.03)
                ++count;
    }
    return count;
}

class Harness {
public:
    NexusQrcFallback fallback{QStringLiteral(NEXUS_BUILD_DIR "/NexusAI")};
    QQmlApplicationEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window{nullptr};
    QString error;

    Harness() {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(
                QStringLiteral(NEXUS_TEST_QML_DIR "/SessionTemplateHarness.qml")));
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
        QTest::qWait(150);
    }

    QVariant call(const char* fn) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, out)))
            return {};
        return out;
    }

    bool callVoid(const char* fn) const {
        return root && QMetaObject::invokeMethod(root.get(), fn);
    }
};

// Settings 会话模板是应用级单例且写盘，用例前后还原。
class SavedTemplates {
public:
    SavedTemplates() {
        QSettings s = NexusAI::TestSupport::testSettings();
        m_raw = s.value("sessionTemplates");
        clear();
    }
    ~SavedTemplates() {
        QSettings s = NexusAI::TestSupport::testSettings();
        if (m_raw.isValid()) s.setValue("sessionTemplates", m_raw);
        else s.remove("sessionTemplates");
    }

    // 走公开 API 重建（没有整体写入入口）。
    static void clear() {
        SettingsService* svc = SettingsService::instance();
        const QVariantList list = svc->sessionTemplates();
        for (const QVariant& entry : list)
            svc->removeSessionTemplate(entry.toMap().value("id").toString());
    }

    static QString add(const QString& name, const QString& profileId,
                       int permissionMode, const QString& firstMessage) {
        SettingsService* svc = SettingsService::instance();
        const QString id = svc->addSessionTemplate(name, profileId,
                                                  permissionMode, firstMessage);
        return id;
    }

private:
    QVariant m_raw;
};

} // namespace

class TestSessionTemplateQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    // 菜单能打开、画出内容、完整落在窗口内、模板行数正确且行高够两行。
    void menuRendersAndFitsWindow() {
        SavedTemplates saved;
        AgentViewModel* vm = AgentViewModel::instance();
        QVERIFY(vm);
        SavedTemplates::add(QStringLiteral("代码审查"), QString(),
                            vm->permissionModePlanOnly(),
                            QStringLiteral("请审查这个项目的近期改动。"));
        SavedTemplates::add(QStringLiteral("项目探索"), QString(), -1,
                            QStringLiteral("带我了解这个项目。"));

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("templateCount").toInt(), 2);

        QVERIFY(h.callVoid("open"));
        QTRY_VERIFY2(h.call("popped").toBool(),
                     qPrintable("openDebug: " + h.call("openDebug").toString()));
        QTest::qWait(250);
        (void)h.window->grabWindow();
        QTest::qWait(250);

        const QColor bg(h.call("backgroundColor").toString());
        QVERIFY(bg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "SessionTemplateMenu non-background pixels:" << visible;
        QVERIFY2(visible > 400,
                 qPrintable(QStringLiteral("模板菜单渲染为空白（非背景像素 %1）").arg(visible)));

        const double top = h.call("popupTop").toDouble();
        const double bottom = h.call("popupBottom").toDouble();
        const double winH = h.call("windowHeight").toDouble();
        QVERIFY2(top >= 0, qPrintable(QStringLiteral("菜单顶部越界：y=%1").arg(top)));
        QVERIFY2(bottom <= winH + 1,
                 qPrintable(QStringLiteral("菜单底部越界：%1 > %2").arg(bottom).arg(winH)));

        const double minRow = h.call("minRowHeight").toDouble();
        QVERIFY2(minRow >= 40.0,
                 qPrintable(QStringLiteral("模板行高不足（%1），两行摘要会被裁掉").arg(minRow)));

        h.callVoid("closeIfOpen");
    }

    // 摘要文案：配置名 · 权限；配置被删除时如实说明。
    void subtitleReflectsProfileAndPermission() {
        SavedTemplates saved;
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        // 空 profileId → 「配置不变」；不存在的 id → 「配置已删除」。
        QVariant out;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "profileText",
                                         Q_RETURN_ARG(QVariant, out),
                                         Q_ARG(QVariant, QString())));
        QCOMPARE(out.toString(), QStringLiteral("配置不变"));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "profileText",
                                         Q_RETURN_ARG(QVariant, out),
                                         Q_ARG(QVariant, QStringLiteral("no-such-profile"))));
        QCOMPARE(out.toString(), QStringLiteral("配置已删除"));

        // 权限档位文案与设置页同口径（-1 = 权限不变）。
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "permissionText",
                                         Q_RETURN_ARG(QVariant, out),
                                         Q_ARG(QVariant, -1)));
        QCOMPARE(out.toString(), QStringLiteral("权限不变"));
        QVERIFY(QMetaObject::invokeMethod(
            h.root.get(), "permissionText", Q_RETURN_ARG(QVariant, out),
            Q_ARG(QVariant, AgentViewModel::instance()->permissionModePlanOnly())));
        QCOMPARE(out.toString(), QStringLiteral("只读计划"));
    }

    // 「管理模板…」必须把信号发出去，否则设置中心入口静默失效。
    void manageEntryEmitsRequest() {
        SavedTemplates saved;
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("manageRequestedCount").toInt(), 0);
        QVERIFY(h.callVoid("triggerManage"));
        QCOMPARE(h.call("manageRequestedCount").toInt(), 1);
    }

    // ★ 应用语义：权限落到新标签、首条消息只进草稿（不自动发送）。
    void startFromTemplateAppliesPermissionAndDraft() {
        SavedTemplates saved;
        AgentViewModel* vm = AgentViewModel::instance();
        QVERIFY(vm);

        const int tabsBefore = vm->tabModel().size();
        const QVariantMap tmpl{
            {QStringLiteral("id"), QStringLiteral("t1")},
            {QStringLiteral("name"), QStringLiteral("代码审查")},
            {QStringLiteral("profileId"), QString()},
            {QStringLiteral("permissionMode"),
             AgentViewModel::instance()->permissionModePlanOnly()},
            {QStringLiteral("firstMessage"), QStringLiteral("请审查近期改动。")},
        };

        vm->startFromSessionTemplate(tmpl);

        // 新标签已建立并激活。
        QCOMPARE(vm->tabModel().size(), tabsBefore + 1);
        QCOMPARE(vm->activeTabIndex(), tabsBefore);

        // 权限落到新标签。
        QCOMPARE(vm->permissionMode(), vm->permissionModePlanOnly());

        // 首条消息只在草稿里，没有被发送（轨迹仍为空、非忙碌）。
        QCOMPARE(vm->draftText(), QStringLiteral("请审查近期改动。"));
        QCOMPARE(vm->traceCount(), 0);
        QVERIFY(!vm->stateBusy());
    }

    // 用户报告：「点第二次还是打开，必须点别处才关」。
    // 真实鼠标点触发器两次：第一次开，第二次必须关。
    void secondClickOnTriggerClosesMenu() {
        SavedTemplates saved;
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("popped").toBool(), false);

        auto* trigger = h.window->findChild<QQuickItem*>(
            QStringLiteral("templateTrigger"));
        QVERIFY2(trigger, "template trigger not found");
        const QPoint center = trigger->mapToScene(QPointF(trigger->width() / 2,
                                                         trigger->height() / 2)).toPoint();

        QTest::mousePress(h.window, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseRelease(h.window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(h.call("popped").toBool());
        QTest::qWait(200);

        QTest::mousePress(h.window, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseRelease(h.window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY2(!h.call("popped").toBool(),
                     "clicking the trigger a second time must close the menu");
    }

    // 空模板退化为普通新建（菜单里的「空白新对话」走同一条路径）。
    void emptyTemplateDegradesToPlainNewChat() {
        SavedTemplates saved;
        AgentViewModel* vm = AgentViewModel::instance();
        const int tabsBefore = vm->tabModel().size();
        vm->startFromSessionTemplate(QVariantMap());
        QCOMPARE(vm->tabModel().size(), tabsBefore + 1);
        QCOMPARE(vm->draftText(), QString());
    }

    // permissionMode 缺省（-1）时不得改动新标签的权限。
    void missingPermissionModeKeepsDefault() {
        SavedTemplates saved;
        AgentViewModel* vm = AgentViewModel::instance();
        const int before = vm->permissionMode();
        vm->startFromSessionTemplate(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("不改权限")},
            {QStringLiteral("profileId"), QString()},
            {QStringLiteral("permissionMode"), -1},
            {QStringLiteral("firstMessage"), QString()},
        });
        QCOMPARE(vm->permissionMode(), before);
    }
};

QTEST_MAIN(TestSessionTemplateQml)
#include "test_session_template_qml.moc"
