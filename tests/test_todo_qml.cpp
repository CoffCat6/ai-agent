// F1: TodoListPanel 的渲染与状态回归。
//
// 既有测试从不加载该组件；而 todo_write -> AgentVM.todoList 的数据链早已
// 存在，真正缺失的是"用户能否看见"。本用例加载 build/NexusAI/ 文件系统
// 副本里的真实组件（与 test_charts_qml 同一套 qrc 回退机制），锁住：
//   1. 空列表不占位（visible=false、implicitHeight=0）；
//   2. 三种状态与优先级正确映射到 delegate 属性；
//   3. 折叠只隐藏列表、展开恢复；
//   4. 面板确实画出了内容（像素级非背景断言）。
//
// 不通过 AgentVM 注入数据：AgentVM 是应用级门面，测试里构造成本高；组件
// 只依赖 todos 数组，直接注入 todo_write 的输出形状即可覆盖展示契约。

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

#include "viewmodels/ThemeProvider.h"

namespace {

// build/NexusAI/qmldir 的 prefer 指向测试进程不存在的 qrc；把 NexusAI
// 前缀重写到构建目录的文件系统副本（与 test_charts_qml 相同方案）。
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

QVariantMap todo(const QString& content, const QString& status, const QString& priority) {
    return QVariantMap{{"content", content}, {"status", status}, {"priority", priority}};
}

// Repeater delegates are visual children but not necessarily QObject children
// of the window, so QObject::findChildren misses them. Walk childItems().
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
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/TodoPanelHarness.qml")));
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
        QTest::qWait(150);   // layout + first paint
    }

    QVariant call(const char* fn) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn, Q_RETURN_ARG(QVariant, out)))
            return {};
        return out;
    }

    bool setTodos(const QVariantList& items) const {
        return root && QMetaObject::invokeMethod(root.get(), "setTodos",
                                                 Q_ARG(QVariant, items));
    }

    bool setCollapsed(bool value) const {
        return root && QMetaObject::invokeMethod(root.get(), "setCollapsed",
                                                 Q_ARG(QVariant, value));
    }
};

} // namespace

class TestTodoQml : public QObject {
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

    void rendersStatusesAndPriorities() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        const QVariantList items{
            todo(QStringLiteral("Inspect code"), QStringLiteral("completed"), QStringLiteral("high")),
            todo(QStringLiteral("Apply fix"), QStringLiteral("in_progress"), QStringLiteral("medium")),
            todo(QStringLiteral("Run tests"), QStringLiteral("pending"), QStringLiteral("low"))
        };
        QVERIFY(h.setTodos(items));

        QTRY_COMPARE(h.call("itemCount").toInt(), 3);
        QTRY_COMPARE(h.call("completedCount").toInt(), 1);
        QTRY_VERIFY(h.call("panelVisible").toBool());
        QTRY_VERIFY(h.call("panelHeight").toReal() > 0);
        QTRY_VERIFY(h.call("listVisible").toBool());
        // Force one frame: QQmlApplicationEngine installs an incubation
        // controller, so delegate creation may be flushed by the render
        // loop rather than by plain event processing.
        QTest::qWait(200);
        (void)h.window->grabWindow();
        QTest::qWait(200);
        QTRY_COMPARE(h.call("renderedCount").toInt(), 3);

        QList<QQuickItem*> delegates;
        collectVisualItems(h.window->contentItem(), QStringLiteral("todoItem"), &delegates);
        QCOMPARE(delegates.size(), 3);

        QMap<QString, QQuickItem*> byContent;
        for (QQuickItem* item : delegates)
            byContent.insert(item->property("content").toString(), item);
        QCOMPARE(byContent.size(), 3);

        QCOMPARE(byContent.value(QStringLiteral("Inspect code"))->property("itemStatus").toString(),
                 QStringLiteral("completed"));
        QCOMPARE(byContent.value(QStringLiteral("Inspect code"))->property("priority").toString(),
                 QStringLiteral("high"));
        QCOMPARE(byContent.value(QStringLiteral("Inspect code"))->property("isCompleted").toBool(), true);

        QCOMPARE(byContent.value(QStringLiteral("Apply fix"))->property("itemStatus").toString(),
                 QStringLiteral("in_progress"));
        QCOMPARE(byContent.value(QStringLiteral("Apply fix"))->property("priority").toString(),
                 QStringLiteral("medium"));
        QCOMPARE(byContent.value(QStringLiteral("Apply fix"))->property("isCompleted").toBool(), false);

        QCOMPARE(byContent.value(QStringLiteral("Run tests"))->property("itemStatus").toString(),
                 QStringLiteral("pending"));
        QCOMPARE(byContent.value(QStringLiteral("Run tests"))->property("priority").toString(),
                 QStringLiteral("low"));
        QCOMPARE(byContent.value(QStringLiteral("Run tests"))->property("isCompleted").toBool(), false);

        // 组件真的画出了内容，而不是"数据有、界面空白"。
        const QColor bg(h.call("backgroundColor").toString());
        QVERIFY(bg.isValid());
        const QImage shot = h.window->grabWindow();
        QVERIFY2(!shot.isNull(), "整窗抓图失败（offscreen 渲染异常）");
        const int visible = countNonBackground(shot, bg);
        qInfo() << "TodoListPanel non-background pixels:" << visible;
        QVERIFY2(visible > 200,
                 qPrintable(QStringLiteral("任务清单渲染为空白（非背景像素 %1）").arg(visible)));
    }

    void collapsesAndExpands() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        const QVariantList items{
            todo(QStringLiteral("One"), QStringLiteral("pending"), QStringLiteral("low")),
            todo(QStringLiteral("Two"), QStringLiteral("in_progress"), QStringLiteral("medium"))
        };
        QVERIFY(h.setTodos(items));
        QTRY_VERIFY(h.call("panelVisible").toBool());
        QTRY_VERIFY(h.call("panelHeight").toReal() > 0);
        QTest::qWait(150);
        (void)h.window->grabWindow();   // flush delegate incubation
        QTest::qWait(150);
        const qreal expanded = h.call("panelHeight").toReal();
        QVERIFY(expanded > 22);         // header + at least one row

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
        QVERIFY(h.setTodos({todo(QStringLiteral("One"), QStringLiteral("pending"),
                                 QStringLiteral("low"))}));
        QTRY_VERIFY(h.call("panelVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "clearTodos"));
        QTRY_COMPARE(h.call("itemCount").toInt(), 0);
        QCOMPARE(h.call("panelVisible").toBool(), false);
        QCOMPARE(h.call("panelHeight").toReal(), 0.0);
    }
};

QTEST_MAIN(TestTodoQml)
#include "test_todo_qml.moc"
