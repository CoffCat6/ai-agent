// docs/37 §9 W5-2a：SkillEditorService 的 id 校验、创建/加载往返、覆盖与
// .bak、64 KiB 上限、canonical containment、删除与空目录清理。
#include "core/skills/SkillEditorService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace NexusAI;

namespace {

bool writeFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(content) == content.size() && file.flush();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString skillFilePath(const QString& skillsRoot, const QString& id) {
    return QDir(skillsRoot).filePath(id + QStringLiteral("/SKILL.md"));
}

bool makeDirectoryLink(const QString& linkPath, const QString& target) {
    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setArguments({QStringLiteral("/c"), QStringLiteral("mklink"),
                          QStringLiteral("/J"), QDir::toNativeSeparators(linkPath),
                          QDir::toNativeSeparators(target)});
    process.start();
    if (!process.waitForFinished(15000))
        return false;
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        && QFileInfo(linkPath).isDir();
}

bool makeFileLink(const QString& linkPath, const QString& target) {
    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setArguments({QStringLiteral("/c"), QStringLiteral("mklink"),
                          QDir::toNativeSeparators(linkPath),
                          QDir::toNativeSeparators(target)});
    process.start();
    if (!process.waitForFinished(15000))
        return false;
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        && QFileInfo(linkPath).isFile();
}

void removeDirectoryLink(const QString& linkPath) {
    QProcess::execute(QStringLiteral("cmd.exe"),
                      {QStringLiteral("/c"), QStringLiteral("rmdir"),
                       QDir::toNativeSeparators(linkPath)});
}

} // namespace

class TestSkillsEditor : public QObject {
    Q_OBJECT

private slots:
    void validateIdAcceptsWellFormed();
    void validateIdRejectsUnsafe();
    void validateBodyEnforcesLimit();
    void rootsResolve();
    void roundTrip();
    void declaredEmptyAllowedToolsRoundTrip();
    void overwriteRequiresFlagAndCreatesBackup();
    void rejectsFrontmatterInjection();
    void loadRejectsMissingAndInvalid();
    void loadRejectsOversizedFile();
    void removeDeletesSkillAndEmptyDirectory();
    void removeKeepsDirectoryWithExtraFiles();
    void removeRejectsLinkInsideRoot();
    void rejectsDirectoryLinkOutsideRoot();
    void rejectsFileLinkOutsideRoot();
    // docs/40 §2.1-2.3：结构化 code、Windows id、symlink 保存防护。
    void saveLoadRemoveReportOkCode();
    void saveReportsExistsAndSizeCodes();
    void saveReportsMetadataAndToolCodes();
    void saveRejectsWindowsInvalidIds();
    void removeMissingReportsNotFound();
    void saveRejectsSymlinkSkillFile();
    void saveAndRemoveRejectDirectoryLinkInsideRoot();
};

void TestSkillsEditor::validateIdAcceptsWellFormed() {
    QString error = QStringLiteral("stale");
    for (const QString& id : {QStringLiteral("a"), QStringLiteral("code-review"),
                              QStringLiteral("skill_1.2"), QStringLiteral("A0")}) {
        QVERIFY2(SkillEditorService::validateId(id, &error), qPrintable(id));
        QVERIFY(error.isEmpty());
    }
}

void TestSkillsEditor::validateIdRejectsUnsafe() {
    const QString longId(65, QLatin1Char('a'));
    for (const QString& id : {QString(), QStringLiteral("../x"),
                              QStringLiteral("a/b"), QStringLiteral("a\\b"),
                              QStringLiteral(".."), QStringLiteral("."),
                              QStringLiteral("a..b"), QStringLiteral("a b"),
                              QStringLiteral("a:b"), longId}) {
        QString error;
        QVERIFY2(!SkillEditorService::validateId(id, &error), qPrintable(id));
        QVERIFY(!error.isEmpty());
    }
}

void TestSkillsEditor::validateBodyEnforcesLimit() {
    QString error;
    QVERIFY(SkillEditorService::validateBody(
        QString(SkillEditorService::kMaxFileBytes, QLatin1Char('a')), &error));
    QVERIFY(error.isEmpty());

    const QString tooLarge(SkillEditorService::kMaxFileBytes + 1, QLatin1Char('a'));
    QVERIFY(!SkillEditorService::validateBody(tooLarge, &error));
    QVERIFY(!error.isEmpty());

    // 多字节字符按 UTF-8 字节数计算。
    const QString wide = QString(22000, QChar(0x4E2D)); // 3 bytes each
    QVERIFY(!SkillEditorService::validateBody(wide, &error));
}

