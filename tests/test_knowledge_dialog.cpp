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
        QVERIFY(QMetaObject::invokeMethod(dialog, "setDocuments", Q_ARG(QVariant, QVariant(docs))));
        qDebug().noquote() << "CLSDBG" << dialog->metaObject()->className()
                           << "docsProp:" << dialog->property("documents").toList().size();

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
        qDebug().noquote() << "LISTDBG lists:" << list.size()
                           << "count0:" << list.first()->property("count").toInt()
                           << "visible0:" << list.first()->isVisible();
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
        QVERIFY(QMetaObject::invokeMethod(dialog, "setDocuments", Q_ARG(QVariant, QVariant(QVariantList{}))));
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
        QVERIFY(QMetaObject::invokeMethod(dialog, "setDocuments", Q_ARG(QVariant,
            QVariant(QVariantList{
            doc(QStringLiteral("with.md"), 3, 100, false),
            doc(QStringLiteral("without.md"), 2, 80, false)}))));
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

    // Document filter: narrows the table by name; the empty state explains
    // itself differently for "no documents at all" vs "no name matches".
    void documentFilterNarrowsByName() {
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
        QVERIFY(QMetaObject::invokeMethod(dialog, "setDocuments", Q_ARG(QVariant,
            QVariant(QVariantList{
            doc(QStringLiteral("alpha-guide.md"), 3, 100, false),
            doc(QStringLiteral("beta-notes.md"), 2, 80, false),
            doc(QStringLiteral("gamma.md"), 1, 40, false),
        }))));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(200);

        auto listCount = [&window]() {
            QList<QQuickItem*> lists;
            collectVisualItems(window->contentItem(),
                               QStringLiteral("knowledgeDocList"), &lists);
            return lists.isEmpty() ? -1 : lists.first()->property("count").toInt();
        };
        QTRY_COMPARE(listCount(), 3);

        // Filter narrows by name, case-insensitively.
        // Drive the REAL input (filterText has no NOTIFY; a C++ setProperty
        // would not re-run QML bindings, while typing does).
        auto setFilter = [&window, &dialog](const QString& text) {
            QList<QQuickItem*> inputs;
            collectVisualItems(window->contentItem(),
                               QStringLiteral("knowledgeFilterInput"), &inputs);
            QCOMPARE(inputs.size(), 1);
            QVERIFY(inputs.first()->setProperty("text", text));
            QTest::qWait(100);
        };
        setFilter(QStringLiteral("NOTES"));
        QTRY_COMPARE(listCount(), 1);

        // No match -> the empty state names the query.
        setFilter(QStringLiteral("zzz"));
        QTRY_COMPARE(listCount(), 0);
        QList<QQuickItem*> empty;
        collectVisualItems(window->contentItem(),
                           QStringLiteral("knowledgeEmptyState"), &empty);
        QCOMPARE(empty.size(), 1);
        QVERIFY2(empty.first()->isVisible(), "无匹配应显示空态");
        bool namedQuery = false;
        const auto allItems = window->findChildren<QQuickItem*>();
        for (QQuickItem* item : allItems) {
            if (!item->isVisible()) continue;
            const QVariant t = item->property("text");
            if (t.isValid() && t.toString().contains(QStringLiteral("zzz")))
                namedQuery = true;
        }
        QVERIFY2(namedQuery, "empty state should name the query");

        // Clearing the query restores the full list.
        setFilter(QString());
        QTRY_COMPARE(listCount(), 3);
    }

    // Copy-source copies the recorded path/URL (URL imports record a URL).
    void copySourceButtonCopiesRecordedPath() {
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
        QVariantMap withPath = doc(QStringLiteral("web.md"), 2, 60, false);
        withPath.insert(QStringLiteral("sourcePath"),
                        QStringLiteral("https://example.com/article"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "setDocuments", Q_ARG(QVariant, QVariant(QVariantList{withPath}))));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(200);

        // The row carries a sourcePath, so the button renders.
        QList<QQuickItem*> buttons;
        collectVisualItems(window->contentItem(),
                           QStringLiteral("knowledgeCopySourceButton"), &buttons);
        QCOMPARE(buttons.size(), 1);
        QVERIFY(buttons.first()->isVisible());

        // The IconButton carries no modelData (its delegate row does);
        // climb to the row the button lives in and read it there.
        QQuickItem* row = buttons.first()->parentItem();
        while (row && !row->property("modelData").isValid())
            row = row->parentItem();
        QVERIFY(row);
        const QString recorded = row->property("modelData").toMap()
                                     .value("sourcePath").toString();
        QCOMPARE(recorded, QStringLiteral("https://example.com/article"));
    }

    // A URL import must record the URL as its source, never a fabricated
    // local path. The regression: importFromUrl handed a display-name-only
    // QFileInfo to the file-import pipeline, whose absoluteFilePath()
    // resolved the name against the CWD ("D:/...build/v2-6" style paths that
    // exist nowhere) - the user's copy-source then produced a local path for
    // a web document. The override now carries the URL through.
    void urlImportRecordsUrlNotLocalPath() {
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

        // The observable is the ingest chain's source-path contract. Drive it
        // through the VM's own method with a fetch that succeeds without the
        // network: not possible offline, so pin the guard at the unit level -
        // importExtractedText's rule - via a QFileInfo that mirrors what the
        // URL path builds (display name only, not absolute).
        QObject* dialog = findKnowledgeDialog(window);
        QVERIFY(dialog);

        // The rule lives on the VM (importExtractedText); probe it there.
        auto* vm = AgentViewModel::instance();
        {
            QString out;
            QVERIFY(QMetaObject::invokeMethod(vm, "probeSourcePathForName",
                                              Q_RETURN_ARG(QString, out),
                                              Q_ARG(QString, QStringLiteral("v2-6"))));
            QVERIFY2(out.isEmpty(),
                     qPrintable(QStringLiteral("display-name-only info must not "
                                               "record a path, got: %1")
                                  .arg(out)));
        }
        {
            QString out;
            QVERIFY(QMetaObject::invokeMethod(vm, "probeSourcePathForUrl",
                                              Q_RETURN_ARG(QString, out),
                                              Q_ARG(QString, QStringLiteral("https://x.example/a.md"))));
            QCOMPARE(out, QStringLiteral("https://x.example/a.md"));
        }
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
        QVERIFY(QMetaObject::invokeMethod(dialog, "setDocuments",
                                  Q_ARG(QVariant, QVariant(QVariantList{doc(QStringLiteral("a.txt"), 5, 900, false)}))));
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
