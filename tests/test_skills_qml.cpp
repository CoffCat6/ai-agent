// docs/37 W3：技能设置页（SkillsPage）的 QML 回归。
//
// 覆盖三件事：
//   1. 项目/用户两个来源都被列出，来源字段与页面分组一致；
//   2. 刷新按钮路径（page.refresh → AgentVM.reloadSkills）能发现新增技能；
//   3. 目录说明文案存在，用户不会面对一个空白页猜技能放哪儿。
//
// 加载方式与 test_tools_page_qml 相同：build/NexusAI 的文件系统副本 +
// qrc 回退拦截器 + offscreen。
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

#include "core/agent/AIAgent.h"
#include "core/skills/SkillRegistry.h"
#include "viewmodels/AgentViewModel.h"
#include "viewmodels/ThemeProvider.h"
#include "TestSettingsIsolation.h"

using namespace NexusAI;

namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;

class NexusQrcFallback : public QQmlAbstractUrlInterceptor {
public:
    explicit NexusQrcFallback(QString baseDir) : m_baseDir(std::move(baseDir)) {}
    QUrl intercept(const QUrl& url, DataType) override {
        static const QString prefix = QStringLiteral("/qt/qml/NexusAI");
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString path = url.path();
            if (path.startsWith(prefix)) {
                const QString fsPath = m_baseDir + path.mid(prefix.length());
                if (QFile::exists(fsPath)) return QUrl::fromLocalFile(fsPath);
            }
        }
        return url;
    }
private:
    QString m_baseDir;
};

// 动态创建的 delegate 不保证是窗口的 QObject 后代，按 visual parent 递归。
void collectByName(QQuickItem* item, const QString& name,
                   QList<QQuickItem*>* out) {
    if (item->objectName() == name) out->append(item);
    for (QQuickItem* kid : item->childItems()) collectByName(kid, name, out);
}

