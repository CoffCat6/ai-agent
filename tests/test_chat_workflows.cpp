#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QSemaphore>
#include <QThreadPool>
#include <QScopeGuard>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>

#include "core/agent/AIAgent.h"
#include "core/memory/SqliteMemory.h"
#include "core/tools/ToolRegistry.h"
#include "infrastructure/llm/IAIService.h"
#include "viewmodels/AgentViewModel.h"
#include "viewmodels/ChangeBus.h"
#include "viewmodels/GitViewModel.h"
#include "viewmodels/ThemeProvider.h"
#include "viewmodels/SettingsService.h"
#include "viewmodels/TraceModel.h"
#include "TestSettingsIsolation.h"

// 测试进程的 QSettings 重定向到临时 INI（见 TestSettingsIsolation.h），
// 防止测试改写用户真实设置。
namespace {
const NexusAI::TestSupport::SettingsIsolation kSettingsIsolation;
} // namespace

using namespace NexusAI;

namespace {

// W1：/ 命令补全的 QML 回归夹具。加载完整 main.qml 而不是单独实例化
// InputConsole，因为补全候选来自 Settings 单例、且需要真实窗口焦点才能
// 用 QTest 发送键盘事件。每个用例独立引擎，避免草稿/面板状态互相污染。
struct SlashHarness {
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    QObject* console = nullptr;
    QObject* field = nullptr;
    QObject* popup = nullptr;
    QObject* skillPopup = nullptr;
    QString error;

    bool load() {
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        if (engine.rootObjects().isEmpty()) {
            error = QStringLiteral("main.qml produced no root object");
            return false;
        }
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) {
            error = QStringLiteral("root object is not a QQuickWindow");
            return false;
        }
        window->resize(1200, 800);
        window->show();
        if (!QTest::qWaitForWindowExposed(window)) {
            error = QStringLiteral("window was not exposed");
            return false;
        }
        window->requestActivate();
        QTest::qWait(120);

        console = window->findChild<QObject*>(QStringLiteral("inputConsole"));
        field = window->findChild<QObject*>(QStringLiteral("messageInput"));
        popup = window->findChild<QObject*>(QStringLiteral("slashPopup"));
        skillPopup = window->findChild<QObject*>(QStringLiteral("skillPopup"));
        if (!console) error = QStringLiteral("inputConsole not found");
        else if (!field) error = QStringLiteral("messageInput not found");
        else if (!popup) error = QStringLiteral("slashPopup not found");
        else if (!skillPopup) error = QStringLiteral("skillPopup not found");
        if (!error.isEmpty()) return false;

        if (auto* item = qobject_cast<QQuickItem*>(field)) {
            item->forceActiveFocus();
            QTest::qWait(80);
        }
        return true;
    }

    // 模拟用户输入：先换文本再放光标到末尾，让 onTextChanged /
    // onCursorPositionChanged 各跑一次补全扫描。
    void setInput(const QString& text) {
        field->setProperty("text", text);
        field->setProperty("cursorPosition", text.size());
        QTest::qWait(60);
    }

    QString inputText() const { return field->property("text").toString(); }

    QVariantList results() const {
        return console->property("slashResults").toList();
    }

    QStringList resultNames() const {
        QStringList names;
        for (const QVariant& entry : results())
            names << entry.toMap().value(QStringLiteral("name")).toString();
        return names;
    }

    QStringList skillNames() const {
        QStringList names;
        for (const QVariant& entry :
             console->property("skillResults").toList())
            names << entry.toMap().value(QStringLiteral("name")).toString();
        return names;
    }

    QStringList chipNames() const {
        QStringList names;
        for (const QVariant& entry :
             console->property("skillChips").toList())
            names << entry.toMap().value(QStringLiteral("name")).toString();
        return names;
    }
};

// docs/37 W3：项目技能夹具。与 test_viewmodel 的 skillMentionResolveAndInjection
// 使用同一目录约定（<root>/.nexusai/skills/<id>/SKILL.md）。
bool writeProjectSkill(const QString& projectRoot, const QString& id,
                       const QString& description, const QString& body) {
    const QString dir = projectRoot + QStringLiteral("/.nexusai/skills/") + id;
    if (!QDir().mkpath(dir)) return false;
    QFile file(dir + QStringLiteral("/SKILL.md"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray content =
        QStringLiteral("---\nname: %1\ndescription: %2\n---\n%3\n")
            .arg(id, description, body).toUtf8();
    return file.write(content) == content.size();
}

bool writeBaseSkills(const QString& projectRoot) {
    return writeProjectSkill(projectRoot, QStringLiteral("review"),
                             QStringLiteral("Review a diff"),
                             QStringLiteral("Always report the highest severity first."))
        && writeProjectSkill(projectRoot, QStringLiteral("test"),
                             QStringLiteral("Write tests"),
                             QStringLiteral("Run the suite."))
        && writeProjectSkill(projectRoot, QStringLiteral("commit"),
                             QStringLiteral("Commit changes"),
                             QStringLiteral("Use conventional commits."))
        && writeProjectSkill(projectRoot, QStringLiteral("lint"),
                             QStringLiteral("Lint code"),
                             QStringLiteral("Fix every warning."));
}

} // namespace

class RecordingService : public IAIService {
    Q_OBJECT
public:
    using IAIService::IAIService;
    QVector<QJsonArray> requests;
    QVector<QJsonArray> definitions;
    QString modelName = "workflow-model";
    int cancellations = 0;
    void chatComplete(const QJsonArray& messages, const QJsonArray& tools) override {
        requests.append(messages);
        definitions.append(tools);
    }
    void cancel() override { ++cancellations; }
    void setApiKey(const QString&) override {}
    void setBaseUrl(const QString&) override {}
    void setModel(const QString& name) override { modelName = name; }
    QString model() const override { return modelName; }
    void answer(const QString& text) {
        LLMResponse response;
        response.success = true;
        response.content = text;
        emit responseReady(response);
    }
    void requestConfirmation(const QString& tool = "workflow_confirmation") {
        LLMResponse response;
        response.success = true;
        response.toolCalls.append({"call-test", tool, {}});
        emit responseReady(response);
    }
};

class ConfirmationTool : public ITool {
    QString m_name;
public:
    explicit ConfirmationTool(QString name = "workflow_confirmation") : m_name(std::move(name)) {}
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = m_name;
        def.level = ToolLevel::RequiresConfirm;
        def.parameters = QJsonObject{{"type", "object"}};
        return def;
    }
    ToolResult execute(const QJsonObject&) override { return ToolResult::ok("done"); }
};

