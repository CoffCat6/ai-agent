// G4: FileChangesPanel rendering regression.
//
// The data chain (tool result -> SQLite -> AgentVM.fileCheckpoints) is
// covered by test_tools/test_memory/test_agent; this case locks the visible
// contract: empty state takes no space, rows carry the undone/truncated
// state, collapse works, and the panel actually paints content.

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <memory>

#include "viewmodels/ThemeProvider.h"

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

void collectVisualItems(QQuickItem* parent, const QString& objectName,
                        QList<QQuickItem*>* out) {
    const auto children = parent->childItems();
    for (QQuickItem* child : children) {
        if (child->objectName() == objectName)
            out->append(child);
        collectVisualItems(child, objectName, out);
    }
}

QVariantMap checkpoint(qint64 id, const QString& path, bool existed,
                       bool truncated, bool undone) {
    return QVariantMap{
        {"id", id},
        {"path", path},
        {"existed", existed},
        {"truncated", truncated},
        {"undone", undone},
        {"byteSize", 12},
        {"requestId", 1},
        {"createdAt", QDateTime::currentMSecsSinceEpoch()},
    };
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
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/FileChangesHarness.qml")));
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

    bool setCheckpoints(const QVariantList& items) const {
        return root && QMetaObject::invokeMethod(root.get(), "setCheckpoints",
                                                 Q_ARG(QVariant, items));
    }

    bool setCollapsed(bool value) const {
        return root && QMetaObject::invokeMethod(root.get(), "setCollapsed",
                                                 Q_ARG(QVariant, value));
    }
};

} // namespace

class TestFileChangesQml : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    void emptyStateTakesNoSpace() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QCOMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("panelVisible").toBool(), false);
        QCOMPARE(h.call("panelHeight").toReal(), 0.0);
    }

    void rendersRowsAndStatuses() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList items{
            checkpoint(3, QStringLiteral("/project/src/new.cpp"), false, false, false),
            checkpoint(2, QStringLiteral("/project/src/big.cpp"), true, true, false),
            checkpoint(1, QStringLiteral("/project/src/old.cpp"), true, false, true)
        };
        QVERIFY(h.setCheckpoints(items));

        QTRY_COMPARE(h.call("itemCount").toInt(), 3);
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);
        QVERIFY(h.call("panelVisible").toBool());
        QVERIFY(h.call("panelHeight").toReal() > 0);
        QVERIFY(h.call("listVisible").toBool());
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);

        QList<QQuickItem*> rows;
        collectVisualItems(h.window->contentItem(), QStringLiteral("fileChangeRow"), &rows);
        QCOMPARE(rows.size(), 3);

        QMap<QString, QQuickItem*> byPath;
        for (QQuickItem* row : rows)
            byPath.insert(row->property("modelData").toMap().value("path").toString(), row);
        QCOMPARE(byPath.size(), 3);
        QCOMPARE(byPath.value(QStringLiteral("/project/src/big.cpp"))
                     ->property("truncated").toBool(), true);
        QCOMPARE(byPath.value(QStringLiteral("/project/src/old.cpp"))
                     ->property("undone").toBool(), true);
        QCOMPARE(byPath.value(QStringLiteral("/project/src/new.cpp"))
                     ->property("undone").toBool(), false);

        const QColor bg(h.call("backgroundColor").toString());
        QVERIFY(bg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "FileChangesPanel non-background pixels:" << visible;
        QVERIFY2(visible > 200,
                 qPrintable(QStringLiteral("文件改动面板渲染为空白（非背景像素 %1）").arg(visible)));
    }

    void collapsesAndExpands() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({checkpoint(1, QStringLiteral("/p/a.cpp"), true, false, false),
                                  checkpoint(2, QStringLiteral("/p/b.cpp"), false, false, false)}));
        QTRY_VERIFY(h.call("panelVisible").toBool());
        QTest::qWait(150);
        (void)h.window->grabWindow();
        QTest::qWait(150);
        const qreal expanded = h.call("panelHeight").toReal();
        QVERIFY(expanded > 40);

        QVERIFY(h.setCollapsed(true));
        QTRY_COMPARE(h.call("listVisible").toBool(), false);
        QTRY_VERIFY(h.call("panelHeight").toReal() < expanded);

        QVERIFY(h.setCollapsed(false));
        QTRY_COMPARE(h.call("listVisible").toBool(), true);
        QTRY_COMPARE(h.call("panelHeight").toReal(), expanded);
    }

    void clearingHidesPanel() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.setCheckpoints({checkpoint(1, QStringLiteral("/p/a.cpp"), true, false, false)}));
        QTRY_VERIFY(h.call("panelVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "clearCheckpoints"));
        QTRY_COMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("panelVisible").toBool(), false);
        QCOMPARE(h.call("panelHeight").toReal(), 0.0);
    }
};

QTEST_MAIN(TestFileChangesQml)
#include "test_file_changes_qml.moc"
