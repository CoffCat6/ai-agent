// Unit tests for the glob and grep built-in tools.
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"
#include "core/tools/NetworkFetchTool.h"

#include <QDir>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

using namespace NexusAI;

class TestTools : public QObject {
    Q_OBJECT

private slots:
    void init() {
        m_tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tempDir->isValid());
        Sandbox::setProjectDir(m_tempDir->path());
    }

    void cleanup() {
        Sandbox::setProjectDir(QString());
        m_tempDir.reset();
    }

    // ── Glob tests ──
    void globFindsFilesByExtension() {
        createFile("test.txt", "hello");
        createFile("test.cpp", "code");
        createFile("sub/other.txt", "world");

        auto tool = ToolRegistry::instance().findShared("glob");
        QVERIFY(tool);
        const ToolResult r = tool->execute({{"pattern", "**/*.txt"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("test.txt"));
        QVERIFY(r.output.contains("other.txt"));
        QVERIFY(!r.output.contains("test.cpp"));
    }

    void globNonRecursive() {
        createFile("a.txt", "");
        createFile("b.txt", "");
        createFile("sub/c.txt", "");

        auto tool = ToolRegistry::instance().findShared("glob");
        const ToolResult r = tool->execute({{"pattern", "*.txt"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("a.txt"));
        QVERIFY(r.output.contains("b.txt"));
        QVERIFY(!r.output.contains("c.txt"));
    }

    void globNoMatch() {
        createFile("test.txt", "");

        auto tool = ToolRegistry::instance().findShared("glob");
        const ToolResult r = tool->execute({{"pattern", "**/*.nonexistent"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("No files matched"));
    }

    void globWithSubDirPattern() {
        createFile("src/main.cpp", "");
        createFile("src/util.h", "");
        createFile("other/file.cpp", "");

        auto tool = ToolRegistry::instance().findShared("glob");
        const ToolResult r = tool->execute({{"pattern", "src/**/*.cpp"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("main.cpp"));
        QVERIFY(!r.output.contains("file.cpp"));
    }

    // ── Grep tests ──
    void grepFindsMatchingContent() {
        createFile("test.cpp", "int main() {\n    return 0;\n}\n");

        auto tool = ToolRegistry::instance().findShared("grep");
        QVERIFY(tool);
        const ToolResult r = tool->execute({{"pattern", "main"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("test.cpp"));
        QVERIFY(r.output.contains("main"));
    }

    void grepReturnsLineNumbers() {
        createFile("test.cpp", "line1\nline2\nint main() {}\nline4\n");

        auto tool = ToolRegistry::instance().findShared("grep");
        const ToolResult r = tool->execute({{"pattern", "main"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("test.cpp:3:"));
    }

    void grepNoMatch() {
        createFile("test.txt", "hello world\n");

        auto tool = ToolRegistry::instance().findShared("grep");
        const ToolResult r = tool->execute({{"pattern", "nonexistent_pattern"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("No matches found"));
    }

    void grepRespectsIncludeFilter() {
        createFile("test.cpp", "target\n");
        createFile("test.txt", "target\n");

        auto tool = ToolRegistry::instance().findShared("grep");
        const ToolResult r = tool->execute({
            {"pattern", "target"},
            {"include", "*.cpp"}
        });
        QVERIFY(r.success);
        QVERIFY(r.output.contains("test.cpp"));
        QVERIFY(!r.output.contains("test.txt"));
    }

    void grepInvalidRegex() {
        createFile("test.txt", "hello\n");

        auto tool = ToolRegistry::instance().findShared("grep");
        const ToolResult r = tool->execute({{"pattern", "[invalid"}});
        QVERIFY(!r.success);
        QVERIFY(r.error.contains("Invalid regex"));
    }

    void grepSearchesRecursively() {
        createFile("a.txt", "found\n");
        createFile("sub/b.txt", "found\n");
        auto tool = ToolRegistry::instance().findShared("grep");
        const ToolResult r = tool->execute({{"pattern", "found"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains("a.txt"));
        QVERIFY(r.output.contains("sub/b.txt"));
    }

    // ── TodoWrite tests ──
    void todoWriteValidList() {
        auto tool = ToolRegistry::instance().findShared("todo_write");
        QVERIFY(tool);
        const QJsonArray todos{
            QJsonObject{{"content", "Step 1"}, {"status", "completed"}, {"priority", "high"}},
            QJsonObject{{"content", "Step 2"}, {"status", "in_progress"}, {"priority", "medium"}},
            QJsonObject{{"content", "Step 3"}, {"status", "pending"}}
        };
        const ToolResult r = tool->execute({{"todos", todos}});
        QVERIFY2(r.success, qPrintable(r.error));
        const QJsonDocument doc = QJsonDocument::fromJson(r.output.toUtf8());
        QVERIFY(doc.isArray());
        const QJsonArray result = doc.array();
        QCOMPARE(result.size(), 3);
        QCOMPARE(result.at(0).toObject().value("priority").toString(), QStringLiteral("high"));
        QCOMPARE(result.at(2).toObject().value("priority").toString(), QStringLiteral("medium"));
    }

    void todoWriteEmptyList() {
        auto tool = ToolRegistry::instance().findShared("todo_write");
        const ToolResult r = tool->execute({{"todos", QJsonArray{}}});
        QVERIFY(r.success);
        QCOMPARE(r.output, QStringLiteral("[]"));
    }

    void todoWriteRejectsInvalidStatus() {
        auto tool = ToolRegistry::instance().findShared("todo_write");
        const QJsonArray todos{
            QJsonObject{{"content", "Bad"}, {"status", "unknown"}}
        };
        const ToolResult r = tool->execute({{"todos", todos}});
        QVERIFY(!r.success);
        QVERIFY2(r.error.contains(QStringLiteral("invalid status")), qPrintable(r.error));
    }

    void todoWriteRejectsEmptyContent() {
        auto tool = ToolRegistry::instance().findShared("todo_write");
        const QJsonArray todos{
            QJsonObject{{"content", "  "}, {"status", "pending"}}
        };
        const ToolResult r = tool->execute({{"todos", todos}});
        QVERIFY(!r.success);
        QVERIFY2(r.error.contains(QStringLiteral("empty content")), qPrintable(r.error));
    }

    void todoWriteNormalizesInvalidPriority() {
        auto tool = ToolRegistry::instance().findShared("todo_write");
        const QJsonArray todos{
            QJsonObject{{"content", "Task"}, {"status", "pending"}, {"priority", "urgent"}}
        };
        const ToolResult r = tool->execute({{"todos", todos}});
        QVERIFY2(r.success, qPrintable(r.error));
        const QJsonArray result = QJsonDocument::fromJson(r.output.toUtf8()).array();
        QCOMPARE(result.at(0).toObject().value("priority").toString(), QStringLiteral("medium"));
    }

    void todoWriteIsSafeLevel() {
        auto tool = ToolRegistry::instance().findShared("todo_write");
        QVERIFY(tool);
        QCOMPARE(tool->definition().level, ToolLevel::Safe);
    }

    // ── Git tool tests ──
    // git_log / git_show 必须是 Safe 级只读工具：模型要查历史/看提交详情
    // 时不应被迫退到 shell_exec（那样每次都得弹确认）。
    void gitReadOnlyHistoryToolsRegistered() {
        auto log = ToolRegistry::instance().findShared(QStringLiteral("git_log"));
        auto show = ToolRegistry::instance().findShared(QStringLiteral("git_show"));
        QVERIFY2(log, "missing git_log (the model can only fall back to shell_exec)");
        QVERIFY2(show, "missing git_show (the model can only fall back to shell_exec)");
        QCOMPARE(log->definition().level, ToolLevel::Safe);
        QCOMPARE(show->definition().level, ToolLevel::Safe);
        QVERIFY(log->definition().readOnly);
        QVERIFY(show->definition().readOnly);
    }

    void gitLogListsRecentCommits() {
        QTemporaryDir repo;
        QVERIFY(repo.isValid());
        QVERIFY(initRepo(repo.path()));
        QVERIFY(commitFile(repo.path(), QStringLiteral("a.txt"),
                           QByteArray("x"), QStringLiteral("first commit")));

        Sandbox::setProjectDir(repo.path());
        auto tool = ToolRegistry::instance().findShared(QStringLiteral("git_log"));
        QVERIFY(tool);
        const ToolResult r = tool->execute({});
        Sandbox::setProjectDir(QString());

        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY2(r.output.contains(QStringLiteral("first commit")),
                 qPrintable(r.output));
    }

    void gitLogHonorsLimitAndPath() {
        QTemporaryDir repo;
        QVERIFY(repo.isValid());
        QVERIFY(initRepo(repo.path()));
        for (const char* name : {"one", "two", "three"})
            QVERIFY(commitFile(repo.path(),
                               QStringLiteral("%1.txt").arg(QLatin1String(name)),
                               QByteArray(name),
                               QStringLiteral("commit-%1").arg(QLatin1String(name))));

        Sandbox::setProjectDir(repo.path());
        auto tool = ToolRegistry::instance().findShared(QStringLiteral("git_log"));
        QVERIFY(tool);

        const ToolResult two = tool->execute({{QStringLiteral("limit"), 2}});
        QVERIFY2(two.success, qPrintable(two.error));
        QCOMPARE(two.output.count(QStringLiteral("commit-")), 2);

        const ToolResult one = tool->execute(
            {{QStringLiteral("path"), QStringLiteral("one.txt")}});
        QVERIFY2(one.success, qPrintable(one.error));
        QVERIFY2(one.output.contains(QStringLiteral("commit-one")),
                 qPrintable(one.output));
        QVERIFY2(!one.output.contains(QStringLiteral("commit-three")),
                 qPrintable(one.output));
        Sandbox::setProjectDir(QString());
    }

    void gitShowReturnsCommitDetail() {
        QTemporaryDir repo;
        QVERIFY(repo.isValid());
        QVERIFY(initRepo(repo.path()));
        QVERIFY(commitFile(repo.path(), QStringLiteral("a.txt"),
                           QByteArray("hello"), QStringLiteral("show-me")));

        Sandbox::setProjectDir(repo.path());
        auto tool = ToolRegistry::instance().findShared(QStringLiteral("git_show"));
        QVERIFY(tool);
        const ToolResult r = tool->execute(
            {{QStringLiteral("ref"), QStringLiteral("HEAD")}});
        Sandbox::setProjectDir(QString());

        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY2(r.output.contains(QStringLiteral("show-me")), qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("a.txt")), qPrintable(r.output));
    }
    void gitToolsRegisteredWithCorrectLevels() {
        QVERIFY(ToolRegistry::instance().find("git_status"));
        QVERIFY(ToolRegistry::instance().find("git_diff"));
        QVERIFY(ToolRegistry::instance().find("git_commit"));
        QCOMPARE(ToolRegistry::instance().find("git_status")->definition().level, ToolLevel::Safe);
        QCOMPARE(ToolRegistry::instance().find("git_diff")->definition().level, ToolLevel::Safe);
        QCOMPARE(ToolRegistry::instance().find("git_commit")->definition().level, ToolLevel::RequiresConfirm);
    }

    // G1: PlanOnly keys off readOnly. Every workspace-mutating confirm-level
    // tool must leave the flag false; GET-only retrieval declares it true.
    void readOnlyFlagSeparatesRetrievalFromMutation() {
        const QStringList mutating{
            QStringLiteral("write_file"), QStringLiteral("edit_file"),
            QStringLiteral("shell_exec"), QStringLiteral("git_commit")};
        for (const QString& name : mutating) {
            auto tool = ToolRegistry::instance().findShared(name);
            QVERIFY2(tool, qPrintable(name));
            QCOMPARE(tool->definition().level, ToolLevel::RequiresConfirm);
            QVERIFY2(!tool->definition().readOnly,
                     qPrintable(QStringLiteral("%1 must stay blocked in plan mode")
                                    .arg(name)));
        }

        NetworkFetchTool fetch;
        QCOMPARE(fetch.definition().level, ToolLevel::RequiresConfirm);
        QVERIFY(fetch.definition().readOnly);
    }

    void gitStatusFailsOutsideRepo() {
        auto tool = ToolRegistry::instance().findShared("git_status");
        QVERIFY(tool);
        const ToolResult r = tool->execute({});
        QVERIFY(!r.success);
    }

    void gitDiffFailsOutsideRepo() {
        auto tool = ToolRegistry::instance().findShared("git_diff");
        QVERIFY(tool);
        const ToolResult r = tool->execute({});
        QVERIFY(!r.success);
    }

    // 无项目绑定时 git 工具必须给出可执行的提示，而不是让 GitService
    // 回落到 CWD、把 "Not a git repository: .../build" 抛给模型。
    void gitToolsExplainMissingProject() {
        Sandbox::setProjectDir(QString());   // 模拟未绑定项目的会话
        const QStringList names{QStringLiteral("git_status"),
                                QStringLiteral("git_diff"),
                                QStringLiteral("git_commit")};
        for (const QString& name : names) {
            auto tool = ToolRegistry::instance().findShared(name);
            QVERIFY2(tool, qPrintable(name));
            const ToolResult r = tool->execute({{"message", QStringLiteral("x")}});
            QVERIFY2(!r.success, qPrintable(name));
            QVERIFY2(r.error.contains(QStringLiteral("no project bound")),
                     qPrintable(QStringLiteral("%1 的错误没说明缺少项目：%2")
                                    .arg(name, r.error)));
            // 不能把进程 CWD 泄露成"仓库路径"
            QVERIFY2(!r.error.contains(QStringLiteral("Not a git repository")),
                     qPrintable(r.error));
        }
    }

    // list_dir 的空 path / "." 是"列当前目录"，不是参数缺失。
    void listDirTreatsEmptyPathAsCurrentDirectory() {
        createFile(QStringLiteral("visible.txt"), QStringLiteral("x"));

        auto tool = ToolRegistry::instance().findShared("list_dir");
        QVERIFY(tool);

        // 空 path：有项目 → 列项目根
        const ToolResult empty = tool->execute({});
        QVERIFY2(empty.success, qPrintable(empty.error));
        QVERIFY2(empty.output.contains(QStringLiteral("visible.txt")),
                 qPrintable(empty.output));

        // "." 同样指项目根
        const ToolResult dot = tool->execute({{"path", "."}});
        QVERIFY2(dot.success, qPrintable(dot.error));
        QVERIFY2(dot.output.contains(QStringLiteral("visible.txt")),
                 qPrintable(dot.output));

        // 子目录相对路径
        createFile(QStringLiteral("sub/inner.txt"), QStringLiteral("y"));
        const ToolResult sub = tool->execute({{"path", "sub"}});
        QVERIFY2(sub.success, qPrintable(sub.error));
        QVERIFY2(sub.output.contains(QStringLiteral("inner.txt")),
                 qPrintable(sub.output));
    }

    // 无项目时 list_dir 仍要说明"该传什么"，不能只说 "Path is required"。
    void listDirExplainsMissingProject() {
        Sandbox::setProjectDir(QString());
        auto tool = ToolRegistry::instance().findShared("list_dir");
        QVERIFY(tool);
        const ToolResult r = tool->execute({});
        QVERIFY(!r.success);
        QVERIFY2(r.error.contains(QStringLiteral("path")),
                 qPrintable(QStringLiteral("错误应提示传 path：%1").arg(r.error)));
    }

    void gitCommitRejectsEmptyMessage() {
        auto tool = ToolRegistry::instance().findShared("git_commit");
        QVERIFY(tool);
        const ToolResult r = tool->execute({{"message", ""}});
        QVERIFY(!r.success);
        QVERIFY2(r.error.contains(QStringLiteral("message")), qPrintable(r.error));
    }

    // ── G4: checkpoint payloads ──
    void writeFileCapturesCheckpoint() {
        createFile("existing.txt", "old bytes");
        auto tool = ToolRegistry::instance().findShared("write_file");
        QVERIFY(tool);
        const ToolResult r = tool->execute(
            {{"path", "existing.txt"}, {"content", "new bytes"}});
        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY(r.checkpoint.isValid());
        QVERIFY(QFileInfo(r.checkpoint.path).isAbsolute());
        QCOMPARE(QFileInfo(r.checkpoint.path).fileName(), QStringLiteral("existing.txt"));
        QVERIFY(r.checkpoint.existed);
        QVERIFY(!r.checkpoint.truncated);
        QCOMPARE(r.checkpoint.beforeContent, QByteArray("old bytes"));
        // Phase 1: content detail captured with the write.
        QVERIFY2(r.checkpoint.diff.contains(QStringLiteral("-old bytes")),
                 qPrintable(r.checkpoint.diff));
        QVERIFY2(r.checkpoint.diff.contains(QStringLiteral("+new bytes")),
                 qPrintable(r.checkpoint.diff));
        QCOMPARE(r.checkpoint.addedLines, 1);
        QCOMPARE(r.checkpoint.removedLines, 1);
        QVERIFY(!r.checkpoint.diffTruncated);
        QCOMPARE(r.checkpoint.afterHash,
                 QString::fromLatin1(QCryptographicHash::hash(
                     QByteArray("new bytes"), QCryptographicHash::Sha256).toHex()));
    }

    void writeFileCheckpointMarksNewFile() {
        auto tool = ToolRegistry::instance().findShared("write_file");
        QVERIFY(tool);
        const ToolResult r = tool->execute(
            {{"path", "created.txt"}, {"content", "hello"}});
        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY(r.checkpoint.isValid());
        QVERIFY(!r.checkpoint.existed);
        QVERIFY(!r.checkpoint.truncated);
        QVERIFY(r.checkpoint.beforeContent.isEmpty());
        QVERIFY2(r.checkpoint.diff.contains(QStringLiteral("--- /dev/null")),
                 qPrintable(r.checkpoint.diff));
        QVERIFY2(r.checkpoint.diff.contains(QStringLiteral("+hello")),
                 qPrintable(r.checkpoint.diff));
        QCOMPARE(r.checkpoint.addedLines, 1);
        QCOMPARE(r.checkpoint.removedLines, 0);
        QVERIFY(!r.checkpoint.afterHash.isEmpty());
    }

    void oversizedOriginalDegradesToPathOnly() {
        const QByteArray big(1024 * 1024 + 1, 'x');
        {
            QFile file(m_tempDir->path() + QStringLiteral("/big.txt"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(big), big.size());
        }
        auto tool = ToolRegistry::instance().findShared("write_file");
        QVERIFY(tool);
        const ToolResult r = tool->execute(
            {{"path", "big.txt"}, {"content", "replaced"}});
        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY(r.checkpoint.isValid());
        QVERIFY(r.checkpoint.existed);
        QVERIFY(r.checkpoint.truncated);
        QVERIFY(r.checkpoint.beforeContent.isEmpty());
        QVERIFY(r.checkpoint.diffTruncated);
        QVERIFY(r.checkpoint.diff.isEmpty());
        QVERIFY(!r.checkpoint.afterHash.isEmpty());
    }

    // 600 KiB is small enough to capture for undo (1 MiB cap) but over the
    // diff input bound (512 KiB): the row stays restorable, the diff does not.
    void oversizedDiffInputStillCapturesUndoPayload() {
        const QByteArray medium(600 * 1024, 'y');
        {
            QFile file(m_tempDir->path() + QStringLiteral("/medium.txt"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(medium), medium.size());
        }
        auto tool = ToolRegistry::instance().findShared("write_file");
        QVERIFY(tool);
        const ToolResult r = tool->execute(
            {{"path", "medium.txt"}, {"content", "small"}});
        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY(!r.checkpoint.truncated);
        QCOMPARE(r.checkpoint.beforeContent.size(), medium.size());
        QVERIFY(r.checkpoint.diffTruncated);
        QVERIFY(r.checkpoint.diff.isEmpty());
        QCOMPARE(r.checkpoint.addedLines, 0);
        QCOMPARE(r.checkpoint.removedLines, 0);
    }

    void editFileCapturesExactBeforeBytes() {
        createFile("edit.txt", "hello world");
        auto tool = ToolRegistry::instance().findShared("edit_file");
        QVERIFY(tool);
        const ToolResult r = tool->execute({{"path", "edit.txt"},
                                            {"old_string", "world"},
                                            {"new_string", "nexus"}});
        QVERIFY2(r.success, qPrintable(r.error));
        QVERIFY(r.checkpoint.isValid());
        QVERIFY(r.checkpoint.existed);
        QVERIFY(!r.checkpoint.truncated);
        QCOMPARE(r.checkpoint.beforeContent, QByteArray("hello world"));
        QVERIFY2(r.checkpoint.diff.contains(QStringLiteral("-hello world")),
                 qPrintable(r.checkpoint.diff));
        QVERIFY2(r.checkpoint.diff.contains(QStringLiteral("+hello nexus")),
                 qPrintable(r.checkpoint.diff));
        QCOMPARE(r.checkpoint.addedLines, 1);
        QCOMPARE(r.checkpoint.removedLines, 1);
        QVERIFY(!r.checkpoint.diffTruncated);
        QCOMPARE(r.checkpoint.afterHash,
                 QString::fromLatin1(QCryptographicHash::hash(
                     QByteArray("hello nexus"), QCryptographicHash::Sha256).toHex()));
    }

    void failedEditHasNoCheckpoint() {
        createFile("edit-fail.txt", "hello");
        auto tool = ToolRegistry::instance().findShared("edit_file");
        QVERIFY(tool);
        const ToolResult r = tool->execute({{"path", "edit-fail.txt"},
                                            {"old_string", "missing"},
                                            {"new_string", "x"}});
        QVERIFY(!r.success);
        QVERIFY(!r.checkpoint.isValid());
    }

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;

    // 临时仓库夹具：git 只读工具需要真实仓库才能验证输出形态。
    bool initRepo(const QString& path) {
        const auto run = [&](const QStringList& args) {
            QProcess p;
            p.setWorkingDirectory(path);
            p.start(QStringLiteral("git"), args);
            return p.waitForFinished(10000) && p.exitCode() == 0;
        };
        return run({QStringLiteral("init"), QStringLiteral("-q")})
            && run({QStringLiteral("config"), QStringLiteral("user.email"),
                    QStringLiteral("t@e.c")})
            && run({QStringLiteral("config"), QStringLiteral("user.name"),
                    QStringLiteral("T")});
    }

    bool commitFile(const QString& repoPath, const QString& name,
                    const QByteArray& content, const QString& message) {
        QFile file(QDir(repoPath).filePath(name));
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(content);
        file.close();
        const auto run = [&](const QStringList& args) {
            QProcess p;
            p.setWorkingDirectory(repoPath);
            p.start(QStringLiteral("git"), args);
            return p.waitForFinished(10000) && p.exitCode() == 0;
        };
        return run({QStringLiteral("add"), QStringLiteral("-A")})
            && run({QStringLiteral("commit"), QStringLiteral("-qm"), message});
    }

    void createFile(const QString& relativePath, const QString& content) {
        const QString fullPath = m_tempDir->path() + QLatin1Char('/') + relativePath;
        QDir().mkpath(QFileInfo(fullPath).absolutePath());
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << content;
            file.close();
        }
    }
};

QTEST_MAIN(TestTools)
#include "test_tools.moc"
