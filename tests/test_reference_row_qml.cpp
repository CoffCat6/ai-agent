// G2: ReferenceChipRow 的渲染/状态回归。
//
// 用户反馈「@ 引用后看不出有没有成功」，而该反馈完全由这个组件承担。
// 与 test_todo_qml 同一套方案：加载 build/NexusAI/ 文件系统副本里的真实
// 组件（qrc 前缀重写），锁住：
//   1. 空 checks 时不占位；
//   2. 成功项显示项目相对路径、status=ok；
//   3. 失败项显示 @token、status=error；
//   4. 组件确实画出了内容（像素级非背景断言），防止「逻辑有、界面空白」。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QColor>
#include <QFile>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <memory>

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

QVariantMap check(const QString& token, const QString& path, const QString& error) {
    return QVariantMap{{"token", token}, {"path", path}, {"error", error}};
}

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
    QString error;

    Harness() {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/ReferenceRowHarness.qml")));
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

    bool setChecks(const QVariantList& items) const {
        return root && QMetaObject::invokeMethod(root.get(), "setChecks",
                                                 Q_ARG(QVariant, items));
    }
};

} // namespace

class TestReferenceRowQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    void emptyStateHidesRow() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("chipCount").toInt(), 0);
        QCOMPARE(h.call("rowVisible").toBool(), false);
    }

    void rendersResolvedAndFailedChips() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList checks{
            check(QStringLiteral("src/main.cpp"), QStringLiteral("src/main.cpp"), QString()),
            check(QStringLiteral("missing.txt"), QString(), QStringLiteral("missing")),
            check(QStringLiteral(".env"), QString(), QStringLiteral("sensitive")),
        };
        QVERIFY(h.setChecks(checks));

        QTRY_COMPARE(h.call("chipCount").toInt(), 3);
        QTRY_VERIFY(h.call("rowVisible").toBool());
        QVERIFY(h.call("rowHeight").toReal() > 0);

        QTest::qWait(200);
        (void)h.window->grabWindow();   // flush delegate incubation
        QTest::qWait(200);

        QList<QQuickItem*> chips;
        collectVisualItems(h.window->contentItem(), QStringLiteral("referenceChip"), &chips);
        QCOMPARE(chips.size(), 3);

        QMap<QString, QQuickItem*> byLabel;
        for (QQuickItem* chip : chips)
            byLabel.insert(chip->property("label").toString(), chip);
        QCOMPARE(byLabel.size(), 3);

        auto* resolved = byLabel.value(QStringLiteral("src/main.cpp"));
        QVERIFY(resolved);
        QCOMPARE(resolved->property("status").toString(), QStringLiteral("ok"));

        auto* missing = byLabel.value(QStringLiteral("@missing.txt"));
        QVERIFY(missing);
        QCOMPARE(missing->property("status").toString(), QStringLiteral("error"));

        auto* sensitive = byLabel.value(QStringLiteral("@.env"));
        QVERIFY(sensitive);
        QCOMPARE(sensitive->property("status").toString(), QStringLiteral("error"));

        // The row must actually paint, not merely hold data.
        const QColor bg(h.call("backgroundColor").toString());
        QVERIFY(bg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "ReferenceChipRow non-background pixels:" << visible;
        QVERIFY2(visible > 100,
                 qPrintable(QStringLiteral("引用状态行渲染为空白（非背景像素 %1）").arg(visible)));
    }

    void clearingHidesRow() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setChecks({check(QStringLiteral("main.cpp"),
                                   QStringLiteral("main.cpp"), QString())}));
        QTRY_VERIFY(h.call("rowVisible").toBool());

        QVERIFY(h.setChecks({}));
        QTRY_COMPARE(h.call("chipCount").toInt(), 0);
        QTRY_COMPARE(h.call("rowVisible").toBool(), false);
    }
};

QTEST_MAIN(TestReferenceRowQml)
#include "test_reference_row_qml.moc"
