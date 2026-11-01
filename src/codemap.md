# src/ — 分层索引与 main.cpp 装配详解

## 职责

- 承载 NexusAI 的全部 C++/QML 源码，按 `core → infrastructure → viewmodels → ui/main.cpp` 四层组织；前三层分别是静态库 `nexus_core` / `nexus_infra` / `nexus_viewmodels`，`main.cpp` 与 QML 组成可执行文件 `nexus_ai`。
- `main.cpp`（约 993 行）是唯一的组合根：创建进程级对象、注册工具、注入 ViewModel 单例、建立设置→运行时的信号桥，最后加载 QML 并进入事件循环。
- 构建归属由根 `CMakeLists.txt` 决定；本目录不含构建脚本。

## 设计

| 目录 | 目标 | 一句话职责 | 索引 |
| --- | --- | --- | --- |
| `core/` | `nexus_core` | 无 GUI 的领域核心：ReAct、记忆、工具/沙箱、RAG、Git、MCP、设置/日志等 | [core/codemap.md](core/codemap.md) |
| `infrastructure/` | `nexus_infra` | LLM 协议实现（OpenAI/Gemini/Anthropic/Ollama/Responses）与两种 embedding 后端 | [infrastructure/codemap.md](infrastructure/codemap.md) |
| `viewmodels/` | `nexus_viewmodels` | QML 单例与 UI 状态机；`NexusAI.ViewModels` QML 模块 | [viewmodels/codemap.md](viewmodels/codemap.md) |
| `ui/` | `nexus_ai` | `main.qml` 与组件/页面/面板；QML 模块 `NexusAI` | [ui/codemap.md](ui/codemap.md) |

- **依赖方向**：`core`（仅 Qt Core/Sql/Concurrent/Network）← `infra`（+Network）← `viewmodels`（+Quick/Qml/Pdf/crypt32）← `nexus_ai`（+QuickControls2/Widgets）。
- **Gui-free 边界**：`Qt6::Pdf` 传递依赖 `Qt6::Gui`，因此 `PdfTextExtractor` 放在 `viewmodels` 而非 `core`；`main.cpp` 因托盘需要 `QtWidgets`，基类使用 `QApplication`。
- **QML 单例注入**：`Theme`/`ThemeManager`/`AgentVM`/`Settings`/`ConnectionTester` 等以 `QML_SINGLETON + create()` 暴露；`main.cpp` 先 `instance()` 取到实例、注入依赖，engine 加载时工厂返回同一对象。`TraceModel`/`LogModel`/`ChatSessionController` 是普通库类，不经类型名暴露。
- **进程级长生命周期**：主 `SqliteMemory`、`RagService`、两个 embedding 后端不设 parent、永不删除（后台 `QtConcurrent` 任务持有裸指针）；每个会话 tab 另有自己的 `AIAgent` + `SqliteMemory` + 协议服务集合，随 tab 销毁。
- **线程纪律**：SQLite 只在 GUI 线程访问；工具/embedding 在池线程执行，结果经队列信号回 GUI 线程落库；embedding 后端切换只翻转 `QAtomicInt`，不替换实例。

### 子层内容速览

- `core/`：`agent/`（`AIAgent`、`EpisodicService`、`PermissionMode`、`LLMTypes`）、`memory/`（`IMemory`/`SqliteMemory`）、`tools/`（`ITool`/`ToolRegistry`/`Sandbox`/web 工具）、`rag/`、`git/`、`mcp/`、`utils/`（日志、设置、Token、定价、代理、附件/项目索引）。
- `infrastructure/llm/`：`IAIService` + `LLMServiceBase` + `LlmProtocolRegistry`，协议实现 `OpenAI`/`OpenAIResponses`/`Anthropic`/`Gemini`/`Ollama`；`infrastructure/embed/`：`OllamaEmbedding`、`OpenAICompatibleEmbedding`、`EmbedShutdown`（进程级中断标志）。
- `viewmodels/`：会话编排 `AgentViewModel`/`ChatSessionController`/`TraceModel`；设置与偏好 `SettingsService`/`SettingsBackup`/`PresetPackTransfer`/`Preference*`/`ChangeBus`/`EditBuffer`；主题与本地化 `ThemeProvider`/`ThemeManager`/`I18nManager`/`LocaleProbe`/`FontProbe`；能力面板 `GitViewModel`/`McpManager`/`ConnectionTester`/`CodeHighlighter`/`CitationParser`/`PdfTextExtractor`/`CommitMessageDrafter`。
- `ui/`：壳层与组件，详见 [ui/codemap.md](ui/codemap.md)。

## 数据与控制流

