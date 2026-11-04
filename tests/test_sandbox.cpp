// Sandbox path policy: traversal/bypass resistance and boundary correctness.
#include "core/tools/Sandbox.h"
#include "core/tools/ToolRegistry.h"

#include <QtTest>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

using namespace NexusAI;

class TestSandbox : public QObject {
    Q_OBJECT

private slots:
    void allowsUserWrites();
    void blocksTraversalIntoSystemDirs();
    void blocksSystemPaths();
    // docs/37 B4-D: OS-provided system dirs, ".." / reserved names, and the
    // canonical path returned for TOCTOU-safe writes.
    void systemDirectoriesFollowEnvironment();
    void rejectsParentSegments();
    void rejectsWindowsReservedNames();
    void validateResolvedReturnsCanonicalTarget();
    void boundaryAwarePrefixMatching();
    void blocksRelativeEscape();
    // docs/17 A1/A4: validateToolCall removed (dead code) - the policy
    // is now asserted directly on validatePath incl. the reason out-param.
    void denyReasonsAreDistinguishable();
    void projectDirExtendsReadAllowlist();
    // docs/17 P0-2/P1-4/P1-5: relative-path anchoring, project write gate,
    // per-task execution snapshot.
    void relativePathsAnchorToProjectRoot();
    void projectWriteGate();
    void executionScopePinsProjectDir();
    void userDenyListBlocksReadsAndWrites();
    void denyListPrecedesFullDiskAndProject();
    void defaultDenyListEntries();
    // Batch 5: read policy (audit P1-8, SEC-501)
    void readsRestrictedToUserDirectories();
    void fullDiskReadPolicyOptIn();
    // Batch 19: edit_file tool (FEAT-1901/1903) - driven through the
    // registry because tool classes are internal to ToolRegistry.cpp
    void editFileUniqueReplace();
    void editFileMultipleMatchRejected();
    void editFileReplaceAll();
    void editFileNotFound();
    // docs/34 P3.3: 锚点失效时的自动重定位建议。
    void editFileNotFoundSuggestsRelocation();
    void editFileOutsideWhitelistRejected();
    void editFileCrlfBoundary();
    void editFileDirectoryTargetFailsCleanly();
    // Encoding fallback: read_file must handle non-UTF-8 files gracefully
    void readFileUtf8RoundTrip();
    void readFileNonUtf8FallsBackToLocal();
    // docs/22 F4: write-path link resolution
    void devicePathsAreRejected();
    void junctionParentChainResolvedForWrites();
    // docs/23 G4: write_file is atomic and byte-exact (no CRLF translation)
    void writeFileRoundTripsLfBytes();
    void writeFileToDirectoryFailsWithOpenError();
    // docs/24 H3: read_file refuses oversized files before reading
    void readFileOversizeRefusedBeforeRead();
    // docs/24 H7: credential-shaped env vars never reach the child process
    void shellExecStripsSensitiveEnvironment();
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
    // docs/37 B4-D: system dirs come from the OS (SystemRoot/ProgramFiles),
    // so this stays valid on non-C: system drives and custom SystemRoot.
    const QString systemRoot = qEnvironmentVariable("SystemRoot");
    if (!systemRoot.isEmpty()) {
        QVERIFY(!Sandbox::validatePath(
            QDir(systemRoot).filePath(QStringLiteral("System32/config")), false));
        QVERIFY(!Sandbox::validatePath(
            QDir(systemRoot).filePath(QStringLiteral("explorer.exe")), true));
    }
    QVERIFY(!Sandbox::validatePath("C:\\Windows\\System32\\config", false));
    QVERIFY(!Sandbox::validatePath("C:\\Windows\\explorer.exe", true));
#ifndef Q_OS_WIN
    QVERIFY(!Sandbox::validatePath("/etc/passwd", false));
#endif
    QVERIFY(!Sandbox::validatePath("C:\\Program Files\\app\\data.txt", true));
}

