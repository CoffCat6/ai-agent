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
// docs/32 P1: 子代理（spawn_agent）工具。
#include "core/tools/SubAgentTool.h"
#include "core/tools/ApplyProposalTool.h"
#include "core/tools/SubAgentControlTool.h"
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "core/utils/Logger.h"
#include "core/utils/Activity.h"
#include "core/utils/BackupSchedule.h"
#include "core/utils/NetworkProxyConfig.h"
#include "core/utils/SamplingParams.h"
#include "infrastructure/llm/IAIService.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"
#include "infrastructure/embed/OllamaEmbedding.h"
#include "infrastructure/embed/OpenAICompatibleEmbedding.h"
#include "infrastructure/embed/EmbedShutdown.h"

#include <memory>
#include <atomic>
#include <QAtomicInt>

#include <QDir>
#include <QLockFile>
#include <QTimer>
#ifdef Q_OS_WIN
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

// Local backends (Ollama at localhost) must bypass any proxy: routing
// loopback traffic through e.g. a local clash instance breaks the connection
// ("connection refused" after a seconds-long detour). The factory itself owns
// that rule as well as the user's system/manual/off choice (H1).

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
        QString sourcePath;
        QVector<KnowledgeChunk> chunks;
    };
    QVector<RestoreItem> restoreItems;
    for (const auto& rec : memory->loadKnowledgeDocRecords()) {
        RestoreItem item;
        item.name = rec.name;
        item.content = rec.content;
        item.contentHash = rec.contentHash;
        item.embeddingModel = rec.embeddingModel;
        item.sourcePath = rec.sourcePath;
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
            // 任务 #22：进程退出（aboutToQuit 置位标志）后不再继续恢复/
            // 重嵌入——继续算只会白写一堆 pseudo 降级向量，还会拖住静态
            // 析构阶段的线程池 waitForDone。
            if (embeddingShutdownRequested()) break;
            if (persistedChunksUsable(item.contentHash, item.embeddingModel,
                                      item.content, currentModel)
                    && rag->restorePersistedDocument(
                           item.name, item.chunks, item.sourcePath,
                           restoreGeneration)) {
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

    auto* proxyFactory = new ConfigurableProxyFactory;
    QNetworkProxyFactory::setApplicationProxyFactory(proxyFactory);

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

    // Knowledge base: embeddings with a provider switch — local Ollama or
    // an OpenAI-compatible external service (task #16); the retriever's
    // local pseudo-embedding stays as the offline fallback.
    // docs/24 H6 (docs/25 A2): `ollamaEmbedding`, `openaiEmbedding` and
    // `rag` below are deliberately process-lifetime and NEVER deleted —
    // three discarded-future QtConcurrent workers (startup restore,
    // re-embed-all, AgentViewModel import) hold these raw pointers through
    // rag and may still be running when the app quits; parenting any of
    // them to the app would destroy the object under them (use-after-free
    // at exit). Provider switching therefore never swaps instances either:
    // it only flips the atomic switch below, so the embed closures handed
    // to RagService/EpisodicService stay valid for the whole process.
    auto* ollamaEmbedding = new OllamaEmbedding(nullptr);
    auto* openaiEmbedding = new OpenAICompatibleEmbedding(nullptr);
    // 任务 #22（退出卡住修复 A）：aboutToQuit 置位进程级嵌入中断标志。
    // 上面的 H6 纪律决定了三个后台 worker（restore / re-embed / 导入）
    // 无人取消——future 被丢弃，进程退出时没有任何句柄能打断它们；而
    // 嵌入调用在 QEventLoop 里同步等待最长 300s 的传输超时，main() 返
    // 回后的静态析构阶段线程池 waitForDone() 会无限等下去（窗口消失、
    // 进程挂后台）。标志置位后，两个后端的轮询定时器在 ~200ms 内
    // abort 在途请求、跳过后续批与重试，下面两个后台 for 循环也尽快
    // 收手，进程得以干净退出。
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        requestEmbeddingShutdown();
    });
    // Active-backend switch: written on the GUI thread (applyEmbeddingConfig
    // below), read inside the embed closures on worker threads. Acquire/
    // release makes the flip visible to queued workers; a stale read at
    // worst routes one call through the other backend — which is fully
    // configured as well, never a dangling pointer.
    const auto openAiActive = std::make_shared<QAtomicInt>(0);
    const auto embedText = [ollamaEmbedding, openaiEmbedding, openAiActive](
            const QString& text) {
        return openAiActive->loadAcquire() ? openaiEmbedding->embed(text)
                                           : ollamaEmbedding->embed(text);
    };
    const auto embedTexts = [ollamaEmbedding, openaiEmbedding, openAiActive](
            const QStringList& texts) {
        return openAiActive->loadAcquire() ? openaiEmbedding->embedBatch(texts)
                                           : ollamaEmbedding->embedBatch(texts);
    };
    auto* rag = new RagService(nullptr);
    rag->setEmbedder(embedText);
    rag->setBatchEmbedder(embedTexts);
    // Embedding backend/config application, shared by this startup call and
    // the hot-reload connects further below (provider / model / baseUrl /
    // apiKey / localOnlyMode) — the same re-apply chain web search uses.
    const auto applyEmbeddingConfig = [settings, ollamaEmbedding, openaiEmbedding,
                                       openAiActive, rag, memory]() {
        // Both backends stay fully configured: the inactive one is only a
        // stale atomic read away, and each setter snapshots under its own
        // mutex (docs/22 F3) — mid-flight calls never see torn state.
        ollamaEmbedding->setModel(settings->embeddingModel());
        ollamaEmbedding->setBaseUrl(settings->embeddingBaseUrl());
        openaiEmbedding->setModel(settings->embeddingModel());
        openaiEmbedding->setBaseUrl(settings->embeddingBaseUrl());
        // 任务 #20 修 B1：embeddingApiKey() 是掩码 getter（恒空串），
        // 注入后端必须取真实值出口 embeddingApiKeyValue()——否则
        // 用户配了 key 也发不出 Authorization 头（401）。
        openaiEmbedding->setApiKey(settings->embeddingApiKeyValue());

        // Provider selection, web-search-factory style (createWebSearchBackend
        // above). The gating decision itself is extracted into the pure
        // function SettingsService::embeddingUsesExternalProvider (unit-
        // tested in test_settings, task #20/Mj1): provider == "openai" and
        // localOnlyMode off → external backend; anything else (including
        // unknown keys) → local Ollama. localOnlyMode keeps document
        // content on this machine — blocked requests fall back to local.
        const bool usesExternal = SettingsService::embeddingUsesExternalProvider(
            settings->embeddingProvider(), settings->localOnlyMode());
        if (!usesExternal
                && settings->embeddingProvider() == QLatin1String("openai"))
            qWarning() << "localOnlyMode is on - external embedding requests"
                       << "are blocked, falling back to local Ollama";
        openAiActive->storeRelease(usesExternal ? 1 : 0);

        // Identity switch (model rename OR provider flip — the openai
        // backend's model() carries an "openai:" prefix so the two backends'
        // vector spaces never mix): old chunks are invalid, re-embed in the
        // background (written back via documentPersisted). The startup call
        // only seeds the identity — stale chunks in the database are
        // handled by startKnowledgeRestore's persistedChunksUsable check.
        const QString identity = openAiActive->loadAcquire()
            ? openaiEmbedding->model() : ollamaEmbedding->model();
        const QString previous = rag->embeddingModelName();
        if (previous == identity) return;
        rag->setEmbeddingModelName(identity);
        if (previous.isEmpty()) return;   // first seeding at startup

        struct ReembedItem { QString name; QString content; };
        QVector<ReembedItem> items;
        for (const auto& rec : memory->loadKnowledgeDocRecords())
            items.append({rec.name, rec.content});
        if (items.isEmpty()) return;

        qInfo() << "Embedding identity changed - re-embedding"
                << items.size() << "document(s)";
        const quint64 gen = rag->restoreGeneration();
        (void)QtConcurrent::run([rag, items, gen]() {
            for (const ReembedItem& item : items) {
                // 任务 #22：退出中断（同 startKnowledgeRestore）——置位
                // 后剩余文档不再重嵌入，worker 尽快返回让进程退出。
                if (embeddingShutdownRequested()) break;
                rag->ingestRestoredDocument(item.name, item.content, gen);
            }
        });
    };
    // Apply the configured embedding backend + identity BEFORE the restore
    // snapshot: the persisted-chunk usability check compares against this
    // identity.
    applyEmbeddingConfig();
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

    // L4: web search backend factory. The provider table lives in
    // WebSearchProviderRegistry (shared with the settings schema options and
    // the QML dropdown), so adding a provider never touches this file.
    // Unknown keys fall back to the registry default with a warning (the
    // setting is also clamped in SettingsService; this is defense-in-depth
    // for corrupted configs).
    const auto createWebSearchBackend = [](const QString& providerKey)
        -> std::unique_ptr<WebSearchBackend> {
        if (auto backend = WebSearchProviderRegistry::create(providerKey))
            return backend;
        const QString fallback = WebSearchProviderRegistry::defaultProviderKey();
        qWarning() << "Unknown web search provider:" << providerKey
                   << "- falling back to" << fallback;
        return WebSearchProviderRegistry::create(fallback);
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
    episodicService->setEmbedder(embedText);
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
    agentVM->setAgentFactory([&app, settings, embedText,
                              tabProviderSets, tabMemories, summaryProviders]() -> AIAgent* {
        auto* tabAgent = new AIAgent(&app);
        tabAgent->setToolRegistry(&ToolRegistry::instance());
        // docs/22 F2: the tab agent owns its per-tab SQLite connection and
        // releases it (close + removeDatabase) in its destructor — closing
        // a tab no longer leaks the connection until process exit.
        tabAgent->setOwnedMemory(std::make_unique<SqliteMemory>());
        tabAgent->setSystemPromptOverride(settings->systemPromptOverride());
        tabAgent->setLocalOnlyMode(settings->localOnlyMode());
        auto* summaries = new EpisodicService(tabAgent);
        summaries->setMemory(tabAgent->memory());
        summaries->setEnabled(settings->episodicMemoryEnabled());
        summaries->setEmbedder(embedText);
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
                                const QString& sourcePath,
                                const QVector<KnowledgeChunk>& chunks) {
            memory->upsertKnowledgeDoc(name, content,
                                       knowledgeContentHash(content),
                                       ragForPersist->embeddingModelName(),
                                       sourcePath);
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
    // docs/23 G7（docs/25 E10）：软删除会话的撤销窗口——30 天内可恢复，
    // 到期由 purgeDeletedSessions 硬回收四表。具名常量替代裸魔数。
    static constexpr qint64 kSoftDeleteUndoWindowMs = 30LL * 24 * 60 * 60 * 1000;
    const auto runPrunes = [usageMemory, settings]() {
        const int usage = usageMemory->pruneUsage();
        const int audit = usageMemory->pruneAudit(settings->auditRetentionDays());
        const int tokens = usageMemory->pruneTokenUsage(settings->tokenRetentionDays());
        // G4: file checkpoint retention (30 days, independent of session
        // purge; soft-deleted sessions keep theirs until the hard delete).
        const int checkpoints = usageMemory->pruneFileCheckpoints(
            QDateTime::currentDateTime().addDays(-30).toMSecsSinceEpoch());
        const int purged = usageMemory->purgeDeletedSessions(
            QDateTime::currentDateTime().toMSecsSinceEpoch()
                - kSoftDeleteUndoWindowMs);
        if (usage + audit + tokens + checkpoints + purged > 0)
            qInfo() << "Pruned rows: usage" << usage << "audit" << audit
                    << "token_usage" << tokens << "file_checkpoints" << checkpoints
                    << "deleted sessions" << purged;
    };
    runPrunes();

    auto* pruneTimer = new QTimer(&app);
    pruneTimer->setInterval(6 * 60 * 60 * 1000);
    QObject::connect(pruneTimer, &QTimer::timeout, &app, runPrunes);
    pruneTimer->start();

    // docs/23 D1 的启动自动备份在托盘创建之后与定时备份一起装配（docs/31
    // S4.2 N6）：失败提示需要托盘对象，见下方「数据库快照的自动备份」块。

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
    // ── docs/32 P1: 子代理（spawn_agent）──
    // 注册即门控：agent/subagentEnabled 决定工具是否暴露给模型。profile 快照
    // 由 applySettings 在 GUI 线程写入，池线程经 atomic shared_ptr 读取；
    // 服务工厂在池线程上创建独立 IAIService——并发单位是服务实例
    // （LLMServiceBase 每实例仅一个在途请求），与 per-tab 先例同源。
    struct SubAgentProfileSnapshot {
        bool valid{false};
        QString protocolKey;
        LlmProfileConfig config;
        // docs/35 P4.1: 子代理与主代理同用当前 profile 的上下文窗口，Runner
        // 据此压缩超限历史（0 = 未知，不压缩）。
        int contextLimit{0};
    };
    // docs/35 P4.6: 角色可以点名任意已配置的 profile（coder 用强模型、
    // explore 用快模型）。快照集合按 id 索引；未知 id（profile 已删除）
    // 回退到活动 profile。
    struct SubAgentProfileSet {
        std::shared_ptr<const SubAgentProfileSnapshot> active;
        QHash<QString, std::shared_ptr<const SubAgentProfileSnapshot>> byId;

        std::shared_ptr<const SubAgentProfileSnapshot> forId(
            const QString& profileId) const {
            if (!profileId.isEmpty()) {
                const auto it = byId.constFind(profileId);
                if (it != byId.cend()) return it.value();
            }
            return active;
        }
    };
    auto subAgentProfiles = std::make_shared<
        std::atomic<std::shared_ptr<const SubAgentProfileSet>>>();
    auto subAgentTool = std::make_shared<SubAgentTool>();
    // docs/35 P4.4: 与 spawn_agent 同门控——只有子代理能产提案，关闭时
    // 不把 apply_proposal 暴露给模型。
    auto applyProposalTool = std::make_shared<ApplyProposalTool>();
    // docs/35 P4.3: 后台任务管理工具（wait/status/cancel）与 spawn_agent
    // 同门控，且不会进入子代理目录（SubAgentTool::refreshCatalog 排除）。
    auto subagentWaitTool = std::make_shared<SubAgentWaitTool>();
    auto subagentStatusTool = std::make_shared<SubAgentStatusTool>();
    auto subagentCancelTool = std::make_shared<SubAgentCancelTool>();
    subAgentTool->setServiceFactory(
        [subAgentProfiles](const QString& profileId,
                           const QString& effort) -> IAIService* {
            const std::shared_ptr<const SubAgentProfileSet> set =
                subAgentProfiles->load();
            if (!set) return nullptr;
            const std::shared_ptr<const SubAgentProfileSnapshot> snapshot =
                set->forId(profileId);
            if (!snapshot || !snapshot->valid) return nullptr;
            IAIService* service = LlmProtocolRegistry::createService(
                snapshot->protocolKey, nullptr);
            if (service) {
                LlmProfileConfig config = snapshot->config;
                if (!effort.trimmed().isEmpty())
                    config.reasoningEffort = effort.trimmed();
                service->configure(config);
            }
            return service;
        });
    subAgentTool->setContextLimitResolver(
        [subAgentProfiles](const QString& profileId) {
            const std::shared_ptr<const SubAgentProfileSet> set =
                subAgentProfiles->load();
            const std::shared_ptr<const SubAgentProfileSnapshot> snapshot =
                set ? set->forId(profileId) : nullptr;
            return snapshot ? snapshot->contextLimit : 0;
        });
    // 跨多次 applySettings 调用保持注册状态；连接会拷贝 lambda，故用共享
    // 持有而非值捕获。
    auto spawnRegistered = std::make_shared<bool>(false);

    const auto applySettings = [settings, agentVM, agent, fetchPtr, webPtr,
                                proxyFactory, subAgentTool, subAgentProfiles,
                                spawnRegistered, applyProposalTool,
                                subagentWaitTool, subagentStatusTool,
                                subagentCancelTool,
                                tabMemories, createWebSearchBackend]() {
        // H1: one snapshot drives the application-wide proxy factory, so every
        // QNetworkAccessManager (LLM/embedding/search/MCP/http_fetch) follows
        // the same mode without per-link wiring.
        proxyFactory->setConfig(settings->proxyConfig());
        agentVM->updateModelProfiles(settings->profiles(), settings->activeProfile());
        const auto applyRuntime = [settings](AIAgent* target) {
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
        // default-off) setting; immediate effect on the next fetch. The
        // knowledge-base URL import shares the same gate.
        fetchPtr->setAllowPrivateTargets(settings->allowLocalNetworkFetch());
        agentVM->setAllowPrivateTargets(settings->allowLocalNetworkFetch());

        // T6 (F3): inject the current web search API key (DPAPI-decrypted).
        // 任务 #20 修 Mj2：webSearchApiKey() 是掩码 getter（恒空串），注入
        // 后端必须取真实值出口 webSearchApiKeyValue()——此前用户在 UI 配了
        // key 也报"未配置"（3d45dd5 引入的断裂）。密钥按 provider 分桶，
        // 该 getter 返回的正是「当前选中 provider」那一把。
        // L4: 同时按 configured provider 重建后端。
        webPtr->setApiKey(settings->webSearchApiKeyValue());
        webPtr->setBackend(createWebSearchBackend(settings->webSearchProvider()));

        // T8 (F4): sync episodic memory toggle and LLM service.
        for (auto* summaries : std::as_const(*tabMemories))
            summaries->setEnabled(settings->episodicMemoryEnabled());

        // docs/32 P1: 子代理——注册门控 + 运行参数 + profile 快照刷新。
        // 只读工具快照必须在 GUI 线程抓取（ToolRegistry 无内部锁）。
        subAgentTool->setMaxParallel(settings->subagentMaxParallel());
        subAgentTool->setPresetBackground(settings->subagentPresetBackground());
        // 步数/超时不再是用户可配置限制（2026-11-01）：工具默认不限，仅留
        // Runner 内部安全上限。
        // docs/33 E2: 用户自定义角色档案（预设内建于工具本身）。
        subAgentTool->setUserRoles(
            SubAgentTool::rolesFromVariant(settings->subagentRoles()));
        subAgentTool->refreshCatalog(settings->localOnlyMode());
        // docs/35 P4.6: snapshot EVERY configured profile, not just the active
        // one — a role's profileId picks from this set. Unknown ids fall back
        // to the active profile in forId().
        const auto makeSnapshot = [settings](const QVariantMap& profile) {
            auto snapshot = std::make_shared<SubAgentProfileSnapshot>();
            snapshot->valid =
                !profile.value(QStringLiteral("model")).toString().trimmed().isEmpty()
                && !profile.value(QStringLiteral("baseUrl")).toString().trimmed().isEmpty();
            snapshot->protocolKey = LlmProtocolRegistry::resolve(
                profile.value(QStringLiteral("apiProtocol")).toString(),
                profile.value(QStringLiteral("provider")).toString())->key;
            snapshot->config = profileConfigOf(profile);
            snapshot->contextLimit =
                settings->resolveContextLimitForProfile(profile);
            return snapshot;
        };
        const QVariantMap spawnProfile = settings->activeProfile();
        auto profileSet = std::make_shared<SubAgentProfileSet>();
        for (const QVariant& rawProfile : settings->profiles()) {
            const QVariantMap profile = rawProfile.toMap();
            const QString profileId = profile.value(QStringLiteral("id")).toString();
            if (profileId.isEmpty()) continue;
            profileSet->byId.insert(profileId, makeSnapshot(profile));
        }
        profileSet->active = makeSnapshot(spawnProfile);
        const QString activeProfileId =
            spawnProfile.value(QStringLiteral("id")).toString();
        if (!activeProfileId.isEmpty())
            profileSet->byId.insert(activeProfileId, profileSet->active);
        subAgentTool->setContextLimit(profileSet->active->contextLimit);
        subAgentProfiles->store(std::move(profileSet));
        if (settings->subagentEnabled() && !*spawnRegistered) {
            ToolRegistry::instance().registerTool(subAgentTool);
            ToolRegistry::instance().registerTool(applyProposalTool);
            ToolRegistry::instance().registerTool(subagentWaitTool);
            ToolRegistry::instance().registerTool(subagentStatusTool);
            ToolRegistry::instance().registerTool(subagentCancelTool);
            *spawnRegistered = true;
        } else if (!settings->subagentEnabled() && *spawnRegistered) {
            // 摘除但保留对象：再次启用时同一实例重新注册。
            ToolRegistry::instance().takeTool(QLatin1String(SubAgentTool::kName));
            ToolRegistry::instance().takeTool(QLatin1String(ApplyProposalTool::kName));
            ToolRegistry::instance().takeTool(QLatin1String(SubAgentWaitTool::kName));
            ToolRegistry::instance().takeTool(QLatin1String(SubAgentStatusTool::kName));
            ToolRegistry::instance().takeTool(QLatin1String(SubAgentCancelTool::kName));
            *spawnRegistered = false;
        }
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
    // docs/32 P1: 子代理设置即时生效（开关翻转即注册/注销工具）。
    QObject::connect(settings, &SettingsService::subagentEnabledChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::subagentMaxParallelChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::subagentPresetBackgroundChanged, &app, [applySettings]() { applySettings(); });
    // docs/33 E2: 角色档案变更即时生效（spawn_agent schema 的 role 枚举随快照重建）。
    QObject::connect(settings, &SettingsService::subagentRolesChanged, &app, [applySettings]() { applySettings(); });
    // 工具可用性变化（MCP 上下线、用户禁用/放行）→ 刷新子代理只读快照。
    QObject::connect(&ToolRegistry::instance(), &ToolRegistry::toolsChanged, &app,
                     [subAgentTool, settings]() {
                         subAgentTool->refreshCatalog(settings->localOnlyMode());
                     });
    QObject::connect(settings, &SettingsService::allowFullDiskReadChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowProjectWriteChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::allowLocalNetworkFetchChanged, &app, [applySettings]() { applySettings(); });
    QObject::connect(settings, &SettingsService::episodicMemoryEnabledChanged, &app, [applySettings]() { applySettings(); });
    // L4: provider switch rebuilds the web search backend immediately. 密钥也
    // 随 provider 换桶（webSearchApiKeyValue 读的是当前 provider 那一把），
    // 所以 setApiKey 与 setBackend 必须在同一次 applySettings 里成对执行。
    QObject::connect(settings, &SettingsService::webSearchProviderChanged, &app, [applySettings]() { applySettings(); });
    // 密钥改动（含切换 provider 后补发的那次通知）同样要重新注入。
    QObject::connect(settings, &SettingsService::webSearchApiKeyChanged, &app, [applySettings]() { applySettings(); });
    // H1: proxy changes take effect on the next request (in-flight replies
    // stay on the connection they already negotiated).
    QObject::connect(settings, &SettingsService::proxyConfigChanged, &app, [applySettings]() { applySettings(); });

    // Batch 22 (F8): user-editable system prompt (empty = built-in)
    // 走 VM 下发而不是直接写 agent：套用了提示词预设的标签由预设接管，
    // 全局提示只落在未套用的标签上，并记进每个标签作为"清除预设"的回落值。
    const auto applySystemPrompt = [agentVM, settings]() {
        agentVM->applyGlobalSystemPrompt(settings->systemPromptOverride());
    };
    applySystemPrompt();
    QObject::connect(settings, &SettingsService::systemPromptOverrideChanged, &app, applySystemPrompt);
    // 预设被编辑/改名时，已套用该预设的标签跟上新文本（删除不动，见
    // ChatSessionController::syncPromptPreset）。
    QObject::connect(settings, &SettingsService::promptPresetsChanged, &app, [agentVM, settings]() {
        agentVM->syncPromptPresets(settings->promptPresets());
    });

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

    // FEAT-801 / 外部向量模型（任务 #16）: any embedding-related change
    // (model, endpoint, API key, provider switch, local-only toggle)
    // re-applies the embedding configuration — the same signal → re-apply
    // chain web search uses. Identity changes (model rename, provider flip)
    // trigger the background re-embed inside applyEmbeddingConfig; endpoint
    // and key changes only redirect future requests (stored vectors depend
    // on the model identity, not the service address — Batch 25 G11).
    QObject::connect(settings, &SettingsService::embeddingModelChanged, &app,
                     [applyEmbeddingConfig]() { applyEmbeddingConfig(); });
    QObject::connect(settings, &SettingsService::embeddingBaseUrlChanged, &app,
                     [applyEmbeddingConfig]() { applyEmbeddingConfig(); });
    QObject::connect(settings, &SettingsService::embeddingProviderChanged, &app,
                     [applyEmbeddingConfig]() { applyEmbeddingConfig(); });
    QObject::connect(settings, &SettingsService::embeddingApiKeyChanged, &app,
                     [applyEmbeddingConfig]() { applyEmbeddingConfig(); });
    // localOnlyModeChanged also re-runs applySettings (above); this connect
    // additionally re-evaluates the embedding provider fallback immediately.
    QObject::connect(settings, &SettingsService::localOnlyModeChanged, &app,
                     [applyEmbeddingConfig]() { applyEmbeddingConfig(); });

    // FEAT-803: batch-embedding progress → knowledge panel. Both backends
    // are connected — only the active one ever emits, so provider switches
    // never leave the panel unfed.
    QObject::connect(ollamaEmbedding, &OllamaEmbedding::embedProgress,
                     agentVM, &AgentViewModel::setEmbedProgress);
    QObject::connect(openaiEmbedding, &OpenAICompatibleEmbedding::embedProgress,
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

    // ── 托盘右键菜单：一次建好、终身不改 ──
    // 只保留窗口/会话/退出三项。早前在托盘里放过「模型配置」子菜单做快速
    // 切换（docs/31 S4.4 4b），实用性不高、且是下面这个 Qt 缺陷的触发点，
    // 已按用户要求取消，配置切换统一走设置页。
    //
    // 为什么菜单必须始终不动：Quick 应用里存在 QQuickApplicationWindow 顶层
    // 窗口时，qwindows 的 doUseNativeMenus() 为 true，setContextMenu() 会给
    // 这棵 QMenu 额外建一套 Win32 HMENU 镜像。镜像建好之后再增删动作，
    // QMenu::actionEvent() 会把 ActionAdded/ActionRemoved 转给 QPlatformMenu，
    // qwindows 的 menuItemForTag() 沿 subMenu 链无终止递归（无 visited 集），
    // 最终栈溢出闪退——实测 25 472 层 qwindows 帧、STATUS_STACK_OVERFLOW。
    // 复制配置与右键托盘的闪退都出在这条路径上（docs/31 S4.4 回归）。
    // setData()/setChecked() 这类 ActionChanged 不触发它，增删动作必崩；
    // 后续若要在托盘里加动态项，只能整棵 QMenu 重建后 setContextMenu() 换上，
    // 绝不能对已挂上的菜单 addAction()/clear()。
    QMenu* trayMenu = new QMenu();
    QAction* showAction = trayMenu->addAction(QObject::tr("显示/隐藏主窗"));
    QAction* newChatAction = trayMenu->addAction(QObject::tr("新建会话"));
    trayMenu->addSeparator();
    QAction* quitAction = trayMenu->addAction(QObject::tr("退出"));

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
    // 不能在这里直接 delete menu——quitAction 正是它的子动作，信号还没发
    // 完就销毁宿主对象会在返回时用已释放的 QAction。解除绑定后交给
    // deleteLater。
    QObject::connect(quitAction, &QAction::triggered, &app,
                     [&app, agentVM, tray, trayMenu]() {
        tray->hide();
        tray->setContextMenu(nullptr);
        trayMenu->deleteLater();
        agentVM->stopAllTabs();
        QCoreApplication::quit();
    });

    tray->setContextMenu(trayMenu);
    tray->show();

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

    // docs/31 S4.1 N9：用量达到配额阈值（未超限）时弹一次托盘气泡。
    // 跨 tab 去重已在 AgentViewModel 完成；这里只负责门控与文案。
    // 与任务完成通知不同，**窗口在前台也提示**——这是预算提醒，用户
    // 正在使用时应立刻知道"快超了"，而不是切换窗口后才看到。
    QObject::connect(agentVM, &AgentViewModel::quotaAlert, &app,
        [tray, prefStore](const QString& quotaType, qint64 used, qint64 quota) {
            if (!prefStore->load(QStringLiteral("notice.enabled")).toBool()) return;
            QString label;
            if (quotaType == QLatin1String("dailyToken"))
                label = QObject::tr("今日 Token");
            else if (quotaType == QLatin1String("dailyCost"))
                label = QObject::tr("今日费用");
            else if (quotaType == QLatin1String("monthlyToken"))
                label = QObject::tr("本月 Token");
            else if (quotaType == QLatin1String("monthlyCost"))
                label = QObject::tr("本月费用");
            else
                label = quotaType;
            tray->showMessage(
                QObject::tr("用量接近配额上限"),
                QObject::tr("%1 已用 %2 / %3").arg(label).arg(used).arg(quota),
                QSystemTrayIcon::Information, 6000);
        });

    // docs/31 S4.2 N6：数据库快照的自动备份。启动备份一次
    // （memory/backupOnStartup，保留 docs/23 D1 语义）+ 可选的定时备份
    // （memory/backupIntervalHours，0 = 关闭）。
    // 装配点从初始化早期挪到托盘创建之后：备份失败要弹托盘气泡，而托盘直
    // 到这里才存在；原先的启动备份块在它之前，失败只能静默记日志。
    // worker 完全自包含（自建短连接、只捕获值）：退出时不会悬挂
    // （docs/22 教训）；失败不阻塞启动/退出，只是少一份快照。
    {
        const QString dbPath = memory->databasePath();
        const QString backupDir = SqliteMemory::defaultBackupsDir();
        // 两个共享标志只在 GUI 线程读写：worker 通过 QueuedConnection 回到
        // 主线程后才改，避免跨线程数据竞争。
        auto backupInFlight = std::make_shared<bool>(false);
        auto lastFailureNoticeMs = std::make_shared<qint64>(0);

        const auto runBackup = [&app, settings, tray, prefStore, dbPath, backupDir,
                                backupInFlight, lastFailureNoticeMs]() {
            if (*backupInFlight) return; // 上一轮还没落盘，跳过本轮
            *backupInFlight = true;
            const int keep = settings->backupKeepCount();
            // 失败气泡的静默期至少一个间隔（关闭定时备份时按 1 小时算）：
            // 磁盘满时不会每次启动/每小时都弹同一个提示。
            const qint64 noticeGapMs = qMax<qint64>(
                backupIntervalMs(settings->backupIntervalHours()), 60LL * 60 * 1000);
            QtConcurrent::run([&app, tray, prefStore, dbPath, backupDir, keep,
                               noticeGapMs, backupInFlight, lastFailureNoticeMs]() {
                QDir().mkpath(backupDir);
                const QString target = backupDir
                    + QStringLiteral("/auto_%1.db")
                          .arg(QDateTime::currentDateTime().toString(
                              QStringLiteral("yyyyMMdd_HHmmss")));
                const bool ok = SqliteMemory::createBackupFile(dbPath, target);
                if (ok)
                    SqliteMemory::rotateAutoBackups(backupDir, keep);
                // 结果回 GUI 线程：in-flight 标志与托盘都只在那里碰。
                QMetaObject::invokeMethod(
                    &app,
                    [ok, target, backupDir, tray, prefStore, noticeGapMs,
                     backupInFlight, lastFailureNoticeMs]() {
                        *backupInFlight = false;
                        if (ok) {
                            qInfo() << "Auto backup written:" << target;
                            return;
                        }
                        qWarning() << "Auto backup failed:" << target;
                        // 活动记录留痕（日志轮转后仍可在「活动日志」页查到），
                        // 只记目录与结果，不记数据库内容。
                        Activity::usage(Activity::Kind::Db,
                                        QStringLiteral("db.backup.auto.failed"),
                                        backupDir);
                        const qint64 now = QDateTime::currentMSecsSinceEpoch();
                        if (*lastFailureNoticeMs != 0
                                && now - *lastFailureNoticeMs < noticeGapMs)
                            return;
                        *lastFailureNoticeMs = now;
                        if (!prefStore->load(QStringLiteral("notice.enabled")).toBool())
                            return;
                        tray->showMessage(
                            QObject::tr("自动备份失败"),
                            QObject::tr("无法写入数据库快照目录：%1").arg(backupDir),
                            QSystemTrayIcon::Warning, 6000);
                    },
                    Qt::QueuedConnection);
            });
        };

        auto* backupTimer = new QTimer(&app);
        // 备份间隔以小时计，秒级精度没有意义；VeryCoarse 让系统合并唤醒。
        backupTimer->setTimerType(Qt::VeryCoarseTimer);
        QObject::connect(backupTimer, &QTimer::timeout, &app, runBackup);
        const auto syncBackupTimer = [settings, backupTimer]() {
            const qint64 ms = backupIntervalMs(settings->backupIntervalHours());
            if (ms <= 0) {
                backupTimer->stop();
                return;
            }
            // setInterval 作用于运行中的定时器会重新计时；只有间隔真的变了
            // 才重置倒计时，避免无关的设置变更把下一次备份往后推。
            if (backupTimer->interval() != int(ms)) {
                backupTimer->stop();
                backupTimer->setInterval(int(ms));
            }
            if (!backupTimer->isActive())
                backupTimer->start();
        };
        QObject::connect(settings, &SettingsService::backupIntervalHoursChanged,
                         &app, syncBackupTimer);
        syncBackupTimer();

        if (settings->backupOnStartup())
            runBackup();
    }

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
