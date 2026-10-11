#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QHash>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QPalette>
#include <QtConcurrent/QtConcurrentRun>
#include <QDateTime>
#include <QQuickStyle>
#include <QTranslator>

// docs/20 §4.1（D6/D10）：QSystemTrayIcon / QMenu 住在 QtWidgets；应用基类
// 相应从 QGuiApplication 换成 QApplication（Qt Quick + QApplication 是官方
// 支持的组合，事件分发/输入管线不受影响）。托盘是唯一需要 widgets 的点。

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

#include <QDir>
#include <QLockFile>
#ifdef Q_OS_WIN
#include <QTimer>
#include <QQuickWindow>
#include <QWindow>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <QCloseEvent>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

// docs/20 §3.4（A4 拖拽附件）：拖放由 main.qml 的窗口根级 DropArea 处理
// （QML DropArea 是 Qt 向平台注册拖拽目标、使光标变“允许”的途径），
// 路径解析在 AgentViewModel::handleDroppedFiles 完成（QML 的 url 对象没有
// toLocalFile()，转换只能在 C++ 做），经 attachmentsDropped 推给输入区。

#ifdef Q_OS_WIN
// A4 拖拽附件的第二个坑：**拖拽是否可用取决于进程的启动方式**。
//
// 资源管理器（Explorer.exe）以中等完整性（Medium Integrity Level）运行，
// 而从 VSCode 的 CMake 终端、或以管理员身份（Run as Administrator）启动的
// 进程完整性更高（VSCode 终端会话继承了提升的令牌）。Windows 的 UIPI
// （User Interface Privilege Isolation）会静默丢弃从低完整性进程发往高
// 完整性进程的窗口消息，OLE 拖放握手正是靠这些消息完成的——于是
// RegisterDragDrop 明明成功了，DragEnter 却一次都到不了 Qt，光标一直是
// 🚫，_dropEvent/DropArea 永远不触发。直接双击 exe（与 Explorer 同完整性）
// 则一切正常。微软的官方说明见
// https://learn.microsoft.com/archive/blogs/patricka/q-why-doesnt-drag-and-drop-work-when-my-application-is-running-elevated-a-mandatory-integrity-control-and-uipi
//
// 修法：在窗口创建后，用 ChangeWindowMessageFilterEx 把 OLE 拖放依赖的
// 消息显式放行（MSGFLT_ALLOW），让低完整性来源也能发进来。Qt 的 OLE 拖放
// 走 IDropTarget（RegisterDragDrop），跨完整性握手需要这三条消息：
//   WM_DROPFILES (0x0233)、WM_COPYDATA (0x004A)、WM_COPYGLOBALDATA (0x0049)。
//
// 注意：必须在原生窗口创建之后才能拿到 HWND（winId() 会强制创建），而
// QML 窗口在 engine.load 后才存在，且首次 winId() 偶发拿到中间值——所以
// 用单次 QTimer 延后到事件循环里再挂。
void allowDragDropFromLowerIntegrity(QQuickWindow* window) {
    if (!window)
        return;
    // 单次触发：确保原生 HWND 已就绪（QML 窗口在 show 后才创建平台窗口）。
    QTimer::singleShot(0, window, [window]() {
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (!hwnd)
            return;
        // MSGFLT_ALLOW = 1：允许任意完整性级别（含 Medium 的 Explorer）发送。
        constexpr DWORD kMsgFltAllow = 1;
        ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, kMsgFltAllow, nullptr);
        ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, kMsgFltAllow, nullptr);
        // WM_COPYGLOBALDATA：OLE 拖放握手的核心消息，未放行则一直 🚫。
        ChangeWindowMessageFilterEx(hwnd, 0x0049, kMsgFltAllow, nullptr);
        qInfo() << "Drag & drop message filter relaxed for lower-integrity"
                << "sources (WM_DROPFILES/WM_COPYDATA/0x0049)";
    });
}
#endif


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