class TestChatWorkflows : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir;
    AgentViewModel* vm = nullptr;

    std::unique_ptr<QObject> input(QQmlEngine& engine) {
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QStringLiteral(NEXUS_SOURCE_DIR "/src/ui/components/InputConsole.qml")));
        if (!component.isReady()) qWarning().noquote() << component.errorString();
        return std::unique_ptr<QObject>(component.create());
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        QStandardPaths::setTestModeEnabled(true);
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
        qputenv("QT_QUICK_BACKEND", "software");
        qmlRegisterModule("NexusAI", 1, 0);
        vm = AgentViewModel::instance();
        ToolRegistry::instance().registerTool(std::make_unique<ConfirmationTool>());
    }

    void cleanup() {
        vm->setModelConfigurator({});
        vm->setAgentFactory({});
        vm->setAgent(nullptr);
        vm->setMemory(nullptr);
        while (vm->tabModel().size() > 1) vm->closeTab(1);
        vm->setDraftText({});
        vm->setDraftAttachments({});
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void realInputSendsAttachmentAndKeepsRejectedDraft() {
        QQmlEngine engine;
        auto console = input(engine);
        QVERIFY(console);
        auto* field = console->findChild<QObject*>("messageInput");
        QVERIFY(field);
        field->setProperty("text", "read this file");
        const QVariantList files{QVariantMap{{"kind", "file"}, {"name", "note.txt"}, {"content", "unique payload"}}};
        console->setProperty("pendingAttachments", files);
        QVERIFY(QMetaObject::invokeMethod(console.get(), "sendCurrentText"));
        QCOMPARE(field->property("text").toString(), QString("read this file"));
        QCOMPARE(console->property("pendingAttachments").toList().size(), 1);

        RecordingService service;
        AIAgent agent;
        agent.setLLMService(&service);
        vm->setAgent(&agent);
        QVERIFY(QMetaObject::invokeMethod(console.get(), "sendCurrentText"));
        QCOMPARE(service.requests.size(), 1);
        QVERIFY(service.requests.last().last().toObject().value("content").toString().contains("unique payload"));
        QCOMPARE(agent.messages().last().toObject().value("files").toArray().size(), 1);
        QVERIFY(field->property("text").toString().isEmpty());
        QVERIFY(console->property("pendingAttachments").toList().isEmpty());
        vm->setAgent(nullptr);
    }

    void realInputRestoresEachTab() {
        QQmlEngine engine;
        auto console = input(engine);
        QVERIFY(console);
        auto* field = console->findChild<QObject*>("messageInput");
        QVERIFY(field);
        field->setProperty("text", "draft A");
        console->setProperty("pendingAttachments", QVariantList{
            QVariantMap{{"kind", "file"}, {"name", "a.txt"}, {"content", "A"}}});
        vm->openTab();
        QCOMPARE(field->property("text").toString(), QString());
        QVERIFY(console->property("pendingAttachments").toList().isEmpty());
        field->setProperty("text", "draft B");
        vm->activateTab(0);
        QCOMPARE(field->property("text").toString(), QString("draft A"));
        QCOMPARE(console->property("pendingAttachments").toList().size(), 1);
        vm->activateTab(1);
        QCOMPARE(field->property("text").toString(), QString("draft B"));
        vm->closeTab(1);
        QCOMPARE(field->property("text").toString(), QString("draft A"));
    }

    void editReplacesContextTraceAndStorageAndKeepsFiles() {
        SqliteMemory memory(":memory:");
        RecordingService service;
        AIAgent agent;
        agent.setMemory(&memory);
        agent.setLLMService(&service);
        QVERIFY(agent.startTask("earlier question"));
        service.answer("earlier answer");
        QVERIFY(agent.startTask("old question", {}, QJsonArray{
            QJsonObject{{"name", "note.txt"}, {"content", "attachment evidence"}}}));
        service.answer("old answer");
        const QString session = agent.currentSessionId();
        QVERIFY(agent.editLastUserMessage("new question"));
        const auto sent = service.requests.last();
        QCOMPARE(sent.size(), 4);
        QVERIFY(sent.last().toObject().value("content").toString().contains("attachment evidence"));
        for (const auto& message : sent) {
            QVERIFY(!message.toObject().value("content").toString().contains("old question"));
            QVERIFY(message.toObject().value("content") != QLatin1String("old answer"));
        }
        service.answer("new answer");
        const auto trace = agent.traceModel();
        agent.resumeSession(session);
        const auto restoredTrace = agent.traceModel();
        QCOMPARE(restoredTrace.size(), trace.size());
        for (int i = 0; i < trace.size(); ++i) {
            auto restored = restoredTrace.at(i).toObject();
            restored.remove("order");
            QCOMPARE(restored, trace.at(i).toObject());
        }
        QCOMPARE(memory.loadMessages(session, 100).size(), 4);
        QVERIFY(!QJsonDocument(trace).toJson().contains("old answer"));

        // A persistence failure cannot remove the visible answer or context.
        QSqlQuery query(memory.databaseForTest());
        QVERIFY(query.exec("CREATE TRIGGER refuse_delete BEFORE DELETE ON messages BEGIN SELECT RAISE(ABORT, 'test'); END"));
        const auto before = agent.messages();
        QVERIFY(!agent.editLastUserMessage("must not replace"));
        QCOMPARE(agent.messages(), before);
        QCOMPARE(agent.traceModel(), restoredTrace);
        agent.setMemory(nullptr);
    }

    // docs/21 分叉为新会话：一次回答结束后从这里另起一条对话——副本**包括这条
    // 回答**，以它结尾，用户在新会话里接着问。源会话一字不动，所以分叉可回退，
    // 也不必在同一会话里养一棵分支树。
    void branchForksHistoryIntoNewSession() {
        SqliteMemory memory(":memory:");
        RecordingService service;
        AIAgent agent;
        agent.setMemory(&memory);
        agent.setLLMService(&service);

        QVERIFY(agent.startTask("first question"));
        service.answer("first answer");
        QVERIFY(agent.startTask("second question"));
        service.answer("second answer");
        const QString original = agent.currentSessionId();
        QVERIFY(!original.isEmpty());

        // 从第一条回答分叉：副本 = 第一问 + 第一答，正好以这条回答结尾。
        const QString forked = memory.forkSessionAtAnswer(original, 1,
                                                          QStringLiteral("forked chat"));
        QVERIFY(!forked.isEmpty());
        QVERIFY(forked != original);
        const QJsonArray carriedFromAnswer = memory.loadMessages(forked, 50);
        QCOMPARE(carriedFromAnswer.size(), 2);
        QCOMPARE(carriedFromAnswer.at(1).toObject().value("content").toString(),
                 QStringLiteral("first answer"));
        QCOMPARE(memory.loadMessages(original, 50).size(), 4);   // 源会话不受影响

        agent.resetSession();
        agent.resumeSession(forked);
        QCOMPARE(agent.currentSessionId(), forked);
        // 在新会话里接着问：上下文带着那条被分叉出来的回答。
        QVERIFY(agent.startTask("follow-up on the first answer"));
        service.answer("forked answer");
        QCOMPARE(memory.loadMessages(forked, 50).size(), 4);
        QCOMPARE(memory.loadMessages(original, 50).size(), 4);
        QCOMPARE(memory.loadMessages(original, 50).at(0).toObject()
                     .value("content").toString(), QStringLiteral("first question"));

        // 副本用的是新的行主键：两条对话之后各自继续也不会互相改写。
        agent.resetSession();
        agent.resumeSession(original);
        const QJsonArray originalRows = memory.loadMessages(original, 50);
        QVERIFY(originalRows.at(0).toObject().value("id").toVariant().toLongLong() != 0);
        QCOMPARE(QJsonDocument(carriedFromAnswer).toJson()
                     .contains(QJsonDocument(originalRows).toJson()), false);
        QCOMPARE(memory.loadMessages(original, 50).size(), 4);

        agent.setMemory(nullptr);
    }




    // docs/30 §4.2：后台会话跑完 → 侧栏置"完成未读"点，切回去即清。
    // 这是 VM 里唯一无法自证的状态转移（需要真实的任务 Busy→Idle 边沿），
    // 所以放在有 RecordingService 的集成套件里跑。会话必须真的进数据库，
    // 否则 sessionList() 为空、侧栏根本没有这一行可标。
    void backgroundCompletionMarksConversationUnread() {
        SqliteMemory memory(QStringLiteral(":memory:"));
        RecordingService firstService;
        AIAgent first;
        first.setMemory(&memory);
        first.setLLMService(&firstService);
        RecordingService* secondService = nullptr;
        vm->setAgentFactory([&]() {
            auto* agent = new AIAgent;
            agent->setMemory(&memory);
            secondService = new RecordingService(agent);
            agent->setLLMService(secondService);
            return agent;
        });
        vm->setMemory(&memory);
        vm->setAgent(&first);

        auto entryOf = [this](const QString& id) {
            for (const QVariant& e : vm->sessionList())
                if (e.toMap().value("id").toString() == id) return e.toMap();
            return QVariantMap{};
        };

        // 前台会话 A：发起任务后它就 busy 且 active。
        QVERIFY(vm->startTask("foreground question"));
        const QString sessionA = first.currentSessionId();
        QVERIFY(!sessionA.isEmpty());
        QCOMPARE(entryOf(sessionA).value("active").toBool(), true);
        QCOMPARE(entryOf(sessionA).value("busy").toBool(), true);

        // A 还在忙 → 打开空白对话只能新开一个 controller（引擎约束），
        // 于是 A 变成后台会话。
        const int tabsBefore = vm->tabModel().size();
        vm->newLocalChat();
        QCOMPARE(vm->tabModel().size(), tabsBefore + 1);
        QCOMPARE(entryOf(sessionA).value("active").toBool(), false);
        QCOMPARE(entryOf(sessionA).value("busy").toBool(), true);
        QCOMPARE(entryOf(sessionA).value("open").toBool(), true);

        // 前台（活动）会话跑完 → **不**置未读：用户正看着它。
        QVERIFY(vm->startTask("foreground question 2"));
        const QString sessionB = vm->currentSessionId();
        QVERIFY(!sessionB.isEmpty());
        QVERIFY(sessionB != sessionA);
        secondService->answer("answer for the foreground session");
        QVERIFY(!entryOf(sessionB).value("unread").toBool());

        // 后台会话 A 跑完 → 置未读（这是气泡通知之外的第二重可见性）。
        firstService.answer("answer for the background session");
        QCOMPARE(entryOf(sessionA).value("busy").toBool(), false);
        QCOMPARE(entryOf(sessionA).value("unread").toBool(), true);

        // 点回 A → 未读立刻清掉，并且它重新成为活动会话。
        vm->openConversation(sessionA);
        QCOMPARE(vm->currentSessionId(), sessionA);
        QCOMPARE(entryOf(sessionA).value("unread").toBool(), false);
        QCOMPARE(entryOf(sessionA).value("active").toBool(), true);

        vm->setAgent(nullptr);
        vm->setMemory(nullptr);
        first.setMemory(nullptr);
    }

    void simultaneousApprovalsRouteByRequestAndExpireOnStop() {
        RecordingService firstService;
        AIAgent first;
        first.setLLMService(&firstService);
        first.setToolRegistry(&ToolRegistry::instance());
        RecordingService* secondService = nullptr;
        vm->setAgentFactory([&]() {
            auto* agent = new AIAgent;
            secondService = new RecordingService(agent);
            agent->setLLMService(secondService);
            agent->setToolRegistry(&ToolRegistry::instance());
            return agent;
        });
        vm->setAgent(&first);
        QVERIFY(vm->startTask("A"));
        vm->openTab();
        QVERIFY(vm->startTask("B"));
        firstService.requestConfirmation();
        const QString firstId = vm->pendingConfirmation().value("id").toString();
        secondService->requestConfirmation();
        QCOMPARE(vm->pendingConfirmation().value("count").toInt(), 2);
        QCOMPARE(vm->pendingConfirmation().value("id").toString(), firstId);
        const auto controllers = vm->findChildren<ChatSessionController*>();
        const QString secondId = controllers.last()->agent()->pendingConfirmationId();
        vm->approveToolCall(secondId, false);
        QCOMPARE(secondService->requests.size(), 2);
        QCOMPARE(firstService.requests.size(), 1);
        QCOMPARE(vm->pendingConfirmation().value("id").toString(), firstId);
        vm->activateTab(0);
        vm->stop();
        QVERIFY(vm->pendingConfirmation().isEmpty());
        vm->approveToolCall(firstId, true); // stale verdict is harmless
        QCOMPARE(firstService.requests.size(), 1);
        vm->setAgent(nullptr);
    }

    void modelsAndRunningSnapshotsAreIndependent() {
        RecordingService firstService;
        AIAgent first;
        QHash<AIAgent*, RecordingService*> services{{&first, &firstService}};
        vm->setAgentFactory([&]() {
            auto* agent = new AIAgent;
            services.insert(agent, new RecordingService(agent));
            return agent;
        });
        vm->setModelConfigurator([&](AIAgent* agent, const QVariantMap& profile) {
            auto* service = services.value(agent);
            service->setModel(profile.value("model").toString());
            agent->setLLMService(service);
        });
        QVariantMap profileA{{"id", "A"}, {"model", "model A"}, {"apiKey", "secret"}};
        const QVariantMap profileB{{"id", "B"}, {"model", "model B"}};
        vm->updateModelProfiles({profileA, profileB}, profileA);
        vm->setAgent(&first);
        vm->selectChatModel("A");
        QVERIFY(vm->startTask("first task"));
        vm->openTab();
        vm->selectChatModel("B");
        QCOMPARE(vm->chatModelProfile().value("model").toString(), QString("model B"));
        QCOMPARE(first.currentModel(), QString("model A"));
        QVERIFY(first.isActive());
        QCOMPARE(firstService.cancellations, 0);
        profileA["model"] = "updated A";
        vm->updateModelProfiles({profileA, profileB}, profileB);
        QCOMPARE(first.currentModel(), QString("model A"));
        vm->activateTab(0);
        QVERIFY(!vm->chatModelProfile().contains("apiKey"));
        vm->selectChatModel("B"); // active task rejects replacement
        QCOMPARE(first.currentModel(), QString("model A"));
        firstService.answer("done");
        QVERIFY(vm->startTask("next task"));
        QCOMPARE(first.currentModel(), QString("updated A"));
        vm->setModelConfigurator({});
        vm->setAgent(nullptr);
    }

    void historyFlagsPersistAndArchiveSearchIsExplicit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("history.db");
        {
            SqliteMemory memory(path);
            memory.beginSession("a", "older");
            memory.beginSession("b", "newer");
            memory.saveMessage("a", QJsonObject{{"role", "user"}, {"content", "needle A"}});
            memory.saveMessage("b", QJsonObject{{"role", "user"}, {"content", "needle B"}});
            QVERIFY(memory.setSessionPinned("a", true));
            QCOMPARE(memory.listSessions().first().toObject().value("id").toString(), QString("a"));
            QVERIFY(memory.setSessionArchived("a", true));
            QCOMPARE(memory.listSessions().size(), 1);
            QCOMPARE(memory.listSessions(50, true).size(), 2);
            // Batch 22 (search-aggregate): searchMessages returns one row
            // per SESSION - each session here holds exactly one matching
            // message, so these counts double as session counts.
            for (const QString& keyword : {QString("ne"), QString("needle")}) {
                QCOMPARE(memory.searchMessages(keyword).size(), 1);
                QCOMPARE(memory.searchMessages(keyword, 50, true).size(), 2);
            }
        }
        {
            SqliteMemory reopened(path);
            QCOMPARE(reopened.listSessions(50, true).first().toObject().value("pinned").toBool(), true);
            QVERIFY(reopened.setSessionArchived("a", false));
            QCOMPARE(reopened.listSessions().size(), 2);
            QVERIFY(!reopened.setSessionPinned("missing", true));
        }
    }

    void mainWindowLoadsAtMinimumSize() {
        QQmlApplicationEngine engine;
        QStringList issues;
        connect(&engine, &QQmlEngine::warnings, this, [&issues](const QList<QQmlError>& errors) {
            for (const auto& error : errors) issues.append(error.toString());
        });
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(900, 600);
        QTest::qWait(100);
        QVERIFY(window->findChild<QObject*>("messageInput"));
        QVERIFY2(issues.isEmpty(), qPrintable(issues.join('\n')));
        const QString capture = qEnvironmentVariable("NEXUS_CAPTURE_PATH");
        if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture));
    }

    // ── W1: '/' 命令输入即补全 ───────────────────────────

    // 输入 /re 立即出现 review 候选（而不是等到发送时再展开）。
    void slashCompletionShowsMatchingCommand() {
        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        QVERIFY(!SettingsService::instance()->slashCommands().isEmpty());

        ui.setInput("/re");
        QCOMPARE(ui.console->property("slashOpen").toBool(), true);
        QCOMPARE(ui.popup->property("opened").toBool(), true);
        QCOMPARE(ui.console->property("slashStart").toInt(), 0);
        QCOMPARE(ui.console->property("slashQuery").toString(), QStringLiteral("re"));
        QCOMPARE(ui.console->property("slashIndex").toInt(), 0);
        const QStringList names = ui.resultNames();
        QVERIFY2(names.contains(QStringLiteral("review")), qPrintable(names.join(',')));
        QVERIFY(!names.contains(QStringLiteral("translate")));

        // 继续输入按前缀过滤。
        ui.setInput("/rev");
        QCOMPARE(ui.resultNames(), QStringList{QStringLiteral("review")});
    }

    // ↑/↓ 在候选中移动，不改变输入文本。
    void slashArrowsMoveSelection() {
        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("/");
        QVERIFY(ui.results().size() >= 2);
        QVERIFY(ui.field->property("activeFocus").toBool());
        QCOMPARE(ui.console->property("slashIndex").toInt(), 0);

        QTest::keyClick(ui.window, Qt::Key_Down);
        QTest::qWait(40);
        QCOMPARE(ui.console->property("slashIndex").toInt(), 1);

        QTest::keyClick(ui.window, Qt::Key_Down);
        QTest::qWait(40);
        QCOMPARE(ui.console->property("slashIndex").toInt(), 2);

        QTest::keyClick(ui.window, Qt::Key_Up);
        QTest::qWait(40);
        QCOMPARE(ui.console->property("slashIndex").toInt(), 1);
        QCOMPARE(ui.inputText(), QStringLiteral("/"));
    }

    // Tab 只补全为 "/name "，绝不触发发送。
    void slashTabCompletesWithoutSending() {
        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);

        ui.setInput("/re");
        QVERIFY(ui.field->property("activeFocus").toBool());
        QTest::keyClick(ui.window, Qt::Key_Tab);
        QTest::qWait(80);

        QCOMPARE(ui.inputText(), QStringLiteral("/review "));
        QCOMPARE(ui.console->property("slashOpen").toBool(), false);
        QCOMPARE(service.requests.size(), 0);

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // Enter 在有候选时同样只补全，不发送。
    void slashEnterCompletesWithoutSending() {
        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);

        ui.setInput("/re");
        QVERIFY(ui.field->property("activeFocus").toBool());
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(ui.inputText(), QStringLiteral("/review "));
        QCOMPARE(ui.console->property("slashOpen").toBool(), false);
        QCOMPARE(service.requests.size(), 0);

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // Esc 关闭面板并保留已输入文本。
    void slashEscapeClosesPanel() {
        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("/re");
        QCOMPARE(ui.console->property("slashOpen").toBool(), true);

        QTest::keyClick(ui.window, Qt::Key_Escape);
        QTest::qWait(60);

        QCOMPARE(ui.console->property("slashOpen").toBool(), false);
        QCOMPARE(ui.popup->property("opened").toBool(), false);
        QCOMPARE(ui.inputText(), QStringLiteral("/re"));
    }

    // 路径与 URL 不是命令：/etc/hosts、http://… 不触发；空白后的 /re 才触发。
    void slashIgnoresPathsAndUrls() {
        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));

        ui.setInput("/etc/hosts");
        QCOMPARE(ui.console->property("slashOpen").toBool(), false);

        ui.setInput("curl http://localhost:8080/path");
        QCOMPARE(ui.console->property("slashOpen").toBool(), false);

        ui.setInput("hi /re");
        QCOMPARE(ui.console->property("slashOpen").toBool(), true);
        QCOMPARE(ui.console->property("slashStart").toInt(), 3);
    }

    // 发送路径不变：/review 仍由 expandSlashCommand 展开为模板。
    void slashCommandStillExpandsOnSend() {
        const QVariantList commands = SettingsService::instance()->slashCommands();
        QVERIFY(!commands.isEmpty());
        const QString expanded =
            AgentViewModel::expandSlashCommand(QStringLiteral("/review 代码"), commands);
        QVERIFY(expanded != QStringLiteral("/review 代码"));
        QVERIFY(!expanded.startsWith(QStringLiteral("/review")));
        QVERIFY(expanded.contains(QStringLiteral("请审查")));
        QVERIFY(expanded.endsWith(QStringLiteral("代码")));
    }

    // ── docs/37 W3: '$' Skill 补全 / chip / 发送 ─────────

    // 输入 $re 立即出现 review 候选；$dir/file 不是技能引用。
    void skillCompletionShowsProjectSkills() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));

        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));

        ui.setInput("$re");
        QCOMPARE(ui.console->property("skillOpen").toBool(), true);
        QCOMPARE(ui.skillPopup->property("opened").toBool(), true);
        QCOMPARE(ui.console->property("skillStart").toInt(), 0);
        QCOMPARE(ui.console->property("skillQuery").toString(),
                 QStringLiteral("re"));
        QCOMPARE(ui.console->property("slashOpen").toBool(), false);
        QVERIFY2(ui.skillNames().contains(QStringLiteral("review")),
                 qPrintable(ui.skillNames().join(',')));
        QVERIFY(!ui.skillNames().contains(QStringLiteral("test")));

        ui.setInput("$dir/file");
        QCOMPARE(ui.console->property("skillOpen").toBool(), false);

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // ↑/↓ 在技能候选中移动。
    void skillArrowsMoveSelection() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("$");
        QVERIFY(ui.skillNames().size() >= 2);
        QVERIFY(ui.field->property("activeFocus").toBool());
        QCOMPARE(ui.console->property("skillIndex").toInt(), 0);

        QTest::keyClick(ui.window, Qt::Key_Down);
        QTest::qWait(40);
        QCOMPARE(ui.console->property("skillIndex").toInt(), 1);

        QTest::keyClick(ui.window, Qt::Key_Down);
        QTest::qWait(40);
        QCOMPARE(ui.console->property("skillIndex").toInt(), 2);

        QTest::keyClick(ui.window, Qt::Key_Up);
        QTest::qWait(40);
        QCOMPARE(ui.console->property("skillIndex").toInt(), 1);
        QCOMPARE(ui.inputText(), QStringLiteral("$"));

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // Tab 只补全为 "$name "，不发送；chip 同步出现。
    void skillTabCompletesWithoutSending() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("$re");
        QVERIFY(ui.field->property("activeFocus").toBool());
        QTest::keyClick(ui.window, Qt::Key_Tab);
        QTest::qWait(80);

        QCOMPARE(ui.inputText(), QStringLiteral("$review "));
        QCOMPARE(ui.console->property("skillOpen").toBool(), false);
        QCOMPARE(service.requests.size(), 0);
        QCOMPARE(ui.chipNames(), QStringList{QStringLiteral("review")});

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // Enter 在有候选时同样只补全，不发送。
    void skillEnterCompletesWithoutSending() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("$re");
        QVERIFY(ui.field->property("activeFocus").toBool());
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(ui.inputText(), QStringLiteral("$review "));
        QCOMPARE(ui.console->property("skillOpen").toBool(), false);
        QCOMPARE(service.requests.size(), 0);

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // Esc 关闭技能面板并保留输入。
    void skillEscapeClosesPanel() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("$re");
        QCOMPARE(ui.console->property("skillOpen").toBool(), true);

        QTest::keyClick(ui.window, Qt::Key_Escape);
        QTest::qWait(60);
        QCOMPARE(ui.console->property("skillOpen").toBool(), false);
        QCOMPARE(ui.inputText(), QStringLiteral("$re"));

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // chip：去重、上限 3、× 移除同时删文本里的 $name。
    void skillChipsDedupeCapAndRemove() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("$review $review $test $commit $lint");
        QCOMPARE(ui.chipNames(),
                 (QStringList{QStringLiteral("review"), QStringLiteral("test"),
                              QStringLiteral("commit")}));
        QCOMPARE(ui.console->property("skillChips").toList().size(), 3);

        QVERIFY(QMetaObject::invokeMethod(
            ui.console, "removeSkill",
            Q_ARG(QVariant, QStringLiteral("test"))));
        QTest::qWait(60);
        QVERIFY(!ui.inputText().contains(QStringLiteral("$test")));
        // 删除 test 后剩下的三个不同名字（review/commit/lint）都在上限内，
        // 按出现顺序取前三。
        QCOMPARE(ui.chipNames(),
                 (QStringList{QStringLiteral("review"), QStringLiteral("commit"),
                              QStringLiteral("lint")}));
        QVERIFY(!ui.chipNames().contains(QStringLiteral("test")));

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // W7-B：未知 $skill 只提示不阻断；解析成功或清空后提示消失。
    void skillUnknownHintFollowsDraft() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        auto* hint = ui.window->findChild<QQuickItem*>(
            QStringLiteral("skillUnknownHint"));
        QVERIFY(hint);

        ui.setInput("$nope");
        QVERIFY(hint->isVisible());
        QVERIFY2(hint->property("text").toString().contains(
                     QStringLiteral("$nope")),
                 qPrintable(hint->property("text").toString()));

        ui.setInput("$review");
        QVERIFY(!hint->isVisible());

        ui.setInput("");
        QVERIFY(!hint->isVisible());

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // 发送走 startTaskWithSkills：技能正文进入系统提示，旧三参 startTask 不动。
    void skillSendUsesStartTaskWithSkills() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                               project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));
        ui.setInput("$review 代码");
        QCOMPARE(ui.console->property("skillOpen").toBool(), false);
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTest::qWait(120);

        QCOMPARE(service.requests.size(), 1);
        QVERIFY(ui.inputText().isEmpty());
        QVERIFY(ui.console->property("skillChips").toList().isEmpty());
        QString systemPrompt;
        for (const QJsonValue& entry : service.requests.first()) {
            const QJsonObject message = entry.toObject();
            if (message.value(QStringLiteral("role")).toString()
                    == QLatin1String("system"))
                systemPrompt = message.value(QStringLiteral("content")).toString();
        }
        QVERIFY2(systemPrompt.contains(
                     QStringLiteral("Always report the highest severity first")),
                 qPrintable(systemPrompt.left(300)));

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // W4 P1：补全面板已打开但无候选时，Enter 只关闭面板，不发送半截命令。
    void emptyCompletionEnterDoesNotSend() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(writeBaseSkills(project.path()));
        RecordingService service;
        AIAgent agent;
        vm->setModelConfigurator([&](AIAgent* target, const QVariantMap&) {
            target->setLLMService(&service);
        });
        vm->setAgent(&agent);
        agent.setActiveProject(QStringLiteral("demo"), QStringLiteral("Demo"),
                              project.path());
        vm->reloadSkills();

        SlashHarness ui;
        QVERIFY2(ui.load(), qPrintable(ui.error));

        ui.setInput("$nope");
        QCOMPARE(ui.console->property("skillOpen").toBool(), true);
        QCOMPARE(ui.console->property("skillResults").toList().size(), 0);
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTest::qWait(80);
        QCOMPARE(service.requests.size(), 0);
        QCOMPARE(ui.console->property("skillOpen").toBool(), false);
        QCOMPARE(ui.inputText(), QStringLiteral("$nope"));

        ui.setInput("/zzz");
        QCOMPARE(ui.console->property("slashOpen").toBool(), true);
        QCOMPARE(ui.console->property("slashResults").toList().size(), 0);
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTest::qWait(80);
        QCOMPARE(service.requests.size(), 0);
        QCOMPARE(ui.console->property("slashOpen").toBool(), false);
        QCOMPARE(ui.inputText(), QStringLiteral("/zzz"));

        vm->setAgent(nullptr);
        vm->setModelConfigurator({});
    }

    // A fresh window is a local chat, and BOTH right-panel sections are
    // project-only, so the panel must explain the absence rather than park
    // the stack on a section that cannot render. The knowledge base used to
    // be that fallback; it now lives in the sidebar tool strip with log /
    // audit / settings, because it is global rather than project-scoped.

    // B1 验收：提示词与指令页必须显示可命中的交互控件（首版只渲染了
    // 文本、按钮全部不可见）。打开 prompts 分类后三个「新建」按钮可见
    // 且有实际宽度；失败时打印几何信息辅助定位。
    void promptAssetsPageShowsCreateButtons() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        auto* dialog = window->findChild<QObject*>("settingsCenterDialog");
        QVERIFY(dialog);
        const QMetaObject* meta = dialog->metaObject();
        QVERIFY(meta->invokeMethod(dialog, "openCategory",
                                   Q_ARG(QVariant, QStringLiteral("prompts"))));
        QTest::qWait(300);
        const QStringList names = {
            QStringLiteral("promptPresetAddButton"),
            QStringLiteral("slashAddButton"),
            QStringLiteral("templateAddButton")
        };
        for (const QString& name : names) {
            auto* button = window->findChild<QQuickItem*>(name);
            QVERIFY2(button, qPrintable(QStringLiteral("missing: ") + name));
            QVERIFY2(button->isVisible(), qPrintable(name + " not visible"));
            QVERIFY2(button->width() > 20, qPrintable(name + " too narrow"));
        }
    }


    // P2：设置页组件统一（SettingsSection/SettingRow/SettingsField）后，
    // 所有被改写的分类页必须在真实引擎里实例化成功。qmllint 只做静态检查，
    // Loader 的 source/绑定错误只会出现在 warnings 里，故逐个打开分类页并
    // 断言没有新的 QML 警告。
    void settingsPagesInstantiateWithoutWarnings() {
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                [&warnings](const QList<QQmlError> &list) {
                    for (const QQmlError &error : list)
                        warnings.append(error.toString());
                });
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());

        auto* dialog = engine.rootObjects().first()->findChild<QObject*>("settingsCenterDialog");
        QVERIFY(dialog);
        QTest::qWait(200);
        warnings.clear(); // 启动期与本用例无关的告警

        const QStringList categories = {
            QStringLiteral("general"), QStringLiteral("interface"),
            QStringLiteral("security"), QStringLiteral("network"),
            QStringLiteral("memory"), QStringLiteral("tools"),
            QStringLiteral("agent"), QStringLiteral("knowledge"),
            QStringLiteral("logs"), QStringLiteral("mcp"),
            QStringLiteral("usage"), QStringLiteral("activity"),
            QStringLiteral("backup"), QStringLiteral("projects"),
            QStringLiteral("prompts"), QStringLiteral("model")
        };
        for (const QString &category : categories) {
            QVERIFY(QMetaObject::invokeMethod(dialog, "openCategory",
                                              Q_ARG(QVariant, category)));
            QTest::qWait(120);
            QVERIFY2(warnings.isEmpty(),
                     qPrintable(QStringLiteral("%1: %2")
                                    .arg(category, warnings.join(QLatin1Char('\n')))));
        }
    }


    // docs/31 S1 遗留（O1）：安全 / 用量 / 工具页没有 requestResetDefaults，
    // 底栏「恢复本页默认值」必须回落到逐条 resetValue——否则按钮出现但点了
    // 没反应，且「恢复全部设置」确认文案承诺的"安全、用量会复位"落空。
    void settingsResetCoversServiceOnlyCategories() {
        auto* service = SettingsService::instance();
        service->setLocalOnlyMode(true);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = engine.rootObjects().first();
        auto* dialog = window->findChild<QObject*>("settingsCenterDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openCategory",
                                          Q_ARG(QVariant, QStringLiteral("security"))));
        QTest::qWait(150);

        auto* resetButton = window->findChild<QObject*>("settingsResetCategoryButton");
        QVERIFY2(resetButton, "bottom reset button missing");
        QVERIFY2(resetButton->property("visible").toBool(),
                 "security category must offer '恢复本页默认值'");

        QVERIFY(QMetaObject::invokeMethod(dialog, "performReset"));
        QTest::qWait(50);
        QCOMPARE(service->localOnlyMode(), false);
    }


    // 设置搜索的可发现性回归：输入即切到结果列表，结果计数可见；无匹配
    // 时列表清空（空态由 QML 渲染）。键盘跳转依赖同一份结果模型。
    void settingsSearchShowsResultsAndEmptyState() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = engine.rootObjects().first();
        auto* dialog = window->findChild<QObject*>("settingsCenterDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openCategory",
                                          Q_ARG(QVariant, QStringLiteral("model"))));
        QTest::qWait(150);

        auto* field = window->findChild<QObject*>("settingsSearchField");
        auto* results = window->findChild<QObject*>("settingsSearchResults");
        QVERIFY(field);
        QVERIFY(results);

        field->setProperty("text", QStringLiteral("代理"));
        QTest::qWait(200);
        QVERIFY(results->property("visible").toBool());
        QVERIFY(results->property("count").toInt() > 0);

        field->setProperty("text", QStringLiteral("no-such-setting-xyz"));
        QTest::qWait(200);
        QCOMPARE(results->property("count").toInt(), 0);

        field->setProperty("text", QString());
    }


    // 导航密度回归：诊断类分组默认收起，但当前分类落在这个组时必须自动
    // 展开（否则搜索结果跳转过去后选中项不可见）；窄对话框则收成图标条。
    void settingsNavigationCollapsesAndCompacts() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = engine.rootObjects().first();
        auto* dialog = window->findChild<QObject*>("settingsCenterDialog");
        auto* nav = window->findChild<QObject*>("settingsCategoryNav");
        QVERIFY(dialog);
        QVERIFY(nav);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openCategory",
                                          Q_ARG(QVariant, QStringLiteral("model"))));
        QTest::qWait(120);

        QVariantMap collapsed = nav->property("collapsedGroups").toMap();
        QVERIFY(collapsed.value(QStringLiteral("data")).toBool());

        QVERIFY(QMetaObject::invokeMethod(dialog, "openCategory",
                                          Q_ARG(QVariant, QStringLiteral("logs"))));
        QTest::qWait(120);
        collapsed = nav->property("collapsedGroups").toMap();
        QVERIFY(!collapsed.value(QStringLiteral("data")).toBool());

        // 窗口最小宽度 900 → 对话框 876，命中 <900 的紧凑断点。
        dialog->setProperty("width", 860);
        QTest::qWait(120);
        QVERIFY(nav->property("compact").toBool());
        QCOMPARE(nav->property("width").toInt(), 56);
    }


    // docs/31 S1（O7）：分类页缓存不得让页面互相重叠。缓存后 Loader 保持
    // active，但同一时刻只允许当前分类的页面可见——`visible: active` 会让
    // 所有访问过的页面同时铺在同一个 anchors.fill 区域上（S1 真实回归：
    // 切到第二个分类后两页叠在一起）。顺带锁住缓存上限：访问多页后
    // 只有最近 maxCachedPages 个页面还持有实例。
    void settingsCategoryPagesNeverOverlap() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = engine.rootObjects().first();
        auto* dialog = window->findChild<QObject*>("settingsCenterDialog");
        QVERIFY(dialog);

        const QStringList categories = {
            QStringLiteral("model"), QStringLiteral("agent"),
            QStringLiteral("interface"), QStringLiteral("general"),
            QStringLiteral("network"), QStringLiteral("security"),
            QStringLiteral("mcp"), QStringLiteral("tools"),
            QStringLiteral("memory"), QStringLiteral("knowledge"),
            QStringLiteral("backup"), QStringLiteral("logs"),
            QStringLiteral("usage"), QStringLiteral("activity"),
            QStringLiteral("projects"), QStringLiteral("prompts")
        };
        const auto loaderFor = [dialog](const QString &id) -> QObject* {
            return dialog->findChild<QObject*>(QStringLiteral("settingsPageLoader.") + id);
        };

        for (const QString &category : categories) {
            QVERIFY(QMetaObject::invokeMethod(dialog, "openCategory",
                                              Q_ARG(QVariant, category)));
            QTest::qWait(120);

            int visibleCount = 0;
            QString visibleId;
            for (const QString &id : categories) {
                QObject *loader = loaderFor(id);
                QVERIFY2(loader, qPrintable(QStringLiteral("missing loader: ") + id));
                if (loader->property("visible").toBool()) {
                    ++visibleCount;
                    visibleId = id;
                }
            }
            QVERIFY2(visibleCount == 1,
                     qPrintable(QStringLiteral("category %1: %2 page(s) visible (%3)")
                                    .arg(category).arg(visibleCount).arg(visibleId)));
            QCOMPARE(visibleId, category);
            QVERIFY(loaderFor(category)->property("item").value<QObject*>() != nullptr);
        }

        // 缓存上限：最后一次打开的是 prompts（不参与缓存），此前 5 个
        // 分类页仍是活跃实例；更早的页面已被释放但 loader 本身还在。
        int instantiated = 0;
        for (const QString &id : categories) {
            if (loaderFor(id)->property("item").value<QObject*>() != nullptr)
                ++instantiated;
        }
        QVERIFY2(instantiated <= 6,
                 qPrintable(QStringLiteral("too many cached pages: %1").arg(instantiated)));
        // 上一个分类页被保留但不可见——这才是不重叠 + 不丢状态的组合。
        QObject *prompts = loaderFor(QStringLiteral("prompts"));
        QCOMPARE(prompts->property("visible").toBool(), true);
        QObject *projects = loaderFor(QStringLiteral("projects"));
        QCOMPARE(projects->property("visible").toBool(), false);
    }


    // 视觉底座回归：设置界面不再允许裸像素字号。字号偏好可调时，写死
    // 12px 的说明文字不会跟随缩放，而且各页会逐渐漂移回“10px 说明 + 12px
    // 正文”的旧层级。这里只扫设置页与设置排版组件，聊天区不在此约束内。
    void settingsUiUsesThemeFontTokens() {
        const QStringList files = {
            QStringLiteral("src/ui/pages/GeneralPage.qml"),
            QStringLiteral("src/ui/pages/AgentBehaviorPage.qml"),
            QStringLiteral("src/ui/pages/NetworkPage.qml"),
            QStringLiteral("src/ui/pages/LogsPage.qml"),
            QStringLiteral("src/ui/pages/KnowledgePage.qml"),
            QStringLiteral("src/ui/pages/InterfacePage.qml"),
            QStringLiteral("src/ui/pages/ModelConfigPage.qml"),
            QStringLiteral("src/ui/pages/SecurityPage.qml"),
            QStringLiteral("src/ui/pages/McpPage.qml"),
            QStringLiteral("src/ui/pages/ToolsPage.qml"),
            QStringLiteral("src/ui/pages/MemoryPage.qml"),
            QStringLiteral("src/ui/pages/UsageStatsPage.qml"),
            QStringLiteral("src/ui/pages/ActivityLogPage.qml"),
            QStringLiteral("src/ui/pages/SettingsBackupPage.qml"),
            QStringLiteral("src/ui/pages/ProjectRulesPage.qml"),
            QStringLiteral("src/ui/pages/PromptAssetsPage.qml"),
            QStringLiteral("src/ui/components/SettingsPage.qml"),
            QStringLiteral("src/ui/components/SettingsPageHeader.qml"),
            QStringLiteral("src/ui/components/SettingsSection.qml"),
            QStringLiteral("src/ui/components/SettingRow.qml"),
            QStringLiteral("src/ui/components/SettingsField.qml"),
            QStringLiteral("src/ui/components/SettingCard.qml"),
            QStringLiteral("src/ui/components/PreferenceControl.qml"),
            QStringLiteral("src/ui/components/SettingsCenterDialog.qml"),
            QStringLiteral("src/ui/components/CategoryNav.qml"),
            QStringLiteral("src/ui/components/AppButton.qml"),
            QStringLiteral("src/ui/components/AppScrollBar.qml"),
            QStringLiteral("src/ui/components/StatusLine.qml")
        };
        const QRegularExpression rawFont(QStringLiteral("font\\.pixelSize\\s*:\\s*\\d"));
        for (const QString &rel : files) {
            QFile file(QStringLiteral(NEXUS_SOURCE_DIR "/") + rel);
            QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                     qPrintable(QStringLiteral("cannot open ") + rel));
            const QString text = QString::fromUtf8(file.readAll());
            const auto match = rawFont.match(text);
            QVERIFY2(!match.hasMatch(),
                     qPrintable(QStringLiteral("%1: hardcoded font.pixelSize at offset %2")
                                    .arg(rel)
                                    .arg(match.capturedStart())));
        }
    }


    // docs/30 P1：单对话视图的呈现回归。
    // 这个用例锁的是"界面形态"本身——标签栏必须消失、侧栏必须能表达三种
    // 运行状态、顶栏必须有会话标题、模板入口必须移除、状态提示居中显示。
    // 它们都是纯 QML 契约，数据面语义由 test_viewmodel 的 openConversation
    // 那一组覆盖。
    void singleConversationViewChrome() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(200);

        // 侧栏的会话行来自数据库；空库没有行，状态槽也就无从装配。
        // 预置会话并打开它，让侧栏与顶栏都处于真实形态。
        // 侧栏行与 resumeSession 都依赖一个挂了记忆后端的 agent；cleanup()
        // 会 detach，所以这里显式挂一个（不配 LLM：本用例不跑任务）。
        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("p1-chrome-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("p1-chrome-b"), QStringLiteral("会话 B"));
        AIAgent chromeAgent;
        chromeAgent.setMemory(&memory);
        vm->setMemory(&memory);
        vm->setAgent(&chromeAgent);
        vm->openConversation(QStringLiteral("p1-chrome-a"));
        QTest::qWait(100);
        QCOMPARE(vm->currentSessionId(), QStringLiteral("p1-chrome-a"));

        // 1. 标签栏已经整套移除（组件删除 + QML_FILES 移除）。
        QVERIFY2(window->findChild<QQuickItem*>(QStringLiteral("sessionTabs")) == nullptr,
                 "SessionTabs must be gone");
        QVERIFY(vm->tabModel().size() >= 1);   // 引擎仍保留多 controller 能力

        // 2. 顶栏补上了唯一常显的会话标题（标签栏删除后的替代品）。
        auto* title = window->findChild<QQuickItem*>(
            QStringLiteral("topBarSessionTitle"));
        QVERIFY2(title, "top bar is missing the current-session title");
        QVERIFY(title->isVisible());
        QVERIFY(title->width() > 40);

        // 3. 顶栏的模板入口已移除：侧栏窄条与聊天空态仍有入口，TopBar 这份属冗余。
        QVERIFY2(window->findChild<QQuickItem*>(QStringLiteral("topBarTemplateEntry")) == nullptr,
                 "top bar template entry must be gone");

        // 4. 侧栏每一行都有状态槽（docs/30 §4.2 的三态位）。
        //    注意不能用 window->findChildren 找 delegate 内部的项：它们虽然带
        //    objectName，却不是 window 的 QObject 后代遍历能覆盖的路径。
        //    沿场景图（ScrollView → ColumnLayout → Repeater 实例）显式收集。
        QTest::qWait(300);
        QList<QQuickItem*> stateSlots;
        {
            auto* sbItem = window->findChild<QQuickItem*>(QStringLiteral("sidebarPanel"));
            QVERIFY(sbItem);
            auto* scroll = sbItem->findChild<QQuickItem*>("sidebarTreeScroll");
            QVERIFY(scroll);
            auto* col = scroll->findChild<QQuickItem*>();
            QVERIFY(col);
            for (auto* group : col->childItems())
                for (auto* row : group->childItems())
                    if (QString::fromLatin1(row->metaObject()->className())
                            .startsWith(QLatin1String("ItemDelegate")))
                        for (auto* it : row->findChildren<QQuickItem*>())
                            if (it->objectName() == QLatin1String("sessionStateSlot"))
                                stateSlots.append(it);
        }
        QVERIFY2(!stateSlots.isEmpty(), "sidebar session rows have no state slot");

        // 每个槽里三态控件都在（busy 转圈 / 等待确认 / 未读点），可见性由
        // 模型驱动，这里只要求结构完备——否则模型一变就没有可显示的控件。
        int busyIcons = 0, awaitingIcons = 0, unreadDots = 0;
        for (auto* slot : stateSlots) {
            for (auto* child : slot->findChildren<QQuickItem*>()) {
                const QString name = child->objectName();
                if (name == QLatin1String("sessionBusyIcon")) ++busyIcons;
                else if (name == QLatin1String("sessionAwaitingIcon")) ++awaitingIcons;
                else if (name == QLatin1String("sessionUnreadDot")) ++unreadDots;
            }
        }
        QCOMPARE(busyIcons, stateSlots.size());
        QCOMPARE(awaitingIcons, stateSlots.size());
        QCOMPARE(unreadDots, stateSlots.size());

        // 5. 状态槽在所有行里宽度固定——busy 开始/结束时标题不该左右跳。
        for (auto* slot : stateSlots) QCOMPARE(slot->width(), 14.0);
    }


    // docs/30 修复：会话行加了状态槽之后，时间行必须与标题共用同一左基线。
    // 这是纯几何契约，只有跑真实场景图才能验证——QML 的 ColumnLayout 不会
    // 因为"看起来该对齐"就自动对齐，漏掉 leftMargin 就是 18px 的错位。
    void sessionRowTimeAlignsWithTitle() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(150);

        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("p1-align-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("p1-align-b"), QStringLiteral("会话 B"));
        AIAgent alignAgent;
        alignAgent.setMemory(&memory);
        vm->setMemory(&memory);
        vm->setAgent(&alignAgent);
        vm->openConversation(QStringLiteral("p1-align-a"));
        QTest::qWait(300);

        auto* sbItem = window->findChild<QQuickItem*>(QStringLiteral("sidebarPanel"));
        QVERIFY(sbItem);
        auto* scroll = sbItem->findChild<QQuickItem*>("sidebarTreeScroll");
        QVERIFY(scroll);
        auto* col = scroll->findChild<QQuickItem*>();
        QVERIFY(col);

        int rowsChecked = 0;
        for (auto* group : col->childItems()) {
            for (auto* row : group->childItems()) {
                if (!QString::fromLatin1(row->metaObject()->className())
                         .startsWith(QLatin1String("ItemDelegate")))
                    continue;
                QQuickItem* slot = nullptr;
                QQuickItem* title = nullptr;
                QQuickItem* timeRow = nullptr;
                for (auto* it : row->findChildren<QQuickItem*>())
                    if (it->objectName() == QLatin1String("sessionStateSlot"))
                        slot = it;
                // 内容列 = delegate 的直接 QQuickColumnLayout 子项。直接子项
                // 依次是：标题行 Item（内含重命名框 / 状态槽 / 标题 Text /
                // hover 操作条）、时间 Text。
                QQuickItem* contentCol = nullptr;
                for (auto* child : row->childItems()) {
                    if (QString::fromLatin1(child->metaObject()->className())
                            .startsWith(QLatin1String("QQuickColumnLayout"))) {
                        contentCol = child;
                        break;
                    }
                }
                QVERIFY2(contentCol, "session row has no content column");
                for (auto* child : contentCol->childItems()) {
                    const bool isText = QString::fromLatin1(
                        child->metaObject()->className())
                            .startsWith(QLatin1String("QQuickText"));
                    if (isText) {
                        if (child->isVisible()) timeRow = child;
                        continue;
                    }
                    // 标题行容器：取其中与状态槽并列的那个可见 Text。
                    if (!child->isVisible()) continue;
                    for (auto* inner : child->childItems())
                        if (inner->isVisible()
                                && QString::fromLatin1(inner->metaObject()->className())
                                       .startsWith(QLatin1String("QQuickText")))
                            title = inner;
                }
                if (!slot || !title || !timeRow || !contentCol) continue;

                // 基准：状态槽左缘 = 列内容原点（ColumnLayout 内 leftMargin 0）。
                // 标题的左缘 = 槽宽 + 间距；时间行左缘必须与之相等。
                const qreal slotLeft = slot->mapToItem(contentCol, QPointF(0, 0)).x();
                const qreal titleLeft = title->mapToItem(contentCol, QPointF(0, 0)).x();
                const qreal timeLeft = timeRow->mapToItem(contentCol, QPointF(0, 0)).x();

                QVERIFY2(qAbs(slotLeft - 0.0) < 0.5,
                         qPrintable(QStringLiteral("state slot should start at the "
                                                   "column origin, got %1").arg(slotLeft)));
                QVERIFY2(qAbs(titleLeft - timeLeft) < 0.5,
                         qPrintable(QStringLiteral("time row (%1) is not aligned with "
                                                   "title (%2)").arg(timeLeft).arg(titleLeft)));

                // 缩进基线：状态槽左缘要与分组头的展开箭头同一条竖线（x=20），
                // 标题再往右 18px。这层关系是"缩进不至于太远"的唯一量化依据——
                // 之前三层缩进叠加把标题推到 x=64，比分组头标签(56)还靠右。
                // 用侧栏绝对坐标衡量（相对 row/group 都会被各自的 leftMargin
                // 抵消，量不出真正的视觉缩进）。
                const qreal slotAbs = slot->mapToItem(sbItem, QPointF(0, 0)).x();
                QVERIFY2(qAbs(slotAbs - 20.0) < 1.0,
                         qPrintable(QStringLiteral("state slot left edge is %1, "
                                                   "expected 20 (group header chevron)")
                                        .arg(slotAbs)));
                // 标题宽度必须被夹到 >= 0：最窄侧栏（180px）下悬停操作条
                // 130px 会吃掉全部剩余空间，不夹取会得到负宽度，elide 失效、
                // 标题溢到操作条底下。
                QVERIFY2(title->width() >= 0.0,
                         qPrintable(QStringLiteral("title width went negative: %1")
                                        .arg(title->width())));
                // 操作条必须收得足够窄：docs/30 把它从 6 个按钮（108–130px）
                // 收成 3 个常用 + 「…」（86px）。上限 90 是给"再加一个按钮"
                // 留的余地，一旦越过就说明又塞回去了，窄侧栏会再次压垮标题。
                QQuickItem* actions = nullptr;
                for (auto* it : row->findChildren<QQuickItem*>())
                    if (it->objectName() == QLatin1String("sessionActions"))
                        actions = it;
                if (actions) {
                    QVERIFY2(actions->implicitWidth() <= 90.0,
                             qPrintable(QStringLiteral("session action bar is %1px wide; "
                                                       "keep it at or below 90px so the "
                                                       "title survives at 180px sidebar")
                                            .arg(actions->implicitWidth())));
                    // 180px 最窄侧栏的最坏情况（悬停）也要留住标题：
                    // row - titleInset - 右缘12 - 操作条 - 6px 间隙 > 0
                    const qreal worst = (180.0 - 12.0) - 38.0 - 12.0
                                        - actions->implicitWidth() - 6.0;
                    QVERIFY2(worst > 0,
                             qPrintable(QStringLiteral("at 180px sidebar the hovered title "
                                                       "gets %1px; the action bar is too "
                                                       "wide").arg(worst)));
                }
                const qreal titleAbs = title->mapToItem(sbItem, QPointF(0, 0)).x();
                QVERIFY2(qAbs(titleAbs - 38.0) < 1.0,
                         qPrintable(QStringLiteral("title left edge is %1, "
                                                   "expected 38 (group header icon)")
                                        .arg(titleAbs)));
                ++rowsChecked;
            }
        }
        QVERIFY2(rowsChecked > 0, "no session row was inspected");

        // docs/30：「…」菜单必须弹在按钮**旁边**，而不是窗口左上角。
        // 回归点：曾经用 Popup.Window 取窗口宽高（未 open 时为 0），夹取算式
        // 把 x 压成 8、y 压成 4，菜单整块跑到窗口角上。
        {
            QQuickItem* moreBtn = nullptr;
            for (auto* group : col->childItems())
                for (auto* row : group->childItems()) {
                    if (!QString::fromLatin1(row->metaObject()->className())
                             .startsWith(QLatin1String("ItemDelegate"))) continue;
                    for (auto* it : row->findChildren<QQuickItem*>())
                        if (it->objectName() == QLatin1String("moreBtn"))
                            moreBtn = it;
                    if (!moreBtn) continue;

                    QVERIFY(QMetaObject::invokeMethod(moreBtn, "openMenu"));
                    QTest::qWait(150);
                    QCOMPARE(row->property("moreOpen").toBool(), true);

                    // 内容真的铺开了（3 个菜单项 + 标题留白 >= 3*28）。
                    const qreal contentH = row->property("moreContentHeight").toDouble();
                    QVERIFY2(contentH >= 84.0,
                             qPrintable(QStringLiteral("popup content is %1px tall; "
                                                       "the three menu rows did not lay out")
                                            .arg(contentH)));

                    // 位置：右缘贴按钮右缘，垂直方向紧贴按钮下方。
                    const QPointF btn = moreBtn->mapToItem(nullptr, QPointF(0, 0));
                    const qreal px = row->property("morePopupX").toDouble();
                    const qreal py = row->property("morePopupY").toDouble();
                    const qreal pw = row->property("morePopupWidth").toDouble();
                    QVERIFY2(qAbs((px + pw) - (btn.x() + moreBtn->width())) <= 1.0,
                             qPrintable(QStringLiteral("popup right edge %1 is not flush "
                                                       "with the button %2")
                                            .arg(px + pw).arg(btn.x() + moreBtn->width())));
                    QVERIFY2(qAbs(py - (btn.y() + moreBtn->height() + 4)) <= 1.0,
                             qPrintable(QStringLiteral("popup top %1 is not just below "
                                                       "the button %2")
                                            .arg(py).arg(btn.y() + moreBtn->height() + 4)));
                    break;
                }
        }

        // 最窄侧栏（180px，SplitBar 的下限）：标题宽度必须仍是非负，
        // 且时间行仍与标题对齐。缩进改小之后这里才真正有余量，值得锁住。
        vm->setSidebarWidth(180);
        QTest::qWait(200);
        int narrowChecked = 0;
        for (auto* group : col->childItems()) {
            for (auto* row : group->childItems()) {
                if (!QString::fromLatin1(row->metaObject()->className())
                         .startsWith(QLatin1String("ItemDelegate")))
                    continue;
                // 注意：这里只能验证"非悬停态"宽度非负。悬停态（操作条显示、
                // 6 个按钮 130px）在 180px 侧栏下会把可用宽度压成负数，但
                // hover 无法在 offscreen 测试里稳定触发——QML 侧靠 Math.max(0)
                // 兜底，属真机验收项，不在此假装覆盖。
                for (auto* it : row->findChildren<QQuickItem*>()) {
                    if (!QString::fromLatin1(it->metaObject()->className())
                             .startsWith(QLatin1String("QQuickText")))
                        continue;
                    if (!it->isVisible()) continue;
                    QVERIFY2(it->width() >= 0.0,
                             qPrintable(QStringLiteral("text width went negative at "
                                                       "180px sidebar: %1").arg(it->width())));
                    ++narrowChecked;
                }
            }
        }
        QVERIFY2(narrowChecked > 0, "no text inspected at the narrow sidebar width");

        vm->setAgent(nullptr);
        vm->setMemory(nullptr);
    }

    // docs/30 §5-3：折叠状态按会话分桶。两条会话的第 N 行折叠 key
    // （type:index:内容前缀）会撞车，共用字典会让 B 会话继承 A 的折叠态。
    void transcriptDetailStateIsPerConversation() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QTest::qWait(150);

        SqliteMemory memory(QStringLiteral(":memory:"));
        memory.beginSession(QStringLiteral("p1-fold-a"), QStringLiteral("会话 A"));
        memory.beginSession(QStringLiteral("p1-fold-b"), QStringLiteral("会话 B"));
        AIAgent foldAgent;
        foldAgent.setMemory(&memory);
        vm->setMemory(&memory);
        vm->setAgent(&foldAgent);
        vm->openConversation(QStringLiteral("p1-fold-a"));

        // TranscriptViewport 是本用例的探针：它持有 detailStateBySession。
        const auto viewports = window->findChildren<QQuickItem*>();
        QObject* transcript = nullptr;
        for (auto* item : viewports) {
            if (item->metaObject()->indexOfProperty("detailStateBySession") >= 0) {
                transcript = item;
                break;
            }
        }
        QVERIFY2(transcript, "transcript viewport is missing detailStateBySession");

        auto call = [transcript](const char* fn, const QVariantList& args) {
            QVariant ret;
            QMetaObject::invokeMethod(transcript, fn, Q_RETURN_ARG(QVariant, ret),
                                      Q_ARG(QVariant, args.value(0)),
                                      Q_ARG(QVariant, args.value(1)));
            return ret;
        };

        // 会话 A：折叠 key "k"。
        const QString sessionA = vm->currentSessionId();
        QCOMPARE(sessionA, QStringLiteral("p1-fold-a"));
        call("toggleDetail", {QStringLiteral("k"), true});
        QVERIFY(call("detailExpanded", {QStringLiteral("k"), true}).toBool() == false);

        // 切到 B：同一个 key 不受 A 影响。
        vm->openConversation(QStringLiteral("p1-fold-b"));
        QTest::qWait(80);
        QCOMPARE(vm->currentSessionId(), QStringLiteral("p1-fold-b"));
        QVERIFY2(call("detailExpanded", {QStringLiteral("k"), true}).toBool() == true,
                 "fold state leaked across conversations");

        // 切回 A：A 的折叠态还在。
        vm->openConversation(QStringLiteral("p1-fold-a"));
        QTest::qWait(80);
        QCOMPARE(vm->currentSessionId(), sessionA);
        QVERIFY(call("detailExpanded", {QStringLiteral("k"), true}).toBool() == false);

        vm->setAgent(nullptr);
        vm->setMemory(nullptr);
    }

    // 用户报告：下拉按钮点第二次还是打开，必须点别处才关。
    // 覆盖输入区选项行的三个下拉（模型 / 思考强度 / 权限模式）与顶栏项目徽标。
    // 这三个选择器都是"Popup + onPressed toggle"的同一模式，逐个用真实鼠标
    // 点两次，第二次必须关闭。
    void dropdownTriggersToggleClosedOnSecondClick() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1200, 800);
        QTest::qWait(250);

        const QStringList triggerNames = {
            QStringLiteral("modelSelectorTrigger"),
            QStringLiteral("thinkingEffortTrigger"),
            QStringLiteral("permissionModeTrigger"),
        };
        for (const QString& name : triggerNames) {
            auto* trigger = window->findChild<QQuickItem*>(name);
            QVERIFY2(trigger, qPrintable(QStringLiteral("missing trigger: ") + name));
            // 弹层状态挂在选择器 root 上（触发器是它的子项）。
            QObject* selectorRoot = trigger->parent();
            QVERIFY2(selectorRoot, qPrintable(name + ": trigger has no parent"));
            // 未配置模型时思考强度整体不可用（usable=false），点不开是正确行为，
            // 不是本用例的对象。
            if (selectorRoot->property("usable").isValid()
                    && !selectorRoot->property("usable").toBool())
                continue;
            QCOMPARE(selectorRoot->property("opened").toBool(), false);
            const QPoint center = trigger->mapToScene(
                QPointF(trigger->width() / 2, trigger->height() / 2)).toPoint();

            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center);
            QTRY_VERIFY2(selectorRoot->property("opened").toBool(),
                         qPrintable(name + ": first click must open the popup"));
            QTest::qWait(150);

            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center);
            QTRY_VERIFY2(!selectorRoot->property("opened").toBool(),
                         qPrintable(name + ": second click must close the popup"));
        }
    }

    void freshWindowShowsNoProjectHintInRightPanel() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1100, 800);
        QTest::qWait(150);

        // Precondition: this really is a local session.
        QCOMPARE(vm->activeProjectRoot(), QString());

        // Both project-scoped tabs are hidden ...
        auto* gitTab = window->findChild<QObject*>(QStringLiteral("gitTab"));
        QVERIFY2(gitTab, "context panel is missing gitTab");
        QCOMPARE(gitTab->property("visible").toBool(), false);
        auto* filesTab = window->findChild<QObject*>(QStringLiteral("filesTab"));
        QVERIFY2(filesTab, "context panel is missing filesTab");
        QCOMPARE(filesTab->property("visible").toBool(), false);

        // ... the knowledge tab is gone from this panel entirely ...
        QVERIFY2(window->findChild<QObject*>(QStringLiteral("knowledgeTab")) == nullptr,
                 "the knowledge base must no longer be a right-panel section");

        // ... and the panel shows NOTHING at all. Both pages are project-only,
        // and an empty panel is the honest rendering: there is no "no project"
        // hint to read, because nothing in this panel applies to a local chat.
        QVERIFY2(window->findChild<QQuickItem*>(QStringLiteral("contextPanelNoProject")) == nullptr,
                 "local chat should not carry a no-project hint in the right panel");
        const auto stack = window->findChild<QQuickItem*>(QStringLiteral("projectFileList"));
        if (stack)
            QVERIFY2(!stack->isVisible(), "project file list must stay hidden in a local chat");
        const auto gitHint = window->findChild<QQuickItem*>(QStringLiteral("gitNoProjectHint"));
        if (gitHint)
            QVERIFY2(!gitHint->isVisible(), "git no-project hint must not leak into a local chat");
        // The knowledge base is reachable from the sidebar tool strip instead,
        // next to log / audit / settings.
        auto* kbButton = window->findChild<QObject*>(QStringLiteral("knowledgeButton"));
        QVERIFY2(kbButton, "sidebar is missing the knowledge button");

        // Nothing in the right panel is identifying a repository.
        QVERIFY2(!GitViewModel::instance()->repoInfoLoading(),
                 "新会话不应在识别 Git 仓库");
        QVERIFY2(!GitViewModel::instance()->statusLoading(),
                 "新会话不应在读取 Git 状态");
        QVERIFY2(GitViewModel::instance()->repoInfo().isEmpty(),
                 "新会话不应已有仓库信息");
        const auto texts = window->findChildren<QQuickItem*>();
        for (QQuickItem* item : texts) {
            if (!item->isVisible()) continue;
            const QVariant text = item->property("text");
            if (!text.isValid()) continue;
            QVERIFY2(!text.toString().contains(QStringLiteral("正在识别 Git 仓库")),
                     "可见区域出现了「正在识别 Git 仓库…」");
        }
    }

    void approvalDialogKeepsFirstRequestAndAdvancesQueue() {
        RecordingService firstService;
        AIAgent first;
        first.setLLMService(&firstService);
        first.setToolRegistry(&ToolRegistry::instance());
        RecordingService* secondService = nullptr;
        vm->setAgentFactory([&]() {
            auto* agent = new AIAgent;
            secondService = new RecordingService(agent);
            agent->setLLMService(secondService);
            agent->setToolRegistry(&ToolRegistry::instance());
            return agent;
        });
        vm->setAgent(&first);
        QVERIFY(vm->startTask("first"));
        vm->openTab();
        QVERIFY(vm->startTask("second"));
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import NexusAI
            ApplicationWindow {
                width: 700; height: 550; visible: true
                ToolConfirmDialog { objectName: "approval"; parent: Overlay.overlay }
            })", QUrl());
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto* dialog = window->findChild<QObject*>("approval");
        QVERIFY(dialog);
        firstService.requestConfirmation();
        const auto firstId = vm->pendingConfirmation().value("id").toString();
        QTRY_COMPARE(dialog->property("pendingToolCallId").toString(), firstId);
        secondService->requestConfirmation();
        QTRY_COMPARE(dialog->property("pendingCount").toInt(), 2);
        QCOMPARE(dialog->property("pendingToolCallId").toString(), firstId);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_COMPARE(firstService.requests.size(), 2);
        QTRY_COMPARE(dialog->property("pendingCount").toInt(), 1);
        QVERIFY(dialog->property("pendingToolCallId").toString() != firstId);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(vm->pendingConfirmation().isEmpty());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        vm->setAgent(nullptr);
    }

    void localOnlyKnowledgeGateUsesEachTasksModel() {
        auto& registry = ToolRegistry::instance();
        registry.registerTool(std::make_unique<ConfirmationTool>("search_knowledge_base"));
        RecordingService localService, cloudService;
        AIAgent local, cloud;
        local.setLLMService(&localService);
        cloud.setLLMService(&cloudService);
        for (auto* agent : {&local, &cloud}) {
            agent->setToolRegistry(&registry);
            agent->setLocalOnlyMode(true);
        }
        local.setModelLocal(true);
        cloud.setModelLocal(false);
        QVERIFY(local.startTask("local"));
        QVERIFY(cloud.startTask("cloud"));
        auto exposesKnowledge = [](const QJsonArray& tools) {
            for (const auto& entry : tools)
                if (entry.toObject().value("function").toObject().value("name")
                        == QLatin1String("search_knowledge_base")) return true;
            return false;
        };
        QVERIFY(exposesKnowledge(localService.definitions.last()));
        QVERIFY(!exposesKnowledge(cloudService.definitions.last()));
        cloudService.requestConfirmation("search_knowledge_base");
        QCOMPARE(cloud.state(), AgentState::Thinking);
        QCOMPARE(cloudService.requests.size(), 2);
        local.stop();
        cloud.stop();
    }

    void queuedShellCancellationNeverStartsCommand() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString marker = directory.filePath("must-not-exist.txt");
        auto* pool = QThreadPool::globalInstance();
        const int previousLimit = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        auto blocker = QtConcurrent::run([&]() { entered.release(); release.acquire(); });
        const auto restorePool = qScopeGuard([&]() {
            release.release();
            blocker.waitForFinished();
            pool->waitForDone();
            pool->setMaxThreadCount(previousLimit);
        });
        QVERIFY(entered.tryAcquire(1, 2000));
        RecordingService service;
        AIAgent agent;
        agent.setLLMService(&service);
        agent.setToolRegistry(&ToolRegistry::instance());
        agent.setPermissionMode(PermissionMode::FullAccess);
        QVERIFY(agent.startTask("queued shell"));
        LLMResponse response;
        response.success = true;
        response.toolCalls.append({"queued-shell", "shell_exec", QJsonObject{
            {"command", QStringLiteral("echo must-not-run > \"%1\"").arg(QDir::toNativeSeparators(marker))}}});
        emit service.responseReady(response);
        QCOMPARE(agent.state(), AgentState::ExecutingTool);
        agent.stop();
        release.release();
        QVERIFY(pool->waitForDone(5000));
        QVERIFY(!QFileInfo::exists(marker));
        QCoreApplication::processEvents();
    }

    // A1 (hit-locate) 回归：ChangeBus 的 qint64 在 QML 里是 JS number。
    // 旧代码 value.toLongLong() 抛 TypeError，整条定位链路被吞掉；
    // 修正为 Number(value) 后必须真的滚到命中行并高亮。
    void searchRevealLocatesNumericMessageId() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QTest::qWait(100);
        auto* page = window->findChild<QObject*>(QStringLiteral("agentChatPage"));
        auto* transcript =
            window->findChild<QObject*>(QStringLiteral("transcriptViewport"));
        QVERIFY(page);
        QVERIFY(transcript);
        auto* model = qobject_cast<TraceModel*>(vm->traceModel());
        QVERIFY(model);

        QVariantList nodes;
        for (int i = 0; i < 40; ++i) {
            nodes.append(QVariantMap{{QStringLiteral("id"), QString::number(i)},
                                     {QStringLiteral("type"), QStringLiteral("user_message")},
                                     {QStringLiteral("title"),
                                      QStringLiteral("消息 %1").arg(i)},
                                     {QStringLiteral("status"), QStringLiteral("completed")},
                                     {QStringLiteral("messageId"), i + 1},
                                     {QStringLiteral("timestamp"), i}});
        }
        model->resetNodes(nodes);
        QTRY_COMPARE(transcript->property("count").toInt(), 40);
        QCOMPARE(page->property("revealHighlightIndex").toInt(), -1);

        // messageId 37 → 第 36 行（0 起）。
        ChangeBus::instance()->notify(QStringLiteral("search.revealMessage"),
                                      QVariant::fromValue<qint64>(37));
        QTRY_COMPARE(page->property("revealHighlightIndex").toInt(), 36);
        QTRY_VERIFY(transcript->property("scrollOffset").toReal() > 0.0);
    }

    // 折叠键回归：无 id 的旧/外部 trace 节点（流式中间节点的形态）不得
    // 再用内容前缀做 key。内容更新后 key 必须保持不变，否则用户刚展开
    // 的卡片会被下一帧流式增量折回去。
    void traceDetailKeyIsStableWithoutNodeId() {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Theme", ThemeProvider::instance());
        engine.load(QUrl("qrc:/qt/qml/NexusAI/src/ui/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QQmlComponent comp(
            &engine, QUrl("qrc:/qt/qml/NexusAI/src/ui/components/TraceNodeRouter.qml"));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QVariantMap data{{QStringLiteral("type"), QStringLiteral("user_message")},
                         {QStringLiteral("title"), QStringLiteral("消息")},
                         {QStringLiteral("status"), QStringLiteral("completed")},
                         {QStringLiteral("content"), QStringLiteral("第一段")}};
        QObject* router = comp.createWithInitialProperties(
            {{QStringLiteral("modelData"), data},
             {QStringLiteral("index"), 0},
             {QStringLiteral("showProcess"), true},
             {QStringLiteral("sessionId"), QStringLiteral("s")}});
        QVERIFY2(router, qPrintable(comp.errorString()));
        QCOMPARE(router->property("detailKey").toString(),
                 QStringLiteral("v2:s:0"));
        data[QStringLiteral("content")] = QStringLiteral("第二段，内容已经完全不同");
        router->setProperty("modelData", data);
        QCOMPARE(router->property("detailKey").toString(),
                 QStringLiteral("v2:s:0"));
        delete router;
    }
};

QTEST_MAIN(TestChatWorkflows)
#include "test_chat_workflows.moc"
