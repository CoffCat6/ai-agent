// Unit tests for the glob and grep built-in tools.
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    void gitToolsRegisteredWithCorrectLevels() {
        QVERIFY(ToolRegistry::instance().find("git_status"));
        QVERIFY(ToolRegistry::instance().find("git_diff"));
        QVERIFY(ToolRegistry::instance().find("git_commit"));
        QCOMPARE(ToolRegistry::instance().find("git_status")->definition().level, ToolLevel::Safe);
        QCOMPARE(ToolRegistry::instance().find("git_diff")->definition().level, ToolLevel::Safe);
        QCOMPARE(ToolRegistry::instance().find("git_commit")->definition().level, ToolLevel::RequiresConfirm);
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

    void gitCommitRejectsEmptyMessage() {
        auto tool = ToolRegistry::instance().findShared("git_commit");
        QVERIFY(tool);
        const ToolResult r = tool->execute({{"message", ""}});
        QVERIFY(!r.success);
        QVERIFY2(r.error.contains(QStringLiteral("message")), qPrintable(r.error));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;

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
