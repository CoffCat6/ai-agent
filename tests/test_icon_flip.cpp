// IconButton 翻转动画回归。
//
// 起因：侧栏/上下文面板的开关图标在两种状态间是瞬间替换的——用户反馈
// "不会翻转，没有动画"。补上动画时踩了一个坑：Item 没有 scaleX
// （只有等比 scale，会把图标缩成一个点而不是压扁），必须用 transform:
// Scale 单独缩 X 轴。本用例把这条钉住：
//   1. 开启 flipOnIconChange 时，切换过程中图标确实被横向压扁；
//   2. 动画结束后恢复平铺，且图标已是新形状；
//   3. 未开启时瞬间切换、不压扁（chevron-up/down 这类不该有动画）。
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QFile>
#include <memory>

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
                if (QFile::exists(fsPath)) return QUrl::fromLocalFile(fsPath);
            }
        }
        return url;
    }
private:
    QString m_baseDir;
};

// 找 AppIcon（Canvas）子项
QQuickItem* findIcon(QQuickItem* parent) {
    const auto kids = parent->childItems();
    for (QQuickItem* k : kids) {
        if (k->inherits("QQuickCanvasItem")) return k;
        if (QQuickItem* n = findIcon(k)) return n;
    }
    return nullptr;
}

int brightIn(const QImage& img, int x0, int x1, int y0, int y1) {
    int total = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (img.pixelColor(x, y).lightness() > 60) ++total;
    return total;
}
}

class TestIconFlip : public QObject {
    Q_OBJECT
private slots:
    void flipAnimationInterpolates() {
        NexusQrcFallback fb(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        QQmlApplicationEngine engine;
        engine.addUrlInterceptor(&fb);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent c(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_TEST_QML_DIR "/IconFlipHarness.qml")));
        QVERIFY2(c.isReady(), qPrintable(c.errorString()));
        std::unique_ptr<QObject> root(c.create());
        QVERIFY(root);
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        QTest::qWait(300);

        QQuickItem* flipBtn = win->findChild<QQuickItem*>(QStringLiteral("flipBtn"));
        QQuickItem* plainBtn = win->findChild<QQuickItem*>(QStringLiteral("plainBtn"));
        QVERIFY(flipBtn && plainBtn);
        QQuickItem* flipIcon = findIcon(flipBtn);
        QQuickItem* plainIcon = findIcon(plainBtn);
        QVERIFY(flipIcon && plainIcon);

        // AppIcon 的横向缩放放在 transform: Scale 里，Item::scale() 恒为 1。
        auto xScaleOf = [](QQuickItem* icon) -> qreal {
            const QVariant v = icon->property("xScale");
            if (v.isValid()) return v.toReal();
            // Scale 元素挂在 transform 列表上，读它自己的属性
            const auto trans = icon->property("transform");
            Q_UNUSED(trans);
            return -1;
        };
        // 亮像素的水平跨度：被横向压扁时这个宽度会显著变小。
        auto brightSpanX = [](const QImage& img, int x0, int x1, int y0, int y1) -> int {
            int lo = -1, hi = -1;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    if (img.pixelColor(x, y).lightness() > 60) {
                        if (lo < 0 || x < lo) lo = x;
                        if (x > hi) hi = x;
                    }
            return (lo < 0) ? 0 : (hi - lo + 1);
        };
        const int spanFull = brightSpanX(win->grabWindow(), 20, 100, 20, 100);
        qInfo() << "baseline x-span:" << spanFull;
        QVERIFY(spanFull > 30);
        const int full = brightIn(win->grabWindow(), 20, 100, 20, 100);
        qInfo() << "baseline bright:" << full;
        QVERIFY(full > 500);

        // 切换后在动画窗口内连续采样，取“最窄”的一帧。
        // 固定 45/50ms 单点采样会被首帧调度抖动放大（动画可能晚十几毫秒才
        // 起步，此时 xScale 仍接近 1）；连续采样只要求某一帧确实被压扁。
        flipBtn->setProperty("icon", QStringLiteral("panel-expand"));
        int spanMid = spanFull;
        int mid = 0;
        for (int waited = 0; waited <= 150; waited += 25) {
            if (waited > 0) QTest::qWait(25);
            const QImage sample = win->grabWindow();
            const int span = brightSpanX(sample, 20, 100, 20, 100);
            if (span < spanMid) {
                spanMid = span;
                mid = brightIn(sample, 20, 100, 20, 100);
            }
        }
        qInfo() << "mid x-span:" << spanMid << "bright:" << mid
                << "displayIcon:" << flipIcon->property("icon").toString();

        QTest::qWait(300);  // 动画结束
        const int spanAfter = brightSpanX(win->grabWindow(), 20, 100, 20, 100);
        qInfo() << "after x-span:" << spanAfter;
        QCOMPARE(spanAfter, spanFull);
        const QString after = flipIcon->property("icon").toString();
        qInfo() << "after icon:" << after;
        QCOMPARE(after, QStringLiteral("panel-expand"));

        // 关键断言：中途必须被横向压扁（跨度明显变窄）。
        if (spanMid >= spanFull * 0.9) {
            QFAIL(qPrintable(QStringLiteral("no horizontal squash during flip: "
                                            "mid-span=%1 full-span=%2")
                                 .arg(spanMid).arg(spanFull)));
        }

        // 反向对照：flipOnIconChange=false 的按钮切换应当瞬间生效、不压扁
        const int spanPlainBefore = brightSpanX(win->grabWindow(), 140, 220, 20, 100);
        plainBtn->setProperty("icon", QStringLiteral("panel-expand"));
        QTest::qWait(50);
        const int spanPlainMid = brightSpanX(win->grabWindow(), 140, 220, 20, 100);
        qInfo() << "plain x-span before/mid:" << spanPlainBefore << spanPlainMid;
        // 未开启翻转的按钮：跨度不变（不压扁），图标已瞬间切换。
        QCOMPARE(spanPlainMid, spanPlainBefore);
        QCOMPARE(plainIcon->property("icon").toString(), QStringLiteral("panel-expand"));
        qInfo() << "plain button: instant switch, scale stays 1 (as designed)";
    }
};
QTEST_MAIN(TestIconFlip)
#include "test_icon_flip.moc"
