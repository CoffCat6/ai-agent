// Sandbox path policy: traversal/bypass resistance and boundary correctness.
#include "core/tools/Sandbox.h"

#include <QtTest>
#include <QStandardPaths>

using namespace NexusAI;

class TestSandbox : public QObject {
    Q_OBJECT

private slots:
    void allowsUserWrites();
    void blocksTraversalIntoSystemDirs();
    void blocksSystemPaths();
    void boundaryAwarePrefixMatching();
    void blocksRelativeEscape();
    void validateToolCallPolicy();
};

void TestSandbox::allowsUserWrites() {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QVERIFY2(!docs.isEmpty() && !home.isEmpty(), "standard locations must resolve");
    QVERIFY(Sandbox::validatePath(docs + "/report.txt", true));
    QVERIFY(Sandbox::validatePath(home + "/sub/dir/file.txt", true));
    QVERIFY(Sandbox::validatePath(docs + "/report.txt", false)); // reads allowed too
}

void TestSandbox::blocksTraversalIntoSystemDirs() {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // ".." segments must be collapsed before the prefix check (bypass fixed)
    QVERIFY(!Sandbox::validatePath(docs + "\\..\\..\\..\\Windows\\evil.dll", true));
#ifndef Q_OS_WIN
    // POSIX system paths only resolve as such on Unix; on Windows a leading
    // "/" is a drive-relative path (e.g. "D:\etc\passwd") and not a system dir
    QVERIFY(!Sandbox::validatePath("/etc/../etc/shadow", false));
    QVERIFY(!Sandbox::validatePath("/usr/../etc/passwd", false));
#endif
}

void TestSandbox::blocksSystemPaths() {
    QVERIFY(!Sandbox::validatePath("C:\\Windows\\System32\\config", false));
    QVERIFY(!Sandbox::validatePath("C:\\Windows\\explorer.exe", true));
#ifndef Q_OS_WIN
    QVERIFY(!Sandbox::validatePath("/etc/passwd", false));
#endif
    QVERIFY(!Sandbox::validatePath("C:\\Program Files\\app\\data.txt", true));
}

void TestSandbox::boundaryAwarePrefixMatching() {
    // A sibling of a blocked directory must not be over-blocked by the
    // naive prefix check ("C:\Program Files2" vs blocked "C:\Program Files")
    QVERIFY(Sandbox::validatePath("C:\\Program Files2\\someapp\\data.txt", false));
    // ...but writes outside user directories stay rejected regardless
    QVERIFY(!Sandbox::validatePath("C:\\Program Files2\\evil.txt", true));
}

void TestSandbox::blocksRelativeEscape() {
    // Relative paths are resolved against the process working directory
    // before matching, so traversal outside user dirs is rejected
    QVERIFY(!Sandbox::validatePath("..\\..\\Windows\\evil.dll", true));
}

void TestSandbox::validateToolCallPolicy() {
    ToolDefinition write;
    write.name = "write_file";
    write.level = ToolLevel::RequiresConfirm;
    QVERIFY(!Sandbox::validateToolCall(write,
        QJsonObject{{"path", "C:\\Windows\\evil.dll"}}));
    QVERIFY(Sandbox::validateToolCall(write,
        QJsonObject{{"path",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/ok.txt"}}));

    ToolDefinition read;
    read.name = "read_file";
    read.level = ToolLevel::Safe;
    QVERIFY(Sandbox::validateToolCall(read, QJsonObject{{"path", "C:\\some\\file.txt"}}));

    ToolDefinition dangerous;
    dangerous.name = "rm_rf";
    dangerous.level = ToolLevel::Dangerous;
    QVERIFY(!Sandbox::validateToolCall(dangerous, {}));
}

QTEST_GUILESS_MAIN(TestSandbox)
#include "test_sandbox.moc"
