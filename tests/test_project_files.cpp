// G2: project file index and '@' reference parsing.
//
// These helpers feed both the QML completion popup and the submit-time
// attachment resolver, so the cases below lock the contract the popup
// relies on: which files are indexed, how matches are ordered, when a
// mention is still "in progress", and which references are rejected.
#include "core/utils/ProjectFileIndex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QtTest>

#include <memory>

using namespace NexusAI;

class TestProjectFiles : public QObject {
    Q_OBJECT

private slots:
    void init() {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
    }

    void cleanup() {
        m_root.reset();
    }

    // ── scanner ─────────────────────────────────────────
    void ignoredDirectoryRules() {
        QVERIFY(ProjectFileIndex::isIgnoredDir(".git"));
        QVERIFY(ProjectFileIndex::isIgnoredDir("node_modules"));
        QVERIFY(ProjectFileIndex::isIgnoredDir("build"));
        QVERIFY(ProjectFileIndex::isIgnoredDir("cmake-build-debug"));
        QVERIFY(ProjectFileIndex::isIgnoredDir("build-release"));
        QVERIFY(!ProjectFileIndex::isIgnoredDir("src"));
        QVERIFY(!ProjectFileIndex::isIgnoredDir("builder"));
    }

    void indexableAndSensitiveRules() {
        QVERIFY(ProjectFileIndex::isIndexableFile("main.cpp"));
        QVERIFY(ProjectFileIndex::isIndexableFile("Main.QML"));
        QVERIFY(ProjectFileIndex::isIndexableFile("CMakeLists.txt"));
        QVERIFY(ProjectFileIndex::isIndexableFile("Makefile"));
        QVERIFY(ProjectFileIndex::isIndexableFile("Dockerfile.dev"));
        QVERIFY(ProjectFileIndex::isIndexableFile(".gitignore"));
        QVERIFY(ProjectFileIndex::isIndexableFile(".env.example"));
        QVERIFY(!ProjectFileIndex::isIndexableFile("logo.png"));
        QVERIFY(!ProjectFileIndex::isIndexableFile("archive.zip"));
        QVERIFY(!ProjectFileIndex::isIndexableFile(".env"));
        QVERIFY(!ProjectFileIndex::isSensitiveFile(".env.example"));
        QVERIFY(ProjectFileIndex::isSensitiveFile(".env.local"));
        QVERIFY(ProjectFileIndex::isSensitiveFile("id_rsa"));
        QVERIFY(ProjectFileIndex::isSensitiveFile("server.pem"));
        QVERIFY(!ProjectFileIndex::isSensitiveFile("main.cpp"));
    }

    void scanSkipsIgnoredNonIndexableAndSensitive() {
        writeFile("src/main.cpp", "int main() {}\n");
        writeFile("README.md", "# readme\n");
        writeFile(".env.example", "KEY=\n");
        writeFile("sub dir/space name.txt", "x");
        writeFile(".git/config", "");
        writeFile("node_modules/pkg/index.js", "");
        writeFile("build/generated.cpp", "");
        writeFile("cmake-build-debug/x.cpp", "");
        writeFile("logo.png", "");
        writeFile(".env", "SECRET=1\n");
        writeFile("certs/server.pem", "");

        const QStringList files = ProjectFileIndex::scan(m_root->path());
        QVERIFY(files.contains("src/main.cpp"));
        QVERIFY(files.contains("README.md"));
        QVERIFY(files.contains(".env.example"));
        QVERIFY(files.contains("sub dir/space name.txt"));
        QVERIFY(!files.contains(".git/config"));
        QVERIFY(!files.contains("node_modules/pkg/index.js"));
        QVERIFY(!files.contains("build/generated.cpp"));
        QVERIFY(!files.contains("cmake-build-debug/x.cpp"));
        QVERIFY(!files.contains("logo.png"));
        QVERIFY(!files.contains(".env"));
        QVERIFY(!files.contains("certs/server.pem"));
        // Scanner contract: '/' separators regardless of platform.
        for (const QString& path : files)
            QVERIFY(!path.contains(QLatin1Char('\\')));
    }

