// docs/37 W2：Skill 发现 / 宽松 frontmatter 解析 / 去重 / 搜索 / 上限 / 边界。
#include "core/skills/SkillRegistry.h"
#include "core/utils/SettingsSchema.h"

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

// 在 <skillsRoot>/<id>/SKILL.md 写入内容。
bool writeSkill(const QString& skillsRoot, const QString& id, const QByteArray& content) {
    return writeFile(QDir(skillsRoot).filePath(id + QStringLiteral("/SKILL.md")), content);
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

// 文件符号链接需要 SeCreateSymbolicLinkPrivilege（或开发者模式）；失败时
// 调用方应 QSKIP 而不是把测试判为失败。
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

class TestSkills : public QObject {
    Q_OBJECT

private slots:
    void parsesFullFrontmatter();
    void fallsBackWhenFrontmatterMissing();
    void projectOverridesUser();
    void deduplicatesSameId();
    void searchesByNameAndDescription();
    void truncatesOversizedFile();
    void capsTotalCountAt200();
    void rejectsDirectoryLinkOutsideRoot();
    void rejectsFileLinkOutsideRoot();
    void handlesEmptyAndMissingRoots();
    void settingsSchemaRegistersSkillKeys();
};

void TestSkills::parsesFullFrontmatter() {
    QTemporaryDir userRoot;
    QVERIFY(userRoot.isValid());

    const QByteArray markdown =
        "---\n"
        "name: code-review\n"
        "description: 审查 diff 并给出分级问题\n"
        "version: 1\n"
        "allowed-tools: [read_file, search_code, git_diff]\n"
        "max-bytes: 65536\n"
        "---\n"
        "正文第一行\n"
        "第二行\n";
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("review"), markdown));

    SkillRegistry registry;
    registry.setUserRoot(userRoot.path());
    registry.reload();

    QCOMPARE(registry.all().size(), 1);
    const Skill* skill = registry.find(QStringLiteral("review"));
    QVERIFY(skill);
    QCOMPARE(skill->id, QStringLiteral("review"));
    QCOMPARE(skill->name, QStringLiteral("code-review"));
    QCOMPARE(skill->description, QStringLiteral("审查 diff 并给出分级问题"));
    QCOMPARE(skill->allowedTools,
             QStringList({QStringLiteral("read_file"), QStringLiteral("search_code"),
                          QStringLiteral("git_diff")}));
    QVERIFY(skill->source == SkillSource::User);
    QCOMPARE(skill->sourceId(), QStringLiteral("user"));
    QVERIFY(!skill->truncated);
    QCOMPARE(skill->bytes, qint64(markdown.size()));
    QCOMPARE(skill->body, QStringLiteral("正文第一行\n第二行\n"));
    QVERIFY(!skill->body.contains(QStringLiteral("---")));
    QVERIFY(skill->sourcePath.endsWith(QStringLiteral("SKILL.md")));
    QVERIFY(skill->sourcePath.contains(QStringLiteral("review")));
}

void TestSkills::fallsBackWhenFrontmatterMissing() {
    QTemporaryDir userRoot;
    QVERIFY(userRoot.isValid());
    const QByteArray markdown = "\n\n# 审查规则\n通过后再提交\n";
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("loose"), markdown));

    SkillRegistry registry;
    registry.setUserRoot(userRoot.path());
    registry.reload();

    const Skill* skill = registry.find(QStringLiteral("loose"));
    QVERIFY(skill);
    QCOMPARE(skill->name, QStringLiteral("loose")); // 缺省 = 目录名
    QCOMPARE(skill->description, QStringLiteral("# 审查规则")); // 首个非空正文行
    QVERIFY(skill->body.contains(QStringLiteral("# 审查规则")));
    QVERIFY(skill->allowedTools.isEmpty());
    QVERIFY(!skill->truncated);
}

void TestSkills::projectOverridesUser() {
    QTemporaryDir projectRoot;
    QTemporaryDir userRoot;
    QVERIFY(projectRoot.isValid());
    QVERIFY(userRoot.isValid());

    const QString projectSkills =
        QDir(projectRoot.path()).filePath(QStringLiteral(".nexusai/skills"));
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("shared"),
                       "---\nname: shared\ndescription: 用户版本\n---\nuser body\n"));
    QVERIFY(writeSkill(projectSkills, QStringLiteral("shared"),
                       "---\nname: shared\ndescription: 项目版本\n---\nproject body\n"));

    SkillRegistry registry;
    registry.setUserRoot(userRoot.path());
    registry.setProjectRoot(projectRoot.path());
    QCOMPARE(registry.projectSkillsRoot(), projectSkills);
    registry.reload();

    QCOMPARE(registry.all().size(), 1);
    const Skill* skill = registry.find(QStringLiteral("shared"));
    QVERIFY(skill);
    QCOMPARE(skill->description, QStringLiteral("项目版本"));
    QVERIFY(skill->body.contains(QStringLiteral("project body")));
    QVERIFY(skill->source == SkillSource::Project);
    QCOMPARE(skill->sourceId(), QStringLiteral("project"));
    QVERIFY(skill->sourcePath.contains(QStringLiteral(".nexusai")));
}

