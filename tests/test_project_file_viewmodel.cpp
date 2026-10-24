// Sidebar file browser facade: ProjectFileViewModel.
//
// The C++ dirlisting rules live in test_project_files (ProjectFileIndex).
// What is tested here is the facade's own contract, i.e. the parts QML
// depends on and that the indexer knows nothing about:
//   - a local chat (empty root) touches nothing and yields nothing
//   - listings are cached, and the cache is dropped on root change / refresh
//   - failures are NOT cached, so a transient error cannot stick
//   - absolutePath keeps every path inside the project root
#include "viewmodels/ProjectFileViewModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace NexusAI;

class TestProjectFileViewModel : public QObject {
    Q_OBJECT

private slots:
    void init() {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
    }

    void cleanup() {
        ProjectFileViewModel::instance()->setRoot(QString());
        m_root.reset();
    }

    // A local chat has no project, so the browser must stay inert rather than
    // walking the filesystem or reporting an error.
    void emptyRootYieldsNothing() {
        ProjectFileViewModel vm;
        QCOMPARE(vm.root(), QString());
        QVERIFY(vm.children(QString()).isEmpty());
        QVERIFY(vm.absolutePath(QStringLiteral("a.txt")).isEmpty());

        const QVariantMap preview = vm.preview(QStringLiteral("a.txt"));
        QCOMPARE(preview.value("error").toString(), QStringLiteral("no-project"));
    }

    void childrenListsOneLevel() {
        writeFile("src/main.cpp", "x\n");
        writeFile("README", "r\n");

        ProjectFileViewModel vm;
        vm.setRoot(m_root->path());
        const QVariantList rows = vm.children(QString());
        QVERIFY2(rows.size() >= 2, "根目录应列出条目");

        QStringList names;
        for (const QVariant& row : rows)
            names.append(row.toMap().value("name").toString());
        QVERIFY(names.contains(QStringLiteral("src")));
        QVERIFY(names.contains(QStringLiteral("README")));

        // Navigating one level down returns that directory's own entries.
        const QVariantList nested = vm.children(QStringLiteral("src"));
        QCOMPARE(nested.size(), 1);
        QCOMPARE(nested.first().toMap().value("name").toString(),
                 QStringLiteral("main.cpp"));
        QCOMPARE(nested.first().toMap().value("isDir").toBool(), false);
    }

    // Listings are cached until something invalidates them; a file created
    // afterwards only shows up after clearCache() (the browser has no watcher).
    void listingsAreCachedUntilCleared() {
        writeFile("first.txt", "1\n");

        ProjectFileViewModel vm;
        vm.setRoot(m_root->path());
        QCOMPARE(vm.children(QString()).size(), 1);

        writeFile("second.txt", "2\n");
        // Still cached: the new file is not visible yet.
        QCOMPARE(vm.children(QString()).size(), 1);

        vm.clearCache();
        QCOMPARE(vm.children(QString()).size(), 2);
    }

    // Switching projects must not serve the previous project's listing.
    void rootChangeDropsCache() {
        QTemporaryDir other;
        QVERIFY(other.isValid());
        writeFile("only-in-first.txt", "1\n");
        QFile second(other.path() + "/only-in-second.txt");
        QVERIFY(second.open(QIODevice::WriteOnly));
        second.write("2\n");
        second.close();

        ProjectFileViewModel vm;
        QSignalSpy rootSpy(&vm, &ProjectFileViewModel::rootChanged);

        vm.setRoot(m_root->path());
        QCOMPARE(rootSpy.count(), 1);
        QCOMPARE(vm.children(QString()).size(), 1);
        QCOMPARE(vm.children(QString()).first().toMap().value("name").toString(),
                 QStringLiteral("only-in-first.txt"));

        vm.setRoot(other.path());
        QCOMPARE(rootSpy.count(), 2);
        const QVariantList rows = vm.children(QString());
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("name").toString(),
                 QStringLiteral("only-in-second.txt"));