void TestSkillsEditor::rootsResolve() {
    QCOMPARE(SkillEditorService::projectSkillsRoot(QStringLiteral("/tmp/proj")),
             QDir(QStringLiteral("/tmp/proj")).filePath(QStringLiteral(".nexusai/skills")));
    QVERIFY(SkillEditorService::projectSkillsRoot(QString()).isEmpty());
    QVERIFY(!SkillEditorService::userSkillsRoot().isEmpty());
}

void TestSkillsEditor::roundTrip() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    draft.id = QStringLiteral("code-review");
    draft.name = QStringLiteral("代码审查");
    draft.description = QStringLiteral("审查 diff 并给出分级问题");
    draft.allowedTools = {QStringLiteral("read_file"), QStringLiteral("search_code")};
    draft.body = QStringLiteral("第一步：读取文件\n第二步：输出结果\n");

    QString error = QStringLiteral("stale");
    QVERIFY2(SkillEditorService::save(root.path(), draft, false, &error),
             qPrintable(error));
    QVERIFY(error.isEmpty());

    const QString filePath = skillFilePath(root.path(), draft.id);
    QVERIFY(QFileInfo(filePath).isFile());
    const QByteArray content = readFile(filePath);
    QVERIFY(content.startsWith("---\nname: "));
    QVERIFY(content.endsWith(draft.body.toUtf8()));

    SkillEditorService::Draft loaded;
    QVERIFY2(SkillEditorService::load(root.path(), draft.id, &loaded, &error),
             qPrintable(error));
    QCOMPARE(loaded.id, draft.id);
    QCOMPARE(loaded.name, draft.name);
    QCOMPARE(loaded.description, draft.description);
    QCOMPARE(loaded.allowedTools, draft.allowedTools);
    QVERIFY(loaded.allowedToolsSpecified);
    QCOMPARE(loaded.body, draft.body);
}

// docs/38 §3：加载到 `allowed-tools` 键即保留“已声明”；已声明的空列表保存
// 为 `allowed-tools: []`，未声明则省略该键。
void TestSkillsEditor::declaredEmptyAllowedToolsRoundTrip() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString id = QStringLiteral("empty-list");
    const QString filePath = skillFilePath(root.path(), id);
    QVERIFY(writeFile(filePath,
                      "---\nname: empty-list\ndescription: 禁止全部\n"
                      "allowed-tools: []\n---\nbody\n"));

    SkillEditorService::Draft loaded;
    QString error;
    QVERIFY2(SkillEditorService::load(root.path(), id, &loaded, &error),
             qPrintable(error));
    QVERIFY(loaded.allowedToolsSpecified);
    QVERIFY(loaded.allowedTools.isEmpty());

    // 原样保存：空但已声明必须写出 `allowed-tools: []`。
    QVERIFY2(SkillEditorService::save(root.path(), loaded, true, &error),
             qPrintable(error));
    const QByteArray content = readFile(filePath);
    QVERIFY(content.contains("allowed-tools: []"));

    SkillEditorService::Draft reloaded;
    QVERIFY2(SkillEditorService::load(root.path(), id, &reloaded, &error),
             qPrintable(error));
    QVERIFY(reloaded.allowedToolsSpecified);
    QVERIFY(reloaded.allowedTools.isEmpty());
    QCOMPARE(reloaded.body, loaded.body);

    // 未声明 + 空列表：省略键，重新加载后 specified=false。
    SkillEditorService::Draft undeclared;
    undeclared.id = QStringLiteral("undeclared");
    undeclared.body = QStringLiteral("body\n");
    QVERIFY2(SkillEditorService::save(root.path(), undeclared, false, &error),
             qPrintable(error));
    const QByteArray undeclaredContent =
        readFile(skillFilePath(root.path(), undeclared.id));
    QVERIFY(!undeclaredContent.contains("allowed-tools"));

    SkillEditorService::Draft undeclaredLoaded;
    QVERIFY2(SkillEditorService::load(root.path(), undeclared.id, &undeclaredLoaded,
                                      &error),
             qPrintable(error));
    QVERIFY(!undeclaredLoaded.allowedToolsSpecified);
    QVERIFY(undeclaredLoaded.allowedTools.isEmpty());
}

