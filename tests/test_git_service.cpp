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

    void logFiltersByMessageAndAuthor() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("feat: add A [core]")));

        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Bob Lee")}));
        QVERIFY(repo.write(QStringLiteral("b.txt"), "b1\n"));
        QVERIFY(repo.addAll()
                && repo.commitWithBody(QStringLiteral("fix: repair B"),
                                       QStringLiteral("details about the panel")));

        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Alice Zhang")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\ntwo\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("docs: update A")));

        GitService git(repo.path());
        QString error;
        const auto subjects = [&error](const QVector<GitCommitInfo>& commits) {
            QStringList out;
            for (const GitCommitInfo& c : commits) out << c.subject;
            return out;
        };

        GitLogQuery query;
        query.grep = QStringLiteral("repair");
        auto filtered = git.log(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(subjects(filtered), QStringList{QStringLiteral("fix: repair B")});

        // Case-insensitive fixed strings: the user types words, not regexes.
        query.grep = QStringLiteral("REPAIR b");
        filtered = git.log(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(filtered.size(), 1);

        // A literal bracket must not be read as a character class.
        query.grep = QStringLiteral("[core]");
        filtered = git.log(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(subjects(filtered), QStringList{QStringLiteral("feat: add A [core]")});

        // --grep also sees the body, which is what "search commits" means.
        query.grep = QStringLiteral("panel");
        filtered = git.log(query, &error);
        QCOMPARE(subjects(filtered), QStringList{QStringLiteral("fix: repair B")});

        query = GitLogQuery();
        query.author = QStringLiteral("bob");
        filtered = git.log(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.at(0).author, QStringLiteral("Bob Lee"));

        // Both kinds of limit must match (git's default across limit types).
        query.grep = QStringLiteral("B");
        filtered = git.log(query, &error);
        QCOMPARE(subjects(filtered), QStringList{QStringLiteral("fix: repair B")});
        query.author = QStringLiteral("alice");
        QVERIFY2(git.log(query, &error).isEmpty(),
                 "grep and author are combined with AND");
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // No match is an empty page, not an error.
        query = GitLogQuery();
        query.grep = QStringLiteral("zzz-nothing");
        QVERIFY2(git.log(query, &error).isEmpty(), qPrintable(error));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // Paging still applies to the filtered set. "a" matches all three
        // subjects case-insensitively (even "repair"), so this also proves the
        // filter is not silently narrowing to one field.
        query = GitLogQuery();
        query.grep = QStringLiteral("a");
        QCOMPARE(git.log(query, &error).size(), 3);
        query.limit = 1;
        query.offset = 1;
        const auto page = git.log(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.size(), 1);
        QCOMPARE(page.at(0).subject, QStringLiteral("fix: repair B"));
        query.offset = 2;
        QCOMPARE(subjects(git.log(query, &error)),
                 QStringList{QStringLiteral("feat: add A [core]")});

        // Filter and path combine.
        query = GitLogQuery();
        query.path = QStringLiteral("a.txt");
        query.grep = QStringLiteral("update");
        QCOMPARE(subjects(git.log(query, &error)),
                 QStringList{QStringLiteral("docs: update A")});

        // The unfiltered signature keeps behaving exactly as before.
        const auto all = git.log(10, 0, QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(all.size(), 3);
    }

    void comparesAnyTwoCommits() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        QVERIFY(repo.write(QStringLiteral("b.txt"), "b1\nb2\n"));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\ntwo\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));
        QVERIFY(repo.run({QStringLiteral("mv"), QStringLiteral("a.txt"),
                          QStringLiteral("c.txt")})
                && repo.addAll() && repo.commit(QStringLiteral("renamed a")));

        GitService git(repo.path());
        QString error;
        const auto commits = git.log(10, 0, QString(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(commits.size(), 3);
        const QString second = commits.at(1).sha;
        const QString head = commits.at(0).sha;
        const QString first = commits.at(2).sha;

        // first..second: a.txt edited and b.txt added.
        const auto range = git.compareFiles(first, second, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(range.size(), 2);
        bool sawModifiedA = false;
        bool sawAddedB = false;
        for (const GitFileStat& file : range) {
            if (file.path == QStringLiteral("a.txt")) {
                sawModifiedA = file.status.startsWith(QStringLiteral("M"))
                            && file.additions == 1 && file.removals == 0;
            } else if (file.path == QStringLiteral("b.txt")) {
                sawAddedB = file.status.startsWith(QStringLiteral("A"))
                         && file.additions == 2;
            }
        }
        QVERIFY2(sawModifiedA, "a.txt must show as +1 modified");
        QVERIFY2(sawAddedB, "b.txt must show as added with 2 lines");

        // second..head is a pure rename, which the shared parser must keep.
        const auto renamed = git.compareFiles(second, head, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(renamed.size(), 1);
        QVERIFY2(renamed.at(0).status.startsWith(QStringLiteral("R")),
                 qPrintable(renamed.at(0).status));
        QCOMPARE(renamed.at(0).oldPath, QStringLiteral("a.txt"));
        QCOMPARE(renamed.at(0).path, QStringLiteral("c.txt"));

        // Diff text: whole range and one path.
        const QString whole = git.compareDiff(first, second, QString(), nullptr,
                                              &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(whole.contains(QStringLiteral("+b1")), qPrintable(whole));
        QVERIFY2(whole.contains(QStringLiteral("+two")), qPrintable(whole));

        const QString oneFile = git.compareDiff(first, second,
                                                QStringLiteral("b.txt"), nullptr,
                                                &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(oneFile.contains(QStringLiteral("+b1")), qPrintable(oneFile));
        QVERIFY2(!oneFile.contains(QStringLiteral("+two")),
                 "a path filter must exclude the other file");

        // Same commit compared with itself is empty, not an error.
        QVERIFY(git.compareFiles(head, head, &error).isEmpty());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(git.compareDiff(head, head, QString(), nullptr, &error).isEmpty());
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // A bad id never reaches git, in either direction.
        QVERIFY(git.compareFiles(QStringLiteral("not-a-sha"), head, &error)
                  .isEmpty());
        QVERIFY2(error.contains(QStringLiteral("Invalid commit")),
                 qPrintable(error));
        QVERIFY(git.compareDiff(head, QStringLiteral("../../etc/passwd"),
                                QString(), nullptr, &error).isEmpty());
        QVERIFY2(error.contains(QStringLiteral("Invalid commit")),
                 qPrintable(error));
        // A path outside the repository is refused too.
        error.clear();
        QVERIFY(git.compareDiff(first, head, QStringLiteral("../outside.txt"),
                                nullptr, &error).isEmpty());
        QVERIFY2(!error.isEmpty(), qPrintable(error));
    }

    void blamesEachLineAndMarksUncommitted() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Ann A")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        const QString firstSha =
            GitService(repo.path()).log(1, 0, QString(), nullptr).at(0).sha;

        QVERIFY(repo.run({QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("Bob B")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\ntwo\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));

        // Line two is then edited without committing: blame must call that out
        // instead of attributing it to the last real commit.
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\ntwo edited\n"));

        GitService git(repo.path());
        QString error;
        const auto lines = git.blame(QStringLiteral("a.txt"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(lines.size(), 2);

        QCOMPARE(lines.at(0).lineNo, 1);
        QCOMPARE(lines.at(0).text, QStringLiteral("one"));
        QCOMPARE(lines.at(0).author, QStringLiteral("Ann A"));
        QCOMPARE(lines.at(0).sha, firstSha);
        QCOMPARE(lines.at(0).shortSha, firstSha.left(7));
        QCOMPARE(lines.at(0).summary, QStringLiteral("first"));
        QVERIFY(lines.at(0).timestamp > 0);
        QVERIFY2(!lines.at(0).uncommitted(), "a committed line is attributed");

        QCOMPARE(lines.at(1).lineNo, 2);
        QCOMPARE(lines.at(1).text, QStringLiteral("two edited"));
        QVERIFY2(lines.at(1).uncommitted(),
                 "a worktree-only line must not claim a commit");
        QVERIFY2(lines.at(1).toJson().value("uncommitted").toBool(),
                 "the JSON flag is what the QML row reads");

        // Untracked files have no blame at all, and say so.
        QVERIFY(repo.write(QStringLiteral("u.txt"), "fresh\n"));
        error.clear();
        QVERIFY(git.blame(QStringLiteral("u.txt"), &error).isEmpty());
        QVERIFY2(!error.isEmpty(), qPrintable(error));

        // A path outside the repository is refused before git is called.
        error.clear();
        QVERIFY(git.blame(QStringLiteral("../outside.txt"), &error).isEmpty());
        QVERIFY2(!error.isEmpty(), qPrintable(error));
    }

    void drawsMultiBranchGraphFromLocalRefs() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\ntwo\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));

        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-b"), QStringLiteral("side")}));
        QVERIFY(repo.write(QStringLiteral("s.txt"), "side\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("side work")));

        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-")})
                || repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                             QStringLiteral("master")}));
        QVERIFY(repo.write(QStringLiteral("m.txt"), "main\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("main work")));
        QVERIFY(repo.run({QStringLiteral("merge"), QStringLiteral("--no-ff"),
                          QStringLiteral("-m"), QStringLiteral("merge side"),
                          QStringLiteral("side")}));

        QByteArray branchBytes;
        QVERIFY(repo.run({QStringLiteral("rev-parse"),
                          QStringLiteral("--abbrev-ref"),
                          QStringLiteral("HEAD")}, &branchBytes));
        const QString branch = QString::fromUtf8(branchBytes).trimmed();
        QVERIFY(!branch.isEmpty());

        // A commit that was never merged in only shows up under --all.
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          QStringLiteral("-b"), QStringLiteral("orphan")}));
        QVERIFY(repo.write(QStringLiteral("o.txt"), "orphan\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("orphan work")));
        QVERIFY(repo.run({QStringLiteral("checkout"), QStringLiteral("-q"),
                          branch}));

        GitService git(repo.path());
        QString error;
        const auto wide = git.graph(50, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(!wide.isEmpty(), "a merged branch must produce graph rows");

        int commits = 0;
        bool sawJoin = false;
        bool sawHeadDecoration = false;
        QStringList subjects;
        for (const GitGraphLine& line : wide) {
            if (!line.hasCommit) {
                if (!line.art.trimmed().isEmpty()) sawJoin = true;
                continue;
            }
            ++commits;
            subjects << line.subject;
            if (line.refs.contains(QStringLiteral("HEAD")))
                sawHeadDecoration = true;
        }
        QVERIFY2(commits >= 4, qPrintable(subjects.join("|")));
        QVERIFY2(subjects.contains(QStringLiteral("merge side")),
                 qPrintable(subjects.join("|")));
        QVERIFY2(subjects.contains(QStringLiteral("side work")),
                 "the merged branch line belongs to HEAD's history too");
        QVERIFY2(sawJoin, "the merge must draw a join row (|\\ / |/)");
        QVERIFY2(sawHeadDecoration, "HEAD must be decorated on its own row");
        QVERIFY2(subjects.contains(QStringLiteral("orphan work")),
                 "--all must include the unmerged branch");

        // Same repository, current branch only: the unmerged commit is gone.
        const auto narrow = git.graph(50, false, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QStringList narrowSubjects;
        for (const GitGraphLine& line : narrow)
            if (line.hasCommit) narrowSubjects << line.subject;
        QVERIFY2(!narrowSubjects.contains(QStringLiteral("orphan work")),
                 qPrintable(narrowSubjects.join("|")));
        QVERIFY2(narrowSubjects.contains(QStringLiteral("merge side")),
                 qPrintable(narrowSubjects.join("|")));

        // Limit is honoured, and the short ids are what git resolves.
        const auto small = git.graph(2, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        int smallCommits = 0;
        for (const GitGraphLine& line : small)
            if (line.hasCommit) ++smallCommits;
        QCOMPARE(smallCommits, 2);

        // Non-repository roots report instead of returning an empty picture.
        GitService none(QStringLiteral("Q:/definitely-not-a-repo-9x7"));
        error.clear();
        QVERIFY(none.graph(10, true, &error).isEmpty());
        QVERIFY2(!error.isEmpty(), qPrintable(error));
    }

    void listsAnnotatedAndLightweightTags() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("first")));

        GitService empty(repo.path());
        QString error;
        QVERIFY2(empty.tags(&error).isEmpty(), qPrintable(error));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const QString head = empty.log(1, 0, QString(), &error).at(0).sha;
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(repo.run({QStringLiteral("tag"), QStringLiteral("light-1")}));
        QVERIFY(repo.run({QStringLiteral("tag"), QStringLiteral("-a"),
                          QStringLiteral("ann-1"), QStringLiteral("-m"),
                          QStringLiteral("release one")}));
        QVERIFY(repo.write(QStringLiteral("a.txt"), "one\ntwo\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("second")));
        QVERIFY(repo.run({QStringLiteral("tag"), QStringLiteral("-a"),
                          QStringLiteral("ann-2"), QStringLiteral("-m"),
                          QStringLiteral("new release")}));

        GitService git(repo.path());
        const auto tags = git.tags(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(tags.size(), 3);

        // Sorted by name by for-each-ref: ann-1, ann-2, light-1.
        QCOMPARE(tags.at(0).name, QStringLiteral("ann-1"));
        QVERIFY2(tags.at(0).annotated, "git tag -a must be reported as annotated");
        QCOMPARE(tags.at(0).subject, QStringLiteral("release one"));
        QVERIFY2(tags.at(0).date > 0, "an annotated tag has a tagger date");
        // A tag object is not the commit: `sha` must be the peeled commit (full,
        // so it compares against log rows) and `target` its short display form.
        QVERIFY2(tags.at(0).sha == head
                     && tags.at(0).target == head.left(7),
                 qPrintable(QStringLiteral("peeled sha expected, got %1 / %2")
                                .arg(tags.at(0).sha, tags.at(0).target)));

        QVERIFY2(!tags.at(2).annotated, "git tag alone is lightweight");
        QCOMPARE(tags.at(2).name, QStringLiteral("light-1"));
        QCOMPARE(tags.at(2).sha, head);
        QCOMPARE(tags.at(1).name, QStringLiteral("ann-2"));
        QVERIFY2(tags.at(1).sha != head, "the later tag points at the later commit");
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

    // 模型常把"."、"src/"、"<项目根绝对路径>"直接当 path 传下来。
    // 回归点：normalizePath 曾把"指向仓库根"的路径（"."/
    // "<root>/."）判成 "Path escapes the repository"——cleanPath 会把
    // "<root>/." 折叠成 "<root>"，于是 abs.size() <= prefix.size() 成立，
    // 被误当作越界。
    void pathAcceptsRepoRootAndSubdirs() {
        TempRepo repo;
        QVERIFY(repo.valid() && repo.init());
        QVERIFY(repo.write(QStringLiteral("src/a.txt"), "a\n"));
        QVERIFY(repo.addAll() && repo.commit(QStringLiteral("base")));
        QVERIFY(repo.write(QStringLiteral("src/a.txt"), "b\n"));

        GitService git(repo.path());
        const QString root = git.repositoryRoot();
        QVERIFY(!root.isEmpty());

        // 全部指向"仓库根"或"仓库内子目录"，都必须能正常读。
        const QStringList accepted{
            QString(),                              // 空 = 整个仓库
            QStringLiteral("."),                    // 点 = 仓库根
            QStringLiteral("./"),                   // 带斜杠的点
            root,                                   // 绝对路径（仓库根）
            root + QStringLiteral("/."),            // 绝对路径带 "/."
            QDir::toNativeSeparators(root),         // 绝对路径（反斜杠）
            QStringLiteral("src"),                  // 相对子目录
            QStringLiteral("src/"),                 // 相对子目录带斜杠
            QStringLiteral("./src"),
            root + QStringLiteral("/src"),          // 绝对子目录
            QDir::toNativeSeparators(root + QStringLiteral("/src")),
        };

        for (const QString& path : accepted) {
            QString error;
            const QString text = git.statusText(path, &error);
            QVERIFY2(!text.isNull(),
                     qPrintable(QStringLiteral("statusText 拒绝了合法路径 [%1]: %2")
                                    .arg(path).arg(error)));

            bool truncated = false;
            const QString diff = git.workingDiff(path, false, &truncated, &error);
            QVERIFY2(!diff.isNull(),
                     qPrintable(QStringLiteral("workingDiff 拒绝了合法路径 [%1]: %2")
                                    .arg(path).arg(error)));
        }

        // 真正越界的仍必须被拒。
        const QStringList rejected{
            QStringLiteral("../outside"),
            QStringLiteral(".."),
            root + QStringLiteral("/../outside"),
            QStringLiteral("/etc/passwd"),
            QStringLiteral("C:/Windows"),
        };
        for (const QString& path : rejected) {
            QString error;
            const QString text = git.statusText(path, &error);
            QVERIFY2(text.isNull(),
                     qPrintable(QStringLiteral("statusText 放过了越界路径 [%1]").arg(path)));
        }
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
