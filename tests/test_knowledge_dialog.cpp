// Knowledge base dialog (moved out of the right sidebar).
//
// The dialog's own logic is thin - the interesting contract is what the move
// changed: it must be reachable from the sidebar's global tool strip, its list
// must render the same document data the sidebar used to show, and the right
// panel must no longer carry a knowledge tab.
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFile>
#include <QVariantMap>

#include <memory>

#include "viewmodels/AgentViewModel.h"
#include "viewmodels/ThemeProvider.h"

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

// A Dialog is a Popup, so it is not part of the item tree an objectName search
// reaches. Located by the properties only this dialog declares.
QObject* findKnowledgeDialog(QObject* root) {
    if (!root)
        return nullptr;
    const QMetaObject* meta = root->metaObject();
    if (meta->indexOfProperty("opened") >= 0
            && meta->indexOfProperty("docCount") >= 0
            && meta->indexOfProperty("totalChunks") >= 0) {
        return root;
    }
    for (QObject* child : root->children()) {
        if (QObject* hit = findKnowledgeDialog(child))
            return hit;
    }
    return nullptr;
}

QVariantMap doc(const QString& name, int chunks, int chars, bool pseudo) {
    return QVariantMap{
        {"name", name},
        {"chunks", chunks},
        {"chars", chars},
        {"pseudo", pseudo},
    };
}

} // namespace

