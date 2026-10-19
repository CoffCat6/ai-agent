// G2: '@' 补全的交互回归——真实按键/鼠标事件。
//
// 用户反馈：弹窗能列出文件，但 Enter/Tab/鼠标都选不中。此前的单测只覆盖
// C++ 解析与组件渲染，从未驱动过键鼠事件。本用例加载真实的 InputConsole，
// 注入 mention 状态后分别用 QTest 发送 Enter、Tab、Down+Enter 和鼠标点击，
// 断言文本被替换为 `@路径 `（带空格路径加引号），弹窗关闭。

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <QVariantList>

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
};

QTEST_MAIN(TestMentionInteraction)
#include "test_mention_interaction.moc"
