// Sandbox path policy: traversal/bypass resistance and boundary correctness.
#include "core/tools/Sandbox.h"
#include "core/tools/ToolRegistry.h"

#include <QtTest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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
    // Batch 5: read policy (audit P1-8, SEC-501)
    void readsRestrictedToUserDirectories();
    void fullDiskReadPolicyOptIn();
    // Batch 19: edit_file tool (FEAT-1901/1903) - driven through the
    // registry because tool classes are internal to ToolRegistry.cpp
    void editFileUniqueReplace();
    void editFileMultipleMatchRejected();
    void editFileNotFound();
    void editFileOutsideWhitelistRejected();
    void editFileCrlfBoundary();
    void editFileDirectoryTargetFailsCleanly();
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
    // naive prefix check ("C:\Program Files2" vs blocked "C:\Program Files").
    // READS outside the user allowlist are rejected by the default policy
    // (audit P1-8) - only writes used to be rejected here.
    QVERIFY(!Sandbox::validatePath("C:\\Program Files2\\someapp\\data.txt", false));
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
    // Reads outside the user allowlist are rejected by the default policy
    // (audit P1-8); only allowlisted reads pass.
    QVERIFY(!Sandbox::validateToolCall(read, QJsonObject{{"path", "C:\\some\\file.txt"}}));

    ToolDefinition dangerous;
    dangerous.name = "rm_rf";
    dangerous.level = ToolLevel::Dangerous;
    QVERIFY(!Sandbox::validateToolCall(dangerous, {}));
}

// SEC-501: the default read policy restricts reads to the user allowlist.
// Home-root-wide reads kept the audited exposure alive (e.g. ~/.ssh,
// browser profiles live under the home directory), so reads use a NARROWER
// allowlist than writes: Documents/Downloads/Desktop/app data - NOT all of
// the home directory.
void TestSandbox::readsRestrictedToUserDirectories() {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    QVERIFY(Sandbox::validatePath(docs + "/notes.txt", false));      // allowlisted
    QVERIFY(!Sandbox::validatePath(temp + "/other.txt", false));     // outside allowlist
    QVERIFY(!Sandbox::validatePath(home + "/.ssh/id_rsa", false));   // sensitive, blocked
    QVERIFY(!Sandbox::validatePath("C:\\ProgramData\\secret.txt", false));
}

// Opt-in full-disk reads restore the legacy behavior (system dirs stay
// blocked); the test restores the default policy afterwards.
void TestSandbox::fullDiskReadPolicyOptIn() {
    Sandbox::setReadPolicy(Sandbox::ReadPolicy::FullDisk);
    QVERIFY(Sandbox::validatePath("C:\\ProgramData\\secret.txt", false));
    QVERIFY(Sandbox::validatePath(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation) + "/other.txt", false));
    QVERIFY(!Sandbox::validatePath("C:\\Windows\\System32\\config", false)); // still blocked
    Sandbox::setReadPolicy(Sandbox::ReadPolicy::UserDirectories);
    QVERIFY(!Sandbox::validatePath("C:\\ProgramData\\secret.txt", false));
}

// ── Batch 19: edit_file tool ──────────────────────────────
// Tool classes are internal to ToolRegistry.cpp, so tests drive execute()
// through the registry singleton. Temp dirs are created under Documents -
// a plain QTemporaryDir lands in TempLocation, which the sandbox refuses.

namespace {

QTemporaryDir sandboxTempDir() {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QTemporaryDir(docs + QStringLiteral("/nexusai-sandboxtests-XXXXXX"));
}

ToolResult runEdit(const QString& path, const QString& oldString, const QString& newString) {
    ITool* tool = ToolRegistry::instance().find(QStringLiteral("edit_file"));
    if (!tool) return ToolResult::fail("edit_file not registered");
    return tool->execute(QJsonObject{{"path", path},
                                     {"old_string", oldString},
                                     {"new_string", newString}});
}

QString readFileText(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

} // namespace

void TestSandbox::editFileUniqueReplace() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("edit.txt");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("alpha\nbeta\ngamma\n");
    f.close();