        // Same root again is a no-op, not another signal.
        vm.setRoot(other.path());
        QCOMPARE(rootSpy.count(), 2);
    }

    // A failing listing must not be memoised: the directory may simply not
    // exist yet, and the user's next refresh should see it.
    void failuresAreNotCached() {
        ProjectFileViewModel vm;
        vm.setRoot(m_root->path());

        // Missing directory: empty, and nothing cached.
        QVERIFY(vm.children(QStringLiteral("later")).isEmpty());

        QDir().mkpath(m_root->path() + "/later");
        writeFile("later/inside.txt", "x\n");
        QCOMPARE(vm.children(QStringLiteral("later")).size(), 1);
    }

    void previewAndOpenPathContainment() {
        writeFile("src/hello.cpp", "int main() { return 0; }\n");

        ProjectFileViewModel vm;
        vm.setRoot(m_root->path());

        const QVariantMap preview = vm.preview(QStringLiteral("src/hello.cpp"));
        QVERIFY2(preview.value("error").toString().isEmpty(),
                 qPrintable(preview.value("error").toString()));
        QVERIFY(preview.value("text").toString().contains(QStringLiteral("int main")));
        QCOMPARE(preview.value("binary").toBool(), false);

        // absolutePath resolves inside the root ...
        const QString absolute = vm.absolutePath(QStringLiteral("src/hello.cpp"));
        QVERIFY(!absolute.isEmpty());
        QCOMPARE(QFileInfo(absolute).canonicalFilePath(),
                 QFileInfo(m_root->path() + "/src/hello.cpp").canonicalFilePath());

        // ... and refuses to leave it.
        QVERIFY(vm.absolutePath(QStringLiteral("../outside.txt")).isEmpty());
        QVERIFY(vm.absolutePath(QStringLiteral("src/../../etc/passwd")).isEmpty());
        // Option-like names are refused outright.
        QVERIFY(vm.absolutePath(QStringLiteral("-rf")).isEmpty());
    }

    // The preview reports truncation and binary detection through the facade,
    // since that is what the panel renders its notices from.
    // openAbsolute routes by scheme: the knowledge base records both file
    // paths and http(s) URLs as a source. A URL used to go through
    // QUrl::fromLocalFile, which produced an invalid URL and the open
    // silently failed (measured: the user clicked "open source" on a URL-
    // imported document and nothing happened).
    // The ROUTE probe is the offline-decidable contract: QDesktopServices
    // itself cannot launch anything in this GUI-less process, so the test
    // asserts which branch each source takes, not the launch outcome.
    void openAbsoluteRoutesUrlsToWebAndFilesToLocal() {
        writeFile("real.md", "x");
        ProjectFileViewModel vm;
        vm.setRoot(m_root->path());

        // http(s) sources route to the browser branch - the whole fix.
        QCOMPARE(vm.openAbsoluteRoute(QStringLiteral("https://example.com/a.md")),
                 QStringLiteral("web"));
        QCOMPARE(vm.openAbsoluteRoute(QStringLiteral("http://example.com/b.md")),
                 QStringLiteral("web"));

        // A real local path routes to the file branch.
        QCOMPARE(vm.openAbsoluteRoute(m_root->path() + "/real.md"),
                 QStringLiteral("file"));

        // Missing files and garbage route nowhere.
        QVERIFY(vm.openAbsoluteRoute(m_root->path() + "/nope.md").isEmpty());
        QVERIFY(vm.openAbsoluteRoute(QString()).isEmpty());
        QVERIFY(vm.openAbsoluteRoute(QStringLiteral("not a path or url")).isEmpty());
    }

    void previewReportsTruncationAndBinary() {
        writeFile("big.txt", QByteArray(4096, 'a'));
        writeFile("blob.bin", QByteArray("MZ\x00\x00rest", 9));

        ProjectFileViewModel vm;
        vm.setRoot(m_root->path());

        const QVariantMap big = vm.preview(QStringLiteral("big.txt"), 1024);
        QCOMPARE(big.value("truncated").toBool(), true);
        QCOMPARE(big.value("bytes").toLongLong(), qint64(4096));

        const QVariantMap blob = vm.preview(QStringLiteral("blob.bin"));
        QCOMPARE(blob.value("binary").toBool(), true);
    }

private:
    void writeFile(const QString& relative, const QByteArray& content) {
        const QString full = m_root->path() + "/" + relative;
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile file(full);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(full));
        file.write(content);
    }

    std::unique_ptr<QTemporaryDir> m_root;
};

QTEST_GUILESS_MAIN(TestProjectFileViewModel)
#include "test_project_file_viewmodel.moc"
