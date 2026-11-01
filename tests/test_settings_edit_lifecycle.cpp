// Regression test (docs/20 §4.1): settings text fields must persist their
// value even when the page is destroyed while the field still has focus.
//
// Root cause this pins down: SettingsCenterDialog hosts every category page in
// a Loader and flips `active` on category switch / dialog close. Destroying
// the page does NOT emit TextField/TextArea::editingFinished, so the original
// GeneralPage code (which only saved on editingFinished) silently dropped any
// in-progress edit — "changed a setting, reopened the app, value is gone".
// The fix adds a debounce timer plus Component.onDestruction; this test drives
// the same lifecycle and asserts the value reaches the sink.

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSettings>
#include <QSignalSpy>

#include <QUrl>
#include <memory>
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

class TestSettingsEditLifecycle : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    // The page fixture mirrors the real GeneralPage wiring: a debounce timer
    // and a destruction-time commit feeding an invokable sink.
    void inProgressEditSurvivesPageDestruction() {
        QQmlEngine engine;
        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/SettingsEditHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(600, 400);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        // Find the hosted page loader and its text field.
        QObject* loader = win->findChild<QObject*>(QStringLiteral("pageLoader"));
        QVERIFY(loader);
        QObject* field = win->findChild<QObject*>(QStringLiteral("editField"));
        QVERIFY(field);

        // Simulate typing without Enter and without moving focus.
        const QString typed = QStringLiteral("137");
        QMetaObject::invokeMethod(field, "insert", Q_ARG(int, 0), Q_ARG(QString, typed));

        // Destroy the page exactly like switching settings category does.
        loader->setProperty("active", false);

        QVariant committed;
        QMetaObject::invokeMethod(root.get(), "readCommitted", Q_RETURN_ARG(QVariant, committed));
        QCOMPARE(committed.toString(), typed);
    }

    // Guard the negative case: with no commit hook the edit is lost, which is
    // what the bug looked like. This keeps the test honest — if the harness
    // ever stopped reproducing the loss, the assertion above would be vacuous.
    void editingFinishedAloneDoesNotFireOnDestruction() {        QQmlEngine engine;
        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/SettingsEditHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(600, 400);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* loader = win->findChild<QObject*>(QStringLiteral("pageLoader"));
        QVERIFY(loader);
        QObject* editingOnly = win->findChild<QObject*>(QStringLiteral("editingFinishedOnlyField"));
        QVERIFY(editingOnly);

        QMetaObject::invokeMethod(editingOnly, "insert", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("99")));
        loader->setProperty("active", false);

        QVariant fired;
        QMetaObject::invokeMethod(root.get(), "readEditingFinishedCount", Q_RETURN_ARG(QVariant, fired));
        // Documented Qt behaviour: destruction must not be relied upon to emit
        // editingFinished (verified against Qt 6.9).
        QCOMPARE(fired.toInt(), 0);
    }

    // SettingsField-style row: the reusable CHILD commits on destruction and
    // the PARENT page's handler still receives it (verified: parent handlers
    // stay connected while children are being torn down).
    void childRowCommitsOnPageDestruction() {
        QQmlEngine engine;
        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/SettingsEditHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(600, 400);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* loader = win->findChild<QObject*>(QStringLiteral("pageLoader"));
        QVERIFY(loader);
        QObject* row = win->findChild<QObject*>(QStringLiteral("settingsRow"));
        QVERIFY2(row, "settingsRow not found in harness");
        QObject* input = win->findChild<QObject*>(QStringLiteral("settingsRowInput"));
        QVERIFY(input);

        const QString typed = QStringLiteral("gpt-4o-mini");
        QMetaObject::invokeMethod(input, "insert", Q_ARG(int, 0), Q_ARG(QString, typed));
        loader->setProperty("active", false);

        QVariant sink;
        QMetaObject::invokeMethod(root.get(), "readRowSink", Q_RETURN_ARG(QVariant, sink));
        QCOMPARE(sink.toString(), typed);
    }

    // A clean row (never edited) must NOT submit on destruction: otherwise
    // every page rebuild rewrites the stored value (spurious profilesChanged,
    // and for the model field it would re-add the current model to the list).
    void cleanRowDoesNotSubmitOnDestruction() {
        QQmlEngine engine;
        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/SettingsEditHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(600, 400);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* loader = win->findChild<QObject*>(QStringLiteral("pageLoader"));
        QVERIFY(loader);
        loader->setProperty("active", false);

        QVariant count;
        QMetaObject::invokeMethod(root.get(), "readRowSubmitCount", Q_RETURN_ARG(QVariant, count));
        QCOMPARE(count.toInt(), 0);
    }

    // Password echo must commit the REAL text, not the bullet mask.
    void secretRowCommitsRealTextNotMask() {
        QQmlEngine engine;
        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/SettingsEditHarness.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(win);
        win->resize(600, 400);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QObject* loader = win->findChild<QObject*>(QStringLiteral("pageLoader"));
        QVERIFY(loader);
        QObject* row = win->findChild<QObject*>(QStringLiteral("secretRow"));
        QVERIFY(row);
        QObject* input = win->findChild<QObject*>(QStringLiteral("secretRowInput"));
        QVERIFY(input);

        const QString key = QStringLiteral("sk-secret-value");
        QMetaObject::invokeMethod(input, "insert", Q_ARG(int, 0), Q_ARG(QString, key));
        loader->setProperty("active", false);

        QVariant sink;
        QMetaObject::invokeMethod(root.get(), "readSecretSink", Q_RETURN_ARG(QVariant, sink));
        QCOMPARE(sink.toString(), key);
        QVERIFY2(!sink.toString().contains(QChar(0x2022)),
                 "committed the Password echo mask instead of the real text");
    }
};

QTEST_MAIN(TestSettingsEditLifecycle)
#include "test_settings_edit_lifecycle.moc"
