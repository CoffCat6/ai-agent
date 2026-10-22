#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
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
#include <QDir>
#include <QtConcurrent/QtConcurrentRun>

#include "core/agent/AIAgent.h"
#include "core/memory/SqliteMemory.h"
#include "core/tools/ToolRegistry.h"
#include "infrastructure/llm/IAIService.h"
#include "viewmodels/AgentViewModel.h"
#include "viewmodels/GitViewModel.h"
#include "viewmodels/ThemeProvider.h"

using namespace NexusAI;

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

    // A fresh window is a local chat: the right panel must open on the
    // knowledge base, not on the (hidden) Git section. Parking the stack on
    // Git there is what made the first launch show the Git panel's
    // "identifying a repository" line for a session that has no project.
    void freshWindowOpensPanelOnKnowledgeNotGit() {
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

        auto* gitTab = window->findChild<QObject*>(QStringLiteral("gitTab"));
        QVERIFY2(gitTab, "上下文面板缺少 gitTab");
        auto* kbTab = window->findChild<QObject*>(QStringLiteral("knowledgeTab"));
        QVERIFY2(kbTab, "上下文面板缺少 knowledgeTab");

        // The Git tab is hidden for local chats ...
        QCOMPARE(gitTab->property("visible").toBool(), false);
        // ... and the panel is not showing its section.
        QCOMPARE(kbTab->property("highlighted").toBool(), true);
        QCOMPARE(gitTab->property("highlighted").toBool(), false);

        // Nothing in the right panel is identifying a repository. The Git
        // section is not the visible stack page here (knowledge is), so its
        // own "no project" line is expected to be off screen - what matters
        // is that no git query is in flight and no "identifying" text is on
        // the visible page.
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
};

QTEST_MAIN(TestChatWorkflows)
#include "test_chat_workflows.moc"
