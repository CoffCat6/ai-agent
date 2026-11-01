// 任务 20：统计页三图表（DonutChart / LineTrendChart / BarCompareChart）
// 的渲染级回归测试。
//
// 背景：批次六（febaff1）交付后用户真机反馈 LineTrendChart 图表区域
// 完全空白，而数据链路已实证正常（用户库 d30 有 19 天数据、总计
// 1,676,757；组件在 offscreen 独立 harness 下也能渲染）。trend 空白的
// 根因未 100% 锁定，嫌疑收敛到「真实应用环境特异」。而 ctest 既有测试
// 全是 C++ QTest，没有任何用例真正加载过这三个 QML 组件——qmllint 是
// 静态检查、test_memory 是 C++ 数据层——这是盲区，本测试补上。
//
// 设计要点：
//   * 进程内加载**真实组件**：图表 QML 的 qrc 资源在 nexus_ai.exe 里，
//     测试进程访问不到；但 qt_add_qml_module 在 build/NexusAI/ 留有
//     文件系统副本（qmldir + .qml，构建时自动同步，已 diff 验证与源
//     一致）。测试引擎 addImportPath(构建目录) 后 `import NexusAI`
//     解析到该 qmldir；qmldir 的 `prefer :/qt/qml/NexusAI/` 会把类型
//     URL 指向测试进程不存在的 qrc，且 prefer 只做优先不做回退，
//     故再装一个 URL 拦截器把该前缀重写回文件系统副本（见
//     NexusQrcFallback，两处机制缺一不可）。
//   * Theme 单例注册在 NexusAI.ViewModels（链接 nexus_viewmodelsplugin
//     后进程内可用）。harness 同时 import NexusAI 与 NexusAI.ViewModels，
//     与真实页面 UsageStatsPage 一致，且兼容图表组件 import 行修复
//     前后的两种形态。
//   * 为什么用 offscreen：headless 下稳定（Canvas 已实证能完整渲染），
//     并与 test_droparea / test_settings_edit_lifecycle 共用同一套
//     QT_QPA_PLATFORM_PLUGIN_PATH 环境注入（见 tests/CMakeLists.txt）。
//   * 像素断言的意义：守护「图表渲染非空白」这一用户可见契约。数据
//     链路正常、组件存在都不代表用户看得到图——只有像素是真的。
//     QQuickWindow::grabImage() 同步抓取整窗（含卡片背景，最接近用户
//     所见），再按图表 item 的场景坐标切出 boundingRect 区域统计：
//     「非卡片背景色像素」度量图表画了多少内容；折线、首柱、主扇区
//     均使用 Theme.brandPrimary（基准色经 harness 从真实 Theme 单例
//     读取，主题调整时自动跟随）。
//   * 基线价值：若 trend 的像素断言失败 = 复现了用户反馈的空白 bug，
//     此时如实输出像素统计并报告；若通过 = offscreen 环境无法复现
//     （根因在真实环境特异），同样有结论价值。两种结果都不允许
//     放宽断言凑绿。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlProperty>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QColor>
#include <QFile>

#include <cmath>
#include <memory>
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

namespace {

// qrc → 文件系统副本的回退拦截器。
//
// 背景：build/NexusAI/qmldir（qt_add_qml_module 生成的模块文件系统副本）
// 里写着 `prefer :/qt/qml/NexusAI/`，图表类型的 URL 因此优先解析为
// qrc:/qt/qml/NexusAI/src/...。但该 qrc 只嵌在 nexus_ai.exe 里，测试
// 进程访问不到，而 Qt 6.9 的 prefer 指令**只做优先、不做失败回退**
// （实测直接报 "No such file or directory"，不会落到文件系统）。
// 本拦截器补上这个回退：把 /qt/qml/NexusAI/ 前缀的 qrc URL 重写到
// build/NexusAI/ 下的文件系统副本（构建时自动同步，已 diff 验证与
// src 源文件一致），仅在副本确实存在时重写，其余 URL 原样放行
// （NexusAI.ViewModels 等真实 qrc 资源不受影响）。
class NexusQrcFallback : public QQmlAbstractUrlInterceptor {
public:
    explicit NexusQrcFallback(QString baseDir)
        : m_baseDir(std::move(baseDir)) {}

