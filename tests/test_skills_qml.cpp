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
#include <QSettings>

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
        QDir(m_userSkillDir).removeRecursively();
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

private:
    static QQuickItem* windowItem(const Harness& h) {
        return qobject_cast<QQuickItem*>(h.window->contentItem());
    }

    AIAgent* m_agent = nullptr;
};

QTEST_MAIN(TestSkillsQml)
#include "test_skills_qml.moc"