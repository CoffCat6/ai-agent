// Phase 2: read-only git history service.
//
// Each case builds a throwaway repository with real git commands, so the
// parser is exercised against actual git output (rename detection, -z
// records, numstat, empty repositories) rather than fixtures.

#include "core/git/GitService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>

using namespace NexusAI;

namespace {

class TempRepo {
public:
    TempRepo() = default;

    bool valid() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }

    QString filePath(const QString& relative) const {
        return m_dir.filePath(relative);
    }

    bool init() {
        if (!m_dir.isValid()) return false;
        if (!run({QStringLiteral("init"), QStringLiteral("-q")})) return false;
        return run({QStringLiteral("config"), QStringLiteral("user.name"),
                    QStringLiteral("Test User")})
            && run({QStringLiteral("config"), QStringLiteral("user.email"),
                    QStringLiteral("test@example.com")})
            && run({QStringLiteral("config"), QStringLiteral("commit.gpgsign"),
                    QStringLiteral("false")})
            && run({QStringLiteral("config"), QStringLiteral("core.autocrlf"),
                    QStringLiteral("false")})
            && run({QStringLiteral("config"), QStringLiteral("core.safecrlf"),
                    QStringLiteral("false")});
    }

    bool write(const QString& relative, const QByteArray& content) {
        const QString full = filePath(relative);
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile file(full);
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(content) == content.size();
    }

    bool remove(const QString& relative) {
        return QFile::remove(filePath(relative));
    }

    bool run(const QStringList& args, QByteArray* output = nullptr) const {
        QProcess process;
        process.setProgram(QStringLiteral("git"));
        process.setArguments(args);
        process.setWorkingDirectory(m_dir.path());
        process.setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
        env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process.setProcessEnvironment(env);
        process.start();
        if (!process.waitForStarted(3000)) return false;
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
        const QByteArray all = process.readAll();
        if (output) *output = all;
        return process.exitCode() == 0;
    }

    bool addAll() const {
        return run({QStringLiteral("add"), QStringLiteral("-A")});
    }

    bool commit(const QString& message) const {
        return run({QStringLiteral("commit"), QStringLiteral("-q"),
                    QStringLiteral("-m"), message});
    }

    bool commitWithBody(const QString& subject, const QString& body) const {
        return run({QStringLiteral("commit"), QStringLiteral("-q"),
                    QStringLiteral("-m"), subject, QStringLiteral("-m"), body});
    }

private:
    QTemporaryDir m_dir;
};

} // namespace