    QUrl intercept(const QUrl& url, DataType /*type*/) override {
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

// 归一化 RGB 距离（0 = 同色，1 = 纯黑 vs 纯白）。
// 任务口径：与 Theme.brandPrimary 距离 < 0.15 判为品牌主色系像素
// （折线、首柱、主扇区、图例色块都是该色系）。
double colorDistance(int r, int g, int b, const QColor& ref)
{
    const double dr = (r - ref.red()) / 255.0;
    const double dg = (g - ref.green()) / 255.0;
    const double db = (b - ref.blue()) / 255.0;
    return std::sqrt((dr * dr + dg * dg + db * db) / 3.0);
}

// 单图表的像素统计结果
struct ChartPixelStats {
    qint64 total = 0;    // 图表区域的总像素数
    qint64 visible = 0;  // 非卡片背景色像素（图表真正画了内容的区域）
    qint64 brand = 0;    // 品牌主色系像素（RGB 距离 < 0.15）
};

// 统计图表 boundingRect 内的像素构成。背景 = 卡片底色（Theme.bgPrimary，
// 经 harness 从真实 Theme 单例读取），与背景距离 > 0.02 视为「画了内容」
// （0.02 的余量吸收抗锯齿过渡带的轻微色偏）。
ChartPixelStats scanChartImage(const QImage& image, const QColor& bg, const QColor& brand)
{
    ChartPixelStats stats;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    stats.total = qint64(argb.width()) * argb.height();
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(argb.scanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            const QRgb px = line[x];
            const int r = qRed(px), g = qGreen(px), b = qBlue(px);
            if (colorDistance(r, g, b, bg) > 0.02)
                ++stats.visible;
            if (colorDistance(r, g, b, brand) < 0.15)
                ++stats.brand;
        }
    }
    return stats;
}

QString describeStats(const QString& name, const ChartPixelStats& s)
{
    return QStringLiteral("%1 —— 总像素 %2 / 可见(非卡片背景) %3 / 品牌主色系 %4")
        .arg(name).arg(s.total).arg(s.visible).arg(s.brand);
}

} // namespace

class TestChartsQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // ThemeManager 单例读 QSettings 决定明暗主题；对齐主程序的组织/
        // 应用名，保证拿到确定的默认浅色主题（真实用户默认形态）。
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    // 用例一：三图表用用户真实数据渲染，逐图表做像素级断言。
    void chartsRenderWithRealData() {
        // 声明顺序保证析构反序：engine 先析构、拦截器后析构
        // （addUrlInterceptor 不获取所有权）。
        NexusQrcFallback qrcFallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&qrcFallback);
        // 关键一步：让 `import NexusAI` 解析到 build/NexusAI/qmldir
        // （模块的文件系统副本；qrc 版只在 nexus_ai.exe 里）。
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/ChartsHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win != nullptr);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        // 等布局稳定 + Canvas 首轮 paint（渲染线程需要事件循环时间）
        QTest::qWait(300);

        // 像素基准色均取自真实 Theme 单例（经 harness 转成 "#rrggbb"），
        // 不硬编码 —— 主题令牌调整时断言基准自动跟随。
        QVariant brandVar, bgVar;
        QVERIFY2(QMetaObject::invokeMethod(root.get(), "brandPrimaryColor",
                                           Q_RETURN_ARG(QVariant, brandVar)),
                 "harness 缺少 brandPrimaryColor() —— 夹具与测试不同步");
        QVERIFY2(QMetaObject::invokeMethod(root.get(), "cardBackgroundColor",
                                           Q_RETURN_ARG(QVariant, bgVar)),
                 "harness 缺少 cardBackgroundColor() —— 夹具与测试不同步");
        const QColor brand(brandVar.toString());
        const QColor cardBg(bgVar.toString());
        QVERIFY2(brand.isValid(),
                 qPrintable(QStringLiteral("品牌主色无效: %1").arg(brandVar.toString())));
        QVERIFY2(cardBg.isValid(),
                 qPrintable(QStringLiteral("卡片背景色无效: %1").arg(bgVar.toString())));

        // 同步抓取整窗一次（QQuickWindow::grabWindow 阻塞渲染线程直到
        // 完成，含卡片背景，最接近用户所见）；循环里按各图表的场景
        // 坐标切出 boundingRect 区域分别统计。
        const QImage windowImage = win->grabWindow();
        QVERIFY2(!windowImage.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const qreal dpr = win->devicePixelRatio();

        struct ChartCase { const char* objectName; const char* caption; };
        static const ChartCase cases[] = {
            { "donutChart", "DonutChart 环形图" },
            { "trendChart", "LineTrendChart 折线图" },
            { "barChart", "BarCompareChart 柱状图" },
        };
        for (const ChartCase& c : cases) {
            const QString caption = QString::fromUtf8(c.caption);
            QQuickItem* item = win->findChild<QQuickItem*>(QLatin1String(c.objectName));
            QVERIFY2(item != nullptr,
                     qPrintable(QStringLiteral("%1 未找到（objectName=%2）"
                                               "—— 模块加载失败或布局坍塌")
                                    .arg(caption, QLatin1String(c.objectName))));

            // 布局未塌陷：真实卡片 260 高，扣边距与标题后图表区应远超 100
            QVERIFY2(item->width() > 100 && item->height() > 100,
                     qPrintable(QStringLiteral("%1 尺寸塌陷: %2x%3")
                                    .arg(caption).arg(item->width()).arg(item->height())));

            // 像素级断言：切出图表 boundingRect（场景坐标 → 设备像素，
            // 与窗口边界求交防御 DPR/取整误差）
            const QPointF scenePos = item->mapToScene(QPointF(0, 0));
            QRect deviceRect(qRound(scenePos.x() * dpr), qRound(scenePos.y() * dpr),
                             qRound(item->width() * dpr), qRound(item->height() * dpr));
            deviceRect = deviceRect.intersected(windowImage.rect());
            QVERIFY2(!deviceRect.isEmpty(),
                     qPrintable(QStringLiteral("%1 图表区域越界: %2x%3 @(%4,%5)")
                                    .arg(caption).arg(item->width()).arg(item->height())
                                    .arg(scenePos.x()).arg(scenePos.y())));
            const ChartPixelStats stats =
                scanChartImage(windowImage.copy(deviceRect), cardBg, brand);
            qInfo() << describeStats(caption, stats);

            // 可见内容断言：网格/轴标/图形/图例任何一类都应贡献数百像素；
            // 全为卡片背景色 = 图表整体空白（含数据未注入、onPaint 抛
            // 异常等形态）
            QVERIFY2(stats.visible > 500,
                     qPrintable(QStringLiteral("%1 渲染为空白（非背景像素仅 %2）")
                                    .arg(caption).arg(stats.visible)));

            // 核心断言（任务阈值 50，刻意保守）：折线/首柱/主扇区/
            // 图例色块均使用品牌主色。trend 失败 = 复现用户反馈的
            // 空白 bug —— 如实报告像素统计，绝不放宽阈值凑绿。
            QVERIFY2(stats.brand > 50,
                     qPrintable(QStringLiteral("%1 品牌主色系像素仅 %2（阈值 50）"
                                               "—— 若为 trend 即复现用户反馈的空白 bug")
                                    .arg(caption).arg(stats.brand)));
        }
    }

    // 用例二：trend 数据派生链（series → points → maxValue/axisMax）。
    // 用户反馈 trend 空白但数据链路实证正常，这里把「注入 19 天后组件
    // 派生层正确」锁进测试，与用例一的像素断言互补——一个证明数据
    // 到位、一个证明像素可见，两份证据交叉定位问题层。
    void trendDataPipelineSanity() {
        NexusQrcFallback qrcFallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&qrcFallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/ChartsHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win != nullptr);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QQuickItem* trend = win->findChild<QQuickItem*>(QStringLiteral("trendChart"));
        QVERIFY(trend != nullptr);

        // QML 声明的属性经 QQmlProperty 读取（QML 动态元对象通道）
        const QVariant pointsVar = QQmlProperty(trend, "points").read();
        const QVariantList points = pointsVar.toList();
        QCOMPARE(points.size(), 19);
        QCOMPARE(QQmlProperty(trend, "hasData").read().toBool(), true);

        const double maxValue = QQmlProperty(trend, "maxValue").read().toDouble();
        QCOMPARE(maxValue, 468935.0);   // 10-15 峰值
        const double axisMax = QQmlProperty(trend, "axisMax").read().toDouble();
        QVERIFY2(std::isfinite(axisMax) && axisMax > 0.0,
                 qPrintable(QStringLiteral("axisMax 非有限正数: %1").arg(axisMax)));
        // y 轴上界必须覆盖峰值，否则折线会画出绘图区（或压缩为贴顶平线）
        QVERIFY2(axisMax >= maxValue,
                 qPrintable(QStringLiteral("y 轴上界 %1 未覆盖峰值 %2")
                                .arg(axisMax).arg(maxValue)));

        // 注入链完整性：首点 / 峰值点 / 末点
        QCOMPARE(points.first().toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("09-28"));
        QCOMPARE(points.at(16).toMap().value(QStringLiteral("value")).toDouble(), 468935.0);
        QCOMPARE(points.last().toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("10-17"));
    }

};

// offscreen 平台：本用例创建真实 QQuickWindow，CI 无显示器；
// 平台由 ctest 命令行注入（-platform offscreen），插件路径经
// QT_QPA_PLATFORM_PLUGIN_PATH 指向 SDK（见 tests/CMakeLists.txt）。
QTEST_MAIN(TestChartsQml)
#include "test_charts_qml.moc"