#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QtConcurrent/QtConcurrentRun>
#include <QQuickStyle>
#include <QTranslator>

#include "viewmodels/ThemeManager.h"
#include "viewmodels/ThemeProvider.h"
#include "viewmodels/AgentViewModel.h"
#include "viewmodels/SettingsService.h"
#include "viewmodels/ConnectionTester.h"
#include "core/agent/AIAgent.h"
#include "core/memory/SqliteMemory.h"
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "core/utils/Logger.h"
#include "core/utils/Activity.h"
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

    // FEAT-804: load the matching translation if one is bundled. Missing or
    // untranslated strings fall back to their source text.
    QTranslator translator;
    if (translator.load(QLocale::system(), QStringLiteral("nexus_ai"),
                        QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        app.installTranslator(&translator);
        qInfo() << "Loaded translation:" << translator.filePath();
    }

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
    // Apply the configured embedding model BEFORE the restore snapshot: the
    // persisted-chunk usability check compares against this identity.
    embedding->setModel(settings->embeddingModel());
    rag->setEmbeddingModelName(embedding->model());
    // Expose the knowledge base to the model as a safe auto-executed tool
    ToolRegistry::instance().registerTool(std::make_unique<KnowledgeSearchTool>(rag));

    // ── Batch 14 (SEC-1401/1402): local-only mode gate ──
    // When enabled, the knowledge-base tool is gated off for cloud providers:
    // the model no longer sees the schema, and stale calls from history get
    // a structured error instead of an execution (document content must not
    // leave this machine). Local providers (ollama) keep the tool - content
    // never leaves the machine there. Cloud detection is by provider type,
    // not baseUrl: an OpenAI-compatible gateway on 127.0.0.1 still forwards
    // to a cloud upstream, so it counts as cloud.
    const auto applyLocalOnlyGate = [settings]() {
        const bool cloud = settings->activeProfile().value("provider").toString()
                           != QLatin1String("ollama");
        ToolRegistry::instance().setToolEnabled(
            QStringLiteral("search_knowledge_base"),
            !(cloud && settings->localOnlyMode()));
    };
    QObject::connect(settings, &SettingsService::localOnlyModeChanged, &app,
                     applyLocalOnlyGate);
    // Profile switch may move between ollama and a cloud provider: re-apply.
    QObject::connect(settings, &SettingsService::activeProfileChanged, &app,
                     applyLocalOnlyGate);
    applyLocalOnlyGate();

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
    // Ingestion completes on worker threads; documentPersisted arrives
    // queued on the GUI thread (&app context) so all SQLite access stays
    // single-threaded. Covers import, re-embed AND the background restore.
    auto* ragForPersist = rag;
    QObject::connect(rag, &RagService::documentPersisted, &app,
        [memory, ragForPersist](const QString& name, const QString& content,
                                const QVector<KnowledgeChunk>& chunks) {
            memory->upsertKnowledgeDoc(name, content,
                                       knowledgeContentHash(content),
                                       ragForPersist->embeddingModelName());
            memory->replaceKnowledgeChunks(name, chunks);
        });
    QObject::connect(agentVM, &AgentViewModel::documentRemoved, &app,
        [memory](const QString& name) { memory->removeKnowledgeDoc(name); });
    QObject::connect(agentVM, &AgentViewModel::knowledgeCleared, &app,
        [memory]() { memory->clearKnowledgeDocs(); });

    // ── Usage trail sink (日志增强) ──────────────────────
    // User operations / network outcomes flow through the Activity facade;
    // persistence happens on the GUI thread (SQLite is single-threaded) via
    // a queued invocation from whichever thread emitted the event.
    auto* usageMemory = memory;
    Activity::setUsageSink(
        [usageMemory, &app](const QString& category, const QString& action,
                            const QString& target, quint64 requestId,
                            const QString& detail) {
            // SqliteMemory is not a QObject; marshal onto the GUI thread via
            // the application object (SQLite is single-threaded).
            QMetaObject::invokeMethod(
                &app,
                [usageMemory, category, action, target, requestId, detail]() {
                    usageMemory->addUsageEvent(category, action, target,
                                               requestId, detail);
                },
                Qt::QueuedConnection);
        });
    const int pruned = memory->pruneUsage();
    if (pruned > 0)
        qInfo() << "Pruned" << pruned << "expired usage-log rows";

    // Restore previously imported documents in the background (Batch 6).
    // Snapshot records AND chunks on the GUI thread (the SQLite connection
    // is single-threaded); the worker then picks per document:
    //   - chunks usable (content hash unchanged + same embedding model):
    //     rebuilt from stored vectors with ZERO embedding requests;
    //   - otherwise: full re-ingest, writing the new chunk layout back via
    //     documentPersisted (self-healing for upgraded/stale databases).
    // Each step binds to the restore generation it started with: a
    // clear/remove mid-restore supersedes the rest.
    {
        struct RestoreItem {
            QString name;
            QString content;
            QString contentHash;
            QString embeddingModel;
            QVector<KnowledgeChunk> chunks;
        };
        QVector<RestoreItem> restoreItems;
        for (const auto& rec : memory->loadKnowledgeDocRecords()) {
            RestoreItem item;
            item.name = rec.name;
            item.content = rec.content;
            item.contentHash = rec.contentHash;
            item.embeddingModel = rec.embeddingModel;
            item.chunks = memory->loadKnowledgeChunks(rec.name);
            restoreItems.append(item);
        }
        if (!restoreItems.isEmpty()) {
            qInfo() << "Restoring" << restoreItems.size() << "knowledge document(s)";
            auto* ragPtr = rag;
            const QString currentModel = rag->embeddingModelName();
            const quint64 restoreGeneration = rag->restoreGeneration();
            (void)QtConcurrent::run([ragPtr, restoreItems, currentModel,
                                     restoreGeneration]() {
                int fromChunks = 0, reEmbedded = 0;
                for (const RestoreItem& item : restoreItems) {
                    if (persistedChunksUsable(item.contentHash, item.embeddingModel,
                                              item.content, currentModel)
                            && ragPtr->restorePersistedDocument(
                                   item.name, item.chunks, restoreGeneration)) {
                        ++fromChunks;
                        continue;
                    }
                    ++reEmbedded;
                    ragPtr->ingestRestoredDocument(item.name, item.content,
                                                   restoreGeneration);
                }
                qInfo() << "Knowledge restore:" << fromChunks
                        << "from persisted chunks," << reEmbedded << "re-embedded";
            });
        }
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

        // Read sandbox policy follows the (opt-in, default-off) setting
        Sandbox::setReadPolicy(settings->allowFullDiskRead()
            ? Sandbox::ReadPolicy::FullDisk : Sandbox::ReadPolicy::UserDirectories);

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
    QObject::connect(settings, &SettingsService::allowFullDiskReadChanged, &app, [applySettings]() { applySettings(); });

    // FEAT-801: switching the embedding model invalidates every stored
    // chunk vector → full knowledge re-embed in the background (the new
    // layout is written back via documentPersisted).
    QObject::connect(settings, &SettingsService::embeddingModelChanged, &app,
        [settings, embedding, rag, memory]() {
            if (embedding->model() == settings->embeddingModel()) return;
            embedding->setModel(settings->embeddingModel());
            rag->setEmbeddingModelName(embedding->model());

            struct ReembedItem { QString name; QString content; };
            QVector<ReembedItem> items;
            for (const auto& rec : memory->loadKnowledgeDocRecords())
                items.append({rec.name, rec.content});
            if (items.isEmpty()) return;

            qInfo() << "Embedding model changed - re-embedding"
                    << items.size() << "document(s)";
            const quint64 gen = rag->restoreGeneration();
            (void)QtConcurrent::run([rag, items, gen]() {
                for (const ReembedItem& item : items)
                    rag->ingestRestoredDocument(item.name, item.content, gen);
            });
        });

    // FEAT-803: batch-embedding progress → knowledge panel
    QObject::connect(embedding, &OllamaEmbedding::embedProgress,
                     agentVM, &AgentViewModel::setEmbedProgress);

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
