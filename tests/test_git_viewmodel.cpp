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
};

QTEST_GUILESS_MAIN(TestGitViewModel)
#include "test_git_viewmodel.moc"