void TestSkillsEditor::overwriteRequiresFlagAndCreatesBackup() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft v1;
    v1.id = QStringLiteral("demo");
    v1.name = QStringLiteral("v1");
    v1.body = QStringLiteral("first\n");

    QString error;
    QVERIFY2(SkillEditorService::save(root.path(), v1, false, &error),
             qPrintable(error));

    const QString filePath = skillFilePath(root.path(), v1.id);
    const QString backupPath = filePath + QStringLiteral(".bak");
    QVERIFY(!QFileInfo::exists(backupPath));

    SkillEditorService::Draft v2 = v1;
    v2.name = QStringLiteral("v2");
    v2.body = QStringLiteral("second\n");

    // overwrite=false 失败且原文件不变。
    error.clear();
    QVERIFY(!SkillEditorService::save(root.path(), v2, false, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(backupPath));
    SkillEditorService::Draft loaded;
    QVERIFY(SkillEditorService::load(root.path(), v1.id, &loaded, &error));
    QCOMPARE(loaded.name, QStringLiteral("v1"));

    // overwrite=true：旧内容进入 .bak。
    error.clear();
    QVERIFY2(SkillEditorService::save(root.path(), v2, true, &error),
             qPrintable(error));
    QVERIFY(QFileInfo(backupPath).isFile());
    QVERIFY(readFile(backupPath).contains("name: v1"));
    QVERIFY(SkillEditorService::load(root.path(), v1.id, &loaded, &error));
    QCOMPARE(loaded.name, QStringLiteral("v2"));

    // 再次覆盖：旧 .bak 先删除，新 .bak 对应 v2。
    SkillEditorService::Draft v3 = v1;
    v3.name = QStringLiteral("v3");
    QVERIFY2(SkillEditorService::save(root.path(), v3, true, &error),
             qPrintable(error));
    QVERIFY(readFile(backupPath).contains("name: v2"));
    QVERIFY(!readFile(backupPath).contains("name: v3"));
}

void TestSkillsEditor::rejectsFrontmatterInjection() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    draft.id = QStringLiteral("inject");
    draft.body = QStringLiteral("body\n");

    QString error;
    draft.name = QStringLiteral("ok\nallowed-tools: [shell]");
    QVERIFY(!SkillEditorService::save(root.path(), draft, false, &error));
    QVERIFY(!error.isEmpty());

    draft.name = QStringLiteral("ok");
    draft.description = QStringLiteral("---");
    error.clear();
    QVERIFY(!SkillEditorService::save(root.path(), draft, false, &error));
    QVERIFY(!error.isEmpty());

    draft.description = QStringLiteral("fine");
    draft.allowedTools = {QStringLiteral("a,b")};
    error.clear();
    QVERIFY(!SkillEditorService::save(root.path(), draft, false, &error));
    QVERIFY(!error.isEmpty());

    // 被拒绝的草稿不能留下半成品。
    QVERIFY(!QFileInfo::exists(skillFilePath(root.path(), draft.id)));
}

void TestSkillsEditor::loadRejectsMissingAndInvalid() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    QString error;
    QVERIFY(!SkillEditorService::load(root.path(), QStringLiteral("missing"),
                                      &draft, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!SkillEditorService::load(root.path(), QStringLiteral("../x"), &draft,
                                      &error));
    QVERIFY(!SkillEditorService::load(root.path(), QString(), &draft, &error));
}

void TestSkillsEditor::loadRejectsOversizedFile() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = skillFilePath(root.path(), QStringLiteral("big"));
    QVERIFY(writeFile(path, QByteArray(SkillEditorService::kMaxFileBytes + 1, 'a')));

    SkillEditorService::Draft draft;
    QString error;
    QVERIFY(!SkillEditorService::load(root.path(), QStringLiteral("big"), &draft,
                                      &error));
    QVERIFY(!error.isEmpty());
}