    void scanHonorsMaxFilesAndMissingRoot() {
        writeFile("a.cpp", "");
        writeFile("b.cpp", "");
        writeFile("c.cpp", "");
        QCOMPARE(ProjectFileIndex::scan(m_root->path(), 2).size(), 2);
        QVERIFY(ProjectFileIndex::scan(m_root->path() + "/does-not-exist").isEmpty());
        QVERIFY(ProjectFileIndex::scan(QString()).isEmpty());
    }

    // ── completion ──────────────────────────────────────
    void completeOrdersByRelevance() {
        const QStringList files{
            "src/main.cpp", "src/domain.cpp", "docs/main.md", "main.cpp"};

        const QStringList matches = ProjectFileIndex::complete(files, "main");
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first(), QStringLiteral("main.cpp"));
        // Basename prefix beats basename-contains ("domain.cpp").
        QCOMPARE(matches.last(), QStringLiteral("src/domain.cpp"));
        QVERIFY(matches.contains("docs/main.md"));

        // Empty query lists everything, shortest path first.
        const QStringList all = ProjectFileIndex::complete(files, "");
        QCOMPARE(all.size(), 4);
        QCOMPARE(all.first(), QStringLiteral("main.cpp"));

        QCOMPARE(ProjectFileIndex::complete(files, "main", 2).size(), 2);
        QVERIFY(ProjectFileIndex::complete(files, "", 0).isEmpty());
        QVERIFY(ProjectFileIndex::complete(files, "no-such-file").isEmpty());
    }

    void completeIsCaseInsensitive() {
        const QStringList files{"src/MainWindow.qml"};
        QCOMPARE(ProjectFileIndex::complete(files, "mainwindow").size(), 1);
        QCOMPARE(ProjectFileIndex::complete(files, "SRC/MAIN").size(), 1);
    }

    // ── mentionAt ───────────────────────────────────────
    void mentionAtTracksInProgressToken() {
        const QString text = QStringLiteral("look at @src/ma");
        const auto m = ProjectFileIndex::mentionAt(text, text.size());
        QVERIFY(m.active);
        QCOMPARE(m.start, 8);
        QCOMPARE(m.query, QStringLiteral("src/ma"));
        QVERIFY(!m.quoted);

        const auto empty = ProjectFileIndex::mentionAt(QStringLiteral("@"), 1);
        QVERIFY(empty.active);
        QCOMPARE(empty.start, 0);
        QVERIFY(empty.query.isEmpty());
    }

    void mentionAtIgnoresEmailAddresses() {
        const QString text = QStringLiteral("mail me at a@b.com");
        QVERIFY(!ProjectFileIndex::mentionAt(text, text.size()).active);
    }

    void mentionAtEndsAtWhitespaceNewlineAndClosedQuote() {
        const QString spaced = QStringLiteral("use @src/main.cpp please");
        QVERIFY(!ProjectFileIndex::mentionAt(spaced, spaced.size()).active);

        const QString afterNewline = QStringLiteral("@src/main.cpp\nnext");
        QVERIFY(!ProjectFileIndex::mentionAt(afterNewline, afterNewline.size()).active);

        const QString quoted = QStringLiteral("@\"docs/my file");
        const auto m = ProjectFileIndex::mentionAt(quoted, quoted.size());
        QVERIFY(m.active);
        QVERIFY(m.quoted);
        QCOMPARE(m.query, QStringLiteral("docs/my file"));

        const QString closed = QStringLiteral("@\"docs/my file\" ");
        QVERIFY(!ProjectFileIndex::mentionAt(closed, closed.size()).active);
    }

    void mentionAtRequiresTokenStart() {
        const QString text = QStringLiteral("issue#12@foo");
        QVERIFY(!ProjectFileIndex::mentionAt(text, text.size()).active);
    }

    void mentionAtRequiresTokenBoundary() {
        // Cursor inside an existing token: accepting a match would replace
        // only [start, cursor) and leave the tail behind.
        const QString text = QStringLiteral("@src/main.cpp");
        QVERIFY(!ProjectFileIndex::mentionAt(text, 4).active);
        QVERIFY(ProjectFileIndex::mentionAt(text, text.size()).active);

        const QString quoted = QStringLiteral("@\"docs/my file\"");
        QVERIFY(!ProjectFileIndex::mentionAt(quoted, 8).active);
        QVERIFY(!ProjectFileIndex::mentionAt(quoted, quoted.size()).active);
    }

    // ── mentionPaths ────────────────────────────────────
    void mentionPathsParsesQuotedAndStripsPunctuation() {
        const QString text = QStringLiteral(
            "see @src/main.cpp, then @\"docs/my file.md\" and @src/main.cpp.");
        const QStringList paths = ProjectFileIndex::mentionPaths(text);
        QCOMPARE(paths.size(), 2);
        QCOMPARE(paths.at(0), QStringLiteral("src/main.cpp"));
        QCOMPARE(paths.at(1), QStringLiteral("docs/my file.md"));
    }

    void mentionPathsNormalizesSeparatorsAndSkipsEmails() {
        const QString text = QStringLiteral("open @src\\core\\main.cpp or a@b.com");
        const QStringList paths = ProjectFileIndex::mentionPaths(text);
        QCOMPARE(paths, QStringList{QStringLiteral("src/core/main.cpp")});
    }

    // ── resolveReferences ───────────────────────────────
    void resolveReadsProjectFiles() {
        writeFile("src/main.cpp", "int main() { return 0; }\n");

        const auto refs = ProjectFileIndex::resolveReferences(
            m_root->path(), QStringLiteral("read @src/main.cpp"));
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.first().path, QStringLiteral("src/main.cpp"));
        QVERIFY(refs.first().content.contains("int main"));
        QVERIFY(!refs.first().truncated);
    }

    void resolveRejectsEscapesSensitiveBinariesAndDuplicates() {
        writeFile("src/main.cpp", "ok");
        writeFile(".env", "SECRET=1\n");
        writeFile("blob.dat", QByteArray("abc\0def", 7));

        // Outside the project root.
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        writeFileOutside(outside.path(), "secret.txt", "outside");

        const QString abs = QDir::cleanPath(m_root->path() + "/src/main.cpp");
        const QString text = QStringLiteral(
            "read @src/main.cpp @src/main.cpp @.env @blob.dat @../secret.txt @\"%1\"")
            .arg(abs);
        const auto refs = ProjectFileIndex::resolveReferences(m_root->path(), text);
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.first().path, QStringLiteral("src/main.cpp"));
    }

    void resolveRejectsSymlinkEscape() {
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        writeFileOutside(outside.path(), "secret.txt", "outside");

        const QString target = outside.path() + "/secret.txt";
        const QString link = m_root->path() + "/link.txt";
        if (!QFile::link(target, link) || !QFileInfo(link).isSymLink())
            QSKIP("symbolic links are not available on this filesystem");

        const auto refs = ProjectFileIndex::resolveReferences(
            m_root->path(), QStringLiteral("read @link.txt"));
        QVERIFY(refs.isEmpty());
    }

    void resolveTruncatesPerFileBudget() {
        QByteArray content;
        for (int i = 0; i < 100; ++i)
            content.append(char('a' + i % 26));
        writeFile("long.txt", content);

        ProjectFileIndex::ReferenceLimits limits;
        limits.maxBytesPerFile = 20;
        limits.maxTotalBytes = 1000;
        const auto refs = ProjectFileIndex::resolveReferences(
            m_root->path(), QStringLiteral("read @long.txt"), limits);
        QCOMPARE(refs.size(), 1);
        QVERIFY(refs.first().truncated);
        QVERIFY(refs.first().content.contains("truncated"));
        QVERIFY(refs.first().content.size() < content.size() + 100);
    }

    void resolveHonorsFileCountAndTotalBudget() {
        writeFile("a.txt", QByteArray(8, 'a'));
        writeFile("b.txt", QByteArray(8, 'b'));
        writeFile("c.txt", QByteArray(8, 'c'));

        ProjectFileIndex::ReferenceLimits countLimit;
        countLimit.maxFiles = 2;
        QCOMPARE(ProjectFileIndex::resolveReferences(
                     m_root->path(), QStringLiteral("@a.txt @b.txt @c.txt"),
                     countLimit).size(), 2);

        ProjectFileIndex::ReferenceLimits totalLimit;
        totalLimit.maxBytesPerFile = 100;
        totalLimit.maxTotalBytes = 10;
        const auto refs = ProjectFileIndex::resolveReferences(
            m_root->path(), QStringLiteral("@a.txt @b.txt"), totalLimit);
        QCOMPARE(refs.size(), 2);
        QVERIFY(!refs.at(0).truncated);
        // The second file only gets the remaining two bytes.
        QVERIFY(refs.at(1).truncated);
        QVERIFY(refs.at(1).content.contains("first 2 bytes"));
    }

    void resolveMissingRootReturnsNothing() {
        QVERIFY(ProjectFileIndex::resolveReferences(
            QString(), QStringLiteral("@a.txt")).isEmpty());
        QVERIFY(ProjectFileIndex::resolveReferences(
            m_root->path(), QStringLiteral("no mentions here")).isEmpty());
    }

    // ── checkReferences (composer feedback) ─────────────
    void checkReferencesReportsPerTokenStatus() {
        writeFile("src/main.cpp", "ok");
        writeFile(".env", "SECRET=1\n");

        const auto checks = ProjectFileIndex::checkReferences(
            m_root->path(),
            QStringLiteral("@src/main.cpp @missing.txt @.env @../outside.txt"));
        QCOMPARE(checks.size(), 4);
        QCOMPARE(checks.at(0).token, QStringLiteral("src/main.cpp"));
        QVERIFY(checks.at(0).error.isEmpty());
        QCOMPARE(checks.at(0).path, QStringLiteral("src/main.cpp"));
        QCOMPARE(checks.at(1).error, QStringLiteral("missing"));
        QCOMPARE(checks.at(2).error, QStringLiteral("sensitive"));
        QCOMPARE(checks.at(3).error, QStringLiteral("outside"));
    }

    void checkReferencesWithoutProjectAndDedup() {
        const auto none = ProjectFileIndex::checkReferences(
            QString(), QStringLiteral("@a.txt @a.txt"));
        QCOMPARE(none.size(), 1);
        QCOMPARE(none.first().error, QStringLiteral("no-project"));

        QVERIFY(ProjectFileIndex::checkReferences(
            m_root->path(), QStringLiteral("no mentions")).isEmpty());
    }

