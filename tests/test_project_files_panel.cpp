// Sidebar project file browser (ProjectFilesPanel).
//
// The C++ side (dir listing rules, cache, containment) is covered by
// test_project_files / test_project_file_viewmodel. These cases are about
// what the panel itself decides: that a local chat stays inert, that
// expanding a folder loads exactly one more level, that the filter narrows
// without losing folder navigation, and that the preview reports truncation
// and binary instead of looking empty.
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariantMap>

#include <memory>

#include "viewmodels/ProjectFileViewModel.h"
#include "viewmodels/ThemeProvider.h"

using namespace NexusAI;

namespace {

// The app's QML module lives under qrc for the real binary; tests read the
// filesystem copy in the build tree instead.
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
    QString error;

    explicit Harness(const QString& qmlFile = QStringLiteral("ProjectFilesHarness.qml")) {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/") + qmlFile));
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

    QVariant call1(const char* fn, const QVariant& arg) const {
        QVariant out;
        if (!root || !QMetaObject::invokeMethod(root.get(), fn,
                                                Q_RETURN_ARG(QVariant, out),
                                                Q_ARG(QVariant, arg)))
            return {};
        return out;
    }

    bool invoke1(const char* fn, const QVariant& arg) const {
        return root && QMetaObject::invokeMethod(root.get(), fn, Q_ARG(QVariant, arg));
    }
};

} // namespace

class TestProjectFilesPanel : public QObject {
    Q_OBJECT

private slots:
    void init() {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
    }

    void cleanup() {
        ProjectFileViewModel::instance()->setRoot(QString());
        m_dir.reset();
    }