class TestGitService : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QString error;
        if (!GitService::isGitAvailable(&error))
            QSKIP(qPrintable(QStringLiteral("git is not available: %1").arg(error)));
    }

    void nonRepositoryIsReported() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GitService git(dir.path());

        QVERIFY(!git.isRepository());
        QVERIFY(git.repositoryRoot().isEmpty());

        const QJsonObject info = git.repoInfo();
        QCOMPARE(info.value("isRepo").toBool(), false);
        QVERIFY(info.value("error").toString()
                    .contains(QStringLiteral("Not a git repository")));

        QString error;
        QVERIFY(git.log(10, 0, QString(), &error).isEmpty());
        QVERIFY2(!error.isEmpty(), "a missing repository must produce a readable error");
        QVERIFY(git.statusText(QString(), &error).isNull());
    }

    void emptyRepositoryHasNoHistory() {
        TempRepo repo;
        QVERIFY(repo.valid());
        QVERIFY(repo.init());

        GitService git(repo.path());
        const QJsonObject info = git.repoInfo();
        QCOMPARE(info.value("isRepo").toBool(), true);
        QVERIFY(!info.value("root").toString().isEmpty());
        QVERIFY(!info.value("branch").toString().isEmpty());
        QVERIFY(info.value("head").toString().isEmpty());
        QCOMPARE(info.value("dirty").toBool(), false);

        QString error;
        const auto commits = git.log(10, 0, QString(), &error);
        QVERIFY2(commits.isEmpty(), "an empty repository has no commits");
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void logPagesAndFiltersByPath() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.write(QStringLiteral("b.txt"), "b1\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        QVERIFY(repo.write(QStringLiteral("b.txt"), "b2\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\nmore\n"));
        QVERIFY(repo.addAll()
                && repo.commitWithBody(QStringLiteral("third subject"),
                                       QStringLiteral("body line one\nbody line two")));

        GitService git(repo.path());
        QString error;
        const auto commits = git.log(10, 0, QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(commits.size(), 3);
        QCOMPARE(commits.at(0).subject, QStringLiteral("third subject"));
        QCOMPARE(commits.at(2).subject, QStringLiteral("first"));
        QCOMPARE(QFileInfo(git.repositoryRoot()).canonicalFilePath(),
                 QFileInfo(repo.path()).canonicalFilePath());
        QCOMPARE(commits.at(0).author, QStringLiteral("Test User"));
        QCOMPARE(commits.at(0).authorEmail, QStringLiteral("test@example.com"));
        QVERIFY(commits.at(0).timestamp > 0);
        QVERIFY(commits.at(0).shortSha.size() >= 4);
        QVERIFY(commits.at(2).parents.isEmpty());
        QCOMPARE(commits.at(1).parents, commits.at(2).sha);

        const auto page = git.log(1, 1, QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.size(), 1);
        QCOMPARE(page.at(0).subject, QStringLiteral("second"));

        const auto aHistory = git.log(10, 0, QStringLiteral("a.txt"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(aHistory.size(), 2);
        QCOMPARE(aHistory.at(0).subject, QStringLiteral("third subject"));
        QCOMPARE(aHistory.at(1).subject, QStringLiteral("first"));

        const GitCommitInfo detail = git.commit(commits.at(0).sha, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(detail.sha, commits.at(0).sha);
        QCOMPARE(detail.subject, QStringLiteral("third subject"));
        QVERIFY2(detail.body.contains(QStringLiteral("body line one")),
                 qPrintable(detail.body));
        QVERIFY2(detail.body.contains(QStringLiteral("body line two")),
                 qPrintable(detail.body));
    }

    void commitFilesAndDiff() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("dir with space/old name.txt"),
                           "alpha\nbeta\n"));
        QVERIFY(repo.write(QStringLiteral("plain.txt"), "keep\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));

        GitService git(repo.path());
        QString error;
        const auto base = git.log(1, 0, QString(), &error);
        QCOMPARE(base.size(), 1);

        QVERIFY(repo.write(QStringLiteral("dir with space/old name.txt"),
                           "alpha\nBETA\n"));
        QVERIFY(repo.write(QStringLiteral("new.txt"), "hello\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("change")));
        const auto latest = git.log(1, 0, QString(), &error);
        QCOMPARE(latest.size(), 1);

        const auto files = git.commitFiles(latest.at(0).sha, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(files.size(), 2);
        GitFileStat modified;
        GitFileStat added;
        for (const GitFileStat& file : files) {
            if (file.path == QStringLiteral("dir with space/old name.txt"))
                modified = file;
            else if (file.path == QStringLiteral("new.txt"))
                added = file;
        }
        QCOMPARE(modified.status, QStringLiteral("M"));
        QCOMPARE(modified.additions, 1);
        QCOMPARE(modified.removals, 1);
        QVERIFY(!modified.binary);
        QCOMPARE(added.status, QStringLiteral("A"));
        QCOMPARE(added.additions, 1);
        QCOMPARE(added.removals, 0);

        bool truncated = false;
        const QString diff = git.commitDiff(latest.at(0).sha, QString(), &truncated, &error);
        QVERIFY2(!diff.isNull(), qPrintable(error));
        QVERIFY(!truncated);
        QVERIFY2(diff.contains(QStringLiteral("-beta")), qPrintable(diff));
        QVERIFY2(diff.contains(QStringLiteral("+BETA")), qPrintable(diff));
        QVERIFY2(diff.contains(QStringLiteral("+hello")), qPrintable(diff));

        const QString filtered =
            git.commitDiff(latest.at(0).sha, QStringLiteral("new.txt"), nullptr, &error);
        QVERIFY2(!filtered.isNull(), qPrintable(error));
        QVERIFY(filtered.contains(QStringLiteral("+hello")));
        QVERIFY(!filtered.contains(QStringLiteral("BETA")));

        // Absolute paths inside the repo are accepted too.
        const QString absolute = QFileInfo(repo.filePath(QStringLiteral("new.txt")))
                                     .absoluteFilePath();
        const QString filteredAbs =
            git.commitDiff(latest.at(0).sha, absolute, nullptr, &error);
        QVERIFY2(!filteredAbs.isNull(), qPrintable(error));
        QVERIFY(filteredAbs.contains(QStringLiteral("+hello")));
    }

    void fileHistoryFollowsRename() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("before.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("add file")));
        QVERIFY(repo.run({QStringLiteral("mv"), QStringLiteral("before.txt"),
                          QStringLiteral("after.txt")}));
        QVERIFY(repo.commit(QStringLiteral("rename file")));
        QVERIFY(repo.write(QStringLiteral("after.txt"), "one\ntwo\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("edit file")));

        GitService git(repo.path());
        QString error;
        const auto history =
            git.fileHistory(QStringLiteral("after.txt"), 10, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(history.size(), 3);
        QCOMPARE(history.at(0).subject, QStringLiteral("edit file"));
        QCOMPARE(history.at(1).subject, QStringLiteral("rename file"));
        QCOMPARE(history.at(2).subject, QStringLiteral("add file"));

        // Rename status and the two-NUL numstat form must both parse.
        const auto renamed = git.commitFiles(history.at(1).sha, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(renamed.size(), 1);
        QVERIFY(renamed.at(0).status.startsWith(QLatin1Char('R')));
        QCOMPARE(renamed.at(0).oldPath, QStringLiteral("before.txt"));
        QCOMPARE(renamed.at(0).path, QStringLiteral("after.txt"));
        QCOMPARE(renamed.at(0).additions, 0);
        QCOMPARE(renamed.at(0).removals, 0);

        // Without --follow the pre-rename history is not reachable.
        const auto plain = git.log(10, 0, QStringLiteral("after.txt"), &error);
        QCOMPARE(plain.size(), 2);
    }

    void mergeCommitDiffUsesFirstParent() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("base.txt"), "base\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));

        QByteArray branchOut;
        QVERIFY(repo.run({QStringLiteral("symbolic-ref"),
                          QStringLiteral("--short"), QStringLiteral("HEAD")},
                         &branchOut));
        const QString mainBranch = QString::fromUtf8(branchOut).trimmed();
        QVERIFY(!mainBranch.isEmpty());

        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-b"), QStringLiteral("side")}));
        QVERIFY(repo.write(QStringLiteral("side.txt"), "side\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("side")));

        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          mainBranch}));
        QVERIFY(repo.write(QStringLiteral("main.txt"), "main\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("main")));
        QVERIFY(repo.run({QStringLiteral("merge"), QStringLiteral("-q"),
                          QStringLiteral("--no-ff"), QStringLiteral("side"),
                          QStringLiteral("-m"), QStringLiteral("merge side")}));

        GitService git(repo.path());
        QString error;
        const auto commits = git.log(1, 0, QString(), &error);
        QCOMPARE(commits.size(), 1);
        QCOMPARE(commits.at(0).subject, QStringLiteral("merge side"));

        // A merge's own diff is normally empty; the service reports the
        // changes introduced relative to its first parent.
        const auto files = git.commitFiles(commits.at(0).sha, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.at(0).path, QStringLiteral("side.txt"));
        QCOMPARE(files.at(0).status, QStringLiteral("A"));

        const QString diff = git.commitDiff(commits.at(0).sha, QString(), nullptr, &error);
        QVERIFY2(!diff.isNull(), qPrintable(error));
        QVERIFY2(diff.contains(QStringLiteral("+side")), qPrintable(diff));
    }

    void workingTreeStatusAndDiff() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("tracked.txt"), "v1\n"));
        QVERIFY(repo.write(QStringLiteral("staged.txt"), "s1\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));

        QVERIFY(repo.write(QStringLiteral("tracked.txt"), "v2\n"));
        QVERIFY(repo.write(QStringLiteral("staged.txt"), "s2\n"));
        QVERIFY(repo.run({QStringLiteral("add"), QStringLiteral("staged.txt")}));
        QVERIFY(repo.write(QStringLiteral("untracked.txt"), "u\n"));

        GitService git(repo.path());
        QString error;
        const auto entries = git.status(QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 3);

        bool sawModified = false;
        bool sawStaged = false;
        bool sawUntracked = false;
        for (const GitStatusEntry& entry : entries) {
            if (entry.path == QStringLiteral("tracked.txt")) {
                sawModified = true;
                QCOMPARE(QString(entry.indexStatus), QStringLiteral(" "));
                QCOMPARE(QString(entry.worktreeStatus), QStringLiteral("M"));
            } else if (entry.path == QStringLiteral("staged.txt")) {
                sawStaged = true;
                QCOMPARE(QString(entry.indexStatus), QStringLiteral("M"));
                QCOMPARE(QString(entry.worktreeStatus), QStringLiteral(" "));
            } else if (entry.path == QStringLiteral("untracked.txt")) {
                sawUntracked = true;
                QCOMPARE(QString(entry.indexStatus), QStringLiteral("?"));
                QCOMPARE(QString(entry.worktreeStatus), QStringLiteral("?"));
            }
        }
        QVERIFY(sawModified && sawStaged && sawUntracked);

        bool truncated = false;
        const QString unstaged =
            git.workingDiff(QStringLiteral("tracked.txt"), false, &truncated, &error);
        QVERIFY2(!unstaged.isNull(), qPrintable(error));
        QVERIFY(!truncated);
        QVERIFY2(unstaged.contains(QStringLiteral("-v1")), qPrintable(unstaged));
        QVERIFY2(unstaged.contains(QStringLiteral("+v2")), qPrintable(unstaged));

        const QString staged =
            git.workingDiff(QStringLiteral("staged.txt"), true, nullptr, &error);
        QVERIFY2(!staged.isNull(), qPrintable(error));
        QVERIFY2(staged.contains(QStringLiteral("+s2")), qPrintable(staged));

        const QString allUnstaged = git.workingDiff(QString(), false, nullptr, &error);
        QVERIFY(allUnstaged.contains(QStringLiteral("+v2")));
        QVERIFY(!allUnstaged.contains(QStringLiteral("+s2")));

        const QString text = git.statusText(QString(), &error);
        QVERIFY2(!text.isNull(), qPrintable(error));
        QVERIFY2(text.contains(QStringLiteral("##")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("tracked.txt")), qPrintable(text));
    }

    void rejectsUnsafeInputs() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "a\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));

        GitService git(repo.path());
        QString error;
        const auto commits = git.log(1, 0, QString(), &error);
        QCOMPARE(commits.size(), 1);

        // Option-like revisions are refused before reaching git.
        QVERIFY(git.commitDiff(QStringLiteral("--upload-pack=calc"), QString(),
                               nullptr, &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(!git.commit(QStringLiteral("HEAD"), &error).isValid());
        QVERIFY(!error.isEmpty());
        QVERIFY(git.commitFiles(QStringLiteral("deadbeef"), &error).isEmpty());
        QVERIFY(!error.isEmpty());

        // Paths cannot escape the repository or turn into pathspec magic.
        QVERIFY(git.log(5, 0, QStringLiteral("../outside.txt"), &error).isEmpty());
        QVERIFY2(error.contains(QStringLiteral("escapes")), qPrintable(error));
        QVERIFY(git.commitDiff(commits.at(0).sha, QStringLiteral(":a.txt"),
                               nullptr, &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(git.workingDiff(QStringLiteral("-p"), false, nullptr, &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    void outputCapTruncatesDiff() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QByteArray before;
        QByteArray after;
        for (int i = 0; i < 400; ++i) {
            before += QByteArray("old line ") + QByteArray::number(i) + '\n';
            after += QByteArray("new line ") + QByteArray::number(i) + '\n';
        }
        QVERIFY(repo.write(QStringLiteral("large.txt"), before));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QVERIFY(repo.write(QStringLiteral("large.txt"), after));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("rewrite")));

        GitService git(repo.path());
        QString error;
        const auto commits = git.log(1, 0, QString(), &error);
        QCOMPARE(commits.size(), 1);

        GitService bounded(repo.path(), 1024, 8000);
        bool truncated = false;
        const QString diff =
            bounded.commitDiff(commits.at(0).sha, QString(), &truncated, &error);
        QVERIFY2(!diff.isNull(), qPrintable(error));
        QVERIFY2(truncated, "a diff above the output cap must be flagged");
        QVERIFY2(diff.size() <= 1024,
                 qPrintable(QStringLiteral("diff size %1").arg(diff.size())));
    }
};

QTEST_GUILESS_MAIN(TestGitService)
#include "test_git_service.moc"