1. `QApplication` 构造；设置 organization/application name 与 `APP_VERSION`（编译宏来自 `app.rc.in` 所在构建段）。
2. `QLockFile`（`%TEMP%/NexusAI.instance.lock`，`tryLock(100)`）做单实例；失败打印后 `return 0`。锁是 `main` 局部变量，退出即释放。
3. `QQuickStyle::setStyle("Basic")`：允许 QML 自定义 TextField/Dialog 背景，避免原生样式告警。
4. `new ConfigurableProxyFactory` + `QNetworkProxyFactory::setApplicationProxyFactory`：全应用代理（本地回环直连由工厂内部规则保证）。
5. `Logger::install()` 安装消息处理器并写轮转日志；`Logger::backfillFromFile()` 回填日志面板历史。
6. 偏好基础设施：`PreferenceRegistry`、`ChangeBus`、`PreferenceStore`（接 ChangeBus）、`FontProbe`、`LocaleProbe`，全部 parent 到 app；随后 `I18nManager::applyLocale(currentLocale())`。
7. `SettingsService::instance()` 挂到 app，作为设置读取与变更广播入口。
8. `LlmProtocolRegistry::createServiceSet(&app)` 生成"协议键 → `IAIService*`"模板集合（每个已注册协议一个实例）。
9. `new SqliteMemory()`（进程级、无 parent）；`new OllamaEmbedding(nullptr)`、`new OpenAICompatibleEmbedding(nullptr)`；`aboutToQuit` 连接 `requestEmbeddingShutdown()`，让退出时在途嵌入尽快中止。
10. 共享原子开关 `openAiActive`；`embedText`/`embedTexts` 闭包按开关选后端；`new RagService(nullptr)` 注入单条/批量 embedder。
11. `applyEmbeddingConfig()`：同步两后端 model/baseUrl、外部后端 apiKey（`embeddingApiKeyValue()` 真值）；用 `SettingsService::embeddingUsesExternalProvider(provider, localOnlyMode)` 决定后端（local-only 时回退 Ollama 并告警）；身份（`"openai:"` 前缀模型名）变化时记录新身份并在池线程重嵌入全部文档（启动首次只播种身份）。
12. 工具注册：`KnowledgeSearchTool(rag)`、`NetworkFetchTool`、`WebSearchTool` 进 `ToolRegistry::instance()`；Web 搜索后端工厂查 `WebSearchProviderRegistry`（`duckduckgo`/`zhipu`/`deepseek`，未知回退注册表默认项）。
13. local-only 门禁 `applyLocalOnlyGate()`：把 `web_search` 与所有 `mcp__*` 写入 `ToolRegistry::setPolicyBlocked`；监听 `localOnlyModeChanged` 与 `activeProfileChanged`。
14. Agent 装配：`new AIAgent(&app)`，注入 `ToolRegistry`、主 `memory`；`new EpisodicService(&app)` 注入 memory 与 `embedText`，再 `agent->setEpisodicService()`。
15. 主题：`ThemeManager`/`ThemeProvider` 单例挂 app；`applyControlPalette()` 把 `ThemeProvider` 颜色写入 `QPalette`（Basic 控件取调色板），`themeChanged` 时重刷。
16. `AgentViewModel` 注入：RAG、memory、知识内容提供器；`setModelConfigurator` 按 `apiProtocol` 取服务、空 model/baseUrl 则置空，并同步 EpisodicService 的摘要服务、上下文上限与 `setModelLocal`；`setAgentFactory` 为每个 tab 建 agent/own memory/摘要服务/独立服务集，并在 `destroyed` 时清理三张 `QHash`。
17. `ConnectionTester` 挂 app；RAG 持久化桥：`documentPersisted → upsertKnowledgeDoc + replaceKnowledgeChunks`、`documentRemoved`、`knowledgeCleared` 均回到 GUI 线程写 SQLite。
18. `McpManager::loadAndConnectAll()` 读取 `mcp_servers.json` 连接启用的 MCP server 并桥接工具。
19. 用量/审计：`Activity::setUsageSink` 用 `QMetaObject::invokeMethod(&app, ..., QueuedConnection)` 落库；启动即 `runPrunes()`，并起 6 小时 `QTimer` 周期清理 usage/audit/token/checkpoint 与 30 天软删会话；**托盘创建之后**装配数据库快照自动备份（docs/31 S4.2）——`backupOnStartup` 启动一次、`memory/backupIntervalHours`（0/1/6/24/168，`BackupSchedule.h` 归一）驱动 `Qt::VeryCoarseTimer` 定时触发，两者共用 `runBackup`（`backupInFlight` 防重入，池线程 `VACUUM INTO` + `rotateAutoBackups` 按 `backupKeepCount` 轮转）；失败回主线程写活动记录并弹托盘警告（受 `notice.enabled` 门控，静默期 ≥ 一个间隔）。
20. `startKnowledgeRestore(memory, rag)` 先在 GUI 线程快照文档与 chunks，再池线程恢复：chunks 可用则零请求重建，否则重嵌入；`databaseRestored` 后重跑同一函数。
21. `applySettings()`（并由十余个 `SettingsService` 变更信号重入）：代理、模型 profiles、步数/超时/local-only、`Sandbox` 读策略与项目写/用户黑名单、http_fetch 私网放行、web search key/backend、情节记忆开关。另有 `applySystemPrompt()`、`applyPermissionMode()`（默认权限模式注入 VM）与 embedding 五个变更信号。
22. 两个 embedding 后端的 `embedProgress` 都接到 `AgentVM::setEmbedProgress`；`agentVM->setAgent(agent)` 完成主 agent 绑定。
23. 托盘/自启动桥：`general.autoStart` 偏好 → `settings->setAutoStart()`（Windows 上再与 HKCU Run 项做一次迁移/双向协调）。
24. `QQmlApplicationEngine` 注入 `trayAvailable`（`QSystemTrayIcon::isSystemTrayAvailable()`），`objectCreationFailed → exit(-1)`，加载 `qrc:/qt/qml/NexusAI/src/ui/main.qml`；根对象为空或非 `QQuickWindow` 时返回 -1。
25. Windows：`allowDragDropFromLowerIntegrity(mainWindow)` 延后一个事件循环，用 `ChangeWindowMessageFilterEx` 放行 `WM_DROPFILES`/`WM_COPYDATA`/`0x0049`（UIPI）。
26. 图标回退链 `:/icons/nexus-64.png → :/icons/nexus-256.png → QApplication::windowIcon()`；创建 `QSystemTrayIcon` + `QMenu`（显示/隐藏主窗、新建会话、退出）。
27. 托盘交互：菜单固定三项（显示/隐藏主窗、新建会话、退出），**一次建好后不再增删动作**——Quick 应用里 `doUseNativeMenus()` 为 true，`setContextMenu()` 会给 QMenu 建 Win32 HMENU 镜像，此后再 `addAction()`/`clear()` 会触发 qwindows 的 `menuItemForTag()` 无限递归栈溢出（docs/31 S4.4 闪退根因；原「模型配置」子菜单已按用户要求取消，配置切换统一走设置页）。显示/隐藏按"可见且激活"切换；新建会话唤起窗口并 `agentVM->newChat()`；退出先隐藏托盘、解除菜单、`stopAllTabs()`，再 `QCoreApplication::quit()`；双击托盘切显隐。
28. `taskFinished`：受 `notice.enabled` 控制，窗口可见且激活时不打扰，托盘可用/close-to-tray 时 `showMessage` 6 秒。
29. `I18nManager::localeChanged → engine.retranslate`，强制 QML 重新求值 `qsTr`；最后 `app.exec()`。

