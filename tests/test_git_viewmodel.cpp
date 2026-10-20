// Phase 3: GitViewModel runs GitService on a worker thread and reports
// results through request-id signals. These cases use a real temporary
// repository and the event loop to pin the async contract.

#include "viewmodels/GitViewModel.h"

#include "core/git/GitService.h"
#include "core/tools/Sandbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace NexusAI;

namespace {

class TempRepo {
public:
    bool valid() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }

    QString filePath(const QString& relative) const {
        return m_dir.filePath(relative);
    }

    bool init() {
        if (!m_dir.isValid()) return false;
        return run({QStringLiteral("init"), QStringLiteral("-q")})
            && run({QStringLiteral("config"), QStringLiteral("user.name"),
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

    bool write(const QString& relative, const QByteArray& content) const {
        const QString full = filePath(relative);
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile file(full);
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(content) == content.size();
    }

    bool run(const QStringList& args) const {
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
        return process.exitCode() == 0;
    }

    bool addAll() const {
        return run({QStringLiteral("add"), QStringLiteral("-A")});
    }

    // stdout of a command, for tests that must not hardcode the default branch.
    QString output(const QStringList& args) const {
        QProcess process;
        process.setProgram(QStringLiteral("git"));
        process.setArguments(args);
        process.setWorkingDirectory(m_dir.path());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process.setProcessEnvironment(env);
        process.start();
        if (!process.waitForStarted(3000)) return {};
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(1000);
            return {};
        }
        return process.exitCode() == 0
                   ? QString::fromUtf8(process.readAllStandardOutput())
                   : QString();
    }

    bool commit(const QString& message) const {
        return run({QStringLiteral("commit"), QStringLiteral("-q"),
                    QStringLiteral("-m"), message});
    }

private:
    QTemporaryDir m_dir;
};

} // namespace

class TestGitViewModel : public QObject {
    Q_OBJECT

private:
    // Helpers for the P1 cases: read a file, assert a rejected operation, and
    // pull the current log through the async contract.
    static QString readFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(file.readAll());
    }

    static void argsFail(const QList<QVariant>& args) {
        QVERIFY2(!args.at(2).toBool(), qPrintable(args.at(3).toString()));
    }

    static void fetchLog(GitViewModel& vm, QVariantList* out) {
        QSignalSpy spy(&vm, &GitViewModel::repoLogReady);
        vm.requestRepoLog(20, 0);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
        const QList<QVariant> args = spy.takeFirst();
        QVERIFY2(args.at(3).toString().isEmpty(),
                 qPrintable(args.at(3).toString()));
        *out = args.at(1).toList();
    }

    static int commitCount(GitViewModel& vm) {
        QVariantList commits;
        fetchLog(vm, &commits);
        return commits.size();
    }

    static QString topSubject(GitViewModel& vm) {
        QVariantList commits;
        fetchLog(vm, &commits);
        return commits.isEmpty()
                   ? QString()
                   : commits.at(0).toMap().value("subject").toString();
    }

    static bool hasStagedEntry(GitViewModel& vm, const QString& path) {
        for (const QVariant& entry : vm.statusEntries()) {
            const QVariantMap row = entry.toMap();
            if (row.value("path").toString() == path
                    && row.value("indexStatus").toString() != QStringLiteral(" ")
                    && !row.value("indexStatus").toString().isEmpty())
                return true;
        }
        return false;
    }

private slots:
    void initTestCase() {
        QString error;
        if (!GitService::isGitAvailable(&error))
            QSKIP(qPrintable(QStringLiteral("git is not available: %1").arg(error)));
    }

    void cleanup() { Sandbox::setProjectDir(QString()); }