void TestSkills::deduplicatesSameId() {
    QTemporaryDir projectRoot;
    QTemporaryDir userRoot;
    QVERIFY(projectRoot.isValid());
    QVERIFY(userRoot.isValid());

    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("b-user"),
                       "---\nname: b\ndescription: user b\n---\nbody\n"));
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("same"),
                       "---\nname: same\ndescription: user same\n---\nbody\n"));
    QVERIFY(writeSkill(QDir(projectRoot.path()).filePath(QStringLiteral(".nexusai/skills")),
                       QStringLiteral("same"),
                       "---\nname: same\ndescription: project same\n---\nbody\n"));

    SkillRegistry registry;
    registry.setUserRoot(userRoot.path());
    registry.setProjectRoot(projectRoot.path());
    registry.reload();

    QCOMPARE(registry.all().size(), 2);
    QStringList ids;
    for (const Skill& skill : registry.all())
        ids.append(skill.id);
    QCOMPARE(ids, QStringList({QStringLiteral("b-user"), QStringLiteral("same")}));
    QCOMPARE(registry.find(QStringLiteral("same"))->description,
             QStringLiteral("project same"));
}

void TestSkills::searchesByNameAndDescription() {
    QTemporaryDir userRoot;
    QVERIFY(userRoot.isValid());
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("alpha"),
                       "---\nname: Alpha Skill\ndescription: 检查 DIFF\n---\nbody\n"));
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("beta"),
                       "---\nname: beta\ndescription: 翻译文本\n---\nbody\n"));
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("gamma"),
                       "---\nname: gamma\ndescription: 无关内容\n---\nbody\n"));

    SkillRegistry registry;
    registry.setUserRoot(userRoot.path());
    registry.reload();

    QCOMPARE(registry.search(QStringLiteral("ALPHA")).size(), 1);
    QCOMPARE(registry.search(QStringLiteral("alpha")).first().id, QStringLiteral("alpha"));
    // description 子串且大小写不敏感
    QCOMPARE(registry.search(QStringLiteral("diff")).size(), 1);
    QCOMPARE(registry.search(QStringLiteral("BETA")).size(), 1);
    QCOMPARE(registry.search(QString()).size(), 3);
    QCOMPARE(registry.search(QString(), 2).size(), 2);
    QCOMPARE(registry.search(QStringLiteral("  alpha  ")).size(), 1);
    QVERIFY(registry.search(QStringLiteral("no-such-skill")).isEmpty());
}

void TestSkills::truncatesOversizedFile() {
    QTemporaryDir userRoot;
    QVERIFY(userRoot.isValid());

    QByteArray ascii = "---\nname: big\n---\n";
    ascii.append(QByteArray(70 * 1024, 'x'));
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("big"), ascii));

    // 让 64 KiB 边界正好落在三字节 UTF-8 字符中间，验证不会留下半个字符。
    QByteArray utf8 = "---\nname: zh\n---\n";
    utf8.append(QStringLiteral("中").repeated(22000).toUtf8());
    QVERIFY(writeSkill(userRoot.path(), QStringLiteral("zh"), utf8));

    SkillRegistry registry;
    registry.setUserRoot(userRoot.path());
    registry.reload();

    const Skill* big = registry.find(QStringLiteral("big"));
    QVERIFY(big);
    QVERIFY(big->truncated);
    QCOMPARE(big->bytes, qint64(ascii.size()));
    QVERIFY(big->bytes > SkillRegistry::kMaxFileBytes);
    QVERIFY(big->body.size() <= SkillRegistry::kMaxFileBytes);
    QVERIFY(big->name == QStringLiteral("big"));

    const Skill* zh = registry.find(QStringLiteral("zh"));
    QVERIFY(zh);
    QVERIFY(zh->truncated);
    QCOMPARE(zh->bytes, qint64(utf8.size()));
    QVERIFY(!zh->body.contains(QChar(0xFFFD))); // 无替换字符
    QVERIFY(zh->body.size() > 0);
}

void TestSkills::capsTotalCountAt200() {
    QTemporaryDir userRoot;
    QVERIFY(userRoot.isValid());
    for (int i = 0; i < SkillRegistry::kMaxSkills + 5; ++i) {
        const QString id =
            QStringLiteral("s%1").arg(i, 3, 10, QLatin1Char('0'));
        QVERIFY(writeSkill(userRoot.path(), id, "---\nname: x\n---\nbody\n"));
    }

    SkillRegistry registry;
    registry.setUserRoot(userRoot.path());
    registry.reload();

    QCOMPARE(registry.all().size(), SkillRegistry::kMaxSkills);
    QCOMPARE(registry.all().first().id, QStringLiteral("s000"));
    QCOMPARE(registry.all().last().id, QStringLiteral("s199"));
    QVERIFY(registry.find(QStringLiteral("s204")) == nullptr);
}