// docs/37 B4-D: a system drive other than C: (or a custom SystemRoot) must
// still be protected — the block list follows the environment, not a
// hard-coded drive letter. Case-insensitive like the rest of the policy.
void TestSandbox::systemDirectoriesFollowEnvironment() {
#ifdef Q_OS_WIN
    const QByteArray oldRoot = qgetenv("SystemRoot");
    const QByteArray oldPf = qgetenv("ProgramFiles");
    const QByteArray oldPf86 = qgetenv("ProgramFiles(x86)");
    const QByteArray oldData = qgetenv("ProgramData");
    const auto restore = [](const char* name, const QByteArray& value) {
        if (value.isNull()) qunsetenv(name);
        else qputenv(name, value);
    };

    const QString fakeRoot = QStringLiteral("Q:/nexusai_fake_sysroot");
    const QString fakePf = QStringLiteral("Q:/nexusai_fake_program_files");
    const QString fakeData = QStringLiteral("Q:/nexusai_fake_program_data");
    qputenv("SystemRoot", fakeRoot.toUtf8());
    qputenv("ProgramFiles", fakePf.toUtf8());
    qputenv("ProgramFiles(x86)", fakePf.toUtf8());
    qputenv("ProgramData", fakeData.toUtf8());

    Sandbox::DenyReason reason = Sandbox::DenyReason::None;
    QVERIFY(!Sandbox::validatePath(
        fakeRoot + QStringLiteral("/System32/evil.dll"), false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);
    QVERIFY(!Sandbox::validatePath(
        fakePf + QStringLiteral("/app/data.txt"), true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);
    QVERIFY(!Sandbox::validatePath(
        fakeData + QStringLiteral("/secret.txt"), false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);
    // Windows path comparison is case-insensitive.
    QVERIFY(!Sandbox::validatePath(
        QStringLiteral("q:/NEXUSAI_FAKE_SYSROOT/system32/EVIL.DLL"),
        false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);

    restore("SystemRoot", oldRoot);
    restore("ProgramFiles", oldPf);
    restore("ProgramFiles(x86)", oldPf86);
    restore("ProgramData", oldData);
#else
    QSKIP("drive letters / SystemRoot are Windows-specific");
#endif
}

// docs/37 B4-D: ".." is rejected before lexical collapse. CleanPath collapses
// ".." before resolving junctions, which is the wrong order on Windows:
// opening "proj\\junction\\..\\evil" resolves the junction first and can land
// outside the project while looking safe lexically.
void TestSandbox::rejectsParentSegments() {
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    Sandbox::DenyReason reason = Sandbox::DenyReason::None;
    QVERIFY(!Sandbox::validatePath(docs + "/a/../b.txt", true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::Traversal);
    QVERIFY(!Sandbox::validatePath(docs + "\\..\\..\\Windows\\evil.dll",
                                   false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::Traversal);
    // ".." that is part of a file name is not a path segment.
    QVERIFY(Sandbox::validatePath(docs + "/notes..txt", true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::None);
}

// docs/37 B4-D: Win32 reserved device names can resolve to device objects
// (or simply be uncreatable); they are rejected deterministically.
void TestSandbox::rejectsWindowsReservedNames() {
#ifdef Q_OS_WIN
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    Sandbox::DenyReason reason = Sandbox::DenyReason::None;
    QVERIFY(!Sandbox::validatePath(docs + "/CON.txt", true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::InvalidPath);
    QVERIFY(!Sandbox::validatePath(docs + "/sub/AUX", false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::InvalidPath);
    QVERIFY(!Sandbox::validatePath(docs + "/lpt9.log", true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::InvalidPath);
    // Similar-looking ordinary names stay allowed.
    QVERIFY(Sandbox::validatePath(docs + "/console.txt", true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::None);
#else
    QSKIP("reserved device names are Windows-specific");
#endif
}

// docs/37 B4-D: validatePathResolved returns the canonical path that was
// checked, so a write tool can open THAT path instead of the raw link text.
// Not-yet-existing targets resolve parent-canonical + lexical file name.
void TestSandbox::validateResolvedReturnsCanonicalTarget() {
    // Declared before the shared helper namespace below, so use an inline
    // temp dir: must live under Documents (a write-allowlisted location).
    QTemporaryDir dir(QStandardPaths::writableLocation(
                          QStandardPaths::DocumentsLocation)
                      + QStringLiteral("/nexusai-sandboxcanon-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString sub = dir.filePath(QStringLiteral("canon"));
    QVERIFY(QDir().mkpath(sub));

    const auto norm = [](const QString& p) {
        return QDir::fromNativeSeparators(QDir::cleanPath(p));
    };

    QString resolved;
    Sandbox::DenyReason reason = Sandbox::DenyReason::SystemPath;
    QVERIFY(Sandbox::validatePathResolved(sub + "/new.txt", true,
                                          &resolved, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::None);
    QVERIFY(!resolved.isEmpty());
    QCOMPARE(QFileInfo(resolved).fileName(), QStringLiteral("new.txt"));
    QCOMPARE(norm(QFileInfo(resolved).absolutePath())
                 .compare(norm(sub), Qt::CaseInsensitive), 0);

    // Missing intermediate directories: canonical parent + lexical tail.
    QVERIFY(Sandbox::validatePathResolved(sub + "/nested/deeper/x.txt", true,
                                          &resolved, &reason));
    QVERIFY2(norm(resolved).endsWith(QStringLiteral("/nested/deeper/x.txt"),
                                     Qt::CaseInsensitive),
             qPrintable(resolved));

    // Denial must clear the out-param — never hand a stale path to open().
    resolved = QStringLiteral("stale");
    QVERIFY(!Sandbox::validatePathResolved(QStringLiteral("C:\\Windows\\evil.txt"),
                                           true, &resolved, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);
    QVERIFY(resolved.isEmpty());

    // The ordinary allowlisted write path still resolves and passes.
    QVERIFY(Sandbox::validatePathResolved(sub + "/ok.txt", true,
                                          &resolved, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::None);
    QVERIFY(!resolved.isEmpty());
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

// docs/17 A4/A5: the reason out-param must tell the layers apart so the
// tool error message can point the user at the right escape hatch.
void TestSandbox::denyReasonsAreDistinguishable() {
    Sandbox::DenyReason reason = Sandbox::DenyReason::None;

    // System directories: hard-blocked, own reason.
    QVERIFY(!Sandbox::validatePath(QStringLiteral("C:\\Windows\\evil.dll"),
                                   true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);
    QVERIFY(!Sandbox::denialHint(reason).isEmpty());

    // Read outside every allow layer (default read policy).
    QVERIFY(!Sandbox::validatePath(QStringLiteral("C:\\some\\file.txt"),
                                   false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::ReadPolicy);

    // Write outside the write allowlist (Temp lives under Home, so use a
    // non-existent path off the home tree; canonicalize falls back to the
    // lexical absolute form for validation).
    QVERIFY(!Sandbox::validatePath(QStringLiteral("Q:/nexusai_nowhere/x.txt"),
                                   true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::WritePolicy);

    // Allowlisted read reports None.
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QVERIFY(Sandbox::validatePath(docs + "/notes.txt", false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::None);
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
    // docs/37 B4-D: ProgramData is an OS system directory now (it used to be
    // allowed here because only C:\\Windows / Program Files were hard-coded).
    const QString outside =
        QStringLiteral("Q:/nexusai_fulldisk_nowhere/x.txt");
    QVERIFY(!Sandbox::validatePath(outside, false));

    Sandbox::setReadPolicy(Sandbox::ReadPolicy::FullDisk);
    QVERIFY(Sandbox::validatePath(outside, false));
    QVERIFY(Sandbox::validatePath(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation) + "/other.txt", false));
    QVERIFY(!Sandbox::validatePath("C:\\Windows\\System32\\config", false)); // still blocked
    QVERIFY(!Sandbox::validatePath("C:\\ProgramData\\secret.txt", false));   // still blocked
    Sandbox::setReadPolicy(Sandbox::ReadPolicy::UserDirectories);
    QVERIFY(!Sandbox::validatePath(outside, false));
    QVERIFY(!Sandbox::validatePath("C:\\ProgramData\\secret.txt", false));
}

// docs/17 A0/A1: the active project's root enters the READ allow layer
// (writes stay untouched - project dirs are read-only for the model).
void TestSandbox::projectDirExtendsReadAllowlist() {
    // Home-off virtual project dir (need not exist: canonicalize falls back
    // to the lexical absolute form). QTemporaryDir would land under Temp,
    // which is inside Home and thus already write-allowlisted.
    const QString root = QStringLiteral("Q:/nexusai_proj_test");

    // Baseline: outside the user allowlist -> rejected.
    QVERIFY(!Sandbox::validatePath(root + "/src/a.txt", false));

    Sandbox::setProjectDir(root);
    QVERIFY(Sandbox::validatePath(root + "/src/a.txt", false));
    QVERIFY(Sandbox::validatePath(root, false));
    // Sibling boundary: project does not authorize its siblings.
    QVERIFY(!Sandbox::validatePath(root + "2/a.txt", false));
    // Project dirs are READ-ONLY for the model: write policy unchanged.
    Sandbox::DenyReason reason;
    QVERIFY(!Sandbox::validatePath(root + "/src/a.txt", true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::WritePolicy);

    Sandbox::setProjectDir(QString());
    QVERIFY(!Sandbox::validatePath(root + "/src/a.txt", false));
}

// docs/17 P0-2: relative paths anchor to the active project root (they used
// to fall through to the process CWD - the "reads the wrong file" bug).
// Traversal stays rejected: the anchored path is canonicalized like any other.
void TestSandbox::relativePathsAnchorToProjectRoot() {
    const QString root = QStringLiteral("Q:/nexusai_proj_test");

    // No active project: resolvePath is a no-op.
    Sandbox::setProjectDir(QString());
    QCOMPARE(Sandbox::resolvePath(QStringLiteral("src/a.txt")),
             QStringLiteral("src/a.txt"));

    Sandbox::setProjectDir(root);
    QCOMPARE(Sandbox::resolvePath(QStringLiteral("src/a.txt")),
             QDir(root).absoluteFilePath(QStringLiteral("src/a.txt")));
    // Anchored relative path now passes the read policy (project subtree).
    QVERIFY(Sandbox::validatePath(QStringLiteral("src/a.txt"), false));
    // Escape via ".." still rejected (B4-D: rejected before lexical collapse,
    // so a junction cannot re-order the segments during resolution).
    Sandbox::DenyReason reason;
    QVERIFY(!Sandbox::validatePath(QStringLiteral("../../evil.txt"), false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::Traversal);
    // Absolute paths pass through untouched.
    QCOMPARE(Sandbox::resolvePath(root + "/abs.txt"), root + "/abs.txt");

    Sandbox::setProjectDir(QString());
}

// docs/17 P1-4: project dirs are read-only unless the security-page gate
// (security/allowProjectWrite) lifts it; the deny list still wins.
void TestSandbox::projectWriteGate() {
    const QString root = QStringLiteral("Q:/nexusai_proj_test");

    Sandbox::setProjectDir(root);
    QVERIFY(!Sandbox::validatePath(root + "/src/a.txt", true));

    Sandbox::setProjectWriteEnabled(true);
    QVERIFY(Sandbox::validatePath(root + "/src/a.txt", true));
    QVERIFY(Sandbox::validatePath(root + "/deep/dir/b.txt", true));

    Sandbox::setUserDenyList({QStringLiteral("*.env")});
    QVERIFY(!Sandbox::validatePath(root + "/config.env", true));
    Sandbox::setUserDenyList({});

    Sandbox::setProjectWriteEnabled(false);
    QVERIFY(!Sandbox::validatePath(root + "/src/a.txt", true));
    Sandbox::setProjectDir(QString());
}

// docs/17 P1-5: the execution scope pins the project dir on the calling
// thread, so switching the global active project mid-task cannot redirect a
// running tool to another project's subtree.
void TestSandbox::executionScopePinsProjectDir() {
    const QString globalRoot = QStringLiteral("Q:/nexusai_proj_global");
    const QString taskRoot = QStringLiteral("Q:/nexusai_proj_task");

    Sandbox::setProjectDir(globalRoot);
    QCOMPARE(Sandbox::currentProjectDir(), QDir::toNativeSeparators(globalRoot));

    {
        const Sandbox::ExecutionScope scope(taskRoot);
        QCOMPARE(Sandbox::currentProjectDir(),
                 QDir::toNativeSeparators(taskRoot));
        // Reads resolve against the snapshot, not the global root.
        QVERIFY(Sandbox::validatePath(taskRoot + "/a.txt", false));
        QVERIFY(!Sandbox::validatePath(globalRoot + "/a.txt", false));
    }

    // Scope destroyed: back to the global value.
    QCOMPARE(Sandbox::currentProjectDir(), QDir::toNativeSeparators(globalRoot));
    Sandbox::setProjectDir(QString());
}

// docs/17 A1: the user-configured deny list is an absolute denial layer -
// both reads AND writes go through it, globs match file names
// (case-insensitive on Windows), directory entries cover their subtree.
void TestSandbox::userDenyListBlocksReadsAndWrites() {
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    // Baseline: inside the allowlist -> allowed.
    QVERIFY(Sandbox::validatePath(docs + "/notes.txt", false));

    Sandbox::setUserDenyList({QStringLiteral("*.env"),
                              QStringLiteral("mysecrets")});
    // Glob on the file name (case-insensitive on Windows).
    QVERIFY(!Sandbox::validatePath(docs + "/config.ENV", false));
    // Glob also matches the full path form.
    QVERIFY(!Sandbox::validatePath(docs + "/deep/dir/run.env", false));
    // Directory entry covers its subtree (input is canonicalized).
    QVERIFY(!Sandbox::validatePath(docs + "/mysecrets/vault.txt", false));
    // Boundary-aware: a sibling directory with a shared prefix survives.
    QVERIFY(Sandbox::validatePath(docs + "/mysecrets2/vault.txt", false));
    // Non-matching allowlisted file still passes.
    QVERIFY(Sandbox::validatePath(docs + "/notes.txt", false));
    // Writes go through the deny list too.
    QVERIFY(!Sandbox::validatePath(docs + "/run.env", true));
    // Relative-name entries hit the directory segment at ANY depth.
    QVERIFY(!Sandbox::validatePath(docs + "/deep/nested/mysecrets/x",
                                   false));
    // Outside the allowlist the read policy still rejects (reason intact).
    Sandbox::DenyReason reason;
    QVERIFY(!Sandbox::validatePath(QStringLiteral("Q:/nexusai_nowhere/x"),
                                   false, &reason));

    Sandbox::setUserDenyList({});
    QVERIFY(Sandbox::validatePath(docs + "/config.ENV", false));
}

// Precedence: system block > deny list > allow layers (incl. FullDisk and
// the project directory - "import the whole disk as a project" must not
// resurrect ~/.ssh).
void TestSandbox::denyListPrecedesFullDiskAndProject() {
    const QString home =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString temp =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    QTemporaryDir project;
    QVERIFY(project.isValid());
    const QString root = QDir::toNativeSeparators(project.path());

    // Project dir alone: temp file inside it is readable.
    Sandbox::setProjectDir(root);
    QVERIFY(Sandbox::validatePath(root + "/data.txt", false));

    // A deny entry INSIDE the project dir wins over the project allow.
    Sandbox::setUserDenyList({QStringLiteral("*.kdbx")});
    Sandbox::DenyReason reason;
    QVERIFY(!Sandbox::validatePath(root + "/vault.kdbx", false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::DenyList);

    // FullDisk opens everything EXCEPT the deny list and system dirs.
    Sandbox::setReadPolicy(Sandbox::ReadPolicy::FullDisk);
    QVERIFY(Sandbox::validatePath(temp + "/other.txt", false));
    QVERIFY(Sandbox::validatePath(root + "/data.txt", false));
    QVERIFY(!Sandbox::validatePath(root + "/vault.kdbx", false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::DenyList);
    // Default deny list (~/.ssh) still applies under FullDisk.
    QVERIFY(!Sandbox::validatePath(home + "/.ssh/id_rsa", false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::DenyList);
    QVERIFY(!Sandbox::validatePath(QStringLiteral("C:\\Windows\\System32\\config"),
                                   false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);

    // Restore global state for the other tests.
    Sandbox::setReadPolicy(Sandbox::ReadPolicy::UserDirectories);
    Sandbox::setUserDenyList({});
    Sandbox::setProjectDir(QString());
    QVERIFY(!Sandbox::validatePath(temp + "/other.txt", false));
}

// docs/17 A1: the built-in conservative deny list (credentials & key files)
// is always active, independent of the user list.
void TestSandbox::defaultDenyListEntries() {
    const QString home =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    QVERIFY(!Sandbox::validatePath(home + "/.ssh/id_rsa", false));
    QVERIFY(!Sandbox::validatePath(home + "/.gnupg/secring.gpg", false));
    QVERIFY(!Sandbox::validatePath(home + "/.aws/credentials", false));
    // Key-file globs match anywhere - including inside the user allowlist.
    QVERIFY(!Sandbox::validatePath(docs + "/server.pem", false));
    QVERIFY(!Sandbox::validatePath(docs + "/certs/CA.key", false));
    QVERIFY(!Sandbox::validatePath(docs + "/vault.kdbx", false));
    QVERIFY(!Sandbox::validatePath(docs + "/tunnel.ppk", false));
    // Non-matching files stay readable.
    QVERIFY(Sandbox::validatePath(docs + "/report.txt", false));
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

void TestSandbox::editFileReplaceAll() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("replaceall.txt");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x one x two x");
    f.close();

    ITool* tool = ToolRegistry::instance().find(QStringLiteral("edit_file"));
    QVERIFY(tool);
    const ToolResult r = tool->execute(QJsonObject{
        {"path", path},
        {"old_string", QStringLiteral("x")},
        {"new_string", QStringLiteral("y")},
        {"replace_all", true}
    });
    QVERIFY2(r.success, qPrintable(r.error));
    QVERIFY2(r.output.contains(QStringLiteral("Replaced 3 occurrences")), qPrintable(r.output));
    QCOMPARE(readFileText(path), QStringLiteral("y one y two y"));
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

void TestSandbox::editFileNotFoundSuggestsRelocation() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());

    // 1) 仅缩进/空白不同：归一化后唯一命中 → 给出行号。
    const QString indentPath = dir.filePath("indent.cpp");
    {
        QFile f(indentPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("void f() {\n    return 1;\n}\n");
    }
    const ToolResult indent = runEdit(indentPath,
        QStringLiteral("void f() {\nreturn 1;\n}"), QStringLiteral("x"));
    QVERIFY(!indent.success);
    QVERIFY2(indent.error.contains(QStringLiteral("old_string not found")),
             qPrintable(indent.error));
    QVERIFY2(indent.error.contains(QStringLiteral("collapsing whitespace")),
             qPrintable(indent.error));
    QVERIFY2(indent.error.contains(QStringLiteral("line 1")),
             qPrintable(indent.error));
    QCOMPARE(readFileText(indentPath), QStringLiteral("void f() {\n    return 1;\n}\n"));

    // 2) 首行还在、上下文已变（同一文件的先前提案改过相邻代码）→ 给首行位置。
    const QString shiftedPath = dir.filePath("shifted.txt");
    {
        QFile f(shiftedPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("alpha first line\nbeta\ngamma\n");
    }
    const ToolResult shifted = runEdit(shiftedPath,
        QStringLiteral("alpha first line\nDELTA"), QStringLiteral("x"));
    QVERIFY(!shifted.success);
    QVERIFY2(shifted.error.contains(QStringLiteral("first line appears at line 1")),
             qPrintable(shifted.error));
    QCOMPARE(readFileText(shiftedPath), QStringLiteral("alpha first line\nbeta\ngamma\n"));

    // 3) 完全找不到 → 通用重读建议，不编造位置。
    const ToolResult missing = runEdit(shiftedPath, QStringLiteral("zzz"),
                                       QStringLiteral("x"));
    QVERIFY(!missing.success);
    QVERIFY2(missing.error.contains(QStringLiteral("Re-read the file")),
             qPrintable(missing.error));
    QVERIFY(!missing.error.contains(QStringLiteral("line ")));
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
    QVERIFY2(r.error.contains(QStringLiteral("Path blocked")), qPrintable(r.error));
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
    // docs/34 P3.3: 换行符差异给出可操作的重定位提示。
    QVERIFY2(lfMiss.error.contains(QStringLiteral("CRLF line endings")),
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

// ── Encoding fallback: read_file ───────────────────────────
// read_file tries UTF-8 first, then falls back to the system locale
// encoding so non-UTF-8 text files are readable instead of mojibake.

void TestSandbox::readFileUtf8RoundTrip() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("utf8.txt");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QStringLiteral("你好世界\nplain ascii\n").toUtf8());
    f.close();

    ITool* tool = ToolRegistry::instance().find(QStringLiteral("read_file"));
    QVERIFY(tool);
    const ToolResult r = tool->execute(QJsonObject{{"path", path}});
    QVERIFY2(r.success, qPrintable(r.error));
    QVERIFY2(r.output.contains(QStringLiteral("你好世界")),
             qPrintable(r.output));
    QVERIFY2(!r.output.contains(QChar::ReplacementCharacter),
             "UTF-8 file must not produce replacement characters");
}

void TestSandbox::readFileNonUtf8FallsBackToLocal() {
#ifdef Q_OS_WIN
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("gbk.txt");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    // "中文" in GBK/CP936: D6 D0 CE C4 — not valid UTF-8.
    f.write(QByteArray::fromHex("d6d0cec4"));
    f.close();

    ITool* tool = ToolRegistry::instance().find(QStringLiteral("read_file"));
    QVERIFY(tool);
    const ToolResult r = tool->execute(QJsonObject{{"path", path}});
    QVERIFY2(r.success, qPrintable(r.error));
    QVERIFY2(!r.output.contains(QChar::ReplacementCharacter),
             "GBK file should fall back to local encoding, not mojibake");
    QVERIFY2(r.output.contains(QStringLiteral("中文")),
             qPrintable(QStringLiteral("expected 中文, got: %1").arg(r.output)));
#else
    QSKIP("GBK fallback is Windows-specific (local encoding is CP936)");
#endif
}

// docs/22 F4: device / extended-length prefixes bypass every normalization
// Qt can perform — rejected outright before any resolution.
void TestSandbox::devicePathsAreRejected() {
    Sandbox::DenyReason reason = Sandbox::DenyReason::None;
    QVERIFY(!Sandbox::validatePath(QStringLiteral("\\\\.\\C:\\evil.txt"),
                                   true, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);
    QVERIFY(!Sandbox::validatePath(QStringLiteral("\\\\?\\C:\\Users\\evil.txt"),
                                   false, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::SystemPath);
}

// docs/22 F4: a directory junction planted inside the project must not
// tunnel writes to the linked target — the write path canonicalizes the
// deepest existing ancestor, so the policy matches the real destination.
// docs/37 B4-D: validatePathResolved additionally returns that canonical
// destination, which pins the opened path even if the junction is swapped.
void TestSandbox::junctionParentChainResolvedForWrites() {
#ifdef Q_OS_WIN
    QTemporaryDir area;
    QVERIFY(area.isValid());
    const QString root = area.filePath(QStringLiteral("project"));
    QVERIFY(QDir().mkpath(root));

    // Junction target outside every write allowlist: C:\Users\Public exists,
    // is not the current user's profile and is neither system-blocked nor
    // deny-listed (ProgramData is a blocked system dir since B4-D).
    const QFileInfo targetInfo(QStringLiteral("C:\\Users\\Public"));
    if (!targetInfo.isDir())
        QSKIP("C:\\Users\\Public unavailable in this environment");

    const auto makeJunction = [](const QString& link, const QString& target) {
        QProcess mklink;
        // mklink is a cmd builtin that rejects forward slashes ("invalid
        // switch") — both paths must be native-separated.
        mklink.start(QStringLiteral("cmd.exe"),
                     {QStringLiteral("/c"), QStringLiteral("mklink"),
                      QStringLiteral("/J"),
                      QDir::toNativeSeparators(link),
                      QDir::toNativeSeparators(target)});
        return mklink.waitForFinished(5000)
            && QFileInfo(link).exists();
    };

    const QString link = root + QStringLiteral("\\link");
    if (!makeJunction(link, targetInfo.absoluteFilePath()))
        QSKIP("mklink /J unavailable in this environment");

    Sandbox::setProjectDir(root);
    Sandbox::setProjectWriteEnabled(true);

    Sandbox::DenyReason reason = Sandbox::DenyReason::None;
    // Pre-fix: the not-yet-existing file kept the junction path text, which
    // sits inside the project -> wrongly allowed. Post-fix the ancestor
    // chain resolves through the junction to C:\Users\Public -> refused.
    QString resolved;
    QVERIFY(!Sandbox::validatePathResolved(link + QStringLiteral("\\stolen.txt"),
                                           true, &resolved, &reason));
    QCOMPARE(reason, Sandbox::DenyReason::WritePolicy);
    QVERIFY(resolved.isEmpty());
    // Deeper: a missing subdirectory inside the junction resolves too.
    QVERIFY(!Sandbox::validatePath(
        link + QStringLiteral("\\sub\\stolen.txt"), true));
    // Reads through the junction were already refused (canonical path).
    QVERIFY(!Sandbox::validatePath(link + QStringLiteral("\\existing.txt"),
                                   false));

    // No over-rejection: legitimate project writes still pass, including
    // deep paths whose parents do not exist yet.
    QVERIFY(Sandbox::validatePath(root + QStringLiteral("\\real\\new.txt"),
                                  true));
    QVERIFY(Sandbox::validatePath(root + QStringLiteral("\\a\\b\\c\\new.txt"),
                                  true));

    // TOCTOU simulation: validate while the junction points INSIDE an
    // allowed directory, then swap the junction to an out-of-allowlist
    // target. The canonical path returned by the successful check stays
    // pinned to the checked destination; the raw link text would have
    // followed the new target.
    const QString allowedTarget = area.filePath(QStringLiteral("allowed-target"));
    QVERIFY(QDir().mkpath(allowedTarget));
    const QString swapLink = root + QStringLiteral("\\swaplink");
    if (makeJunction(swapLink, allowedTarget)) {
        QString checkedPath;
        QVERIFY2(Sandbox::validatePathResolved(swapLink + QStringLiteral("\\f.txt"),
                                               true, &checkedPath, &reason),
                 qPrintable(Sandbox::denialHint(reason)));
        QCOMPARE(reason, Sandbox::DenyReason::None);

        // Swap the junction to a forbidden destination.
        QVERIFY(QDir().rmdir(swapLink));
        if (makeJunction(swapLink, targetInfo.absoluteFilePath())) {
            QVERIFY(!Sandbox::validatePath(swapLink + QStringLiteral("\\f.txt"),
                                           true, &reason));
            // The previously returned canonical path still points at the
            // checked (allowed) destination, not the swapped one.
            const QString canonicalChecked =
                QDir::fromNativeSeparators(QDir::cleanPath(checkedPath));
            const QString canonicalAllowed =
                QDir::fromNativeSeparators(QDir::cleanPath(allowedTarget));
            QVERIFY2(canonicalChecked.startsWith(
                         canonicalAllowed + QLatin1Char('/'), Qt::CaseInsensitive),
                     qPrintable(checkedPath));
        } else {
            qWarning("mklink /J re-target unavailable; TOCTOU swap not exercised");
        }
    } else {
        qWarning("mklink /J second junction unavailable; TOCTOU swap not exercised");
    }

    // Remove the reparse points before the temp dir goes away: deleting a
    // junction through a recursive directory remove can fail and leaves the
    // QTemporaryDir uncleanable.
    QDir().rmdir(link);
    QDir().rmdir(swapLink);

    Sandbox::setProjectWriteEnabled(false);
    Sandbox::setProjectDir(QString());
#else
    QSKIP("directory junctions are Windows-specific");
#endif
}

// docs/23 G4: write_file writes raw UTF-8 bytes (QSaveFile) — line
// endings are preserved exactly, matching edit_file's byte semantics so
// the two tools cannot produce mixed-line-ending files.
void TestSandbox::writeFileRoundTripsLfBytes() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("write.txt");

    ITool* tool = ToolRegistry::instance().find(QStringLiteral("write_file"));
    QVERIFY(tool);
    const ToolResult r = tool->execute(QJsonObject{
        {"path", path},
        {"content", QStringLiteral("alpha\nbeta\ngamma\n")}});
    QVERIFY2(r.success, qPrintable(r.error));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    f.close();
    // Byte-exact: LF kept, no CRLF translation, UTF-8 payload.
    QCOMPARE(bytes, QByteArray("alpha\nbeta\ngamma\n"));
    QVERIFY2(r.output.contains(QStringLiteral("bytes)")),
             qPrintable(r.output));
}

// docs/23 G4: a failing open must surface as a tool error — never as the
// pre-fix "File written successfully" false success.
void TestSandbox::writeFileToDirectoryFailsWithOpenError() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("subdir");
    QVERIFY(QDir().mkpath(path));

    ITool* tool = ToolRegistry::instance().find(QStringLiteral("write_file"));
    QVERIFY(tool);
    const ToolResult r = tool->execute(QJsonObject{
        {"path", path},
        {"content", QStringLiteral("nope")}});
    QVERIFY(!r.success);
    QVERIFY2(r.error.contains(QStringLiteral("Cannot open file")),
             qPrintable(r.error));
}

// docs/24 H3: a file beyond the pre-read bound is refused before any bulk
// read — the Safe-level tool must not spike memory on a huge target.
void TestSandbox::readFileOversizeRefusedBeforeRead() {
    QTemporaryDir dir = sandboxTempDir();
    QVERIFY(dir.isValid());

    const QString big = dir.filePath("big.txt");
    {
        QFile f(big);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(9 * 1024 * 1024, 'x'));
        f.close();
    }

    ITool* tool = ToolRegistry::instance().find(QStringLiteral("read_file"));
    QVERIFY(tool);
    const ToolResult r = tool->execute(QJsonObject{{"path", big}});
    QVERIFY(!r.success);
    QVERIFY2(r.error.contains(QStringLiteral("File too large")),
             qPrintable(r.error));

    // Regular files keep working.
    const QString small = dir.filePath("small.txt");
    {
        QFile f(small);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("tiny");
    }
    const ToolResult ok = tool->execute(QJsonObject{{"path", small}});
    QVERIFY(ok.success);
    QCOMPARE(ok.output, QStringLiteral("tiny"));
}

// docs/24 H7: credential-shaped environment variables are stripped before
// the child process inherits them — `echo %VAR%` prints the unexpanded
// literal instead of the secret value.
void TestSandbox::shellExecStripsSensitiveEnvironment() {
#ifdef Q_OS_WIN
    qputenv("NEXUS_PROBE_API_KEY", "nx-probe-secret-9137");

    ITool* tool = ToolRegistry::instance().find(QStringLiteral("shell_exec"));
    QVERIFY(tool);
    const ToolResult r = tool->execute(QJsonObject{
        {"command", QStringLiteral("echo %NEXUS_PROBE_API_KEY%")}});
    QVERIFY2(r.success, qPrintable(r.error));
    QVERIFY2(!r.output.contains(QStringLiteral("nx-probe-secret-9137")),
             qPrintable(r.output));
    qunsetenv("NEXUS_PROBE_API_KEY");
#else
    QSKIP("cmd.exe %VAR% probing is Windows-specific");
#endif
}

QTEST_GUILESS_MAIN(TestSandbox)
#include "test_sandbox.moc"
