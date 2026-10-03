#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QHash>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QPalette>
#include <QtConcurrent/QtConcurrentRun>
#include <QQuickStyle>
#include <QTranslator>

#include "viewmodels/ThemeManager.h"
#include "viewmodels/ThemeProvider.h"
#include "viewmodels/AgentViewModel.h"
#include "core/mcp/McpConfig.h"
#include "viewmodels/McpManager.h"
#include "viewmodels/SettingsService.h"
#include "viewmodels/ConnectionTester.h"
#include "viewmodels/PreferenceRegistry.h"
#include "viewmodels/PreferenceStore.h"
#include "viewmodels/ChangeBus.h"
#include "viewmodels/FontProbe.h"
#include "viewmodels/LocaleProbe.h"
#include "viewmodels/I18nManager.h"
#include "core/agent/AIAgent.h"
#include "core/agent/PermissionMode.h"
#include "core/memory/SqliteMemory.h"
#include "core/agent/EpisodicService.h"
#include "core/tools/ToolRegistry.h"
#include "core/tools/Sandbox.h"
#include "core/tools/NetworkFetchTool.h"
#include "core/tools/WebSearchTool.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "core/utils/Logger.h"
#include "core/utils/Activity.h"
#include "core/utils/SamplingParams.h"
#include "infrastructure/llm/IAIService.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"
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

// 一套按协议键索引的 LLM 服务集合：进程级一份 + 每个会话 tab 一份。
using ProviderSet = QHash<QString, IAIService*>;

