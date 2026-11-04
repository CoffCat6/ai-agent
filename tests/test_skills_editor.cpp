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
    void overwriteRequiresFlagAndCreatesBackup();
    void rejectsFrontmatterInjection();
    void loadRejectsMissingAndInvalid();
    void loadRejectsOversizedFile();
    void removeDeletesSkillAndEmptyDirectory();
    void removeKeepsDirectoryWithExtraFiles();
    void removeRejectsLinkInsideRoot();
    void rejectsDirectoryLinkOutsideRoot();
    void rejectsFileLinkOutsideRoot();
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
    QCOMPARE(loaded.body, draft.body);
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

QTEST_GUILESS_MAIN(TestSkillsEditor)

#include "test_skills_editor.moc"