bool writeSkill(const QString& root, const QString& id, const QString& description,
                const QString& body) {
    const QString dir = root + QLatin1Char('/') + id;
    if (!QDir().mkpath(dir)) return false;
    QFile file(dir + QStringLiteral("/SKILL.md"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray content =
        QStringLiteral("---\nname: %1\ndescription: %2\n---\n%3\n")
            .arg(id, description, body).toUtf8();
    return file.write(content) == content.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString skillPath(const QString& skillsRoot, const QString& id) {
    return QDir(skillsRoot).filePath(id + QStringLiteral("/SKILL.md"));
}

struct Harness {
    NexusQrcFallback fallback{QStringLiteral(NEXUS_BUILD_DIR "/NexusAI")};
    QQmlApplicationEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QString error;

    bool load() {
        engine.addUrlInterceptor(&fallback);
        engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(
                QStringLiteral(NEXUS_TEST_QML_DIR "/SkillsHarness.qml")));
        if (!component.isReady()) {
            error = component.errorString();
            return false;
        }
        root.reset(component.create());
        if (!root) {
            error = component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow*>(root.get());
        if (!window) {
            error = QStringLiteral("harness root is not a QQuickWindow");
            return false;
        }
        window->show();
        if (!QTest::qWaitForWindowExposed(window)) {
            error = QStringLiteral("window was not exposed");
            return false;
        }
        QTest::qWait(150);
        return true;
    }

    QObject* page() const {
        return window->findChild<QObject*>(QStringLiteral("skillsPage"));
    }

    int invokeInt(const char* method) const {
        QVariant value;
        if (!QMetaObject::invokeMethod(root.get(), method, Q_RETURN_ARG(QVariant, value)))
            return -1;
        return value.toInt();
    }
};
} // namespace

class TestSkillsQml : public QObject {
    Q_OBJECT

    QTemporaryDir m_project;
    QString m_userRoot;
    QString m_userSkillDir;

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
        qputenv("QT_QUICK_BACKEND", "software");
        QVERIFY(m_project.isValid());

        // 项目技能：<root>/.nexusai/skills/<id>/SKILL.md
        const QString projectSkills =
            m_project.path() + QStringLiteral("/.nexusai/skills");
        QVERIFY(writeSkill(projectSkills, QStringLiteral("project-review"),
                           QStringLiteral("Review a diff"),
                           QStringLiteral("Report highest severity first.")));

        // 用户技能：AppConfigLocation/skills/<id>/SKILL.md
        m_userRoot = SkillRegistry::defaultUserRoot();
        // setTestModeEnabled 之后该目录是测试专用副本；先清掉上一次失败
        // 用例留下的技能，否则「只有 2 个技能」的断言会被历史残留打破。
        QDir(m_userRoot).removeRecursively();
        QVERIFY(QDir().mkpath(m_userRoot));
        m_userSkillDir = m_userRoot + QStringLiteral("/user-helper");
        QVERIFY(writeSkill(m_userRoot, QStringLiteral("user-helper"),
                           QStringLiteral("User helper"),
                           QStringLiteral("Always answer in bullet points.")));

        AgentViewModel* vm = AgentViewModel::instance();
        m_agent = new AIAgent(this);
        vm->setAgent(m_agent);
        m_agent->setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                                  m_project.path());
        vm->reloadSkills();
    }

    void cleanupTestCase() {
        if (AgentViewModel::instance())
            AgentViewModel::instance()->setAgent(nullptr);
        // 整个用户技能根都是测试副本，直接清空避免污染后续运行。
        QDir(m_userRoot).removeRecursively();
    }

    void listsProjectAndUserSkills() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QObject* page = h.page();
        QVERIFY(page);

        // 组件 onCompleted 已经调过 refresh；两个来源各一条。
        QCOMPARE(h.invokeInt("skillCount"), 2);
        QCOMPARE(h.invokeInt("projectCount"), 1);
        QCOMPARE(h.invokeInt("userCount"), 1);

        QList<QQuickItem*> rows;
        collectByName(windowItem(h), QStringLiteral("skillRow"), &rows);
        QCOMPARE(rows.size(), 2);
        QStringList sources;
        for (QQuickItem* row : rows) {
            const QVariantMap data = row->property("modelData").toMap();
            sources << data.value(QStringLiteral("sourceId")).toString();
        }
        sources.sort();
        QCOMPARE(sources, (QStringList{QStringLiteral("project"),
                                       QStringLiteral("user")}));
    }

    void refreshPicksUpNewUserSkill() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QCOMPARE(h.invokeInt("skillCount"), 2);

        QVERIFY(writeSkill(m_userRoot, QStringLiteral("user-extra"),
                           QStringLiteral("Extra helper"),
                           QStringLiteral("Prefer tables.")));
        QVERIFY(QMetaObject::invokeMethod(h.page(), "refresh"));
        QTest::qWait(120);

        QCOMPARE(h.invokeInt("skillCount"), 3);
        QCOMPARE(h.invokeInt("userCount"), 2);
        QDir(m_userRoot + QStringLiteral("/user-extra")).removeRecursively();
    }

    void directoryHintsExplainWhereSkillsLive() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        auto* projectHint =
            h.window->findChild<QQuickItem*>(QStringLiteral("skillsProjectRootHint"));
        auto* userHint =
            h.window->findChild<QQuickItem*>(QStringLiteral("skillsUserRootHint"));
        QVERIFY(projectHint);
        QVERIFY(userHint);
        QVERIFY(projectHint->property("text").toString().contains(
            QStringLiteral(".nexusai/skills")));
        QVERIFY(userHint->property("text").toString().contains(
            QStringLiteral("skills/<id>/SKILL.md")));
    }

    // docs/37 W5-1：技能设置 Section 的控件与开关写读链路。
    void settingsSectionExposesControls() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QVERIFY(h.window->findChild<QQuickItem*>(
            QStringLiteral("skillsEnabledSwitch")));
        QVERIFY(h.window->findChild<QQuickItem*>(
            QStringLiteral("skillsProjectSwitch")));
        QVERIFY(h.window->findChild<QQuickItem*>(
            QStringLiteral("skillsMaxPerMessageSpin")));
        QVERIFY(h.window->findChild<QQuickItem*>(
            QStringLiteral("skillsOpenUserDirButton")));
        QVERIFY(h.window->findChild<QQuickItem*>(
            QStringLiteral("skillsOpenProjectDirButton")));
        // docs/37 W5-2b：新建入口 + 每行编辑/删除按钮。
        QVERIFY(h.window->findChild<QQuickItem*>(
            QStringLiteral("skillsNewButton")));
        QList<QQuickItem*> editButtons;
        QList<QQuickItem*> deleteButtons;
        collectByName(windowItem(h), QStringLiteral("skillEditButton"),
                      &editButtons);
        collectByName(windowItem(h), QStringLiteral("skillDeleteButton"),
                      &deleteButtons);
        QCOMPARE(editButtons.size(), 2);
        QCOMPARE(deleteButtons.size(), 2);
        QVERIFY(h.window->findChild<QObject*>(
            QStringLiteral("skillEditorDialog")));
        QVERIFY(h.window->findChild<QObject*>(
            QStringLiteral("skillsDeleteConfirm")));
    }

    void disablingSkillsEmptiesList() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        auto* vm = AgentViewModel::instance();
        QVERIFY(vm);

        QVERIFY(vm->setSkillFeatureEnabled(false));
        QVERIFY(QMetaObject::invokeMethod(h.page(), "refresh"));
        QTest::qWait(80);
        QCOMPARE(h.invokeInt("skillCount"), 0);
        QVERIFY(vm->listSkills(QString(), 200).isEmpty());

        QVERIFY(vm->setSkillFeatureEnabled(true));
        QVERIFY(QMetaObject::invokeMethod(h.page(), "refresh"));
        QTest::qWait(80);
        QCOMPARE(h.invokeInt("skillCount"), 2);
    }

    void maxPerMessageCapsResolvedSkills() {
        auto* vm = AgentViewModel::instance();
        QVERIFY(vm);
        const QString root = projectSkillsRoot();
        QVERIFY(writeSkill(root, QStringLiteral("cap-review"),
                           QStringLiteral("review"), QStringLiteral("review body")));
        QVERIFY(writeSkill(root, QStringLiteral("cap-test"),
                           QStringLiteral("test"), QStringLiteral("test body")));
        QVERIFY(writeSkill(root, QStringLiteral("cap-commit"),
                           QStringLiteral("commit"), QStringLiteral("commit body")));

        QVERIFY(vm->setSkillMaxPerMessage(2));
        const QVariantList capped =
            vm->resolveSkills(QStringLiteral("$cap-review $cap-test $cap-commit"));
        QCOMPARE(capped.size(), 2);
        QCOMPARE(capped.at(0).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("cap-review"));
        QCOMPARE(capped.at(1).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("cap-test"));

        // 上限放宽后三个都能解析，证明 2 是 clamp 后的真实上限而非发现缺失。
        QVERIFY(vm->setSkillMaxPerMessage(5));
        QCOMPARE(vm->resolveSkills(
                     QStringLiteral("$cap-review $cap-test $cap-commit")).size(), 3);
    }

    void openSkillsDirectoryUsesExpectedRoots() {
        auto* vm = AgentViewModel::instance();
        QVERIFY(vm);
        QString lastPath;
        int calls = 0;
        vm->setOpenUrlHookForTest([&](const QUrl& url) {
            ++calls;
            lastPath = url.toLocalFile();
            return true;
        });

        QVERIFY(vm->openSkillsDirectory(QStringLiteral("user")));
        QCOMPARE(calls, 1);
        QVERIFY(lastPath.endsWith(QLatin1String("/skills")));
        QCOMPARE(QDir::cleanPath(lastPath),
                 QDir::cleanPath(SkillRegistry::defaultUserRoot()));

        QVERIFY(vm->openSkillsDirectory(QStringLiteral("project")));
        QCOMPARE(calls, 2);
        QCOMPARE(QDir::cleanPath(lastPath),
                 QDir::cleanPath(projectSkillsRoot()));

        // 未知来源不触碰文件系统；无项目时 project 分支返回 false。
        QVERIFY(!vm->openSkillsDirectory(QStringLiteral("bogus")));
        QCOMPARE(calls, 2);

        vm->setOpenUrlHookForTest({});
    }

    // docs/37 W5-2b：编辑器新建用户技能 → 磁盘出现 SKILL.md + 页面刷新。
    void editorCreatesUserSkill() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QObject* editor = h.window->findChild<QObject*>(
            QStringLiteral("skillEditorDialog"));
        QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(
            editor, "openCreate", Q_ARG(QVariant, QStringLiteral("user"))));
        QTest::qWait(80);

        auto* idField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillIdField"));
        auto* nameField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillNameField"));
        auto* descField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillDescriptionField"));
        auto* toolsField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillToolsField"));
        auto* bodyArea = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillBodyArea"));
        QVERIFY(idField && nameField && descField && toolsField && bodyArea);

        idField->setProperty("text", QStringLiteral("qml-created"));
        nameField->setProperty("text", QStringLiteral("QML created"));
        descField->setProperty("text", QStringLiteral("created by qml test"));
        toolsField->setProperty("text",
                                QStringLiteral("read_file, search_code"));
        bodyArea->setProperty("text", QStringLiteral("Use tables.\n"));

        QVERIFY(QMetaObject::invokeMethod(editor, "submit"));
        QTest::qWait(120);

        const QString path =
            skillPath(m_userRoot, QStringLiteral("qml-created"));
        QVERIFY2(QFileInfo(path).isFile(), qPrintable(path));
        const QByteArray bytes = readFile(path);
        QVERIFY(bytes.contains("allowed-tools: [read_file, search_code]"));
        QVERIFY(bytes.endsWith("Use tables.\n"));
        // saved() 触发 page.refresh()：用户来源从 1 个变成 2 个。

        QCOMPARE(h.invokeInt("skillCount"), 3);

        QDir(m_userRoot + QStringLiteral("/qml-created")).removeRecursively();
    }

    // 编辑：加载已有技能回填字段 → 修改正文 → 保存后文件更新。
    void editorLoadsAndUpdatesExistingSkill() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QObject* editor = h.window->findChild<QObject*>(
            QStringLiteral("skillEditorDialog"));
        QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(
            editor, "openEdit", Q_ARG(QVariant, QStringLiteral("user-helper")),
            Q_ARG(QVariant, QStringLiteral("user"))));
        QTest::qWait(80);

        auto* idField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillIdField"));
        auto* bodyArea = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillBodyArea"));
        QVERIFY(idField && bodyArea);
        QCOMPARE(idField->property("text").toString(),
                 QStringLiteral("user-helper"));
        QVERIFY(idField->property("readOnly").toBool());
        QCOMPARE(bodyArea->property("text").toString(),
                 QStringLiteral("Always answer in bullet points.\n"));

        bodyArea->setProperty("text", QStringLiteral("Updated body.\n"));
        QVERIFY(QMetaObject::invokeMethod(editor, "submit"));
        QTest::qWait(120);

        const QString path =
            skillPath(m_userRoot, QStringLiteral("user-helper"));
        QVERIFY(readFile(path).contains("Updated body."));
        // 编辑路径是覆盖写，旧内容进入 .bak。
        QVERIFY(QFileInfo(path + QStringLiteral(".bak")).isFile());
        QCOMPARE(h.invokeInt("skillCount"), 2);
    }

    // 非法 id：实时提示非空，submit 不落盘、列表不变。
    void editorRejectsUnsafeId() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QObject* editor = h.window->findChild<QObject*>(
            QStringLiteral("skillEditorDialog"));
        QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(
            editor, "openCreate", Q_ARG(QVariant, QStringLiteral("user"))));
        QTest::qWait(80);

        auto* idField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillIdField"));
        QVERIFY(idField);
        idField->setProperty("text", QStringLiteral("../x"));
        QTest::qWait(30);

        QVERIFY(!editor->property("idError").toString().isEmpty());
        QVERIFY(!AgentViewModel::instance()
                     ->validateSkillId(QStringLiteral("../x"))
                     .isEmpty());
        QVERIFY(AgentViewModel::instance()->validateSkillId(QString()).isEmpty());

        QVERIFY(QMetaObject::invokeMethod(editor, "submit"));
        QTest::qWait(60);
        QVERIFY(!editor->property("errorText").toString().isEmpty());
        QCOMPARE(h.invokeInt("skillCount"), 2);
        // "../x" 解析到用户根之上；校验在拼路径前失败，不能写出任何文件。
        QVERIFY(!QFileInfo(QDir(m_userRoot).absolutePath()
                           + QStringLiteral("/x/SKILL.md"))
                     .exists());
    }

    // 删除：确认弹窗点“删除”后文件消失、页面列表刷新。
    void deleteConfirmationRemovesSkill() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QVERIFY(writeSkill(m_userRoot, QStringLiteral("delete-me"),
                           QStringLiteral("temporary"),
                           QStringLiteral("to be deleted")));
        QVERIFY(QMetaObject::invokeMethod(h.page(), "refresh"));
        QTest::qWait(80);
        QCOMPARE(h.invokeInt("skillCount"), 3);

        QObject* confirm = h.window->findChild<QObject*>(
            QStringLiteral("skillsDeleteConfirm"));
        QVERIFY(confirm);
        QVERIFY(QMetaObject::invokeMethod(
            confirm, "openFor", Q_ARG(QVariant, QStringLiteral("delete-me")),
            Q_ARG(QVariant, QStringLiteral("user"))));
        QTest::qWait(50);

        QVariant removed;
        QVERIFY(QMetaObject::invokeMethod(
            confirm, "confirm", Q_RETURN_ARG(QVariant, removed)));
        QVERIFY(removed.toBool());
        QTest::qWait(80);

        QVERIFY(!QFileInfo(skillPath(m_userRoot,
                                     QStringLiteral("delete-me")))
                     .exists());
        QCOMPARE(h.invokeInt("skillCount"), 2);
    }

    // 覆盖：首次保存返回“已存在”，第二次 submit 才 overwrite 并生成 .bak。
    void createOverwriteNeedsSecondSubmit() {
        Harness h;
        QVERIFY2(h.load(), qPrintable(h.error));
        QVERIFY(writeSkill(m_userRoot, QStringLiteral("overwrite-me"),
                           QStringLiteral("original"),
                           QStringLiteral("original body")));
        QVERIFY(QMetaObject::invokeMethod(h.page(), "refresh"));
        QTest::qWait(80);

        QObject* editor = h.window->findChild<QObject*>(
            QStringLiteral("skillEditorDialog"));
        QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(
            editor, "openCreate", Q_ARG(QVariant, QStringLiteral("user"))));
        QTest::qWait(80);

        auto* idField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillIdField"));
        auto* nameField = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillNameField"));
        auto* bodyArea = h.window->findChild<QQuickItem*>(
            QStringLiteral("skillBodyArea"));
        QVERIFY(idField && nameField && bodyArea);
        idField->setProperty("text", QStringLiteral("overwrite-me"));
        nameField->setProperty("text", QStringLiteral("new name"));
        bodyArea->setProperty("text", QStringLiteral("new body\n"));

        const QString path =
            skillPath(m_userRoot, QStringLiteral("overwrite-me"));

        // 第一次：命中“已存在”，不覆盖、不生成 .bak，只切换确认文案。
        QVERIFY(QMetaObject::invokeMethod(editor, "submit"));
        QTest::qWait(80);
        QVERIFY(editor->property("overwritePending").toBool());
        QVERIFY(editor->property("errorText").toString()
                    .contains(QStringLiteral("已存在")));
        QVERIFY(!QFileInfo(path + QStringLiteral(".bak")).exists());
        QVERIFY(readFile(path).contains("original body"));

        // 第二次：overwrite=true，旧内容进入 .bak，正文更新。
        QVERIFY(QMetaObject::invokeMethod(editor, "submit"));
        QTest::qWait(120);
        QVERIFY(!editor->property("overwritePending").toBool());
        QVERIFY(QFileInfo(path + QStringLiteral(".bak")).isFile());
        QVERIFY(readFile(path).contains("new body"));
        QVERIFY(!readFile(path).contains("original body"));

        QDir(m_userRoot + QStringLiteral("/overwrite-me")).removeRecursively();
    }

    // 每个用例后恢复默认值，避免失败用例把开关留在关闭态影响后续断言。
    void cleanup() {
        auto* vm = AgentViewModel::instance();
        if (!vm) return;
        vm->setSkillFeatureEnabled(true);
        vm->setSkillProjectEnabled(true);
        vm->setSkillMaxPerMessage(3);
        vm->setOpenUrlHookForTest({});
        // 用例失败时函数会提前返回，这里统一清掉编辑器用例创建的用户技能，
        // 保证下一次运行仍从 2 个基线技能开始。
        for (const QString& id : {QStringLiteral("qml-created"),
                                  QStringLiteral("delete-me"),
                                  QStringLiteral("overwrite-me")})
            QDir(m_userRoot + QLatin1Char('/') + id).removeRecursively();
        // maxPerMessage 用例会临时创建 3 个项目技能；它也必须在用例后清掉，
        // 否则后续“共 N 个技能”的断言会被残留文件污染。
        const QString projectRoot = projectSkillsRoot();
        for (const QString& id : {QStringLiteral("cap-review"),
                                  QStringLiteral("cap-test"),
                                  QStringLiteral("cap-commit")})
            QDir(projectRoot + QLatin1Char('/') + id).removeRecursively();
        vm->reloadSkills();
    }

private:
    QString projectSkillsRoot() const {
        return m_project.path() + QStringLiteral("/.nexusai/skills");
    }

    static QQuickItem* windowItem(const Harness& h) {
        return qobject_cast<QQuickItem*>(h.window->contentItem());
    }

    AIAgent* m_agent = nullptr;
};

QTEST_MAIN(TestSkillsQml)
#include "test_skills_qml.moc"