// profile map → 服务配置快照。清洗沿用 SamplingParams 的"未设置/清空/非数字
// 都不发该字段"语义（Batch 15 FEAT-1501/1502、Batch 21 A1）；哪个协议消费哪
// 几个字段由各 Service 的 applyProfileParams 决定，装配层不再按协议分支。
LlmProfileConfig profileConfigOf(const QVariantMap& profile) {
    LlmProfileConfig cfg;
    cfg.baseUrl = profile.value("baseUrl").toString();
    cfg.apiKey = profile.value("apiKey").toString();
    cfg.model = profile.value("model").toString();
    cfg.temperature = SamplingParams::optionalDouble(profile.value("temperature"));
    cfg.maxOutputTokens = SamplingParams::optionalPositiveInt(
        profile.value("maxOutputTokens"));
    cfg.numCtx = SamplingParams::optionalPositiveInt(profile.value("numCtx"));
    cfg.reasoningEffort = SamplingParams::optionalReasoningLevel(
        profile.value("reasoningEffort"));
    return cfg;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("NexusAI");
    app.setApplicationName("NexusAI");
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    // Non-native control style: the UI customizes TextField/Dialog
    // backgrounds everywhere, which the native (windows) style forbids -
    // Basic keeps those customizations effective and silences the
    // "style does not support customization" warnings.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QNetworkProxyFactory::setApplicationProxyFactory(new LoopbackProxyFactory);

    // FEAT-804: load the matching translation if one is bundled. Missing or
    // untranslated strings fall back to their source text.
    // I18nManager is fully wired after the preference infrastructure below;
    // this early translator covers the gap before the engine loads.

    // ── Logging: mirror all Qt messages to a rotating file ──
    // (windowed build has no console; see <AppLocalData>/logs/)
Logger::install();
// T9 (REQ-T9-01): backfill log history from previous session for the in-app
// log panel. Non-blocking; failures return 0 and never prevent startup.
Logger::backfillFromFile();
qInfo() << "NexusAI" << app.applicationVersion() << "starting";
    qInfo() << "Log file:" << Logger::logFilePath();

    // ── Preference infrastructure (Batch: General Settings) ──
    // Must be initialized before SettingsService and ThemeManager so
    // downstream services can register preferences and subscribe to
    // changes on startup.
    auto* prefRegistry = PreferenceRegistry::instance();
    prefRegistry->setParent(&app);
    auto* changeBus = ChangeBus::instance();
    changeBus->setParent(&app);
    auto* prefStore = PreferenceStore::instance();
    prefStore->setParent(&app);
    prefStore->setChangeBus(changeBus);
    auto* fontProbe = FontProbe::instance();
    fontProbe->setParent(&app);
    auto* localeProbe = LocaleProbe::instance();
    localeProbe->setParent(&app);

    // I18n manager: runtime language switching, persisted preference
    auto* i18n = I18nManager::instance();
    i18n->setParent(&app);
    i18n->applyLocale(i18n->currentLocale());

    // Register setting categories (order determines nav display order)
    prefRegistry->registerCategory(QStringLiteral("model"),
        QT_TRANSLATE_NOOP("NexusAI", "模型配置"), 1);
    prefRegistry->registerCategory(QStringLiteral("interface"),
        QT_TRANSLATE_NOOP("NexusAI", "界面设置"), 2);
    prefRegistry->registerCategory(QStringLiteral("general"),
        QT_TRANSLATE_NOOP("NexusAI", "通用设置"), 3);
    // docs/17 A6: 独立安全分类（沙箱/黑名单/隐私门）。
    prefRegistry->registerCategory(QStringLiteral("security"),
        QT_TRANSLATE_NOOP("NexusAI", "安全"), 4);
    prefRegistry->registerCategory(QStringLiteral("mcp"),
        QT_TRANSLATE_NOOP("NexusAI", "MCP 服务器"), 5);
    prefRegistry->registerCategory(QStringLiteral("tools"),
        QT_TRANSLATE_NOOP("NexusAI", "工具管理"), 6);
    // M1: browse / search / forget long-term episodic memory summaries.
    prefRegistry->registerCategory(QStringLiteral("memory"),
        QT_TRANSLATE_NOOP("NexusAI", "记忆管理"), 7);
    // M1 (stats): token / cost usage aggregation.
    prefRegistry->registerCategory(QStringLiteral("usage"),
        QT_TRANSLATE_NOOP("NexusAI", "用量统计"), 8);

    // Internal UI state (not shown in any category panel; categoryId empty)
    {
        PreferenceMeta lastCategory;
        lastCategory.key = QStringLiteral("settings.lastCategory");
        lastCategory.categoryId = QString();
        lastCategory.displayName =
            QT_TRANSLATE_NOOP("NexusAI", "上次访问分类");
        lastCategory.defaultValue = QStringLiteral("model");
        lastCategory.type = PreferenceType::String;
        prefRegistry->registerPreference(lastCategory);
    }

    // ── Settings (QSettings + env fallbacks) ──────────
    // QML singletons are lazily created through their static factories;
    // main.cpp takes them via instance() first so dependencies can be
    // injected before the engine loads the UI.
    auto* settings = SettingsService::instance();
    settings->setParent(&app);

    // ── LLM services ───────────────────────────────────
    // 每个已注册协议一个实例（表在 LlmProtocolRegistry）；活动 profile 的
    // apiProtocol 决定用哪个，改设置时即时换绑。
    const ProviderSet templateSet = LlmProtocolRegistry::createServiceSet(&app);

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
    // Batch 25 (G11): endpoint too — Ollama may live on another host/port.
    embedding->setBaseUrl(settings->embeddingBaseUrl());
    // Expose the knowledge base to the model as a safe auto-executed tool
    ToolRegistry::instance().registerTool(std::make_unique<KnowledgeSearchTool>(rag));

    // Batch 20 (FEAT-2001): web fetch tool; RequiresConfirm per call. The
    // SSRF escape hatch (allowPrivateTargets) follows the settings below.
    auto fetchOwned = std::make_unique<NetworkFetchTool>();
    auto* fetchPtr = fetchOwned.get();
    ToolRegistry::instance().registerTool(std::move(fetchOwned));

    // T6 (F3): web search tool - wired with the configured provider and
    // API key (DPAPI-encrypted). The key is injected after applySettings.
    auto webOwned = std::make_unique<WebSearchTool>();
    auto* webPtr = webOwned.get();
    ToolRegistry::instance().registerTool(std::move(webOwned));

    // L4: web search backend factory. Maps the provider key from settings to
    // a concrete WebSearchBackend. Unknown keys fall back to Tavily with a
    // warning (the setting is also clamped in SettingsService, but this is a
    // defense-in-depth fallback for corrupted configs).
    const auto createWebSearchBackend = [](const QString& providerKey)
        -> std::unique_ptr<WebSearchBackend> {
        if (providerKey == QLatin1String("tavily"))
            return std::make_unique<TavilyBackend>();
        if (providerKey == QLatin1String("duckduckgo"))
            return std::make_unique<DuckDuckGoBackend>();
        qWarning() << "Unknown web search provider:" << providerKey
                   << "- falling back to tavily";
        return std::make_unique<TavilyBackend>();
    };

    // ── Batch 14 (SEC-1401/1402): local-only mode gate ──
    // When enabled, the knowledge-base tool is gated off for cloud providers:
    // the model no longer sees the schema, and stale calls from history get
    // a structured error instead of an execution (document content must not
    // leave this machine). Local providers (ollama) keep the tool - content
    // never leaves the machine there. Cloud detection is by provider type,
    // not baseUrl: an OpenAI-compatible gateway on 127.0.0.1 still forwards
    // to a cloud upstream, so it counts as cloud.
    // docs/17 B1: local-only gating writes the RUNTIME policy layer, never
    // the user's tools/disabled persistence — toggling the mode or switching
    // profiles no longer resets user-disabled switches. Local providers
    // (ollama) keep everything - content never leaves the machine there.
    // Cloud detection is by provider type, not baseUrl: an OpenAI-compatible
    // gateway on 127.0.0.1 still forwards to a cloud upstream, so it counts
    // as cloud.
    const auto applyLocalOnlyGate = [settings]() {
        const QVariantMap profile = settings->activeProfile();
        // 本地与否取自协议注册表的 local 标记：厂商字符串比的是"ollama"，
        // 而真正决定内容出不出机器的是这条线协议走本地还是云端。
        const bool local = LlmProtocolRegistry::isLocal(
            profile.value("apiProtocol").toString(),
            profile.value("provider").toString());
        const bool gate = settings->localOnlyMode() && !local;
        QStringList gated{QStringLiteral("search_knowledge_base"),
                          QStringLiteral("web_search")};
        // T14 (REQ-T14-04): every MCP tool (mcp__*) may reach external
        // servers that forward data off-machine.
        const auto all = ToolRegistry::instance().listAll();
        for (const auto& def : all) {
            if (def.name.startsWith(QStringLiteral("mcp__")))
                gated.append(def.name);
        }
        ToolRegistry::instance().setPolicyBlocked(gated, gate);
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

    // T8 (F4): episodic long-term memory service.
    auto* episodicService = new EpisodicService(&app);
    episodicService->setMemory(memory);
    agent->setEpisodicService(episodicService);

    // ── ViewModels (QML singletons, owned by the app) ──
    auto* themeManager = ThemeManager::instance();
    themeManager->setParent(&app);
    auto* themeProvider = ThemeProvider::instance();
    themeProvider->setParent(&app);

    // Basic-style controls (ComboBox/SpinBox/Switch/Slider…) take their
    // colors from the application palette, not from the themed QML
    // properties - keep the palette in sync with dark/light so they don't
    // render with the system's (light) palette inside a dark UI.
    const auto applyControlPalette = [themeProvider]() {
        QPalette pal;
        const bool dark = themeProvider->isDark();
        const QColor surface = themeProvider->bgSecondary();
        const QColor text = themeProvider->textPrimary();
        const QColor textDim = themeProvider->textSecondary();
        const QColor border = themeProvider->borderSubtle();
        const QColor brand = themeProvider->brandPrimary();

        pal.setColor(QPalette::Window, surface);
        pal.setColor(QPalette::WindowText, text);
        pal.setColor(QPalette::Base, themeProvider->bgPrimary());
        pal.setColor(QPalette::AlternateBase, surface);
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::PlaceholderText, textDim);
        pal.setColor(QPalette::Button, surface);
        pal.setColor(QPalette::ButtonText, text);
        pal.setColor(QPalette::Mid, border);
        pal.setColor(QPalette::Dark, border);
        pal.setColor(QPalette::Highlight, brand);
        pal.setColor(QPalette::HighlightedText, dark ? QColor("#0A0A0F") : QColor("#FFFFFF"));
        pal.setColor(QPalette::ToolTipBase, surface);
        pal.setColor(QPalette::ToolTipText, text);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, textDim);
        pal.setColor(QPalette::Disabled, QPalette::Text, textDim);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, textDim);
        QGuiApplication::setPalette(pal);
    };
    applyControlPalette();
    QObject::connect(themeManager, &ThemeManager::themeChanged, &app, applyControlPalette);

    auto* agentVM = AgentViewModel::instance();
    agentVM->setParent(&app);
    agentVM->setRagService(rag);
    // Batch 17/18: persistence backend for session search and export.
    agentVM->setMemory(memory);
    // Re-embedding pulls persisted content from SQLite (GUI thread, fast).
    agentVM->setKnowledgeContentProvider([memory](const QString& name) {
        return memory->loadKnowledgeDoc(name);
    });
    // Batch 29 (F7): every session tab gets its own agent with its own
    // SQLite connection (same database file — busy_timeout serializes
    // concurrent writes) AND its own full set of protocol services.
    //
    // Per-tab services are load-bearing, not an optimization: LLMServiceBase
    // keeps ONE in-flight request context per service instance and streams
    // through signals broadcast to every attached agent, so two tabs sharing
    // a group would abort each other's streams and leak chunks across
    // traces. The tab agents are destroyed with their tab (services are
    // parented to the agent), and tabProviderSets drops the entry on
    // destroyed so profile re-configuration never touches dead agents.
    const auto applyProfileTo = [settings](const ProviderSet& set) {
        const QVariantMap profile = settings->activeProfile();
        // 只有当前 profile 命中的那一条协议会被配置：把 baseUrl 广播给全部服务
        // 会互相污染（例如以 /v1 结尾的 OpenAI 兼容代理地址被 Anthropic 那条
        // 协议拼成 /v1/v1/messages）。每个服务都在成为活动服务前配置好。
        if (IAIService* service = set.value(profile.value("apiProtocol").toString()))
            service->configure(profileConfigOf(profile));
    };
    const auto activeService = [settings](const ProviderSet& set) -> IAIService* {
        // 未配置的 profile 返回 nullptr，由 agent 报"未配置模型服务"。
        return set.value(settings->activeProfile().value("apiProtocol").toString());
    };

    QHash<AIAgent*, ProviderSet> tabProviderSets;
    agentVM->setAgentFactory([&app, settings, applyProfileTo, activeService,
                              &tabProviderSets]() -> AIAgent* {
        auto* tabAgent = new AIAgent(&app);
        tabAgent->setToolRegistry(&ToolRegistry::instance());
        tabAgent->setMemory(new SqliteMemory());
        tabAgent->setContextLimit(settings->resolveContextLimit());
        tabAgent->setMaxIterations(settings->maxSteps());

        const ProviderSet set = LlmProtocolRegistry::createServiceSet(tabAgent);
        applyProfileTo(set);
        tabProviderSets.insert(tabAgent, set);
        QObject::connect(tabAgent, &QObject::destroyed, &app,
            [&tabProviderSets, tabAgent]() { tabProviderSets.remove(tabAgent); });
        tabAgent->setLLMService(activeService(set));
        return tabAgent;
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

    // ── Batch 31a/31b: MCP servers ─────────────────────
    // Managed by the McpManager singleton (per-server McpClient + tool
    // bridging); the settings page drives runtime add/remove/enable through
    // it. loadAndConnectAll() reads mcp_servers.json and connects every
    // enabled server.
    auto* mcpManager = McpManager::instance();
    mcpManager->setParent(&app);
    mcpManager->loadAndConnectAll();

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

    // Apply the active model profile to the services and select the active
    // protocol service for every tab: the template agent uses the
    // process-lifetime service set, each factory-built tab its own (see the
    // factory comment above).
    const auto applySettings = [settings, agentVM, agent, fetchPtr, webPtr,
                                episodicService, createWebSearchBackend,
                                templateSet, applyProfileTo, activeService,
                                &tabProviderSets]() {
        applyProfileTo(templateSet);
        for (auto it = tabProviderSets.begin(); it != tabProviderSets.end(); ++it)
            applyProfileTo(it.value());

        // The template agent may not be attached to any tab yet (the first
        // applySettings() call runs before applyMode's setAgent), and
        // applyToAgents only reaches tab-attached agents — configure it
        // directly or tab 0 would fail with "No LLM service configured"
        // until a settings change re-runs this lambda.
        agent->setContextLimit(settings->resolveContextLimit());
        // 步数预算（agent/maxSteps）：模板 agent 与各标签 agent 用同一个值。
        agent->setMaxIterations(settings->maxSteps());
        agent->setLLMService(activeService(templateSet));

        agentVM->applyToAgents([&](AIAgent* tabAgent) {
            tabAgent->setContextLimit(settings->resolveContextLimit());
            tabAgent->setMaxIterations(settings->maxSteps());
            const ProviderSet set = tabAgent == agent
                ? templateSet
                : tabProviderSets.value(tabAgent, templateSet);
            tabAgent->setLLMService(activeService(set));
        });

        // Read sandbox policy follows the (opt-in, default-off) setting
        Sandbox::setReadPolicy(settings->allowFullDiskRead()
            ? Sandbox::ReadPolicy::FullDisk : Sandbox::ReadPolicy::UserDirectories);
        // docs/17 P1-4: 项目目录写入放行（默认只读）。
        Sandbox::setProjectWriteEnabled(settings->allowProjectWrite());
        // docs/17 A1: 用户自定义黑名单（与内置默认清单合并生效）。
        QStringList denyList;
        for (const QVariant& entry : settings->readDenyList())
            denyList.append(entry.toString());
        Sandbox::setUserDenyList(denyList);

        // Batch 20: http_fetch SSRF escape hatch follows its (opt-in,
        // default-off) setting; immediate effect on the next fetch.
        fetchPtr->setAllowPrivateTargets(settings->allowLocalNetworkFetch());

        // T6 (F3): inject the current web search API key (DPAPI-decrypted).
        // L4: also (re)create the backend for the configured provider — this
        // was a dead setting before (always Tavily regardless of provider).
        webPtr->setApiKey(settings->webSearchApiKey());
        webPtr->setBackend(createWebSearchBackend(settings->webSearchProvider()));

        // T8 (F4): sync episodic memory toggle and LLM service.
        episodicService->setEnabled(settings->episodicMemoryEnabled());
        episodicService->setLlmService(activeService(templateSet));
    };
    applySettings();

    // docs/17 A1: 黑名单变更即时生效。
    QObject::connect(settings, &SettingsService::readDenyListChanged, &app, [applySettings]() {
        applySettings();
    });

    // Reconfigure instantly when the active profile or global settings change
    QObject::connect(settings, &SettingsService::activeProfileChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::contextTokensChanged, &app, [applySettings]() { applySettings(); });
    // 步数预算改动即时生效（含已在跑的标签）。
    QObject::connect(settings, &SettingsService::maxStepsChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowFullDiskReadChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowProjectWriteChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowLocalNetworkFetchChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::episodicMemoryEnabledChanged, &app, [applySettings]() { applySettings(); });
    // L4: provider switch rebuilds the web search backend immediately.
    QObject::connect(settings, &SettingsService::webSearchProviderChanged, &app, [applySettings]() { applySettings(); });

    // Batch 22 (F8): user-editable system prompt (empty = built-in)
    const auto applySystemPrompt = [agent, settings]() {
        agent->setSystemPromptOverride(settings->systemPromptOverride());
    };
    applySystemPrompt();
    QObject::connect(settings, &SettingsService::systemPromptOverrideChanged, &app, applySystemPrompt);

    // 会话权限模式的全局默认：注入 VM，新 tab 继承；未被用户显式切换过的
    // 既有 tab 追补生效。
    const auto applyPermissionMode = [agentVM, settings]() {
        agentVM->setDefaultPermissionMode(static_cast<int>(
            permissionModeFromString(settings->defaultPermissionMode())));
    };
    // docs/18 期 3: 不再把"上次的项目"回灌到启动标签上——归属按标签持有，
    // 空标签默认本地（普通对话），回灌会与这条默认规则冲突。
    // projects/activeId 降级为"侧栏上次浏览位置"：由 Sidebar 读取以恢复
    // 展开态，不再驱动任何会话归属。
        applyPermissionMode();
    QObject::connect(settings, &SettingsService::defaultPermissionModeChanged,
                     &app, applyPermissionMode);

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

    // Batch 25 (G11): endpoint changes only redirect future requests —
    // stored vectors depend on the embedding model, not the service address,
    // so unlike the model switch above there is no re-embed.
    QObject::connect(settings, &SettingsService::embeddingBaseUrlChanged, &app,
        [settings, embedding]() {
            embedding->setBaseUrl(settings->embeddingBaseUrl());
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

    // Language switches install/remove translators (I18nManager), but the
    // QML engine does not reliably re-evaluate every qsTr binding on the
    // LanguageChange event alone (sidebar buttons, settings dialog buttons
    // and the context-panel notice were observed lagging). Force a full
    // re-evaluation of translation bindings after each switch.
    QObject::connect(i18n, &I18nManager::localeChanged, &engine,
                     &QQmlApplicationEngine::retranslate);

    return app.exec();
}
