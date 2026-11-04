// Unit tests for the glob and grep built-in tools.
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"
#include "core/tools/FileEditUtil.h"
#include "core/tools/NetworkFetchTool.h"

#include <QDir>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QTextStream>
#include <QSemaphore>
#include <QElapsedTimer>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>

#include <atomic>

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

    // ── B2-A: 同路径写工具串行化 ──
    // 读改写竞态的最小复现：两个 edit_file 同时读同一份旧内容，后写者用
    // 自己的内存副本整文件覆盖，先写者的修改被无声丢弃。锁生效后两次
    // 修改都必须留在最终文件里。
    void concurrentEditFileSamePathKeepsBothEdits() {
        const QString filler = makeLargeText("filler line", 60000);
        createFile("race.txt", filler + QStringLiteral("alpha\n") + filler
                                   + QStringLiteral("beta\n"));

        auto tool = ToolRegistry::instance().findShared("edit_file");
        QVERIFY(tool);
        QSemaphore start(0);
        const auto runEdit = [&](const QString& from, const QString& to) {
            return QtConcurrent::run([tool, &start, from, to]() {
                start.acquire();
                return tool->execute({{"path", "race.txt"},
                                      {"old_string", from},
                                      {"new_string", to}});
            });
        };
        QFuture<ToolResult> a = runEdit(QStringLiteral("alpha"),
                                       QStringLiteral("ALPHA"));
        QFuture<ToolResult> b = runEdit(QStringLiteral("beta"),
                                       QStringLiteral("BETA"));
        start.release(2);
        a.waitForFinished();
        b.waitForFinished();
        QVERIFY2(a.result().success, qPrintable(a.result().error));
        QVERIFY2(b.result().success, qPrintable(b.result().error));

        QFile file(QDir(m_tempDir->path()).filePath(QStringLiteral("race.txt")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString finalContent = QString::fromUtf8(file.readAll());
        QVERIFY2(finalContent.contains(QStringLiteral("ALPHA")),
                 "并发 edit 丢失了 ALPHA 修改（后写者覆盖了先写者）");
        QVERIFY2(finalContent.contains(QStringLiteral("BETA")),
                 "并发 edit 丢失了 BETA 修改（后写者覆盖了先写者）");
        QVERIFY(!finalContent.contains(QStringLiteral("alpha")));
        QVERIFY(!finalContent.contains(QStringLiteral("beta")));
    }

    // 串行化之后，第二个使用同一锚点的 edit 必须发现锚点已被前一个消费，
    // 返回可读的「请重读」错误，而不是基于过期内容再次覆盖。
    void concurrentEditOnSameAnchorFailsReadably() {
        const QString filler = makeLargeText("anchor filler", 60000);
        createFile("anchor.txt", filler + QStringLiteral("anchor_value\n") + filler);

        auto tool = ToolRegistry::instance().findShared("edit_file");
        QVERIFY(tool);
        QSemaphore start(0);
        const auto runEdit = [&](const QString& replacement) {
            return QtConcurrent::run([tool, &start, replacement]() {
                start.acquire();
                return tool->execute({{"path", "anchor.txt"},
                                      {"old_string", "anchor_value"},
                                      {"new_string", replacement}});
            });
        };
        QFuture<ToolResult> a = runEdit(QStringLiteral("winner_a"));
        QFuture<ToolResult> b = runEdit(QStringLiteral("winner_b"));
        start.release(2);
        a.waitForFinished();
        b.waitForFinished();

        int successes = 0;
        QString loserError;
        for (const QFuture<ToolResult>* future : {&a, &b}) {
            const ToolResult r = future->result();
            if (r.success) ++successes;
            else loserError = r.error;
        }
        QCOMPARE(successes, 1);
        QVERIFY2(loserError.contains(QStringLiteral("old_string not found")),
                 qPrintable(loserError));
        QVERIFY2(loserError.contains(QStringLiteral("re-read"), Qt::CaseInsensitive),
                 qPrintable(loserError));
    }

    // write_file 是全量覆盖，语义上允许后写赢；但两个执行必须串行完成，
    // 第二个写者的 checkpoint 必须看到第一个写者的结果（existed=true），
    // 而不是两者都认为目标文件不存在。
    void concurrentWriteFileSamePathSerializes() {
        const QString contentA(4 * 1024 * 1024, QLatin1Char('A'));
        const QString contentB(4 * 1024 * 1024, QLatin1Char('B'));

        auto tool = ToolRegistry::instance().findShared("write_file");
        QVERIFY(tool);
        QSemaphore start(0);
        const auto runWrite = [&](const QString& content) {
            return QtConcurrent::run([tool, &start, content]() {
                start.acquire();
                return tool->execute({{"path", "order.txt"},
                                      {"content", content}});
            });
        };
        QFuture<ToolResult> a = runWrite(contentA);
        QFuture<ToolResult> b = runWrite(contentB);
        start.release(2);
        a.waitForFinished();
        b.waitForFinished();
        const ToolResult ra = a.result();
        const ToolResult rb = b.result();
        QVERIFY2(ra.success, qPrintable(ra.error));
        QVERIFY2(rb.success, qPrintable(rb.error));

        // 恰好一个执行把目标当作新文件创建；另一个在其后看到已存在的文件。
        QCOMPARE((!ra.checkpoint.existed ? 1 : 0)
                     + (!rb.checkpoint.existed ? 1 : 0), 1);
        QVERIFY(ra.checkpoint.existed || rb.checkpoint.existed);

        QFile file(QDir(m_tempDir->path()).filePath(QStringLiteral("order.txt")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray finalBytes = file.readAll();
        QCOMPARE(finalBytes.size(), contentA.toUtf8().size());
        QVERIFY(finalBytes == contentA.toUtf8() || finalBytes == contentB.toUtf8());
    }

    // 同一规范路径的不同写法（相对 vs 绝对）必须命中同一把锁。
    void pathWriteGuardSerializesSamePathAcrossSpellings() {
        createFile("guard.txt", "x");
        const QString absolute =
            QDir(m_tempDir->path()).filePath(QStringLiteral("guard.txt"));

        QSemaphore firstHeld(0);
        QSemaphore releaseFirst(0);
        std::atomic<bool> secondEntered{false};

        QFuture<void> first = QtConcurrent::run([&]() {
            PathWriteGuard guard(QStringLiteral("guard.txt"));
            firstHeld.release();
            releaseFirst.acquire();
        });
        if (!firstHeld.tryAcquire(1, 5000)) {
            releaseFirst.release();
            QFAIL("first guard never acquired");
        }

        QFuture<void> second = QtConcurrent::run([&]() {
            PathWriteGuard guard(absolute);   // 同一文件的绝对路径写法
            secondEntered.store(true);
        });
        QTest::qWait(250);
        const bool blocked = !secondEntered.load();
        releaseFirst.release();
        first.waitForFinished();
        second.waitForFinished();
        QVERIFY2(blocked, "同一规范路径的两个 guard 必须串行");
    }

    // 不同路径不能因为共享锁表而互相阻塞（保持并行）。
    void pathWriteGuardDoesNotSerializeDifferentPaths() {
        createFile("a.txt", "a");
        createFile("b.txt", "b");

        QSemaphore firstHeld(0);
        QSemaphore releaseFirst(0);
        std::atomic<bool> secondEntered{false};

        QFuture<void> first = QtConcurrent::run([&]() {
            PathWriteGuard guard(QStringLiteral("a.txt"));
            firstHeld.release();
            releaseFirst.acquire();
        });
        if (!firstHeld.tryAcquire(1, 5000)) {
            releaseFirst.release();
            QFAIL("first guard never acquired");
        }

        QFuture<void> second = QtConcurrent::run([&]() {
            PathWriteGuard guard(QStringLiteral("b.txt"));
            secondEntered.store(true);
        });
        QElapsedTimer clock;
        clock.start();
        while (!secondEntered.load() && clock.elapsed() < 2000)
            QThread::msleep(5);
        const bool overlapped = secondEntered.load();
        releaseFirst.release();
        first.waitForFinished();
        second.waitForFinished();
        QVERIFY2(overlapped, "不同路径的 guard 不应互相阻塞");
    }

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;

    // 把竞态窗口拉长到毫秒级：小文件读改写太快，两个执行可能自然错开，
    // 无法稳定复现无锁时的丢失更新。
    static QString makeLargeText(const QString& linePrefix, int lines) {
        QString text;
        text.reserve(lines * 32);
        for (int i = 0; i < lines; ++i)
            text += linePrefix + QStringLiteral(" %1 abcdefghijklmnop\n").arg(i);
        return text;
    }

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
