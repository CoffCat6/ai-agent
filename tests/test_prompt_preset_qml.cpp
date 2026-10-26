// 提示词预设选择器的渲染与状态回归。
//
// 用户反馈「预设按钮太简陋、与主题不适配」：改造后它从 TopBar 的裸 Menu
// 变成选项行里的胶囊 + 主题化弹出列表。这类改动最容易退化成"逻辑通了、
// 界面看不见"，所以与 test_todo_qml / test_reference_row_qml 同一套
// 「build/NexusAI 文件系统副本 + qrc 回退拦截器 + offscreen」方案加载真实
// 组件，锁住：
//   1. 未套用预设时胶囊回显"默认"，套用后回显预设名（会话级状态可见）；
//   2. 预设被删除后仍显示名字并标记来源失效（不静默退回默认）；
//   3. 触发胶囊确实画出了内容（像素级非背景断言）；
//   4. 弹出列表能打开，并渲染出与预设数量一致的可见行；
//   5. 「管理预设…」真的发出信号（设置中心入口未断线）；
//   6. 摘要把换行压平（两行列表不会被多行正文撑破）。
//
// 预设数据直接写 Settings.promptPresets（应用级单例，测试共用一份设置
// 存储），用例前后保存/还原，避免污染用户配置与后续用例。

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
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <memory>

#include "viewmodels/AgentViewModel.h"
#include "viewmodels/SettingsService.h"

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
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/PromptPresetHarness.qml")));
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

// Settings 是应用级单例：写入会落盘，用例用 QSettings 守卫原始值（与
// test_settings 的 SettingsGuard 同一手法），避免污染用户配置。
class SavedPresets {
public:
    SavedPresets() {
        QSettings s("NexusAI", "NexusAI");
        m_raw = s.value("promptPresets");
        m_override = s.value("agent/systemPromptOverride");
        replaceAll({});
    }
    ~SavedPresets() {
        QSettings s("NexusAI", "NexusAI");
        if (m_raw.isValid()) s.setValue("promptPresets", m_raw);
        else s.remove("promptPresets");
        if (m_override.isValid()) s.setValue("agent/systemPromptOverride", m_override);
        else s.remove("agent/systemPromptOverride");
        // 内存列表随进程结束消失；这里不再调 removePromptPreset 回落——
        // 该接口在持久化失败时有意不改内存（避免界面与磁盘不一致），
        // 沙箱里 QSettings 不可写，调了也不生效。
    }

    // 走公开 API 重建列表：SettingsService 没有测试专用的整体写入入口。
    static void replaceAll(const QVariantList& items) {
        SettingsService* svc = SettingsService::instance();
        while (!svc->promptPresets().isEmpty())
            svc->removePromptPreset(svc->promptPresets().first().toMap()
                                        .value("id").toString());
        for (const QVariant& entry : items) {
            const QVariantMap p = entry.toMap();
            svc->addPromptPreset(p.value("name").toString(),
                                 p.value("prompt").toString());
        }
    }

    // 建一条预设并返回它真实的 id（addPromptPreset 生成 UUID）：界面按 id
    // 比对"当前套用的是哪个预设"，用例必须用真 id 才能验证回显。
    static QString add(const QString& name, const QString& prompt) {
        SettingsService* svc = SettingsService::instance();
        svc->addPromptPreset(name, prompt);
        return svc->promptPresets().last().toMap().value("id").toString();
    }

private:
    QVariant m_raw;
    QVariant m_override;
};

} // namespace

class TestPromptPresetQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    // 未套用预设：胶囊显示默认文案，且确实画出内容。
    void defaultStateRendersTrigger() {
        SavedPresets saved;
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QCOMPARE(h.call("isApplied").toBool(), false);
        QCOMPARE(h.call("triggerLabel").toString(), QStringLiteral("提示词·默认"));
        QVERIFY(h.call("selectorHeight").toReal() >= 24);

        QTest::qWait(150);
        const QColor bg(h.call("backgroundColor").toString());
        QVERIFY(bg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "PromptPresetSelector trigger non-background pixels:" << visible;
        QVERIFY2(visible > 100,
                 qPrintable(QStringLiteral("触发胶囊渲染为空白（非背景像素 %1）").arg(visible)));
    }

    // 套用预设后回显名称；套用的预设已不在列表里（被删除）时仍回显名字
    // 并标记来源失效——不能静默退回默认，那是"提示悄悄换了"的错觉。
    void appliedStateEchoesNameAndFlagsMissingSource() {
        SavedPresets saved;
        const QString reviewerId = SavedPresets::add(
            QStringLiteral("技术评审"), QStringLiteral("你是严谨的技术评审。"));
        QVERIFY(!reviewerId.isEmpty());

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        AgentViewModel::instance()->applyPromptPreset(
            reviewerId, QStringLiteral("技术评审"),
            QStringLiteral("你是严谨的技术评审。"));
        QTRY_COMPARE(h.call("triggerLabel").toString(), QStringLiteral("技术评审"));
        QCOMPARE(h.call("isApplied").toBool(), true);
        QCOMPARE(h.call("sourceMissing").toBool(), false);

        // 预设已被删除：id 还在标签上、列表里查不到。这条路径不依赖任何
        // 设置写盘（沙箱里 QSettings 写失败时删除动作不改内存列表）。
        AgentViewModel::instance()->applyPromptPreset(
            QStringLiteral("preset-that-no-longer-exists"),
            QStringLiteral("技术评审"), QStringLiteral("旧提示"));
        QTRY_VERIFY(h.call("sourceMissing").toBool());
        QVERIFY(h.call("triggerLabel").toString().contains(QStringLiteral("技术评审")));

        AgentViewModel::instance()->clearPromptPreset();
        QTRY_COMPARE(h.call("isApplied").toBool(), false);
        QCOMPARE(h.call("triggerLabel").toString(), QStringLiteral("提示词·默认"));
        QCOMPARE(h.call("sourceMissing").toBool(), false);
    }

    // 弹出列表可打开，并为每个预设渲染一行。
    void popupRendersPresetRows() {
        SavedPresets saved;
        SavedPresets::add(QStringLiteral("技术评审"),
                          QStringLiteral("你是严谨的技术评审。"));
        SavedPresets::add(QStringLiteral("耐心导师"),
                          QStringLiteral("你是耐心的导师。"));
        SavedPresets::add(QStringLiteral("简洁模式"),
                          QStringLiteral("用尽可能少的篇幅回答。"));
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("presetCount").toInt(), 3);

        QVERIFY(h.callVoid("openPopup"));
        QTRY_VERIFY(h.call("opened").toBool());
        QTest::qWait(250);
        (void)h.window->grabWindow();     // flush delegate incubation
        QTest::qWait(250);

        const QColor bg(h.call("backgroundColor").toString());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "PromptPresetSelector popup non-background pixels:" << visible;
        // 触发胶囊 + 分区标题 + 无预设行 + 3 个预设行 + 分隔线 + 管理入口，
        // 内容明显多于只有胶囊的情况。
        QVERIFY2(visible > 400,
                 qPrintable(QStringLiteral("弹出列表渲染过窄/空白（非背景像素 %1）").arg(visible)));

        // 几何自检：列表必须完整落在窗口内（贴底向上弹，越界就把顶部内容
        // 切在屏幕外），且最矮的一行要装得下"名称 + 两行摘要"。
        const double top = h.call("popupTop").toDouble();
        const double bottom = h.call("popupBottom").toDouble();
        const double left = h.call("popupLeft").toDouble();
        const double winH = h.call("windowHeight").toDouble();
        QVERIFY2(top >= 0, qPrintable(QStringLiteral("弹出列表顶部越界：y=%1").arg(top)));
        QVERIFY2(bottom <= winH + 1,
                 qPrintable(QStringLiteral("弹出列表底部越界：%1 > %2").arg(bottom).arg(winH)));
        QVERIFY2(left >= 0, qPrintable(QStringLiteral("弹出列表左侧越界：x=%1").arg(left)));
        QCOMPARE(h.call("presetRowCount").toInt(), 3);
        const double minRow = h.call("minPresetRowHeight").toDouble();
        QVERIFY2(minRow >= 44.0,
                 qPrintable(QStringLiteral("预设行高不足（%1），两行摘要会被裁掉").arg(minRow)));

        h.callVoid("closeIfOpen");
    }

    // 「管理预设…」必须把信号发出去，否则设置中心入口静默失效。
    void manageEntryEmitsRequest() {
        SavedPresets saved;
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("manageRequestedCount").toInt(), 0);
        QVERIFY(h.callVoid("triggerManage"));
        QCOMPARE(h.call("manageRequestedCount").toInt(), 1);
    }

    // 多行提示词要压成单行摘要，否则两行列表会被正文撑破。
    void summarizeFlattensWhitespace() {
        SavedPresets saved;
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVariant out;
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "summarize",
                                         Q_RETURN_ARG(QVariant, out),
                                         Q_ARG(QVariant, QStringLiteral("第一行\n\n  第二行\t第三"))));
        QCOMPARE(out.toString(), QStringLiteral("第一行 第二行 第三"));
    }
};

QTEST_MAIN(TestPromptPresetQml)
#include "test_prompt_preset_qml.moc"