private slots:
    // ── file browser: listDir ───────────────────────────

    // A listing shows what a file manager would show, which is deliberately
    // looser than the indexer: README/LICENSE/.gitignore are real files even
    // though scan() (which feeds model context) would refuse some of them.
    void listDirShowsRealFilesNotOnlyIndexableOnes() {
        writeFile("src/main.cpp", "int main(){}\n");
        writeFile("README", "no extension at all\n");
        writeFile("LICENSE", "MIT\n");
        writeFile(".gitignore", "build/\n");
        writeFile("notes.bin", QByteArray("\x01\x02\x03", 3));

        QString error;
        const auto entries = ProjectFileIndex::listDir(m_root->path(), QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QStringList names;
        for (const auto& entry : entries)
            names.append(entry.name);
        QVERIFY2(names.contains(QStringLiteral("README")), "无后缀文件应可见");
        QVERIFY2(names.contains(QStringLiteral("LICENSE")), "无后缀文件应可见");
        QVERIFY2(names.contains(QStringLiteral(".gitignore")), "点文件应可见");
        QVERIFY2(names.contains(QStringLiteral("notes.bin")), "非白名单后缀应可见");
        QVERIFY2(names.contains(QStringLiteral("src")), "目录应可见");
    }

    // Ignored directories and secrets are the two things the browser hides.
    void listDirHidesIgnoredDirsAndSecrets() {
        writeFile("src/main.cpp", "x\n");
        writeFile(".git/config", "");
        writeFile("node_modules/pkg/index.js", "");
        writeFile("cmake-build-debug/x.cpp", "");
        writeFile(".env", "SECRET=1\n");
        writeFile("id_rsa", "key\n");
        writeFile("server.pem", "cert\n");

        QString error;
        const auto entries = ProjectFileIndex::listDir(m_root->path(), QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QStringList names;
        for (const auto& entry : entries)
            names.append(entry.name);
        QVERIFY2(!names.contains(QStringLiteral(".git")), "ignore 目录不应出现");
        QVERIFY2(!names.contains(QStringLiteral("node_modules")), "ignore 目录不应出现");
        QVERIFY2(!names.contains(QStringLiteral("cmake-build-debug")), "ignore 目录不应出现");
        QVERIFY2(!names.contains(QStringLiteral(".env")), "敏感文件不应出现");
        QVERIFY2(!names.contains(QStringLiteral("id_rsa")), "敏感文件不应出现");
        QVERIFY2(!names.contains(QStringLiteral("server.pem")), "敏感文件不应出现");
        QVERIFY2(names.contains(QStringLiteral("src")), "普通目录应出现");
    }

    // Directories sort before files, each group alphabetically; nesting is
    // one level only, so a child directory comes back as a single row.
    void listDirSortsDirsFirstAndIsOneLevelDeep() {
        writeFile("zebra.txt", "z\n");
        writeFile("alpha.txt", "a\n");
        writeFile("beta/inner/deep.cpp", "d\n");

        QString error;
        const auto entries = ProjectFileIndex::listDir(m_root->path(), QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(entries.size() >= 3);

        // First row must be the directory, whatever its name sorts like.
        QCOMPARE(entries.first().isDir, true);
        QCOMPARE(entries.first().name, QStringLiteral("beta"));

        // The nested file must NOT be in this listing - it is one level deep.
        for (const auto& entry : entries)
            QVERIFY2(entry.name != QStringLiteral("deep.cpp"), "不应递归");

        // Files that follow are alphabetical.
        QStringList files;
        for (const auto& entry : entries)
            if (!entry.isDir) files.append(entry.name);
        QCOMPARE(files, QStringList({QStringLiteral("alpha.txt"),
                                     QStringLiteral("zebra.txt")}));
    }

    // relPath navigates; entries carry root-relative paths with '/' even on
    // Windows (the QML side splits on '/').
    void listDirNavigatesAndReturnsRelativePaths() {
        writeFile("src/core/engine.cpp", "e\n");

        QString error;
        const auto entries = ProjectFileIndex::listDir(m_root->path(),
                                                       QStringLiteral("src"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().name, QStringLiteral("core"));
        QCOMPARE(entries.first().relPath, QStringLiteral("src/core"));
        QVERIFY(entries.first().isDir);
    }

    // Escaping the project must be refused, never resolved.
    void listDirRejectsEscapes() {
        for (const QString& escape : {QStringLiteral(".."),
                                      QStringLiteral("../"),
                                      QStringLiteral("src/../.."),
                                      QStringLiteral("src/../../etc")}) {
            QString error;
            const auto entries = ProjectFileIndex::listDir(m_root->path(), escape, &error);
            QVERIFY2(entries.isEmpty(),
                     qPrintable(QStringLiteral("%1 应被拒绝").arg(escape)));
            QCOMPARE(error, QStringLiteral("outside"));
        }
    }

    // A missing directory reports itself instead of looking empty: the UI has
    // to tell "no files here" from "I could not look".
    void listDirDistinguishesMissingFromEmpty() {
        QDir().mkpath(m_root->path() + "/really-empty");

        QString error;
        (void)ProjectFileIndex::listDir(m_root->path(), QStringLiteral("really-empty"), &error);
        QVERIFY2(error.isEmpty(), "空目录不是错误");
        QCOMPARE(error, QString());

        QString missingError;
        const auto missing = ProjectFileIndex::listDir(
            m_root->path(), QStringLiteral("does-not-exist"), &missingError);
        QVERIFY(missing.isEmpty());
        QCOMPARE(missingError, QStringLiteral("missing"));
    }

    // ── file browser: readFilePreview ───────────────────

    void previewReadsTextAndReportsSize() {
        writeFile("src/hello.cpp", "int main() { return 0; }\n");
        const auto preview = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("src/hello.cpp"));
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QVERIFY2(preview.text.contains(QStringLiteral("int main")), "内容应读出");
        QCOMPARE(preview.bytes, qint64(25));
        QCOMPARE(preview.truncated, false);
        QCOMPARE(preview.binary, false);
    }

    // Truncation is reported, not silent: the UI promises "first N bytes".
    void previewTruncatesAndSaysSo() {
        writeFile("big.txt", QByteArray(4096, 'a'));
        const auto preview = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("big.txt"), 1024);
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QCOMPARE(preview.truncated, true);
        QCOMPARE(preview.bytes, qint64(4096));      // full size, not 1024
        QCOMPARE(preview.text.size(), 1024);

        // Exactly at the cap is not truncated (the extra byte is the probe).
        writeFile("exact.txt", QByteArray(1024, 'b'));
        const auto exact = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("exact.txt"), 1024);
        QCOMPARE(exact.truncated, false);
    }

    // A NUL byte means binary; rendering it as text would show mojibake.
    void previewDetectsBinary() {
        writeFile("blob.bin", QByteArray("MZ\x00\x00rest", 9));
        const auto preview = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("blob.bin"));
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QCOMPARE(preview.binary, true);
        QVERIFY(preview.text.isEmpty());
    }

    void previewRejectsEscapesAndMissing() {
        writeFile("a.txt", "a\n");
        const auto outside = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("../outside.txt"));
        QCOMPARE(outside.error, QStringLiteral("outside"));

        const auto missing = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("nope.txt"));
        QCOMPARE(missing.error, QStringLiteral("missing"));

        const auto noProject = ProjectFileIndex::readFilePreview(
            QString(), QStringLiteral("a.txt"));
        QCOMPARE(noProject.error, QStringLiteral("no-project"));
    }

    // A link that lives inside the project but targets a file outside it must
    // be rejected. This is the difference between the lexical path that
    // resolveUnderRoot sees and the canonical target the OS actually opens.
    void previewRejectsSymlinkEscape() {
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        writeFileOutside(outside.path(), "secret.txt", "outside");

        const QString link = m_root->path() + QStringLiteral("/linked");
        if (!createEscapeLink(link, outside.path()))
            QSKIP("symbolic links/junctions are not available on this filesystem");

        const auto preview = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("linked/secret.txt"));
        QCOMPARE(preview.error, QStringLiteral("outside"));
        QVERIFY(preview.text.isEmpty());

        // A normal file inside the project is still readable in the same
        // canonical-checking code path.
        writeFile("inside.txt", "inside\n");
        const auto normal = ProjectFileIndex::readFilePreview(
            m_root->path(), QStringLiteral("inside.txt"));
        QVERIFY2(normal.error.isEmpty(), qPrintable(normal.error));
        QCOMPARE(normal.text, QStringLiteral("inside\n"));
    }

    private:
    // Returns true when `link` resolves to `target`. Tries a Windows junction
    // first (no administrator rights needed) and falls back to QFile::link.
    bool createEscapeLink(const QString& link, const QString& target) {
#if defined(Q_OS_WIN)
        QProcess process;
        process.setProgram(QStringLiteral("cmd"));
        process.setArguments({
            QStringLiteral("/c"), QStringLiteral("mklink"),
            QStringLiteral("/J"),
            QDir::toNativeSeparators(link),
            QDir::toNativeSeparators(target)});
        process.start();
        if (!process.waitForStarted(3000)) return false;
        if (!process.waitForFinished(5000)) {
            process.kill();
            return false;
        }
        return process.exitCode() == 0 && QFileInfo(link).isDir();
#else
        if (!QFile::link(target, link)) return false;
        return QFileInfo(link).isDir();
#endif
    }

    void writeFile(const QString& relative, const QByteArray& content) {
        const QString full = m_root->path() + "/" + relative;
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile file(full);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(full));
        file.write(content);
    }

    void writeFileOutside(const QString& root, const QString& name,
                          const QByteArray& content) {
        QFile file(root + "/" + name);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.fileName()));
        file.write(content);
    }

    std::unique_ptr<QTemporaryDir> m_root;
};

QTEST_GUILESS_MAIN(TestProjectFiles)
#include "test_project_files.moc"