**设置 → 运行时重应用链**：`readDenyListChanged`/`profilesChanged`/`localOnlyModeChanged`/`activeProfileChanged`/`contextTokensChanged`/`maxStepsChanged`/`taskTimeoutMinutesChanged`/`allowFullDiskReadChanged`/`allowProjectWriteChanged`/`allowLocalNetworkFetchChanged`/`episodicMemoryEnabledChanged`/`webSearchProviderChanged`/`webSearchApiKeyChanged`/`proxyConfigChanged`/`webSearchTestChanged` 全部重跑 `applySettings()`；`systemPromptOverrideChanged` 与 `defaultPermissionModeChanged` 各有独立 apply，embedding 五键重跑 `applyEmbeddingConfig()`（local-only 同时命中两条链）。

**退出路径**：普通关闭由 `main.qml` 的 `onClosing` 先保存窗口几何，再按 `window.closeToTray && trayAvailable` 决定隐藏或接受；唯一真正退出是托盘"退出"（停全部 tab）。`aboutToQuit` 置位嵌入中断标志，后台 restore/re-embed 循环尽快收手；`main` 返回后 `QLockFile` 析构释放，Qt 静态析构阶段的线程池 `waitForDone` 不再被 300s 网络等待拖死。

**清理顺序要点**：托盘退出动作 → `tray->hide()` → `delete trayMenu` → `agentVM->stopAllTabs()` → `quit()` → QML 窗口销毁 → `QApplication` 析构销毁 parent 到 app 的对象（`AIAgent`、`EpisodicService`、各 VM 单例）→ 静态析构等待线程池。无 parent 的 `SqliteMemory`/`RagService`/embedding 后端按设计泄漏到进程结束；`QLockFile` 在 `main` 栈上最后释放。

## 集成点