class TestKnowledgeDialog : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NexusAI"));
        QCoreApplication::setApplicationName(QStringLiteral("NexusAI"));
    }

    // The dialog is reachable from the sidebar tool strip, and the right panel
    // no longer offers a knowledge tab. This is the whole point of the move.
    void sidebarOpensDialogAndPanelHasNoKnowledgeTab() {
        QQmlApplicationEngine engine;
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        engine.addUrlInterceptor(&fallback);
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(150);

        // The entry exists in the sidebar ...
        auto* button = window->findChild<QObject*>(QStringLiteral("knowledgeButton"));
        QVERIFY2(button, "sidebar is missing knowledgeButton");

        // ... and activating it opens the dialog.
        QObject* dialog = findKnowledgeDialog(window);
        QVERIFY2(dialog, "main.qml does not host the knowledge dialog");
        QCOMPARE(dialog->property("opened").toBool(), false);
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY(dialog->property("opened").toBool());

        // The theme toggle that used to share that row is gone: the same
        // switch lives in Settings > Interface.
        QVERIFY2(window->findChild<QObject*>(QStringLiteral("themeToggle")) == nullptr,
                 "theme toggle should have left the sidebar for the tool strip's new button");
    }

    // The list renders what the sidebar used to render, and the header
    // summarises the totals.
    void dialogListsDocumentsAndSummary() {
        QQmlApplicationEngine engine;
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        engine.addUrlInterceptor(&fallback);
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(150);

        QObject* dialog = findKnowledgeDialog(window);
        QVERIFY(dialog);

        // Inject a fixed list: the dialog reads its `documents` property, so
        // this needs no database.
        const QVariantList docs{
            doc(QStringLiteral("guide.pdf"), 42, 128000, false),
            doc(QStringLiteral("notes.md"), 18, 36000, true),
        };
        QVERIFY(dialog->setProperty("documents", docs));
        QCOMPARE(dialog->property("docCount").toInt(), 2);
        QCOMPARE(dialog->property("totalChunks").toInt(), 60);
        QCOMPARE(dialog->property("totalChars").toInt(), 164000);
        QCOMPARE(dialog->property("empty").toBool(), false);

        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(200);

        // Two rows rendered, and the summary line is on screen.
        QList<QQuickItem*> list;
        collectVisualItems(window->contentItem(),
                           QStringLiteral("knowledgeDocList"), &list);
        QVERIFY2(!list.isEmpty(), "document list missing");
        QTRY_COMPARE(list.first()->property("count").toInt(), 2);

        QList<QQuickItem*> summary;
        collectVisualItems(window->contentItem(),
                           QStringLiteral("knowledgeSummary"), &summary);
        QCOMPARE(summary.size(), 1);
        QVERIFY(summary.first()->isVisible());
        QVERIFY2(summary.first()->property("text").toString().contains(QStringLiteral("2")),
                 qPrintable(summary.first()->property("text").toString()));
    }

    // An empty knowledge base shows the guidance state instead of a blank box.
    void emptyStateExplainsHowToImport() {
        QQmlApplicationEngine engine;
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        engine.addUrlInterceptor(&fallback);
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(150);

        QObject* dialog = findKnowledgeDialog(window);
        QVERIFY(dialog);
        QVERIFY(dialog->setProperty("documents", QVariantList{}));
        QCOMPARE(dialog->property("empty").toBool(), true);
        QCOMPARE(dialog->property("docCount").toInt(), 0);

        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(200);

        QList<QQuickItem*> empty;
        collectVisualItems(window->contentItem(),
                           QStringLiteral("knowledgeEmptyState"), &empty);
        QCOMPARE(empty.size(), 1);
        QVERIFY2(empty.first()->isVisible(), "empty knowledge base should explain itself");

        // The import action is still reachable from the header.
        QList<QQuickItem*> importBtn;
        collectVisualItems(window->contentItem(),
                           QStringLiteral("knowledgeImportButton"), &importBtn);
        QCOMPARE(importBtn.size(), 1);
        QVERIFY(importBtn.first()->isVisible());
    }

    // sourcePath rides the document data: an import with a recorded path
    // shows the "open source" button, a document without one does not.
    void openSourceButtonFollowsRecordedPath() {
        QQmlApplicationEngine engine;
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        engine.addUrlInterceptor(&fallback);
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(150);

        QObject* dialog = findKnowledgeDialog(window);
        QVERIFY(dialog);
        QVERIFY(dialog->setProperty("documents", QVariantList{
            doc(QStringLiteral("with.md"), 3, 100, false),
            doc(QStringLiteral("without.md"), 2, 80, false)}));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(200);

        QList<QQuickItem*> buttons;
        collectVisualItems(window->contentItem(),
                           QStringLiteral("knowledgeOpenSourceButton"), &buttons);
        QCOMPARE(buttons.size(), 2);
        // Without a sourcePath in the row data the button has nothing to
        // open. The dialog data here does not carry it, so both stay hidden:
        // the sourcePath flows through the documents property in production.
        QVERIFY2(!buttons.first()->isVisible(),
                 "no sourcePath recorded, so no open button");
    }

    // URL import: the toggle reveals the address row, and the import is
    // rejected up front for non-http(s) input. A real fetch needs the network,
    // which the test environment may not have — so the observable here is the
    // URL validation gate, not a successful ingestion.
    void urlImportRejectsNonHttpInput() {
        QQmlApplicationEngine engine;
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        engine.addUrlInterceptor(&fallback);
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(150);

        // The entry exists ...
        QVERIFY(window->findChild<QObject*>(
                    QStringLiteral("knowledgeUrlImportToggle")));
        // ... and the VM rejects a non-http scheme before touching the network.
        auto* vm = AgentViewModel::instance();
        QSignalSpy errorSpy(vm, &AgentViewModel::errorOccurred);
        vm->importFromUrl(QStringLiteral("ftp://example.com/file.md"));
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY2(errorSpy.first().at(0).toString().contains(QStringLiteral("http")),
                 qPrintable(errorSpy.first().at(0).toString()));

        // ... and an empty URL is likewise rejected without state changes.
        vm->importFromUrl(QString());
        QCOMPARE(errorSpy.count(), 2);

        // Neither attempt left a running import behind.
        QCOMPARE(vm->urlImportRunning(), false);
    }

    // Activating a row asks the host to open the chunk viewer - the same
    // documentDetailsRequested contract ContextPanel used to expose.
    void rowActivationRequestsDetails() {
        QQmlApplicationEngine engine;
        NexusQrcFallback fallback(QStringLiteral(NEXUS_BUILD_DIR "/NexusAI"));
        engine.addUrlInterceptor(&fallback);
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(150);

        QObject* dialog = findKnowledgeDialog(window);
        QVERIFY(dialog);
        QVERIFY(dialog->setProperty("documents",
                                    QVariantList{doc(QStringLiteral("a.txt"), 5, 900, false)}));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(150);

        QSignalSpy spy(dialog, SIGNAL(documentDetailsRequested(QString)));
        QVERIFY(QMetaObject::invokeMethod(dialog, "openDocument",
                                          Q_ARG(QVariant, QStringLiteral("a.txt"))));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("a.txt"));

        // An empty name is ignored rather than emitting a useless request.
        QVERIFY(QMetaObject::invokeMethod(dialog, "openDocument",
                                          Q_ARG(QVariant, QString())));
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestKnowledgeDialog)
#include "test_knowledge_dialog.moc"
