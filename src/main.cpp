#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QtConcurrent/QtConcurrentRun>
#include <QQuickStyle>

#include "viewmodels/ThemeManager.h"
#include "viewmodels/ThemeProvider.h"
#include "viewmodels/AgentViewModel.h"
#include "viewmodels/SettingsService.h"
#include "viewmodels/ConnectionTester.h"
#include "core/agent/AIAgent.h"
#include "core/memory/SqliteMemory.h"
#include "core/tools/ToolRegistry.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "core/utils/Logger.h"
#include "infrastructure/llm/IAIService.h"
#include "infrastructure/llm/OpenAIService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/AnthropicService.h"
#include "infrastructure/embed/OllamaEmbedding.h"

#include <memory>

using namespace NexusAI;

namespace {

// Local backends (Ollama at localhost) must bypass any system proxy:
// routing loopback traffic through e.g. a local clash instance breaks
// the connection ("connection refused" after a seconds-long detour).
class LoopbackProxyFactory : public QNetworkProxyFactory {
public:
    QVector<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override {
        const QString host = query.url().host();
        if (host == QLatin1String("localhost")
            || host == QLatin1String("127.0.0.1")
            || host == QLatin1String("::1"))
            return { QNetworkProxy(QNetworkProxy::NoProxy) };
        return systemProxyForQuery(query);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("NexusAI");
    app.setApplicationName("NexusAI");
    app.setApplicationVersion("0.2.0");

    // Non-native control style: the UI customizes TextField/Dialog
    // backgrounds everywhere, which the native (windows) style forbids -
    // Basic keeps those customizations effective and silences the
    // "style does not support customization" warnings.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QNetworkProxyFactory::setApplicationProxyFactory(new LoopbackProxyFactory);

    // ── Logging: mirror all Qt messages to a rotating file ──
    // (windowed build has no console; see <AppLocalData>/logs/)
    Logger::install();
    qInfo() << "NexusAI" << app.applicationVersion() << "starting";
    qInfo() << "Log file:" << Logger::logFilePath();

    // ── Settings (QSettings + env fallbacks) ──────────
    // QML singletons are lazily created through their static factories;
    // main.cpp takes them via instance() first so dependencies can be
    // injected before the engine loads the UI.
    auto* settings = SettingsService::instance();
    settings->setParent(&app);

    // ── LLM services ───────────────────────────────────
    // All three are instantiated; the active one is selected by provider and
    // can be swapped at runtime when the user changes settings.
    auto* openai = new OpenAIService(&app);
    auto* anthropic = new AnthropicService(&app);
    auto* ollama = new OllamaService(&app);

    // ── Agent core (real ReAct pipeline) ──────────────
    // Process-lifetime memory backend (SQLite, app-data location).
    auto* memory = new SqliteMemory();

    // Knowledge base: Ollama embeddings (nomic-embed-text) with the
    // retriever's local pseudo-embedding as offline fallback.
    auto* embedding = new OllamaEmbedding(&app);
    auto* rag = new RagService(&app);
    rag->setEmbedder([embedding](const QString& text) {
        return embedding->embed(text);
    });
    rag->setBatchEmbedder([embedding](const QStringList& texts) {
        return embedding->embedBatch(texts);
    });
    // Expose the knowledge base to the model as a safe auto-executed tool
    ToolRegistry::instance().registerTool(std::make_unique<KnowledgeSearchTool>(rag));

    auto* agent = new AIAgent(&app);
    agent->setToolRegistry(&ToolRegistry::instance());
    agent->setMemory(memory);

    // ── ViewModels (QML singletons, owned by the app) ──
    auto* themeManager = ThemeManager::instance();
    themeManager->setParent(&app);
    auto* themeProvider = ThemeProvider::instance();
    themeProvider->setParent(&app);
    auto* agentVM = AgentViewModel::instance();
    agentVM->setParent(&app);
    agentVM->setRagService(rag);
    // Re-embedding pulls persisted content from SQLite (GUI thread, fast).
    agentVM->setKnowledgeContentProvider([memory](const QString& name) {
        return memory->loadKnowledgeDoc(name);
    });
    auto* connectionTester = ConnectionTester::instance();
    connectionTester->setParent(&app);

    // ── Knowledge-base persistence ─────────────────────
    // Import/remove/clear arrive from worker threads; the lambdas run on
    // the GUI thread (&app context) so all SQLite access stays single-threaded.
    QObject::connect(agentVM, &AgentViewModel::documentImported, &app,
        [memory](const QString& name, const QString& content) {
            memory->upsertKnowledgeDoc(name, content);
        });
    QObject::connect(agentVM, &AgentViewModel::documentRemoved, &app,
        [memory](const QString& name) { memory->removeKnowledgeDoc(name); });
    QObject::connect(agentVM, &AgentViewModel::knowledgeCleared, &app,
        [memory]() { memory->clearKnowledgeDocs(); });

    // Restore previously imported documents in the background (embedding
    // may hit the network); the queued changed() signal updates the UI.
    const QJsonObject savedDocs = memory->loadKnowledgeDocs();
    if (!savedDocs.isEmpty()) {
        qInfo() << "Restoring" << savedDocs.size() << "knowledge document(s)";
        auto ragPtr = rag;
        const auto docs = savedDocs.toVariantMap();
        (void)QtConcurrent::run([ragPtr, docs]() {
            for (auto it = docs.constBegin(); it != docs.constEnd(); ++it)
                ragPtr->ingestDocument(it.key(), it.value().toString());
        });
    }

    // Apply the active model profile to the services and select the provider
    const auto applySettings = [settings, openai, anthropic, ollama, agent]() {
        const QVariantMap profile = settings->activeProfile();
        const QString provider = profile.value("provider").toString();
        const QString baseUrl = profile.value("baseUrl").toString();
        const QString apiKey = profile.value("apiKey").toString();
        const QString model = profile.value("model").toString();

        // Only the active provider receives the profile: broadcasting one
        // profile's baseUrl to all services would corrupt the others
        // (e.g. an OpenAI-compatible proxy URL ending in /v1 turned into
        // /v1/v1/messages on the Anthropic path). Each service is configured
        // here right before it becomes the active one.
        if (provider == QLatin1String("anthropic")) {
            anthropic->setApiKey(apiKey);
            anthropic->setBaseUrl(baseUrl);
            anthropic->setModel(model);
        } else if (provider == QLatin1String("ollama")) {
            ollama->setBaseUrl(baseUrl);
            ollama->setModel(model);
        } else if (provider == QLatin1String("openai")) {
            openai->setApiKey(apiKey);
            openai->setBaseUrl(baseUrl);
            openai->setModel(model);
        }
        agent->setContextLimit(settings->contextTokens());

        if (provider.isEmpty()) {
            // No profile configured: the agent reports a helpful error
            agent->setLLMService(nullptr);
            return;
        }
        IAIService* active = provider == QLatin1String("ollama")
            ? static_cast<IAIService*>(ollama)
            : provider == QLatin1String("anthropic")
            ? static_cast<IAIService*>(anthropic)
            : static_cast<IAIService*>(openai);
        agent->setLLMService(active);
    };
    applySettings();

    // Reconfigure instantly when the active profile or global settings change
    QObject::connect(settings, &SettingsService::activeProfileChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::contextTokensChanged, &app, [applySettings]() { applySettings(); });

    // Live pipeline vs. built-in simulation
    const auto applyMode = [settings, agentVM, agent]() {
        agentVM->setAgent(settings->simulationMode() ? nullptr : agent);
    };
    applyMode();
    QObject::connect(settings, &SettingsService::simulationModeChanged, &app, [applyMode]() { applyMode(); });

    // ── QML Engine ─────────────────────────────────────
    // Theme/ThemeManager/AgentVM/Settings/ConnectionTester resolve to the
    // NexusAI.ViewModels QML singletons above (create() returns the very
    // instances wired up here).
    QQmlApplicationEngine engine;

    // Load main QML
    const QUrl url(QStringLiteral("qrc:/qt/qml/NexusAI/src/ui/main.qml"));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