- **QML**：`NexusAI.ViewModels`（C++ 类型/单例）与 `NexusAI`（QML 文件）两个模块；`main.qml` 通过 `qrc:/qt/qml/NexusAI/src/ui/main.qml` 加载，依赖根 `CMakeLists.txt` 的 `qt_add_qml_module` 与 `QML_IMPORT_PATH`。
- **系统**：SQLite（app data）、HKCU Run 键、`QSystemTrayIcon`/`QMenu`（Widgets）、Windows 消息过滤器、OLE 拖放（QML `DropArea` → `AgentViewModel::handleDroppedFiles`）。
- **信号桥**：`SettingsService`（profiles/localOnly/sandbox/web search/embedding/system prompt/permission）→ 运行时；`ChangeBus`（偏好键）→ 自启动；`RagService`/`AgentViewModel` → SQLite；`Activity` → usage_log。
- **构建/测试**：`nexus_ai` 链接三库 + `nexus_viewmodelsplugin`；`tests/CMakeLists.txt` 复用同一 QML 模块和翻译产物。
- **资源与版本**：图标走 `:/icons/nexus-<size>.png`（`nexus_icon_qrc_check` 守护），翻译走 `:/i18n`，版本宏 `APP_VERSION` 同时供窗口标题与托盘 tooltip 使用。

## 关键文件

- `main.cpp`：组合根、启动/退出顺序、托盘与 UIPI、设置重应用链。
- `app.rc.in`：Windows VERSIONINFO（图标路径由 CMake 相对化，版本来自 `project(VERSION)`）。
- `core/agent/AIAgent.{h,cpp}`、`core/agent/EpisodicService.{h,cpp}`、`core/agent/PermissionMode.h`。
- `core/agent/SubAgentRunner.{h,cpp}`、`core/tools/SubAgentTool.{h,cpp}`：docs/32 P1/P2 只读子代理（spawn_agent，设置键 `agent/subagent*`，注册门控在 main.cpp applySettings，设置页 AgentBehaviorPage）。
- `core/memory/SqliteMemory.{h,cpp}`、`core/tools/{ToolRegistry,Sandbox,WebSearchTool,NetworkFetchTool}.h`。
- `core/rag/RagService.{h,cpp}`、`core/utils/{Logger,Activity,NetworkProxyConfig,SamplingParams}.h`。
- `infrastructure/llm/LlmProtocolRegistry.{h,cpp}`、`infrastructure/embed/{OllamaEmbedding,OpenAICompatibleEmbedding,EmbedShutdown}.h`。
- `viewmodels/AgentViewModel.{h,cpp}`、`viewmodels/{ThemeProvider,ThemeManager,SettingsService,McpManager,PreferenceStore,ChangeBus,I18nManager}.h`。

## 约束与风险

- **core 不得依赖 Gui/Pdf**；需要 QPdfDocument、QQuickWindow、Widgets 的能力只能放 `viewmodels`/`nexus_ai`。
- **进程级对象禁止挂 parent**：`rag`、两个 embedding 后端、主 `memory` 被丢弃的 future 持有；挂到 app 会在退出时 use-after-free。后端切换只翻原子开关，不换实例。
- **单实例锁作用域**：`QLockFile` 必须是 `main` 局部变量；改成静态/堆对象会导致退出不释放。锁冲突时静默退出，目前不会唤起已有窗口。
- **单线程 SQLite**：任何池线程结果必须排队回 GUI 线程；`Activity` sink、RAG 持久化、prune 定时器都依赖此约定。
- **每 tab 独立服务集是硬需求**：`LLMServiceBase` 每实例只有一个在途请求上下文；共享会导致流互相中止/串台。tab 销毁后必须清三张 `QHash`。
- **模型未配置时**：`setModelConfigurator` 在 model/baseUrl 为空时把服务置空，调用方需容忍 `modelConfigured == false`。
- **local-only 是运行时策略层**：只写 `setPolicyBlocked`，不得污染用户 `tools/disabled` 持久化。
- **托盘/关闭**：托盘不可用时藏窗会让应用"失踪"；`trayAvailable` 必须由 `setInitialProperties` 注入，C++ 侧无法拦截 `QQuickCloseEvent`。
- **拖拽 UIPI**：从提权终端启动时必须在原生 HWND 就绪后放行三条消息，且 QML 侧不得对 `drop.urls` 调 `toLocalFile()`。
- **迁移/兼容**：自启动偏好与 Run 键、知识 chunks 的内容哈希/模型身份、软删 30 天窗口都是隐式契约，改动需同步测试。
- **代理工厂所有权**：`ConfigurableProxyFactory` 以裸指针交给 `QNetworkProxyFactory`，全进程只设置一次；不要在别处重复 `setApplicationProxyFactory`，也不要让设置页各自创建工厂。
- **工程模式**：`nexus_ai` 在 Windows 上 `WIN32_EXECUTABLE FALSE`（保留控制台便于诊断），窗口化发布需另行调整；`qInfo()` 日志同时进控制台与轮转文件。