void TestSkillsEditor::removeDeletesSkillAndEmptyDirectory() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    draft.id = QStringLiteral("temporary");
    draft.body = QStringLiteral("body\n");
    QString error;
    QVERIFY2(SkillEditorService::save(root.path(), draft, false, &error),
             qPrintable(error));

    const QString skillDir = QDir(root.path()).filePath(draft.id);
    QVERIFY(QFileInfo(skillDir).isDir());
    QVERIFY2(SkillEditorService::remove(root.path(), draft.id, &error),
             qPrintable(error));
    QVERIFY(!QFileInfo::exists(skillFilePath(root.path(), draft.id)));
    QVERIFY(!QFileInfo::exists(skillDir));
    QVERIFY(QFileInfo(root.path()).isDir());

    // 删除不存在的技能返回 false。
    QVERIFY(!SkillEditorService::remove(root.path(), draft.id, &error));
    QVERIFY(!error.isEmpty());
}

void TestSkillsEditor::removeKeepsDirectoryWithExtraFiles() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    draft.id = QStringLiteral("with-assets");
    draft.body = QStringLiteral("body\n");
    QString error;
    QVERIFY2(SkillEditorService::save(root.path(), draft, false, &error),
             qPrintable(error));

    const QString skillDir = QDir(root.path()).filePath(draft.id);
    const QString extra = QDir(skillDir).filePath(QStringLiteral("notes.txt"));
    QVERIFY(writeFile(extra, QByteArray("keep me")));

    QVERIFY2(SkillEditorService::remove(root.path(), draft.id, &error),
             qPrintable(error));
    QVERIFY(!QFileInfo::exists(skillFilePath(root.path(), draft.id)));
    QVERIFY(QFileInfo(extra).isFile());
    QCOMPARE(readFile(extra), QByteArray("keep me"));
}

void TestSkillsEditor::removeRejectsLinkInsideRoot() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;

    SkillEditorService::Draft victim;
    victim.id = QStringLiteral("victim");
    victim.body = QStringLiteral("victim body\n");
    QVERIFY2(SkillEditorService::save(root.path(), victim, false, &error),
             qPrintable(error));

    QVERIFY(QDir().mkpath(QDir(root.path()).filePath(QStringLiteral("alias"))));
    const QString linkPath =
        QDir(root.path()).filePath(QStringLiteral("alias/SKILL.md"));
    const QString target = skillFilePath(root.path(), QStringLiteral("victim"));
    if (!makeFileLink(linkPath, target))
        QSKIP("文件符号链接创建不可用（需要开发者模式/权限）");

    // 根内 symlink 指向另一个技能：删除 alias 不得删掉 victim 的目标文件。
    error.clear();
    QVERIFY(!SkillEditorService::remove(root.path(), QStringLiteral("alias"),
                                        &error));
    QVERIFY2(error.contains(QStringLiteral("符号链接")), qPrintable(error));
    QVERIFY(QFileInfo(target).isFile());
    QVERIFY(QFileInfo(linkPath).isSymLink());
}