// docs/24 R2: knowledge-base restore — extracted verbatim from the startup
// wiring so the database-restore flow (AgentViewModel::databaseRestored)
// can re-run it against the swapped file. Snapshots the persisted documents
// on the GUI thread (single-threaded SQLite), then hands off to the worker
// pool; each step binds to the generation it started with, so a
// clear/remove/bump mid-restore supersedes the rest.
void startKnowledgeRestore(SqliteMemory* memory, RagService* rag) {
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
    if (restoreItems.isEmpty()) return;
    qInfo() << "Restoring" << restoreItems.size() << "knowledge document(s)";
    const QString currentModel = rag->embeddingModelName();
    const quint64 restoreGeneration = rag->restoreGeneration();
    (void)QtConcurrent::run([rag, restoreItems, currentModel,
                             restoreGeneration]() {
        int fromChunks = 0, reEmbedded = 0;
        for (const RestoreItem& item : restoreItems) {
            if (persistedChunksUsable(item.contentHash, item.embeddingModel,
                                      item.content, currentModel)
                    && rag->restorePersistedDocument(
                           item.name, item.chunks, restoreGeneration)) {
                ++fromChunks;
                continue;
            }
            ++reEmbedded;
            rag->ingestRestoredDocument(item.name, item.content,
                                        restoreGeneration);
        }
        qInfo() << "Knowledge restore:" << fromChunks
                << "from persisted chunks," << reEmbedded << "re-embedded";
    });
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("NexusAI");
    app.setApplicationName("NexusAI");
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    // docs/20 §4.2（D7 单实例锁）：第二个实例直接静默退出。
    //
    // 双写风险是真实的：SQLite 打开时若已有实例在写会拿到
    // "database table is locked"（每次启动日志里的 episodic_fts 告警就是
    // 两个实例同时建 FTS 表的痕迹），重复启动还会双写 QSettings、开两个
    // 托盘。锁文件放在用户临时目录，崩溃残留也会被 QLockFile 的陈旧锁
    // 检测（进程存在性）自动清掉。作用域必须在 main 内——QLockFile 在
    // 析构时释放锁，做成局部变量可保证退出即释放。
    QLockFile instanceLock(QDir::temp().absoluteFilePath(
        QStringLiteral("NexusAI.instance.lock")));
    if (!instanceLock.tryLock(100)) {
        // 已有实例在跑：静默退出（二期可通过 QLocalServer 唤起已有实例）。
        qInfo() << "Another NexusAI instance is already running; exiting.";
        return 0;
    }

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

    // Categories and preference metadata are loaded from SettingsSchema by
    // PreferenceRegistry; runtime services only supply environment-dependent
    // defaults such as the system's available font families.

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
    // docs/20 §5.3 (C3): "searxng" reads its required baseUrl from settings.
    const auto createWebSearchBackend = [settings](const QString& providerKey)
        -> std::unique_ptr<WebSearchBackend> {
        if (providerKey == QLatin1String("tavily"))
            return std::make_unique<TavilyBackend>();
        if (providerKey == QLatin1String("duckduckgo"))
            return std::make_unique<DuckDuckGoBackend>();
        if (providerKey == QLatin1String("searxng"))
            return std::make_unique<SearxBackend>(settings->webSearchSearxBaseUrl());
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
        // Network tools are global policy; knowledge access is evaluated
        // against each agent's own model snapshot.
        const bool gate = settings->localOnlyMode();
        QStringList gated{QStringLiteral("web_search")};
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
    auto tabProviderSets = std::make_shared<QHash<AIAgent*, ProviderSet>>();
    auto tabMemories = std::make_shared<QHash<AIAgent*, EpisodicService*>>();
    auto summaryProviders = std::make_shared<QHash<AIAgent*, ProviderSet>>();
    tabMemories->insert(agent, episodicService);
    summaryProviders->insert(agent, LlmProtocolRegistry::createServiceSet(episodicService));
    agentVM->setModelConfigurator([settings, agent, templateSet, tabProviderSets, tabMemories, summaryProviders](
            AIAgent* target, const QVariantMap& profile) {
        const ProviderSet set = target == agent ? templateSet : tabProviderSets->value(target);
        IAIService* service = set.value(profile.value("apiProtocol").toString());
        if (profile.value("model").toString().trimmed().isEmpty()
                || profile.value("baseUrl").toString().trimmed().isEmpty())
            service = nullptr;
        if (service) service->configure(profileConfigOf(profile));
        target->setLLMService(service);
        // Long-term summaries use the same chosen model through a separate
        // service, so neither foreground streams nor other tabs are touched.
        if (auto* summaries = tabMemories->value(target)) {
            auto* summaryService = service
                ? summaryProviders->value(target).value(profile.value("apiProtocol").toString()) : nullptr;
            if (summaryService) summaryService->configure(profileConfigOf(profile));
            summaries->setLlmService(summaryService);
        }
        target->setContextLimit(settings->resolveContextLimitForProfile(profile));
        target->setModelLocal(LlmProtocolRegistry::isLocal(
            profile.value("apiProtocol").toString(), profile.value("provider").toString()));
    });
    agentVM->setAgentFactory([&app, settings,
                              tabProviderSets, tabMemories, summaryProviders]() -> AIAgent* {
        auto* tabAgent = new AIAgent(&app);
        tabAgent->setToolRegistry(&ToolRegistry::instance());
        // docs/22 F2: the tab agent owns its per-tab SQLite connection and
        // releases it (close + removeDatabase) in its destructor — closing
        // a tab no longer leaks the connection until process exit.
        tabAgent->setOwnedMemory(std::make_unique<SqliteMemory>());
        tabAgent->setMaxIterations(settings->maxSteps());
        tabAgent->setTaskTimeoutMs(settings->taskTimeoutMinutes() * 60 * 1000);
        tabAgent->setSystemPromptOverride(settings->systemPromptOverride());
        tabAgent->setLocalOnlyMode(settings->localOnlyMode());
        auto* summaries = new EpisodicService(tabAgent);
        summaries->setMemory(tabAgent->memory());
        summaries->setEnabled(settings->episodicMemoryEnabled());
        tabAgent->setEpisodicService(summaries);
        tabMemories->insert(tabAgent, summaries);
        summaryProviders->insert(tabAgent, LlmProtocolRegistry::createServiceSet(summaries));
        tabProviderSets->insert(tabAgent, LlmProtocolRegistry::createServiceSet(tabAgent));
        QObject::connect(tabAgent, &QObject::destroyed, &app,
            [tabProviderSets, tabMemories, summaryProviders, tabAgent]() {
                tabProviderSets->remove(tabAgent);
                tabMemories->remove(tabAgent);
                summaryProviders->remove(tabAgent);
            });
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
    // docs/23 G3: 定期收敛 usage_log / audit_log / token_usage。托盘常驻
    // 进程可能数天不重启，仅靠启动清理会让保留策略形同虚设。超时回调在
    // GUI 线程触发（QTimer 归属 app），SQLite 单线程纪律保持成立。
    const auto runPrunes = [usageMemory, settings]() {
        const int usage = usageMemory->pruneUsage();
        const int audit = usageMemory->pruneAudit(settings->auditRetentionDays());
        const int tokens = usageMemory->pruneTokenUsage(settings->tokenRetentionDays());
        // docs/23 G7: 软删除会话的撤销窗口 30 天，到期硬回收四表。
        const int purged = usageMemory->purgeDeletedSessions(
            QDateTime::currentDateTime().toMSecsSinceEpoch()
                - 30LL * 86400 * 1000);
        if (usage + audit + tokens + purged > 0)
            qInfo() << "Pruned rows: usage" << usage << "audit" << audit
                    << "token_usage" << tokens << "deleted sessions" << purged;
    };
    runPrunes();

    auto* pruneTimer = new QTimer(&app);
    pruneTimer->setInterval(6 * 60 * 60 * 1000);
    QObject::connect(pruneTimer, &QTimer::timeout, &app, runPrunes);
    pruneTimer->start();

    // docs/23 D1: startup auto-backup — a VACUUM INTO consistency snapshot
    // (WAL-safe) plus rotation of the auto_* series. The worker is fully
    // self-contained (own short-lived SQLite connection, value captures
    // only): it never touches GUI-thread objects, so an exit mid-backup
    // cannot dangle (docs/22 lesson). Failure is logged, never fatal.
    if (settings->backupOnStartup()) {
        const QString dbPath = memory->databasePath();
        const QString backupDir = SqliteMemory::defaultBackupsDir();
        const int keep = settings->backupKeepCount();
        QtConcurrent::run([dbPath, backupDir, keep]() {
            QDir().mkpath(backupDir);
            const QString target = backupDir
                + QStringLiteral("/auto_%1.db")
                      .arg(QDateTime::currentDateTime().toString(
                          QStringLiteral("yyyyMMdd_HHmmss")));
            if (SqliteMemory::createBackupFile(dbPath, target))
                SqliteMemory::rotateAutoBackups(backupDir, keep);
        });
    }

    // Restore previously imported documents in the background (Batch 6).
    // Snapshot records AND chunks on the GUI thread (the SQLite connection
    // is single-threaded); the worker then picks per document:
    //   - chunks usable (content hash unchanged + same embedding model):
    //     rebuilt from stored vectors with ZERO embedding requests;
    //   - otherwise: full re-ingest, writing the new chunk layout back via
    //     documentPersisted (self-healing for upgraded/stale databases).
    // docs/24 R2: block extracted into startKnowledgeRestore() so the
    // database-restore flow can re-run it against the swapped file.
    startKnowledgeRestore(memory, rag);

    // docs/24 R4: after a successful database restore, reload the knowledge
    // base from the swapped file (direct connection — same GUI-thread stack
    // as the restore orchestration).
    QObject::connect(agentVM, &AgentViewModel::databaseRestored, &app,
                     [memory, rag]() { startKnowledgeRestore(memory, rag); });

    // Summaries have a separate provider: changing defaults must never
    // reconfigure a service used by a running conversation.
    const auto applySettings = [settings, agentVM, agent, fetchPtr, webPtr,
                                tabMemories, createWebSearchBackend]() {
        agentVM->updateModelProfiles(settings->profiles(), settings->activeProfile());
        const auto applyRuntime = [settings](AIAgent* target) {
            target->setMaxIterations(settings->maxSteps());
            target->setTaskTimeoutMs(settings->taskTimeoutMinutes() * 60 * 1000);
            target->setLocalOnlyMode(settings->localOnlyMode());
        };
        applyRuntime(agent);
        agentVM->applyToAgents(applyRuntime);

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
        for (auto* summaries : std::as_const(*tabMemories))
            summaries->setEnabled(settings->episodicMemoryEnabled());
    };
    applySettings();

    // docs/17 A1: 黑名单变更即时生效。
    QObject::connect(settings, &SettingsService::readDenyListChanged, &app, [applySettings]() {
        applySettings();
    });

    QObject::connect(settings, &SettingsService::profilesChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::localOnlyModeChanged, &app, [applySettings]() { applySettings(); });
    // Defaults and idle tabs refresh; active tasks keep their model snapshots.
    QObject::connect(settings, &SettingsService::activeProfileChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::contextTokensChanged, &app, [applySettings]() { applySettings(); });
    // 步数预算改动即时生效（含已在跑的标签）。
    QObject::connect(settings, &SettingsService::maxStepsChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::taskTimeoutMinutesChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowFullDiskReadChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowProjectWriteChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowLocalNetworkFetchChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::episodicMemoryEnabledChanged, &app, [applySettings]() { applySettings(); });
    // L4: provider switch rebuilds the web search backend immediately.
    QObject::connect(settings, &SettingsService::webSearchProviderChanged, &app, [applySettings]() { applySettings(); });
    // docs/20 §5.3 (C3): baseUrl changes rebuild the SearXNG backend too.
    QObject::connect(settings, &SettingsService::webSearchSearxBaseUrlChanged, &app, [applySettings]() { applySettings(); });

    // Batch 22 (F8): user-editable system prompt (empty = built-in)
    const auto applySystemPrompt = [agent, agentVM, settings]() {
        agent->setSystemPromptOverride(settings->systemPromptOverride());
        agentVM->applyToAgents([settings](AIAgent* target) {
            target->setSystemPromptOverride(settings->systemPromptOverride());
        });
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

    // The application always uses the configured model pipeline; no demo
    // simulation mode is exposed or wired into task execution.
    agentVM->setAgent(agent);

    // ── docs/20 工作流 B（桌面集成）：偏好键 → 真实系统行为的桥接 ──
    // PreferenceControl 直接写 prefs/v1/<key>（经 ChangeBus 通知），真值在
    // 系统侧的键在这里桥接：
    //   - general.autoStart → HKCU Run 键（SettingsService::setAutoStart）；
    //   - window.closeToTray → main.qml 的 onClosing 直接读偏好键，无需桥接。
    QObject::connect(changeBus, &ChangeBus::changed, &app,
        [settings](const QString& key, const QVariant& value) {
            if (key != QLatin1String("general.autoStart")) return;
            settings->setAutoStart(value.toBool());
        });
    // 已保存的偏好是期望状态，Windows Run 项是实际状态。若偏好键尚不存在，
    // 先从旧版/外部已有的 Run 项迁入，避免升级时误删用户原有的自启动配置；
    // 否则按明确保存的偏好双向协调。非 Windows 上后端不受支持。
#ifdef Q_OS_WIN
    {
        const QString autoStartKey = QStringLiteral("general.autoStart");
        const bool hasPreference = prefStore->hasStoredValue(autoStartKey);
        const bool preferred = prefStore->load(autoStartKey).toBool();
        const bool registered = settings->autoStart();
        if (!hasPreference && registered)
            prefStore->save(autoStartKey, true);
        else if (hasPreference && preferred != registered)
            settings->setAutoStart(preferred);
    }
#endif

    // ── QML Engine ─────────────────────────────────────
    // Theme/ThemeManager/AgentVM/Settings/ConnectionTester resolve to the
    // NexusAI.ViewModels QML singletons above (create() returns the very
    // instances wired up here).
    QQmlApplicationEngine engine;

    // docs/20 §4.1（D10 完善）：把托盘可用性注入根对象。close-to-tray 必须
    // 先看它——托盘不可用时藏窗口等于让应用失踪（用户只能去任务管理器结束）。
    // 静态查询，创建托盘图标之前即可求值。
    engine.setInitialProperties({
        {QStringLiteral("trayAvailable"), QVariant(QSystemTrayIcon::isSystemTrayAvailable())},
    });

    // Load main QML
    const QUrl url(QStringLiteral("qrc:/qt/qml/NexusAI/src/ui/main.qml"));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!mainWindow)
        return -1;

#ifdef Q_OS_WIN
    // A4 拖拽附件：从 VSCode 终端 / 管理员启动时，UIPI 会静默丢弃来自
    // 中等完整性 Explorer 的 OLE 拖放握手消息，导致光标一直 🚫。这里把
    // 三条握手消息放行，让拖拽在任意启动方式下都可用（见上方说明）。
    allowDragDropFromLowerIntegrity(mainWindow);
#endif

    // ── docs/20 §4.1（D6/D10）：系统托盘 + 后台任务完成通知 ──
    // 图标：assets/icons/nexus.ai.ico 嵌进 exe（任务栏/资源管理器/Alt-Tab），
    // 同一素材的 PNG 走 Qt 资源 :/icons/（窗口 + 托盘），小尺寸清晰。
    // QIcon 允许失败（资源路径写错时静默变空图标 = 托盘看起来"没反应"），
    // 这里显式断言并在缺失时退回 exe 内嵌图标，避免再次静默退化。
    QIcon appIcon(QStringLiteral(":/icons/nexus-64.png"));
    if (appIcon.isNull()) {
        qWarning() << "tray: :/icons/nexus-64.png missing from the resource bundle;"
                      " falling back to the exe icon";
        appIcon = QIcon(QStringLiteral(":/icons/nexus-256.png"));
    }
    if (appIcon.isNull())
        appIcon = QApplication::windowIcon();
    mainWindow->setIcon(appIcon);

    auto* tray = new QSystemTrayIcon(appIcon, &app);
    tray->setToolTip(QStringLiteral("NexusAI v%1").arg(APP_VERSION));
    auto* trayMenu = new QMenu();
    QAction* showAction = trayMenu->addAction(QObject::tr("显示/隐藏主窗"));
    QAction* newChatAction = trayMenu->addAction(QObject::tr("新建会话"));
    trayMenu->addSeparator();
    QAction* quitAction = trayMenu->addAction(QObject::tr("退出"));
    tray->setContextMenu(trayMenu);
    tray->show();

    // 显示/隐藏主窗（切走窗口后靠托盘召回）。
    QObject::connect(showAction, &QAction::triggered, &app, [mainWindow]() {
        if (mainWindow->isVisible() && mainWindow->isActive())
            mainWindow->hide();
        else {
            mainWindow->show();
            mainWindow->raise();
            mainWindow->requestActivate();
        }
    });
    // 新建会话：唤起主窗并触发 VM 的 newChat（沿用现有封口语义）。
    QObject::connect(newChatAction, &QAction::triggered, &app, [mainWindow, agentVM]() {
        mainWindow->show();
        mainWindow->raise();
        mainWindow->requestActivate();
        agentVM->newChat();
    });
    // 退出：先停全部 busy tab（沿用"关 tab 停任务"封口语义），再退出。
    QObject::connect(quitAction, &QAction::triggered, &app, [&app, agentVM, tray, trayMenu]() {
        tray->hide();
        delete trayMenu;
        agentVM->stop();
        QCoreApplication::quit();
    });
    // 双击托盘 = 显示/隐藏主窗（系统托盘的惯用交互）。
    QObject::connect(tray, &QSystemTrayIcon::activated, &app,
        [mainWindow](QSystemTrayIcon::ActivationReason reason) {
            if (reason != QSystemTrayIcon::DoubleClick) return;
            if (mainWindow->isVisible() && mainWindow->isActive())
                mainWindow->hide();
            else {
                mainWindow->show();
                mainWindow->raise();
                mainWindow->requestActivate();
            }
        });

    // D10：任务从 Busy→Idle 时弹气泡通知（标题带会话名）。
    // 仅当窗口**可见且激活**（用户正在看）时才静默——最小化（minimized 时
    // isVisible() 仍为 true）与藏进托盘都要通知，否则切走窗口就收不到。
    // 「系统通知」开关（prefs/v1/notice.enabled，默认开）关闭后一律不打扰。
    QObject::connect(agentVM, &AgentViewModel::taskFinished, &app,
        [mainWindow, tray, prefStore](const QString& sessionTitle, bool success) {
            if (!prefStore->load(QStringLiteral("notice.enabled")).toBool()) return;
            if (mainWindow->isVisible() && mainWindow->isActive()) return;
            const bool closeToTray = prefStore->load(
                QStringLiteral("window.closeToTray")).toBool();
            if (!closeToTray && !QSystemTrayIcon::isSystemTrayAvailable())
                return;
            tray->showMessage(
                success ? QObject::tr("任务完成") : QObject::tr("任务结束"),
                sessionTitle.isEmpty() ? QObject::tr("会话已回复")
                                       : sessionTitle,
                success ? QSystemTrayIcon::Information
                        : QSystemTrayIcon::Warning, 6000);
        });

    // 关闭行为：prefs/v1/window.closeToTray（默认 false = 直接退出）。
    // QQuickCloseEvent 没有公共头文件（qquickwindow.h 只前置声明），C++ 侧
    // 无法可靠拦截 → 在 main.qml 的 onClosing 里按同一偏好键分支，托盘菜单
    // 的"退出"动作是唯一真正退出路径（先停全部 busy tab）。

    // Language switches install/remove translators (I18nManager), but the
    // QML engine does not reliably re-evaluate every qsTr binding on the
    // LanguageChange event alone (sidebar buttons, settings dialog buttons
    // and the context-panel notice were observed lagging). Force a full
    // re-evaluation of translation bindings after each switch.
    QObject::connect(i18n, &I18nManager::localeChanged, &engine,
                     &QQmlApplicationEngine::retranslate);

    return app.exec();
}
