#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

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
#include "infrastructure/llm/IAIService.h"
#include "infrastructure/llm/OpenAIService.h"
#include "infrastructure/llm/OllamaService.h"
#include "infrastructure/llm/AnthropicService.h"
#include "infrastructure/embed/OllamaEmbedding.h"

#include <memory>

using namespace NexusAI;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("NexusAI");
    app.setApplicationName("NexusAI");
    app.setApplicationVersion("0.2.0");

    // ── Settings (QSettings + env fallbacks) ──────────
    auto* settings = new SettingsService(&app);

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
    // Expose the knowledge base to the model as a safe auto-executed tool
    ToolRegistry::instance().registerTool(std::make_unique<KnowledgeSearchTool>(rag));

    auto* agent = new AIAgent(&app);
    agent->setToolRegistry(&ToolRegistry::instance());
    agent->setMemory(memory);

    // ── ViewModels ─────────────────────────────────────
    auto* themeManager = new ThemeManager(&app);
    auto* themeProvider = new ThemeProvider(themeManager, &app);
    auto* agentVM = new AgentViewModel(&app);
    agentVM->setRagService(rag);
    auto* connectionTester = new ConnectionTester(&app);

    // Apply the active model profile to the services and select the provider
    const auto applySettings = [settings, openai, anthropic, ollama, agent]() {
        const QVariantMap profile = settings->activeProfile();
        const QString provider = profile.value("provider").toString();
        const QString baseUrl = profile.value("baseUrl").toString();
        const QString apiKey = profile.value("apiKey").toString();
        const QString model = profile.value("model").toString();

        openai->setApiKey(apiKey);
        openai->setBaseUrl(baseUrl);
        openai->setModel(model);
        anthropic->setApiKey(apiKey);
        anthropic->setBaseUrl(baseUrl);
        anthropic->setModel(model);
        ollama->setBaseUrl(baseUrl);
        ollama->setModel(model);
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
    QQmlApplicationEngine engine;

    // Register context properties for QML
    engine.rootContext()->setContextProperty("Theme", themeProvider);
    engine.rootContext()->setContextProperty("ThemeManager", themeManager);
    engine.rootContext()->setContextProperty("AgentVM", agentVM);
    engine.rootContext()->setContextProperty("Settings", settings);
    engine.rootContext()->setContextProperty("ConnectionTester", connectionTester);

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