    // A local chat has no project: the panel must say so and never walk the
    // filesystem. This is the same failure mode the Git panel had (a
    // never-ending "identifying" line), so it gets the same guard.
    void localChatShowsHintAndListsNothing() {
        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));

        QCOMPARE(h.call("projectRoot").toString(), QString());
        QCOMPARE(h.call("hasProject").toBool(), false);
        QCOMPARE(h.call("rowCount").toInt(), 0);

        QList<QQuickItem*> hint;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("projectFileNoProject"), &hint);
        QCOMPARE(hint.size(), 1);
        QVERIFY2(hint.first()->isVisible(), "本地会话应提示未关联项目");

        // The tree must not be on screen at all.
        QList<QQuickItem*> lists;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("projectFileList"), &lists);
        for (QQuickItem* list : lists)
            QVERIFY2(!list->isVisible(), "无项目时不应显示文件树");
    }

    // The root lists one level; a folder's children appear only after it is
    // expanded, which is the whole point of listing lazily.
    void expandingAFolderLoadsOneMoreLevel() {
        writeFile("README", "readme\n");
        writeFile("src/main.cpp", "int main(){}\n");
        writeFile("src/core/engine.cpp", "engine\n");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_COMPARE(h.call("hasProject").toBool(), true);
        QTRY_VERIFY(h.call("rowCount").toInt() >= 2);

        // Root level: the folder plus the file, no grandchildren yet.
        QStringList names = h.call("rowNames").toStringList();
        QVERIFY2(names.contains(QStringLiteral("src")), "根目录应含 src");
        QVERIFY2(names.contains(QStringLiteral("README")), "根目录应含 README");
        QVERIFY2(!names.contains(QStringLiteral("main.cpp")), "未展开时不应出现子项");

        // Directories sort before files.
        QCOMPARE(h.call1("rowIsDirAt", 0).toBool(), true);

        const int before = h.call("rowCount").toInt();
        QVERIFY(h.invoke1("toggleFolder", QStringLiteral("src")));
        QTRY_VERIFY(h.call("rowCount").toInt() > before);

        names = h.call("rowNames").toStringList();
        QVERIFY2(names.contains(QStringLiteral("main.cpp")), "展开后应出现子项");
        QVERIFY2(names.contains(QStringLiteral("core")), "展开后应出现子目录");
        QVERIFY2(!names.contains(QStringLiteral("engine.cpp")),
                 "孙级目录不应自动展开");

        // Collapsing hides them again.
        QVERIFY(h.invoke1("toggleFolder", QStringLiteral("src")));
        QTRY_COMPARE(h.call("rowCount").toInt(), before);
    }

    // Indented rows carry their depth so the delegate can offset them.
    void nestedRowsCarryDepth() {
        writeFile("a/b/c.txt", "c\n");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_VERIFY(h.call("rowCount").toInt() >= 1);

        QCOMPARE(h.call1("rowDepthAt", 0).toInt(), 0);
        QVERIFY(h.invoke1("toggleFolder", QStringLiteral("a")));
        QTRY_VERIFY(h.call("rowCount").toInt() >= 2);
        QCOMPARE(h.call1("rowDepthAt", 1).toInt(), 1);
    }

    // Filtering narrows the list; clearing it restores the tree.
    void filterNarrowsAndClears() {
        writeFile("alpha.txt", "a\n");
        writeFile("beta.txt", "b\n");
        writeFile("gamma.md", "g\n");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_COMPARE(h.call("rowCount").toInt(), 3);

        QVERIFY(h.invoke1("setFilter", QStringLiteral("beta")));
        QTRY_COMPARE(h.call("rowCount").toInt(), 1);
        QCOMPARE(h.call("rowNames").toStringList().first(), QStringLiteral("beta.txt"));

        QVERIFY(h.invoke1("setFilter", QString()));
        QTRY_COMPARE(h.call("rowCount").toInt(), 3);
    }

    // The preview reads the file and reports its own state.
    void selectingAFileShowsItsContent() {
        writeFile("notes.txt", "hello from the file\n");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_VERIFY(h.call("rowCount").toInt() >= 1);

        QCOMPARE(h.call("previewVisible").toBool(), false);
        QVERIFY(h.invoke1("selectFile", QStringLiteral("notes.txt")));
        QTRY_COMPARE(h.call("previewVisible").toBool(), true);

        QCOMPARE(h.call("selectedPath").toString(), QStringLiteral("notes.txt"));
        QCOMPARE(h.call("previewName").toString(), QStringLiteral("notes.txt"));
        QVERIFY2(h.call("previewText").toString().contains(QStringLiteral("hello from the file")),
                 qPrintable(h.call("previewText").toString()));
        QCOMPARE(h.call("previewError").toString(), QString());
        QCOMPARE(h.call("previewBinary").toBool(), false);

        // The preview pane is actually on screen with the text in it.
        QTest::qWait(120);
        QList<QQuickItem*> areas;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("projectFilePreviewText"), &areas);
        QCOMPARE(areas.size(), 1);

        // Collapsing the preview clears the selection.
        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "closePreview"));
        QTRY_COMPARE(h.call("previewVisible").toBool(), false);
        QCOMPARE(h.call("selectedPath").toString(), QString());
    }

    // A binary file must say so rather than render mojibake or look empty.
    void binaryPreviewIsReported() {
        writeFile("blob.bin", QByteArray("MZ\x00\x00rest", 9));

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_VERIFY(h.call("rowCount").toInt() >= 1);

        QVERIFY(h.invoke1("selectFile", QStringLiteral("blob.bin")));
        QTRY_COMPARE(h.call("previewBinary").toBool(), true);
        QVERIFY2(h.call("previewText").toString().isEmpty(), "二进制不应有正文");

        QTest::qWait(120);
        QList<QQuickItem*> notices;
        collectVisualItems(h.window->contentItem(),
                           QStringLiteral("projectFileNotices"), &notices);
        QCOMPARE(notices.size(), 1);
        QVERIFY2(notices.first()->isVisible(), "二进制文件应有提示");
        QVERIFY2(notices.first()->property("text").toString().contains(QStringLiteral("二进制")),
                 qPrintable(notices.first()->property("text").toString()));
    }

    // A deleted file reports "missing" instead of showing an empty preview.
    void missingFileIsReported() {
        writeFile("gone.txt", "bye\n");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_VERIFY(h.call("rowCount").toInt() >= 1);

        QFile::remove(m_dir->path() + "/gone.txt");
        QVERIFY(h.invoke1("selectFile", QStringLiteral("gone.txt")));
        QTRY_COMPARE(h.call("previewError").toString(), QStringLiteral("missing"));
        QVERIFY2(h.call("previewText").toString().isEmpty(), "读不到内容时不应有正文");
    }

    // Secrets never reach the list, so they cannot be previewed by accident.
    void sensitiveFilesNeverAppear() {
        writeFile(".env", "SECRET=1\n");
        writeFile("id_rsa", "key\n");
        writeFile("ok.txt", "fine\n");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_VERIFY(h.call("rowCount").toInt() >= 1);

        const QStringList names = h.call("rowNames").toStringList();
        QVERIFY2(!names.contains(QStringLiteral(".env")), ".env 不应出现在列表");
        QVERIFY2(!names.contains(QStringLiteral("id_rsa")), "id_rsa 不应出现在列表");
        QVERIFY2(names.contains(QStringLiteral("ok.txt")), "普通文件应出现");
    }

    // colapseAll folds every folder currently in the tree.
    void collapseAllFoldsEverything() {
        writeFile("a/b/deep.txt", "d\n");
        writeFile("c.txt", "c\n");

        Harness h;
        QVERIFY2(h.window, qPrintable(h.error));
        QVERIFY(h.invoke1("setProjectRoot", m_dir->path()));
        QTRY_VERIFY(h.call("rowCount").toInt() >= 1);

        QVERIFY(h.invoke1("toggleFolder", QStringLiteral("a")));
        QVERIFY(h.invoke1("toggleFolder", QStringLiteral("a/b")));
        QTRY_VERIFY(h.call("rowNames").toStringList().contains(QStringLiteral("deep.txt")));

        QVERIFY(QMetaObject::invokeMethod(h.root.get(), "collapseAll"));
        QTRY_VERIFY(!h.call("rowNames").toStringList().contains(QStringLiteral("deep.txt")));
        // Folders themselves stay visible.
        QVERIFY(h.call("rowNames").toStringList().contains(QStringLiteral("a")));
    }

private:
    void writeFile(const QString& relative, const QByteArray& content) {
        const QString full = m_dir->path() + "/" + relative;
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile file(full);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(full));
        file.write(content);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_MAIN(TestProjectFilesPanel)
#include "test_project_files_panel.moc"