    const ToolResult r = runEdit(path, QStringLiteral("beta"), QStringLiteral("BETA"));
    QVERIFY(r.success);
    QVERIFY(r.output.contains(QStringLiteral("Replaced 1 occurrence")));
    QCOMPARE(readFileText(path), QStringLiteral("alpha\nBETA\ngamma\n"));
}

void TestSandbox::editFileMultipleMatchRejected() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("multi.txt");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x one x two x");
    f.close();

    const ToolResult r = runEdit(path, QStringLiteral("x"), QStringLiteral("y"));
    QVERIFY(!r.success);
    QVERIFY2(r.error.contains(QStringLiteral("matches 3 locations")),
             qPrintable(r.error));
    QCOMPARE(readFileText(path), QStringLiteral("x one x two x"));   // untouched
}

void TestSandbox::editFileNotFound() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("nf.txt");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("abc");
    f.close();

    const ToolResult r = runEdit(path, QStringLiteral("zzz"), QStringLiteral("y"));
    QVERIFY(!r.success);
    QVERIFY2(r.error.contains(QStringLiteral("old_string not found")), qPrintable(r.error));
    QCOMPARE(readFileText(path), QStringLiteral("abc"));             // untouched
}

void TestSandbox::editFileOutsideWhitelistRejected() {
    // Writes allow user directories (including the whole home), so the
    // rejection test needs a system path - the tool must refuse BEFORE
    // touching the filesystem (no file created). Platform-appropriate path,
    // same convention as blocksSystemPaths().
#ifdef Q_OS_WIN
    const QString path = QStringLiteral("C:\\Windows\\Temp\\nexusai-edit-blocked.txt");
#else
    const QString path = QStringLiteral("/etc/nexusai-edit-blocked.txt");
#endif

    const ToolResult r = runEdit(path, QStringLiteral("secret"), QStringLiteral("nope"));
    QVERIFY(!r.success);
    QVERIFY2(r.error.contains(QStringLiteral("sandbox policy")), qPrintable(r.error));
    QVERIFY2(!QFile::exists(path), "blocked edit must not create the file");
}

// Exact-match semantics: line endings are NOT normalized, so an LF old_string
// cannot hit a CRLF file; a CRLF old_string replaces and preserves the rest.
void TestSandbox::editFileCrlfBoundary() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("crlf.txt");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("line1\r\nline2\r\nline3\r\n");
    f.close();

    const ToolResult lfMiss = runEdit(path, QStringLiteral("line1\nline2"),
                                      QStringLiteral("replaced"));
    QVERIFY(!lfMiss.success);
    QVERIFY2(lfMiss.error.contains(QStringLiteral("old_string not found")),
             qPrintable(lfMiss.error));

    const ToolResult crlfHit = runEdit(path, QStringLiteral("line1\r\nline2"),
                                       QStringLiteral("LINE1"));
    QVERIFY(crlfHit.success);
    QCOMPARE(readFileText(path), QStringLiteral("LINE1\r\nline3\r\n"));
}

// Batch 21 (A2) related: a directory target must fail cleanly. Note the
// failure surfaces at the pre-write read step (open(ReadOnly) on a directory
// fails on most platforms), NOT at the QSaveFile commit step - commit/short-
// write failures are not injectable without an injectable write seam, so the
// atomic-write path itself stays covered only by code review. What this test
// pins: the tool must fail and never report a successful replace.
void TestSandbox::editFileDirectoryTargetFailsCleanly() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString target = dir.filePath("targetdir");
    QVERIFY(QDir().mkpath(target));

    const ToolResult r = runEdit(target, QStringLiteral("x"), QStringLiteral("y"));
    QVERIFY2(!r.success, qPrintable(r.error));
}

QTEST_GUILESS_MAIN(TestSandbox)
#include "test_sandbox.moc"