    void repoInfoHistoryAndCommitDiff() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\ntwo\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));

        Sandbox::setProjectDir(repo.path());
        GitViewModel vm;

        QSignalSpy infoSpy(&vm, &GitViewModel::repoInfoChanged);
        QVERIFY(!vm.repoInfoLoading());
        vm.refreshRepoInfo();
        QVERIFY(vm.repoInfoLoading());
        QTRY_COMPARE_WITH_TIMEOUT(infoSpy.count(), 1, 10000);
        QVERIFY(!vm.repoInfoLoading());
        QCOMPARE(vm.repoInfo().value("isRepo").toBool(), true);
        QCOMPARE(QFileInfo(vm.repoInfo().value("root").toString()).canonicalFilePath(),
                 QFileInfo(repo.path()).canonicalFilePath());

        QSignalSpy historySpy(&vm, &GitViewModel::fileHistoryReady);
        const QString historyId = vm.requestFileHistory(QStringLiteral("a.txt"), 10);
        QVERIFY(!historyId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 10000);
        const QList<QVariant> historyArgs = historySpy.takeFirst();
        QCOMPARE(historyArgs.at(0).toString(), historyId);
        QCOMPARE(historyArgs.at(1).toString(), QStringLiteral("a.txt"));
        QVERIFY2(historyArgs.at(3).toString().isEmpty(),
                 qPrintable(historyArgs.at(3).toString()));
        const QVariantList commits = historyArgs.at(2).toList();
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits.at(0).toMap().value("subject").toString(),
                 QStringLiteral("second"));
        const QString sha = commits.at(0).toMap().value("sha").toString();
        QVERIFY(!sha.isEmpty());

        QSignalSpy diffSpy(&vm, &GitViewModel::commitDiffReady);
        const QString diffId =
            vm.requestCommitDiff(sha, QStringLiteral("a.txt"));
        QVERIFY(!diffId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(diffSpy.count(), 1, 10000);
        const QList<QVariant> diffArgs = diffSpy.takeFirst();
        QCOMPARE(diffArgs.at(0).toString(), diffId);
        QCOMPARE(diffArgs.at(1).toString(), sha);
        QCOMPARE(diffArgs.at(2).toString(), QStringLiteral("a.txt"));
        QVERIFY2(diffArgs.at(3).toString().contains(QStringLiteral("+two")),
                 qPrintable(diffArgs.at(3).toString()));
        QCOMPARE(diffArgs.at(4).toBool(), false);
        QVERIFY2(diffArgs.at(5).toString().isEmpty(),
                 qPrintable(diffArgs.at(5).toString()));
    }

    void commitDiffFallsBackForPreRenameCommits() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("old.txt"), "alpha\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("add")));
        QVERIFY(repo.run({QStringLiteral("mv"), QStringLiteral("old.txt"),
                          QStringLiteral("new.txt")}));
        QVERIFY(repo.commit(QStringLiteral("rename")));

        Sandbox::setProjectDir(repo.path());
        GitViewModel vm;
        QSignalSpy historySpy(&vm, &GitViewModel::fileHistoryReady);
        vm.requestFileHistory(QStringLiteral("new.txt"), 10);
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 10000);
        const QVariantList commits = historySpy.takeFirst().at(2).toList();
        QCOMPARE(commits.size(), 2);
        const QString oldestSha =
            commits.at(1).toMap().value("sha").toString();
        QVERIFY(!oldestSha.isEmpty());

        // The oldest commit only knows old.txt; asking with the current path
        // returns no diff, so the VM retries with the whole commit.
        QSignalSpy diffSpy(&vm, &GitViewModel::commitDiffReady);
        vm.requestCommitDiff(oldestSha, QStringLiteral("new.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(diffSpy.count(), 1, 10000);
        const QList<QVariant> args = diffSpy.takeFirst();
        QVERIFY2(args.at(5).toString().isEmpty(),
                 qPrintable(args.at(5).toString()));
        QVERIFY2(args.at(3).toString().contains(QStringLiteral("+alpha")),
                 qPrintable(args.at(3).toString()));
    }

    void workingDiffAndRepositoryFailure() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "changed\n"));

        Sandbox::setProjectDir(repo.path());
        GitViewModel vm;
        QSignalSpy spy(&vm, &GitViewModel::workingDiffReady);
        const QString id = vm.requestWorkingDiff(QStringLiteral("a.txt"), false);
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), id);
        QCOMPARE(args.at(1).toString(), QStringLiteral("a.txt"));
        QCOMPARE(args.at(2).toBool(), false);
        QVERIFY2(args.at(3).toString().contains(QStringLiteral("-one")),
                 qPrintable(args.at(3).toString()));
        QVERIFY2(args.at(3).toString().contains(QStringLiteral("+changed")),
                 qPrintable(args.at(3).toString()));
        QVERIFY(args.at(5).toString().isEmpty());

        // Untracked files have no `git diff` output; the service synthesizes
        // an all-additions view so they can be reviewed before `git add`.
        QVERIFY(repo.write(QStringLiteral("new.txt"), "fresh\n"));
        QSignalSpy untrackedSpy(&vm, &GitViewModel::workingDiffReady);
        const QString untrackedId =
            vm.requestWorkingDiff(QStringLiteral("new.txt"), false);
        QVERIFY(!untrackedId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(untrackedSpy.count(), 1, 10000);
        const QList<QVariant> untrackedArgs = untrackedSpy.takeFirst();
        QCOMPARE(untrackedArgs.at(0).toString(), untrackedId);
        QVERIFY2(untrackedArgs.at(3).toString().contains(
                     QStringLiteral("+fresh")),
                 qPrintable(untrackedArgs.at(3).toString()));
        QVERIFY(untrackedArgs.at(5).toString().isEmpty());

        QTemporaryDir plain;
        QVERIFY(plain.isValid());
        Sandbox::setProjectDir(plain.path());
        GitViewModel other;
        QSignalSpy infoSpy(&other, &GitViewModel::repoInfoChanged);
        other.refreshRepoInfo();
        QTRY_COMPARE_WITH_TIMEOUT(infoSpy.count(), 1, 10000);
        QCOMPARE(other.repoInfo().value("isRepo").toBool(), false);

        QSignalSpy historySpy(&other, &GitViewModel::fileHistoryReady);
        other.requestFileHistory(QStringLiteral("a.txt"), 5);
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 10000);
        const QList<QVariant> failed = historySpy.takeFirst();
        QVERIFY(failed.at(2).toList().isEmpty());
        QVERIFY2(!failed.at(3).toString().isEmpty(),
                 "a non-repository must surface an error");
    }

    // Phase 2: status tree data, repo-wide log paging, commit file list and
    // the explicit projectRoot boundary (no Sandbox mirror required).
    void statusRepoLogAndCommitFiles() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "base\n"));
        QVERIFY(repo.write(QStringLiteral("b.txt"), "b\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QVERIFY(repo.write(QStringLiteral("b.txt"), "b2\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));

        // a.txt: staged edit plus a later worktree edit (both groups);
        // c.txt: untracked.
        QVERIFY(repo.write(QStringLiteral("a.txt"), "staged\n"));
        QVERIFY(repo.run({QStringLiteral("add"), QStringLiteral("a.txt")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "worktree\n"));
        QVERIFY(repo.write(QStringLiteral("c.txt"), "untracked\n"));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QCOMPARE(vm.projectRoot(), repo.path());

        vm.refreshRepoInfo();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.repoInfoLoading(), 10000);
        QCOMPARE(vm.repoInfo().value("isRepo").toBool(), true);

        vm.refreshStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.statusLoading()
                                 && !vm.statusEntries().isEmpty(), 10000);
        QVERIFY2(vm.statusError().isEmpty(), qPrintable(vm.statusError()));
        bool sawAIndex = false;
        bool sawAWorktree = false;
        bool sawUntracked = false;
        for (const QVariant& entry : vm.statusEntries()) {
            const QVariantMap row = entry.toMap();
            const QString path = row.value("path").toString();
            if (path == QStringLiteral("a.txt")) {
                sawAIndex = row.value("indexStatus").toString()
                    == QStringLiteral("M");
                sawAWorktree = row.value("worktreeStatus").toString()
                    == QStringLiteral("M");
            } else if (path == QStringLiteral("c.txt")) {
                sawUntracked = row.value("indexStatus").toString()
                    == QStringLiteral("?")
                    && row.value("worktreeStatus").toString()
                    == QStringLiteral("?");
            }
        }
        QVERIFY2(sawAIndex, "a.txt must be staged");
        QVERIFY2(sawAWorktree, "a.txt must also have a worktree edit");
        QVERIFY2(sawUntracked, "c.txt must be reported as untracked");

        QSignalSpy logSpy(&vm, &GitViewModel::repoLogReady);
        const QString logId = vm.requestRepoLog(10, 0);
        QVERIFY(!logId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(logSpy.count(), 1, 10000);
        const QList<QVariant> logArgs = logSpy.takeFirst();
        QCOMPARE(logArgs.at(0).toString(), logId);
        QCOMPARE(logArgs.at(2).toInt(), 0);
        QVERIFY2(logArgs.at(3).toString().isEmpty(),
                 qPrintable(logArgs.at(3).toString()));
        const QVariantList commits = logArgs.at(1).toList();
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits.at(0).toMap().value("subject").toString(),
                 QStringLiteral("second"));
        const QString headSha = commits.at(0).toMap().value("sha").toString();
        QVERIFY(!headSha.isEmpty());

        QSignalSpy pageSpy(&vm, &GitViewModel::repoLogReady);
        vm.requestRepoLog(1, 1);
        QTRY_COMPARE_WITH_TIMEOUT(pageSpy.count(), 1, 10000);
        const QList<QVariant> pageArgs = pageSpy.takeFirst();
        QCOMPARE(pageArgs.at(2).toInt(), 1);
        QCOMPARE(pageArgs.at(1).toList().size(), 1);

        QSignalSpy filesSpy(&vm, &GitViewModel::commitFilesReady);
        const QString filesId = vm.requestCommitFiles(headSha);
        QVERIFY(!filesId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(filesSpy.count(), 1, 10000);
        const QList<QVariant> fileArgs = filesSpy.takeFirst();
        QCOMPARE(fileArgs.at(0).toString(), filesId);
        QCOMPARE(fileArgs.at(1).toString(), headSha);
        QVERIFY2(fileArgs.at(3).toString().isEmpty(),
                 qPrintable(fileArgs.at(3).toString()));
        bool sawB = false;
        for (const QVariant& file : fileArgs.at(2).toList()) {
            if (file.toMap().value("path").toString() == QStringLiteral("b.txt"))
                sawB = true;
        }
        QVERIFY2(sawB, "commit files must list b.txt");

        // Switching roots clears the old repository's state immediately.
        vm.setProjectRoot(QString());
        QVERIFY(vm.statusEntries().isEmpty());
        QVERIFY(vm.repoInfo().isEmpty());
    }

    // P0: explicit stage/unstage/commit write chain, one operation at a time.
    void stageCommitAndUnstage() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "base\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "changed\n"));
        QVERIFY(repo.write(QStringLiteral("new.txt"), "fresh\n"));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);
        QSignalSpy busySpy(&vm, &GitViewModel::operationBusyChanged);

        const QString stageId = vm.stageFiles(
            {QStringLiteral("a.txt"), QStringLiteral("new.txt")});
        QVERIFY(!stageId.isEmpty());
        QVERIFY2(vm.operationBusy(), "write must hold the busy gate");
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QList<QVariant> args = opSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), stageId);
        QCOMPARE(args.at(1).toString(), QStringLiteral("stage"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QVERIFY(args.at(3).toString().isEmpty());
        QVERIFY(!vm.operationBusy());
        QVERIFY(!busySpy.isEmpty());

        const auto findEntry = [&vm](const QString& path) {
            for (const QVariant& entry : vm.statusEntries()) {
                const QVariantMap row = entry.toMap();
                if (row.value("path").toString() == path) return row;
            }
            return QVariantMap();
        };
        vm.refreshStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.statusLoading(), 10000);
        QCOMPARE(findEntry(QStringLiteral("a.txt")).value("indexStatus").toString(),
                 QStringLiteral("M"));
        QCOMPARE(findEntry(QStringLiteral("new.txt")).value("indexStatus").toString(),
                 QStringLiteral("A"));
        QCOMPARE(findEntry(QStringLiteral("new.txt")).value("worktreeStatus").toString(),
                 QStringLiteral(" "));

        const QString unstageId =
            vm.unstageFiles({QStringLiteral("new.txt")});
        QVERIFY(!unstageId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("unstage"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        vm.refreshStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.statusLoading(), 10000);
        QCOMPARE(findEntry(QStringLiteral("new.txt")).value("worktreeStatus").toString(),
                 QStringLiteral("?"));

        QVERIFY(!vm.stageFiles({QStringLiteral("new.txt")}).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QVERIFY(opSpy.takeFirst().at(2).toBool());

        const QString commitId = vm.commitStaged(QStringLiteral("P0 commit"));
        QVERIFY(!commitId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("commit"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QVERIFY(!vm.operationBusy());

        QSignalSpy logSpy(&vm, &GitViewModel::repoLogReady);
        vm.requestRepoLog(5, 0);
        QTRY_COMPARE_WITH_TIMEOUT(logSpy.count(), 1, 10000);
        const QVariantList commits = logSpy.takeFirst().at(1).toList();
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits.at(0).toMap().value("subject").toString(),
                 QStringLiteral("P0 commit"));

        vm.refreshStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.statusLoading(), 10000);
        QVERIFY2(vm.statusEntries().isEmpty(), "worktree must be clean after commit");
    }

    void commitRejectsEmptyMessageAndNothingStaged() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "base\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "changed\n"));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        QVERIFY(!vm.commitStaged(QStringLiteral("   ")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QList<QVariant> args = opSpy.takeFirst();
        QCOMPARE(args.at(2).toBool(), false);
        QVERIFY2(args.at(3).toString().contains(QStringLiteral("message")),
                 qPrintable(args.at(3).toString()));
        QVERIFY2(vm.operationError().contains(QStringLiteral("message")),
                 qPrintable(vm.operationError()));

        QVERIFY(!vm.commitStaged(QStringLiteral("no staged files")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QCOMPARE(args.at(2).toBool(), false);
        QVERIFY2(args.at(3).toString().contains(QStringLiteral("Nothing is staged")),
                 qPrintable(args.at(3).toString()));
    }

    void stageAndCommitUnbornRepository() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("first.txt"), "first\n"));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        QVERIFY(!vm.stageFiles({QStringLiteral("first.txt")}).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QVERIFY(opSpy.takeFirst().at(2).toBool());

        // reset HEAD -- <path> must also unstage before the first commit.
        QVERIFY(!vm.unstageFiles({QStringLiteral("first.txt")}).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QVERIFY2(opSpy.takeFirst().at(2).toBool(), qPrintable(vm.operationError()));

        QVERIFY(!vm.stageFiles({QStringLiteral("first.txt")}).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QVERIFY(opSpy.takeFirst().at(2).toBool());

        QVERIFY(!vm.commitStaged(QStringLiteral("first commit")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        const QList<QVariant> args = opSpy.takeFirst();
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));

        QSignalSpy logSpy(&vm, &GitViewModel::repoLogReady);
        vm.requestRepoLog(5, 0);
        QTRY_COMPARE_WITH_TIMEOUT(logSpy.count(), 1, 10000);
        const QVariantList commits = logSpy.takeFirst().at(1).toList();
        QCOMPARE(commits.size(), 1);
        QCOMPARE(commits.at(0).toMap().value("subject").toString(),
                 QStringLiteral("first commit"));
    }

    void discardTrackedAndUntracked() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("base\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("changed\n")));
        QVERIFY(repo.write(QStringLiteral("u.txt"), QByteArrayLiteral("untracked\n")));

        QTemporaryDir backup;
        QVERIFY(backup.isValid());

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        vm.setDiscardBackupRoot(backup.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        // Tracked: `git restore` reverts the worktree and needs no backup.
        QVERIFY(!vm.discardPath(QStringLiteral("a.txt")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QList<QVariant> args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("discard"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QCOMPARE(readFile(repo.filePath(QStringLiteral("a.txt"))),
                 QStringLiteral("base\n"));
        QVERIFY2(vm.lastBackupPath().isEmpty(),
                 "a tracked restore must not claim a backup");

        // Untracked without a backup root: refuse, keep the file.
        vm.setDiscardBackupRoot(QString());
        QVERIFY(!vm.discardPath(QStringLiteral("u.txt")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QVERIFY2(!args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QVERIFY2(QFile::exists(repo.filePath(QStringLiteral("u.txt"))),
                 "an unbackuped untracked delete must refuse");

        // Untracked with a backup root: copied first, then removed.
        vm.setDiscardBackupRoot(backup.path());
        QVERIFY(!vm.discardPath(QStringLiteral("u.txt")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QVERIFY2(!QFile::exists(repo.filePath(QStringLiteral("u.txt"))),
                 "the untracked file must be gone");
        QVERIFY2(!vm.lastBackupPath().isEmpty(),
                 "discard must report where it backed the file up");
        QCOMPARE(readFile(vm.lastBackupPath()), QStringLiteral("untracked\n"));

        vm.refreshStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.statusLoading(), 10000);
        QVERIFY2(vm.statusEntries().isEmpty(),
                 "both discards must leave a clean worktree");
    }

    void amendAndUndoStayRecoverable() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("first\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("second\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        // An empty message can never rewrite history.
        QVERIFY(!vm.amendLast(QStringLiteral("   ")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        argsFail(opSpy.takeFirst());

        QVERIFY(!vm.amendLast(QStringLiteral("second amended")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QList<QVariant> args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("amend"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QCOMPARE(topSubject(vm), QStringLiteral("second amended"));
        QCOMPARE(commitCount(vm), 2);

        // Undo drops the commit but keeps its changes staged.
        QVERIFY(!vm.undoLastCommit().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("undo"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QCOMPARE(commitCount(vm), 1);
        QTRY_VERIFY_WITH_TIMEOUT(hasStagedEntry(vm, QStringLiteral("a.txt")), 15000);

        // The remaining commit is the root: undoing it would make the branch
        // unborn, so it must be refused.
        QVERIFY(!vm.undoLastCommit().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        argsFail(opSpy.takeFirst());
        QCOMPARE(commitCount(vm), 1);
    }

    void publishedAndMergeCommitsAreRefused() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("first\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));

        QTemporaryDir remote;
        QVERIFY(remote.isValid());
        QVERIFY(repo.run({QStringLiteral("init"), QStringLiteral("--bare"),
                          QStringLiteral("--quiet"), remote.path()}));
        QVERIFY(repo.run({QStringLiteral("remote"), QStringLiteral("add"),
                          QStringLiteral("origin"), remote.path()}));
        QVERIFY(repo.run({QStringLiteral("push"), QStringLiteral("--quiet"),
                          QStringLiteral("origin"),
                          QStringLiteral("HEAD:refs/heads/main")}));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        // HEAD is now held by a remote-tracking branch: no rewriting.
        QVERIFY(!vm.amendLast(QStringLiteral("rewritten")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        const QList<QVariant> amendArgs = opSpy.takeFirst();
        QVERIFY2(!amendArgs.at(2).toBool(),
                 "amend must refuse a published commit");
        QVERIFY2(amendArgs.at(3).toString().contains(QStringLiteral("published")),
                 qPrintable(amendArgs.at(3).toString()));
        QCOMPARE(topSubject(vm), QStringLiteral("first"));

        QVERIFY(!vm.undoLastCommit().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QVERIFY2(!opSpy.takeFirst().at(2).toBool(),
                 "undo must refuse a published commit");
        QCOMPARE(commitCount(vm), 1);
    }

    void undoRefusesMergeCommit() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("base\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        const QString branch = repo.output({QStringLiteral("rev-parse"),
                                            QStringLiteral("--abbrev-ref"),
                                            QStringLiteral("HEAD")}).trimmed();
        QVERIFY(!branch.isEmpty());
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-b"), QStringLiteral("feature")}));
        QVERIFY(repo.write(QStringLiteral("f.txt"), QByteArrayLiteral("feature\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("feature commit")));
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          branch}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("main\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("main commit")));
        QVERIFY(repo.run({QStringLiteral("merge"), QStringLiteral("--no-ff"),
                          QStringLiteral("-m"), QStringLiteral("merge commit"),
                          QStringLiteral("feature")}));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        QVERIFY(!vm.undoLastCommit().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        const QList<QVariant> args = opSpy.takeFirst();
        QVERIFY2(!args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QVERIFY2(args.at(3).toString().contains(QStringLiteral("merge")),
                 qPrintable(args.at(3).toString()));
        QCOMPARE(commitCount(vm), 4);
    }

    void stashRoundTrip() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("base\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        // Only untracked content is not a stash.
        QVERIFY(repo.write(QStringLiteral("u.txt"), QByteArrayLiteral("untracked\n")));
        QVERIFY(!vm.stashPush(QStringLiteral("nothing tracked")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QVERIFY2(!opSpy.takeFirst().at(2).toBool(),
                 "untracked-only worktree must not stash");

        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("dirty\n")));
        QVERIFY(!vm.stashPush(QStringLiteral("wip note")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QList<QVariant> args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("stashPush"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QCOMPARE(readFile(repo.filePath(QStringLiteral("a.txt"))),
                 QStringLiteral("base\n"));

        QSignalSpy listSpy(&vm, &GitViewModel::stashListReady);
        const QString listId = vm.requestStashList();
        QVERIFY(!listId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 15000);
        const QList<QVariant> listArgs = listSpy.takeFirst();
        QCOMPARE(listArgs.at(0).toString(), listId);
        QVERIFY2(listArgs.at(2).toString().isEmpty(),
                 qPrintable(listArgs.at(2).toString()));
        const QVariantList stashes = listArgs.at(1).toList();
        QCOMPARE(stashes.size(), 1);
        QCOMPARE(stashes.at(0).toMap().value("index").toInt(), 0);
        QVERIFY2(stashes.at(0).toMap().value("subject").toString()
                     .contains(QStringLiteral("wip note")),
                 qPrintable(stashes.at(0).toMap().value("subject").toString()));

        // Out-of-range indexes are refused before git is invoked.
        QVERIFY(!vm.stashPop(5).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        QVERIFY2(!opSpy.takeFirst().at(2).toBool(), "index 5 has no stash");

        QVERIFY(!vm.stashPop(0).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("stashPop"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));
        QCOMPARE(readFile(repo.filePath(QStringLiteral("a.txt"))),
                 QStringLiteral("dirty\n"));

        QSignalSpy emptySpy(&vm, &GitViewModel::stashListReady);
        vm.requestStashList();
        QTRY_COMPARE_WITH_TIMEOUT(emptySpy.count(), 1, 15000);
        QVERIFY2(emptySpy.takeFirst().at(1).toList().isEmpty(),
                 "popped stash list must be empty");
    }

    void stageAndUnstageSingleHunk() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QByteArray base;
        for (int i = 1; i <= 40; ++i)
            base += QStringLiteral("line %1\n").arg(i).toUtf8();
        QVERIFY(repo.write(QStringLiteral("a.txt"), base));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));

        // Two edits far apart, so the worktree diff has exactly two hunks.
        QByteArray work = base;
        work.replace(QByteArrayLiteral("line 2\n"), QByteArrayLiteral("line 2 edited\n"));
        work.replace(QByteArrayLiteral("line 30\n"), QByteArrayLiteral("line 30 edited\n"));
        QVERIFY(repo.write(QStringLiteral("a.txt"), work));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy hunkSpy(&vm, &GitViewModel::diffHunksReady);
        const QString hunkId = vm.requestDiffHunks(QStringLiteral("a.txt"), false);
        QVERIFY(!hunkId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(hunkSpy.count(), 1, 15000);
        QList<QVariant> args = hunkSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), hunkId);
        QVERIFY2(args.at(4).toString().isEmpty(), qPrintable(args.at(4).toString()));
        QVariantList hunks = args.at(3).toList();
        QCOMPARE(hunks.size(), 2);
        QCOMPARE(hunks.at(0).toMap().value("additions").toInt(), 1);
        QCOMPARE(hunks.at(0).toMap().value("removals").toInt(), 1);
        QVERIFY2(hunks.at(0).toMap().value("summary").toString()
                     .contains(QStringLiteral("line 2 edited")),
                 qPrintable(hunks.at(0).toMap().value("summary").toString()));
        QVERIFY2(hunks.at(1).toMap().value("summary").toString()
                     .contains(QStringLiteral("line 30 edited")),
                 qPrintable(hunks.at(1).toMap().value("summary").toString()));

        QSignalSpy opSpy(&vm, &GitViewModel::operationFinished);

        // A row addressed with the wrong header is refused before git is run:
        // the list the UI holds must never stage a block it never showed.
        const QVariantMap first = hunks.at(0).toMap();
        const QString wrongHeader = QStringLiteral("@@ -9,9 +9,9 @@ fake");
        QVERIFY(!vm.stageHunks(QStringLiteral("a.txt"),
                               {QVariantMap{{QStringLiteral("index"), 0},
                                            {QStringLiteral("header"), wrongHeader}}}
                               ).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QVERIFY2(!args.at(2).toBool(), "a stale hunk header must be refused");
        QVERIFY2(args.at(3).toString().contains(QStringLiteral("refresh")),
                 qPrintable(args.at(3).toString()));

        QVERIFY(!vm.stageHunks(QStringLiteral("a.txt"),
                               {QVariantMap{{QStringLiteral("index"),
                                                first.value("index")},
                                            {QStringLiteral("header"),
                                                first.value("header")}}}
                               ).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("stageHunk"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));

        // Only the first hunk is in the index...
        const QString stagedDiff = repo.output(
            {QStringLiteral("diff"), QStringLiteral("--cached")});
        QVERIFY2(stagedDiff.contains(QStringLiteral("line 2 edited")),
                 qPrintable(stagedDiff));
        QVERIFY2(!stagedDiff.contains(QStringLiteral("line 30 edited")),
                 "hunk 1 must stay unstaged");
        // ...and the second one is still a worktree change.
        hunkSpy.clear();
        vm.requestDiffHunks(QStringLiteral("a.txt"), true);
        QTRY_COMPARE_WITH_TIMEOUT(hunkSpy.count(), 1, 15000);
        const QVariantList stagedHunks = hunkSpy.takeFirst().at(3).toList();
        QCOMPARE(stagedHunks.size(), 1);

        // Taking the staged hunk back out reverses exactly what was written.
        QVERIFY(!vm.unstageHunks(QStringLiteral("a.txt"),
                                {QVariantMap{{QStringLiteral("index"),
                                                 stagedHunks.at(0).toMap()
                                                     .value("index")},
                                             {QStringLiteral("header"),
                                                 stagedHunks.at(0).toMap()
                                                     .value("header")}}}
                                ).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(opSpy.count(), 1, 15000);
        args = opSpy.takeFirst();
        QCOMPARE(args.at(1).toString(), QStringLiteral("unstageHunk"));
        QVERIFY2(args.at(2).toBool(), qPrintable(args.at(3).toString()));

        vm.refreshStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.statusLoading(), 10000);
        QVERIFY2(!vm.statusEntries().isEmpty(), "the file is still modified");
        for (const QVariant& entry : vm.statusEntries()) {
            const QVariantMap row = entry.toMap();
            if (row.value("path").toString() != QStringLiteral("a.txt")) continue;
            QCOMPARE(row.value("indexStatus").toString(), QStringLiteral(" "));
            QCOMPARE(row.value("worktreeStatus").toString(), QStringLiteral("M"));
        }
        // The worktree itself was never touched by partial staging.
        QCOMPARE(readFile(repo.filePath(QStringLiteral("a.txt"))),
                 QString::fromUtf8(work));
    }

    void hunkStagingRefusesUnpatchableFiles() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), QByteArrayLiteral("base\n")));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));

        GitViewModel vm;
        vm.setProjectRoot(repo.path());
        QSignalSpy hunkSpy(&vm, &GitViewModel::diffHunksReady);

        auto expectRefusal = [&vm, &hunkSpy](const QString& path, bool staged,
                                             const QString& expectedFragment) {
            hunkSpy.clear();
            vm.requestDiffHunks(path, staged);
            QTRY_COMPARE_WITH_TIMEOUT(hunkSpy.count(), 1, 15000);
            const QList<QVariant> args = hunkSpy.takeFirst();
            QVERIFY2(args.at(3).toList().isEmpty(),
                     "no hunks may be offered for a refused file");
            QVERIFY2(!args.at(4).toString().isEmpty(),
                     "the refusal must carry a reason");
            QVERIFY2(args.at(4).toString().contains(expectedFragment),
                     qPrintable(args.at(4).toString()));
        };

        // Untracked: no index entry to patch.
        QVERIFY(repo.write(QStringLiteral("u.txt"), QByteArrayLiteral("fresh\n")));
        expectRefusal(QStringLiteral("u.txt"), false, QStringLiteral("as a whole"));

        // Deleted in the worktree.
        QVERIFY(QFile::remove(repo.filePath(QStringLiteral("a.txt"))));
        expectRefusal(QStringLiteral("a.txt"), false, QStringLiteral("as a whole"));
    }

    void branchViewReportsTrackingAndMasksUrls() {
        TempRepo origin;
        QVERIFY(origin.valid() && origin.init());
        QVERIFY(origin.write(QStringLiteral("a.txt"), QByteArrayLiteral("base\n")));
        QVERIFY(origin.addAll() && origin.commit(QStringLiteral("base")));

        TempRepo clone;
        QVERIFY(clone.valid());
        QVERIFY(clone.run({QStringLiteral("clone"), QStringLiteral("-q"),
                           origin.path(), QStringLiteral(".")}));
        QVERIFY(clone.run({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test User")}));
        QVERIFY(clone.run({QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("test@example.com")}));
        const QString branch = clone.output({QStringLiteral("rev-parse"),
                                            QStringLiteral("--abbrev-ref"),
                                            QStringLiteral("HEAD")}).trimmed();
        QVERIFY(!branch.isEmpty());

        // One local commit makes the branch ahead; a divergent remote commit
        // plus a fetch adds the behind side.
        QVERIFY(clone.write(QStringLiteral("b.txt"), QByteArrayLiteral("local\n")));
        QVERIFY(clone.addAll() && clone.commit(QStringLiteral("local only")));
        QVERIFY(origin.write(QStringLiteral("c.txt"), QByteArrayLiteral("remote\n")));
        QVERIFY(origin.addAll() && origin.commit(QStringLiteral("remote only")));
        QVERIFY(clone.run({QStringLiteral("fetch"), QStringLiteral("--quiet"),
                           QStringLiteral("origin")}));
        QVERIFY(clone.run({QStringLiteral("branch"), QStringLiteral("no-upstream")}));
        QVERIFY(clone.run({QStringLiteral("tag"), QStringLiteral("v1-test")}));

        GitViewModel vm;
        vm.setProjectRoot(clone.path());
        QSignalSpy viewSpy(&vm, &GitViewModel::branchViewReady);
        const QString viewId = vm.requestBranchView();
        QVERIFY(!viewId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(viewSpy.count(), 1, 15000);
        const QList<QVariant> args = viewSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), viewId);
        QVERIFY2(args.at(2).toString().isEmpty(), qPrintable(args.at(2).toString()));

        const QVariantMap payload = args.at(1).toMap();
        QVariantMap tracked;
        QVariantMap orphan;
        const QVariantList branches = payload.value("branches").toList();
        QCOMPARE(branches.size(), 2);
        for (const QVariant& value : branches) {
            const QVariantMap row = value.toMap();
            if (row.value("name").toString() == branch) tracked = row;
            if (row.value("name").toString() == QStringLiteral("no-upstream"))
                orphan = row;
        }
        QVERIFY2(!tracked.isEmpty(), "the checked-out branch must be listed");
        QVERIFY2(tracked.value("current").toBool(), "the HEAD branch is current");
        QCOMPARE(tracked.value("upstream").toString(),
                 QStringLiteral("origin/") + branch);
        QCOMPARE(tracked.value("trackState").toString(),
                 QStringLiteral("diverged"));
        QVERIFY2(tracked.value("ahead").toInt() >= 1,
                 "a local commit must show as ahead");
        QVERIFY2(tracked.value("behind").toInt() >= 1,
                 "a fetched remote commit must show as behind");
        QVERIFY2(!orphan.isEmpty(), "a branch without an upstream must be listed");
        QCOMPARE(orphan.value("trackState").toString(), QStringLiteral("none"));
        QCOMPARE(orphan.value("upstream").toString(), QString());

        const QVariantList remotes = payload.value("remotes").toList();
        QCOMPARE(remotes.size(), 1);
        QCOMPARE(remotes.at(0).toMap().value("name").toString(),
                 QStringLiteral("origin"));
        QVERIFY2(remotes.at(0).toMap().value("branches").toStringList()
                     .contains(QStringLiteral("origin/") + branch),
                 "remote-tracking refs must be listed");

        // Tags ride along in the same view model payload.
        const QVariantList tagList = payload.value("tags").toList();
        QCOMPARE(tagList.size(), 1);
        QCOMPARE(tagList.at(0).toMap().value("name").toString(),
                 QStringLiteral("v1-test"));
        QVERIFY2(!tagList.at(0).toMap().value("annotated").toBool(),
                 "git tag alone is a lightweight tag");
        const QString tagSha = tagList.at(0).toMap().value("sha").toString();
        QVERIFY2(tagSha.length() == 40,
                 qPrintable(QStringLiteral("full commit sha expected, got %1").arg(tagSha)));
        QVERIFY2(tagSha.startsWith(tracked.value("sha").toString()),
                 "the tag must point at the checked-out commit");

        // A credential in the remote URL must never reach the view model.
        QVERIFY(clone.run({QStringLiteral("remote"), QStringLiteral("set-url"),
                           QStringLiteral("origin"),
                           QStringLiteral("https://user:secr3t@127.0.0.1/x.git")}));
        viewSpy.clear();
        vm.requestBranchView();
        QTRY_COMPARE_WITH_TIMEOUT(viewSpy.count(), 1, 15000);
        const QString url = viewSpy.takeFirst().at(1).toMap()
                                .value("remotes").toList().at(0).toMap()
                                .value("url").toString();
        QVERIFY2(!url.contains(QStringLiteral("secr3t")), qPrintable(url));
        QVERIFY2(!url.contains(QStringLiteral("user")), qPrintable(url));
        QVERIFY2(url.contains(QStringLiteral("127.0.0.1")), qPrintable(url));
    }

    void explicitRootNonRepository() {
        QTemporaryDir plain;
        QVERIFY(plain.isValid());
        GitViewModel vm;
        vm.setProjectRoot(plain.path());
        vm.refreshRepoInfo();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.repoInfoLoading(), 10000);
        QCOMPARE(vm.repoInfo().value("isRepo").toBool(), false);

        vm.refreshStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.statusLoading(), 10000);
        QVERIFY2(!vm.statusError().isEmpty(),
                 "a non-repository root must surface a status error");
        QVERIFY(vm.statusEntries().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestGitViewModel)
#include "test_git_viewmodel.moc"