void TestSkillsEditor::rejectsDirectoryLinkOutsideRoot() {
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());

    const QString target = QDir(outside.path()).filePath(QStringLiteral("SKILL.md"));
    QVERIFY(writeFile(target, QByteArray("---\nname: escape\n---\nsecret\n")));

    const QString linkPath = QDir(root.path()).filePath(QStringLiteral("escape"));
    if (!makeDirectoryLink(linkPath, outside.path()))
        QSKIP("junction 创建不可用（需要 mklink /J 权限）");

    SkillEditorService::Draft draft;
    QString error;
    QVERIFY(!SkillEditorService::load(root.path(), QStringLiteral("escape"), &draft,
                                      &error));
    QVERIFY(!error.isEmpty());

    SkillEditorService::Draft injected;
    injected.id = QStringLiteral("escape");
    injected.name = QStringLiteral("pwn");
    injected.body = QStringLiteral("overwritten\n");
    error.clear();
    QVERIFY(!SkillEditorService::save(root.path(), injected, true, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(target),
             QByteArray("---\nname: escape\n---\nsecret\n"));

    error.clear();
    QVERIFY(!SkillEditorService::remove(root.path(), QStringLiteral("escape"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(QFileInfo(target).isFile());

    removeDirectoryLink(linkPath);
}

void TestSkillsEditor::rejectsFileLinkOutsideRoot() {
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());

    const QString skillDir = QDir(root.path()).filePath(QStringLiteral("linked"));
    QVERIFY(QDir().mkpath(skillDir));
    const QString target = QDir(outside.path()).filePath(QStringLiteral("SKILL.md"));
    QVERIFY(writeFile(target, QByteArray("---\nname: outside\n---\nsecret\n")));
    const QString linkPath = QDir(skillDir).filePath(QStringLiteral("SKILL.md"));
    if (!makeFileLink(linkPath, target))
        QSKIP("文件符号链接创建不可用（需要 SeCreateSymbolicLinkPrivilege）");

    SkillEditorService::Draft draft;
    QString error;
    QVERIFY(!SkillEditorService::load(root.path(), QStringLiteral("linked"), &draft,
                                      &error));
    QVERIFY(!error.isEmpty());

    SkillEditorService::Draft injected;
    injected.id = QStringLiteral("linked");
    injected.body = QStringLiteral("overwritten\n");
    error.clear();
    QVERIFY(!SkillEditorService::save(root.path(), injected, true, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(readFile(target).contains("name: outside"));

    error.clear();
    QVERIFY(!SkillEditorService::remove(root.path(), QStringLiteral("linked"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(QFileInfo(target).isFile());

    QFile::remove(linkPath);
}

// docs/40 §2.1：成功路径必须回填 "ok"，且不破坏 error 语义。
void TestSkillsEditor::saveLoadRemoveReportOkCode() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    draft.id = QStringLiteral("coded");
    draft.name = QStringLiteral("coded");
    draft.body = QStringLiteral("body\n");

    QString error = QStringLiteral("stale");
    QString code = QStringLiteral("stale");
    QVERIFY2(SkillEditorService::save(root.path(), draft, false, &error, &code),
             qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(code, QStringLiteral("ok"));

    SkillEditorService::Draft loaded;
    code = QStringLiteral("stale");
    QVERIFY2(SkillEditorService::load(root.path(), draft.id, &loaded, &error, &code),
             qPrintable(error));
    QCOMPARE(code, QStringLiteral("ok"));

    code = QStringLiteral("stale");
    QVERIFY2(SkillEditorService::remove(root.path(), draft.id, &error, &code),
             qPrintable(error));
    QCOMPARE(code, QStringLiteral("ok"));
}

void TestSkillsEditor::saveReportsExistsAndSizeCodes() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    draft.id = QStringLiteral("twice");
    draft.body = QStringLiteral("body\n");
    QString error;
    QString code;
    QVERIFY2(SkillEditorService::save(root.path(), draft, false, &error, &code),
             qPrintable(error));
    QCOMPARE(code, QStringLiteral("ok"));

    // 第二次未覆盖：exists（不再靠中文文案判断）。
    error.clear();
    code.clear();
    QVERIFY(!SkillEditorService::save(root.path(), draft, false, &error, &code));
    QCOMPARE(code, QStringLiteral("exists"));
    QVERIFY(!error.isEmpty());

    SkillEditorService::Draft oversized;
    oversized.id = QStringLiteral("oversized");
    oversized.body = QString(SkillEditorService::kMaxFileBytes + 1, QLatin1Char('a'));
    error.clear();
    code.clear();
    QVERIFY(!SkillEditorService::save(root.path(), oversized, false, &error, &code));
    QCOMPARE(code, QStringLiteral("too_large"));
    QVERIFY(!error.isEmpty());
}

void TestSkillsEditor::saveReportsMetadataAndToolCodes() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft draft;
    draft.id = QStringLiteral("fields");
    draft.body = QStringLiteral("body\n");
    QString error;
    QString code;

    draft.name = QStringLiteral("bad\nname");
    QVERIFY(!SkillEditorService::save(root.path(), draft, false, &error, &code));
    QCOMPARE(code, QStringLiteral("invalid_metadata"));

    draft.name = QStringLiteral("fine");
    draft.allowedTools = {QStringLiteral("a,b")};
    error.clear();
    code.clear();
    QVERIFY(!SkillEditorService::save(root.path(), draft, false, &error, &code));
    QCOMPARE(code, QStringLiteral("invalid_tools"));
}

// docs/40 §2.2：尾点/尾空格与 Windows 保留设备名（含带扩展名形态）。
void TestSkillsEditor::saveRejectsWindowsInvalidIds() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    for (const QString& id : {QStringLiteral("review."),
                              QStringLiteral("review "),
                              QStringLiteral("CON"),
                              QStringLiteral("con.txt"),
                              QStringLiteral("PRN"),
                              QStringLiteral("NUL"),
                              QStringLiteral("COM1"),
                              QStringLiteral("lpt9")}) {
        SkillEditorService::Draft draft;
        draft.id = id;
        draft.body = QStringLiteral("body\n");
        QString error;
        QString code;
        QVERIFY2(!SkillEditorService::save(root.path(), draft, false, &error, &code),
                 qPrintable(id));
        QCOMPARE(code, QStringLiteral("invalid_id"));
        QVERIFY2(!error.isEmpty(), qPrintable(id));
    }

    // 错误文案要说明原因，便于编辑器提示。
    QString error;
    QVERIFY(!SkillEditorService::validateId(QStringLiteral("review."), &error));
    QVERIFY2(error.contains(QStringLiteral("结尾")), qPrintable(error));
    QVERIFY(!SkillEditorService::validateId(QStringLiteral("CON"), &error));
    QVERIFY2(error.contains(QStringLiteral("保留")), qPrintable(error));
}

void TestSkillsEditor::removeMissingReportsNotFound() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString error;
    QString code;
    QVERIFY(!SkillEditorService::remove(root.path(), QStringLiteral("missing"),
                                        &error, &code));
    QCOMPARE(code, QStringLiteral("not_found"));
    QVERIFY(!error.isEmpty());
}

// docs/40 §2.3：根内 symlink 指向另一个技能也不得写入/覆盖目标。
void TestSkillsEditor::saveRejectsSymlinkSkillFile() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    SkillEditorService::Draft victim;
    victim.id = QStringLiteral("victim");
    victim.body = QStringLiteral("victim body\n");
    QString error;
    QString code;
    QVERIFY2(SkillEditorService::save(root.path(), victim, false, &error, &code),
             qPrintable(error));

    QVERIFY(QDir().mkpath(QDir(root.path()).filePath(QStringLiteral("alias"))));
    const QString linkPath =
        QDir(root.path()).filePath(QStringLiteral("alias/SKILL.md"));
    const QString target = skillFilePath(root.path(), QStringLiteral("victim"));
    const QByteArray before = readFile(target);
    if (!makeFileLink(linkPath, target))
        QSKIP("文件符号链接创建不可用（需要 SeCreateSymbolicLinkPrivilege）");

    SkillEditorService::Draft attack;
    attack.id = QStringLiteral("alias");
    attack.name = QStringLiteral("attack");
    attack.body = QStringLiteral("overwritten\n");

    error.clear();
    code.clear();
    QVERIFY(!SkillEditorService::save(root.path(), attack, true, &error, &code));
    QCOMPARE(code, QStringLiteral("symlink"));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(target), before);
    QVERIFY(!readFile(target).contains("overwritten"));
    QVERIFY(QFileInfo(linkPath).isSymLink());

    QFile::remove(linkPath);
}

void TestSkillsEditor::saveAndRemoveRejectDirectoryLinkInsideRoot() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;

    SkillEditorService::Draft victim;
    victim.id = QStringLiteral("victim");
    victim.body = QStringLiteral("victim body\n");
    QVERIFY2(SkillEditorService::save(root.path(), victim, false, &error),
             qPrintable(error));

    const QString linkPath = root.filePath(QStringLiteral("alias"));
    const QString target = root.filePath(QStringLiteral("victim"));
    if (!makeDirectoryLink(linkPath, target))
        QSKIP("目录 junction 创建不可用（需要权限）");

    // 根内目录 junction 指向另一个技能：save 不得顺着链接覆盖目标。
    SkillEditorService::Draft attack;
    attack.id = QStringLiteral("alias");
    attack.body = QStringLiteral("pwned\n");
    QString code;
    QVERIFY(!SkillEditorService::save(root.path(), attack, true, &error, &code));
    QCOMPARE(code, QStringLiteral("symlink"));
    QVERIFY(!QString::fromUtf8(
                 readFile(skillFilePath(root.path(), QStringLiteral("victim"))))
                 .contains(QStringLiteral("pwned")));

    // remove 同样不得删除目标技能的文件/目录。
    error.clear();
    code.clear();
    QVERIFY(!SkillEditorService::remove(root.path(), QStringLiteral("alias"),
                                        &error, &code));
    QCOMPARE(code, QStringLiteral("symlink"));
    QVERIFY(QFileInfo(skillFilePath(root.path(), QStringLiteral("victim")))
                .isFile());
    QVERIFY(QFileInfo(linkPath).isDir());

    removeDirectoryLink(linkPath);
}

QTEST_GUILESS_MAIN(TestSkillsEditor)

#include "test_skills_editor.moc"