void TestSkills::rejectsDirectoryLinkOutsideRoot() {
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    QVERIFY(writeSkill(outside.path(), QStringLiteral("evil"),
                       "---\nname: evil\ndescription: escaped\n---\nbody\n"));

    const QString linkPath = QDir(root.path()).filePath(QStringLiteral("linked"));
    if (!makeDirectoryLink(linkPath, QDir(outside.path()).filePath(QStringLiteral("evil"))))
        QSKIP("junction/symlink creation unavailable on this host");

    {
        SkillRegistry registry;
        registry.setUserRoot(root.path());
        registry.reload();
        QVERIFY2(registry.find(QStringLiteral("linked")) == nullptr,
                 "skill behind an out-of-root junction must be rejected");
        QVERIFY(registry.all().isEmpty());
    }
    removeDirectoryLink(linkPath);
}

void TestSkills::rejectsFileLinkOutsideRoot() {
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());

    const QString outsideFile = QDir(outside.path()).filePath(QStringLiteral("SKILL.md"));
    QVERIFY(writeFile(outsideFile, "---\nname: escaped\n---\nbody\n"));
    const QString skillDir = QDir(root.path()).filePath(QStringLiteral("sneaky"));
    QVERIFY(QDir().mkpath(skillDir));
    const QString skillPath = QDir(skillDir).filePath(QStringLiteral("SKILL.md"));
    if (!makeFileLink(skillPath, outsideFile))
        QSKIP("file symlink creation unavailable on this host");

    {
        SkillRegistry registry;
        registry.setUserRoot(root.path());
        registry.reload();
        QVERIFY2(registry.find(QStringLiteral("sneaky")) == nullptr,
                 "SKILL.md symlink escaping the root must be rejected");
    }
    QFile::remove(skillPath);
}

void TestSkills::handlesEmptyAndMissingRoots() {
    QTemporaryDir emptyRoot;
    QVERIFY(emptyRoot.isValid());

    SkillRegistry registry;
    registry.setUserRoot(emptyRoot.path());
    registry.setProjectRoot(QDir(emptyRoot.path()).filePath(QStringLiteral("no-project")));
    registry.reload();

    QVERIFY(registry.all().isEmpty());
    QVERIFY(registry.search(QStringLiteral("anything")).isEmpty());
    QVERIFY(registry.find(QStringLiteral("anything")) == nullptr);

    // 默认用户根仍来自 QStandardPaths（测试不触碰它，只验证可用性）。
    QVERIFY(!SkillRegistry::defaultUserRoot().isEmpty());
    QCOMPARE(SkillRegistry::kMaxSkills, 200);
    QCOMPARE(SkillRegistry::kMaxFileBytes, qint64(64 * 1024));
}

void TestSkills::settingsSchemaRegistersSkillKeys() {
    const SettingsKeyDef* enabled = SettingsSchema::definition(
        QStringLiteral("skills/enabled"));
    QVERIFY(enabled);
    QCOMPARE(enabled->type, SettingValueType::Bool);
    QCOMPARE(enabled->defaultValue.toBool(), true);
    QVERIFY(SettingsSchema::isValidValue(QStringLiteral("skills/enabled"), true));
    QVERIFY(!SettingsSchema::isValidValue(QStringLiteral("skills/enabled"), 1));

    const SettingsKeyDef* projectEnabled = SettingsSchema::definition(
        QStringLiteral("skills/projectEnabled"));
    QVERIFY(projectEnabled);
    QCOMPARE(projectEnabled->type, SettingValueType::Bool);
    QCOMPARE(projectEnabled->defaultValue.toBool(), true);

    const SettingsKeyDef* maxPerMessage = SettingsSchema::definition(
        QStringLiteral("skills/maxPerMessage"));
    QVERIFY(maxPerMessage);
    QCOMPARE(maxPerMessage->type, SettingValueType::Int);
    QCOMPARE(maxPerMessage->defaultValue.toInt(), 3);
    QVERIFY(maxPerMessage->constraint.hasRange);
    QCOMPARE(maxPerMessage->constraint.min, 1);
    QCOMPARE(maxPerMessage->constraint.max, 5);
    QCOMPARE(SettingsSchema::clampInt(QStringLiteral("skills/maxPerMessage"), 0), 1);
    QCOMPARE(SettingsSchema::clampInt(QStringLiteral("skills/maxPerMessage"), 9), 5);
    QVERIFY(SettingsSchema::isValidValue(QStringLiteral("skills/maxPerMessage"), 3));
    QVERIFY(!SettingsSchema::isValidValue(QStringLiteral("skills/maxPerMessage"), 6));
}

QTEST_GUILESS_MAIN(TestSkills)

#include "test_skills.moc"