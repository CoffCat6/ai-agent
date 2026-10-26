# src/viewmodels/ — ViewModel 层（QML 门面 + 会话/设置编排）

## 职责

- 本目录是 MVVM 的 **VM 层**：把 core（ReAct/记忆/RAG/Git/MCP）与 infrastructure（LLM/embedding）的能力包装成 QML 可直接绑定/调用的对象，不承担任何视觉、布局或动画职责（那些在 `src/ui/`）。
- 三类职责并存：
  1. **应用级门面单例**：`AgentVM`、`Settings`、`GitVM`、`McpManager`、`Theme`、`I18n`、`PreferenceStore`、`SettingsBackup` 等，经 `QML_SINGLETON` 暴露给整个 QML 树。
  2. **会话对象与列表模型**：`ChatSessionController`（每个标签一份会话状态）+ `TraceModel` / `LogModel`（增量 `QAbstractListModel`），只由门面转发，不按类型暴露。
  3. **纯函数/一次性助手**：`Citation::parse`、`PdfTextExtractor`、`CommitMessageDrafter` 的静态工具、`CodeHighlighter`（`QSyntaxHighlighter` 附件）。
- 依赖方向单向：`nexus_viewmodels` → `nexus_infra` → `nexus_core`；`nexus_core` 不依赖 Qt6::Gui，所以需要 `QImage`/`QPdfDocument` 的代码（图片压缩、PDF 解析）被刻意留在这里。

## 设计（模式/抽象/关键类型）

### 1. QML 单例协议与 AOT 约束

- 统一模式：`QML_SINGLETON` + `QML_NAMED_ELEMENT(Name)` + `static instance()`（惰性 `new`）+ `static create(QQmlEngine*, QJSEngine*)`（`setObjectOwnership(instance(), CppOwnership)` 后返回同一实例）。
- `AgentViewModel` / `SettingsService` / `ConnectionTester` **故意没有默认构造**：可默认构造的 `QML_SINGLETON` 会让引擎优先走 Constructor 而非 `create()`，产生第二个未注入依赖的实例。
- `main.cpp` 先用 `instance()` 拿到对象、`setParent(&app)` 并注入依赖，再由 `QQmlApplicationEngine::load()` 通过 `create()` 取回**同一对象**。
- QML 模块 `NexusAI.ViewModels` 由 `qt_add_qml_module` 生成，`OUTPUT_DIRECTORY` 固定到 `NexusAI/ViewModels`；否则静态模块的 qmldir 落在 build 根目录，AOT 代码会嵌入过期的方法索引（会话恢复时 trace delegate 调 `AgentVM` 会崩溃）。
- 同一原因：新增 `Q_INVOKABLE` 一律追加在类尾（`inspectFileReferences`、`undoFileCheckpoint` 的注释即此约定），避免已有方法索引整体偏移。
- 门面属性大量使用“活动标签转发”：`stateChanged` / `traceChanged` / `responseReady` / `draftTextChanged` 只在 `controller == active()` 时重发；`toolConfirmRequired` / `errorOccurred` / `episodicChanged` 则任意标签都转发（确认框与错误是全局 UX）。常量属性（`stateIdle`…`permissionModePlanOnly`）把 core 的 `enum class` 镜像给 QML，避免 QML 里出现魔数。

### 2. 多标签会话架构（Batch 29，F7）

- `AgentViewModel` = 门面 + 全局状态（会话列表、项目、知识库、布局持久化、工具注册表、批量导入队列）。`ChatSessionController` = **一个标签的会话**（trace / finalResponse / draft / sessionId / token / 权限 / 输入历史），刻意“不知道标签概念”。
- 每个标签一个 `AIAgent`：tab 0 挂 main.cpp 的共享模板 agent（`owned=false`），其余由 `setAgentFactory()` 工厂创建（`owned=true`，析构时 `deleteLater`，并拥有独立 `SqliteMemory` 连接）。
- **每标签独立 ProviderSet 是硬要求**：`LLMServiceBase` 每个实例只保存一份在途请求上下文并向所有挂接的 agent 广播信号，共享会导致两个标签互相中止流、chunk 串到对方 trace。
- 切换标签不打断任务（后台标签继续跑自己的 trace）；`loadSession()` 在活动标签 busy 时改为 `addTab()` 新开标签承载会话。
- 关标签：先 `stop()`（agent 封口未答复的工具调用）再 `deleteLater()`；最后一个标签不可关。`closeAllTabs()` 静默清空（不 emit），供数据库恢复使用。

### 3. 会话状态机

- `AgentState`：`Idle → Thinking → (AwaitingToolConfirm) → ExecutingTool → … → Summarizing → Completed | Error`；`isBusy()` = Thinking / AwaitingToolConfirm / ExecutingTool / Summarizing。
- 状态唯一来源是 `AIAgent::stateChanged`，`ChatSessionController::setState` 只做去重与转发；`stateText` 提供英文短标签。
- 发送：`AgentVM.startTask` → 图片解码/缩放/JPEG 压缩 → `active()->startTask`（busy 拒绝、写入 50 条输入历史环、`Activity::usage` 记 `task.start`）→ `AIAgent::startTask`。
- 工具确认：agent 发 `toolConfirmRequired` → controller → VM 入 `m_confirmations` 队列并公开 `pendingConfirmation`（含 sessionId/sessionTitle/count）→ QML 对话框 → `approveToolCall(id)` 按 id 找回等待中的 controller，并校验 `agent()->pendingConfirmationId() == id` 后才放行。每次 `stateChanged` 走 `pruneConfirmations()` 清理失效项。
- 权限：`0=ConfirmChanges 1=AutoEdit 2=FullAccess 3=PlanOnly`，**内存态**；用户显式 `setPermissionMode` 标记 `overridden`，此后全局默认（Settings 的 `agent/defaultPermissionMode`）只追补未被覆盖的标签。

### 4. Trace 增量模型

- core 侧 trace 是全量 `QJsonArray` + 整体通知；`TraceModel::applyDelta` 按节点 `id` 做 diff：尾部追加 → `rowsInserted`，原地字段变化 → `dataChanged`，重排/删除/重复 id → `resetNodes`。避免流式期间 QML ListView 反复销毁 delegate（Canvas 图标/动画重启）。
- `lastIndexOfType()` 是消息级操作按钮的定位依据：`finishTask` 会在 summary 后追加 `metrics`，整表末行恒为 metrics，不能用 `count()-1`；`rankOfTypeAt()` 用于分叉锚点（第几条 summary）。

### 5. 设置的四层分工

| 层 | 位置 | 作用 |
| --- | --- | --- |
| `SettingsSchema` | `core/utils` | 唯一事实源：键注册表、默认值、类型/约束、`clampInt`/`options`、`kSchemaVersion = 4` |
| `SettingsService` | 本目录 | 强类型属性 + QSettings 直读直写（`SettingsRepository`）+ profile 管理 + DPAPI + 迁移 |
| `PreferenceRegistry` | 本目录 | 从 `SettingsSchema` 载入 `PreferenceMeta`/`CategoryMeta`，提供元数据、默认值、`normalize/validate/coerce` |
| `PreferenceStore` / `EditBuffer` / `ChangeBus` | 本目录 | 偏好键 `prefs/v1/<key>` 的读写/批量写/重置 + 写穿缓冲 + 变更广播 |

- `SettingsService` 管“AI 管线/安全/网络/日志/配额”这类**有强类型属性、会被 main.cpp 消费**的键（`agent/*`、`security/*`、`logging/*`、`webSearch/*`、`network/*`、`embedding/*`、`memory/*`、`projects/*`），键直接落 QSettings。
- `Preference*` 管“设置页可枚举、需要显示名/约束/重置”的偏好（`ui.*` 主题/字体/语言/布局默认、`general.*`、`window.closeToTray` 等），物理键加 `prefs/v1/` 前缀；`legacyKeys` 支持旧键迁移（如 `ui.themeMode` 由布尔 `isDark` 转换）。
- `EditBuffer` 是 `PreferenceStore` 的校验闸门：`stage()` 用 registry 约束校验后**立即** `saveBatch`（写穿），`dirty` 仅瞬时；校验失败保留暂存值供字段错误标签使用，`rollback()` 丢弃。

### 6. 加密、备份与迁移

- API key（模型 profile / web search / embedding / 代理认证）用 Windows DPAPI `CryptProtectData`（范围 `NexusAI.apiKey`）加密，落盘前缀 `dpapi:v1:`；非 Windows 退化为明文。加密失败时 `persist()` 直接中止并 `persistenceError`，绝不静默写明文。
- 掩码契约：`apiKey()`/`webSearchApiKey()`/`embeddingApiKey()`/`proxyPassword()` 对 QML 恒返回空串，只暴露 `*KeySet`；真实值经非属性、非 invokable 的 `*Value()` 出口给 main.cpp（历史上 main.cpp 误用掩码 getter 导致 401/“未配置”）。
- profile 以一条 JSON blob（`modelProfiles`）+ `activeProfileId` 存储；`load()` 做 schema 版本迁移（v2 清空存量模型列表、v4 归一 contextTokens 默认值），旧单 provider 键迁移为 profile 后删除明文 `apiKey`/`provider`。
- `SettingsBackup` 只导出**可移植**键：排除 `security/*`、`ui/*`、`encrypted`、`legacy`、`modelProfiles`、`agent/defaultPermissionMode`、`projects/activeId`、`tools/disabled` 等；格式 `nexusai-settings` v1，profiles 用白名单字段，MCP server 一律 `enabled=false`，导入后需重启。
- 数据库备份/恢复是另一条链：`AgentVM.backupDatabaseNow/previewDatabaseBackup/restoreDatabaseFrom` 走 `SqliteMemory` 的 VACUUM INTO 快照，不经过 `SettingsBackup`。

### 7. 异步与防串话

- `QtConcurrent::run` + `QFutureWatcher`（watcher 挂 `this`，worker **只捕获值**，绝不捕获 `this`）用于：PDF 文本提取（`QFutureWatcher<PdfExtractResult>`）、`RagService::ingestDocument`/re-embed（`QFutureWatcher<int>`）、批量导入每文件一个池任务。
- `GitViewModel` 的 `runAsync<Result>` 模板：watcher 以 context 为父、finished 后 `result()` + `deleteLater`；每个请求带 `requestId`，调用方（对话框）可丢弃过期应答；`rootGeneration/repoGeneration/statusGeneration/writeGeneration` 四代计数器丢弃切根/被覆盖的旧结果。
- 代数防串话还有：`AgentViewModel::m_projectFilesGeneration`（`@` 文件索引后台扫描，30s TTL）、`RagService::bumpRestoreGeneration()`（数据库恢复前使在途知识库恢复任务失效）。
- 写操作串行化：Git 只允许一个写操作（`operationBusy`，冲突返回空 id）；批量知识库导入在 GUI 线程串行推进队列，取消只停止派发后续文件。
- 生命周期：单例由 `main.cpp` `setParent(&app)`；标签 controller 以 VM 为父、其 agent 由 controller 拥有；`McpManager` 删除 client 采用 40s 延迟退役（覆盖在途执行上限）+ `aboutToQuit` 同步回收；`RagService`/`SqliteMemory`/embedding 后端刻意**进程级永不析构**，因为被丢弃的 QtConcurrent worker 仍持有其裸指针。

| 异步任务 | 触发 | 执行体 | 回 GUI 的路径 | 防串话手段 |
| --- | --- | --- | --- | --- |
| `@` 项目文件扫描 | `listProjectFiles` 缓存过期 | `ProjectFileIndex::scan` | `QMetaObject::invokeMethod(Queued)` + `QPointer` | `m_projectFilesGeneration` |
| PDF 文本提取 | `importDocument(.pdf)` | `extractPdfText` | `QFutureWatcher<PdfExtractResult>`（父为 VM） | `m_knowledgeBusy` 串行化（同一时刻仅一个导入） |
| 知识库导入/重嵌 | `importDocument(s)`/`reembedDocument` | `RagService::ingestDocument` | `QFutureWatcher<int>`，完成后发 notice/error | `RagService` 内部 hash/段数预算 |
| 批量导入 | `importDocuments` | 每文件一个池任务 | GUI 线程回调里写结果并链到下一文件 | 队列串行 + `m_batchCancelRequested` |
| Git 读请求 | `GitVM.requestX` | `GitService` | `runAsync` + `*Ready(requestId,…)` | root/status/repo 代际 + requestId |
| Git 写请求 | `stage/commit/…` | `GitService`（60s 超时） | `operationFinished` | `operationBusy` + `writeGeneration` |
| 知识库恢复 | 启动 / 数据库恢复 | `startKnowledgeRestore`（main.cpp） | `documentPersisted` 队列到 GUI | `bumpRestoreGeneration` |

### 8. 输入附件与富输入

- 图片：QML 只传 data URL 或路径，`AgentViewModel` 内用 `QImage` 解码、最长边缩到 1568、JPEG q=85 重编码，避免超长 base64 直接进上下文；剪贴板图片走 `clipboardImageDataUrl()`。
- 文本附件：`{kind:"file", name, content}`，单文件上限 512 KB（`MaxTextAttachmentBytes`），含 NUL 字节视为二进制拒绝，并以 `noticeOccurred` 给出用户提示。
- 拖放：`handleDroppedFiles(QList<QUrl>)` 接收 QML `drop.urls` 原始值（QML 的 url 无 `toLocalFile()`，路径转换必须在 C++ 做），按 `AttachmentClassifier` 分类后汇总为 `attachmentsDropped` 信号。
- `@` 引用与附件共用 `startTask` 的 `files` 参数：`resolveFileReferences` 生成 `{kind:"file", name:相对路径, content}`，`inspectFileReferences` 只做状态检查（不读内容）供输入框标红。
- 只读/计划模式（`PermissionMode::PlanOnly`）由 agent 侧执行策略实现，VM 只负责把模式透传到对应标签的 agent，并在 `tabModel` 中暴露 `permissionMode` 供标签徽标渲染。

### 9. 观测与统计出口

- `AgentVM` 是统计页的单一数据入口：`tokenStats/tokenStatsDetailed/tokenStatsBySource/costStats/quotaStatus/tokenUsageRecords/listUsageEvents/sessionTokenBreakdown/contextComposition` 全部透传 `IMemory` 或活动 agent，QML 不直接接触 SQLite。
- 用户动作经 `Activity::usage` 记入 `usage_log`（新建/切换/删除会话、发送/停止/重生成、工具批准/拒绝、备份/恢复），main.cpp 的 sink 用 `QueuedConnection` 保证 SQLite 单线程。
- `LogModel` 通过 `Logger` 环形缓冲做进程级追加采集（1s 定时器），面板可见时另开高频轮询；过滤变更整表重建，追加只发插入。
- 成本/配额：`costChanged` 只转发活动标签；`quotaWarning` 由 controller 直接转发，`strictQuotaMode` 的拒绝逻辑在 agent 侧。

## 类清单（表格）

| 类名 | 角色 | 关键 QML 接口 |
| --- | --- | --- |
| `AgentViewModel` | 主门面（`AgentVM`）：标签管理 + 全局会话/项目/知识库/布局/工具/统计 | `startTask`、`stop`、`approveToolCall`、`loadSession`、`newChat`、`regenerate`、`editAndResend`、`branchFromAnswer`、`listProjectFiles`、`importDocument(s)`、`undoFileCheckpoint`、`restoreDatabaseFrom`；属性 `state/traceModel/tabModel/knowledgeDocuments/...` |
| `ChatSessionController` | 单标签会话状态与 agent 适配（非 QML 类型） | 无（C++ 侧：`attachAgent`、`startTask`、`stop`、`loadSession`、`permissionMode`） |
| `TraceModel` | trace 增量 `QAbstractListModel`（非 QML 类型） | 经 `AgentVM.traceModel` 使用；roles `id/type/title/status/args/result/timestamp/modelData` |
| `LogModel` | 应用内日志列表模型（会话内追加、过滤/日期） | `AgentVM.logModel`（CONSTANT）；`filterLevel/filterText/filterCategory/filterDate`、`clearView()`、`toClipboardText()` |
| `ChangeBus` | 偏好变更广播总线 | `notify(key, value)`；信号 `changed(key, value)` |
| `EditBuffer` | 偏好校验写穿缓冲（单例） | `stage/commit/rollback/resetToDefault`、属性 `dirty/staged/firstInvalidKey/validationError` |
| `SettingsService` | 设置与模型 profile 门面（`Settings`），DPAPI/迁移；三组用户自建文本资产（`promptPresets`/`slashCommands`/`sessionTemplates`，JSON blob 持久化，提示词与指令页管理，备份白名单自动携带；首次启动经 `maybeSeedPromptAssets()` 种入4 预设 + 4 指令 + 3 模板（固定 id `default-*`，键为 "[]" 视为已清空不回灌）） | `profiles/activeProfile`、`addProfile/updateProfile/selectModelForProfile`、`addPromptPreset/addSlashCommand/addSessionTemplate` 及各自 update/remove、`testProxy`、`resetGeneralToDefaults`、`modelContextWindow` |
| `SettingsBackup` | 可移植设置导入导出（不含凭据/SQLite） | `exportSettings`、`previewImport`、`importSettings`、`lastError`、`restartRequired` |
| `PreferenceRegistry` | 偏好元数据注册表（来自 SettingsSchema） | `hasPreference/defaultValue/displayName/preferenceMeta/categories` |
| `PreferenceStore` | `prefs/v1/<key>` 持久化 + legacy 回退（单例） | `load/hasStoredValue/save/saveBatch/reset/resetBatch` |
| `ThemeManager` | 主题/字体状态与系统配色跟随 | `themeMode/isDark/fontSans/fontMono/baseFontSize` |
| `ThemeProvider` | 设计令牌门面（`Theme`，纯头文件、颜色/间距/圆角/字号/时间格式化） | `bgPrimary/textPrimary/brandPrimary/...`、`formatDateTime/formatTime` |
| `FontProbe` | 字体可用性与平台默认字体探测 | `isAvailable/systemDefaultSans/systemDefaultMono/availableFamilies` |
| `LocaleProbe` | 可用语言枚举（`:/i18n` 资源扫描） | `availableLocales()` |
| `I18nManager` | 翻译器装载与运行时切语言（`I18n`） | `currentLocale/availableLocales`、`applyLocale(locale)` |
| `ConnectionTester` | 一次性最小请求测试连接（不动实时管线） | `test(protocol, baseUrl, apiKey, model)`；信号 `testFinished(ok, message)` |
| `GitViewModel` | 异步 Git 门面（`GitVM`）：只读历史/分支/blame + 显式写操作 | `refreshStatus`、`requestRepoLog/DiffHunks/Blame/Graph`、`stageFiles/commitStaged/undoLastCommit/discardPath/stashPush` |
| `ProjectFileViewModel` | 右侧栏文件浏览门面（`ProjectFileVM`）：一层目录懒读 + 预览 + 打开，按 relPath 缓存（切根失效、失败不缓存），只读无写 | `children/preview/absolutePath/clearCache`、`openInDefaultApp`（清 `ELECTRON_RUN_AS_NODE`）、`openWithKnownEditor`（注册表探测 MarkText/VSCodium/VSCode 的 shell\open\command 应急拉起）、`openAbsolute`（**按 scheme 分流**：http(s) → 浏览器 openUrl，本地 → fromLocalFile——URL 源此前走 fromLocalFile 得无效 URL 静默打不开；判定与执行分离为 `openAbsoluteRoute` 探针，GUI-less 测试可断言路由）、`canonicalAbsolute`（知识库源文件用，不受项目根约束） |
| `CommitMessageDrafter` | 一次性“从暂存 diff 起草提交信息”（`CommitDrafter`） | `draft(stagedDiff, recentSubjects)`、`cancel()`；信号 `drafted` |
| `McpManager` | MCP server 运行时管理（`McpManager`）：连接/工具桥接/持久化 | `addServer/removeServer/setEnabled/reconnect`、`servers`、`lastError` |
| `CodeHighlighter` | 代码块正则高亮 + 语言识别（QML 元素，非单例）。语言来源二选一：`sourceName` 扩展名映射（文件预览；未知扩展名保持纯文本不猜），无名字走内容启发式（聊天代码块）；`visualLineSpans(wrapWidth)` 从 `QTextLayout` 取每逻辑行视觉行数（须显式传视口宽，惰性布局需内部自建且设 WrapAnywhere）；单行 >8KB 跳过高亮 | `document/text/sourceName/languageName/visualLineSpans/hasHighlightedText` |
| `CitationParser` | `[文档名]` / `[doc:名#chunk]` 解析唯一实现 | `parse(text)`、`hasCitation(segments)` |
| `PdfTextExtractor` | 自由函数 `extractPdfText` + `PdfExtractStatus`/`PdfExtractResult` | 无（C++/测试使用） |

## 数据与控制流

1. **发送与流式**：QML Composer → `AgentVM.startTask(text, images, files)` → `configureModel(active())` + 图片压缩 → `ChatSessionController::startTask`（历史环/Usage）→ `AIAgent::startTask` → `stateChanged` / `traceChanged`（全量 JSON）→ `TraceModel::applyDelta` → 门面仅对活动标签 `emit traceChanged` → ListView 只收到插入/变更行。
2. **工具确认**：`AIAgent::toolConfirmRequired` → controller → VM 队列 + `pendingConfirmation` + 全局信号 → 确认对话框 → `AgentVM.approveToolCall(id, ok)` → 按 id 找回 controller 并校验 `pendingConfirmationId` → `AIAgent::approveToolCall`。
3. **会话切换/恢复**：Sidebar → `loadSession(sessionId)` → 在 `sessionList` 查 `projectId`（查不到保持当前归属）→ busy 则 `addTab(project)` 否则 `setActiveProjectId(project)` → `controller.loadSession`（busy 时拒绝）→ `AIAgent::resumeSession` → `sessionChanged`/`traceChanged` → 重建 trace、checkpoints、刷新共享会话列表。
4. **会话派生操作**：`newChat`→`agent.resetSession`；`regenerate`→`agent.regenerateLast`；`editAndResend`→`agent.editLastUserMessage`（整轮替换，含持久化）；`branchFromAnswer`→`memory.forkSessionAtAnswer(sessionId, summary 的 rank, 唯一标题)`→`refreshSessions`→`loadSession`（源会话不动）。
5. **项目与 `@` 引用**：`importProject(path)` → canonicalize → `memory.createProject` → `refreshProjects` → 仅当活动标签尚无会话时绑定归属；`listProjectFiles(query)` 读 30s TTL 缓存，过期则后台 `ProjectFileIndex::scan`；`resolveFileReferences(text)` 把 `@path` 变成 `{kind:"file",name,content}` 附件；`inspectFileReferences` 逐 token 返回 `no-project/missing/outside/sensitive`。
5b. **项目规则（G3/R3）**：设置页经 `loadProjectRules` / `writeProjectRules`（失败发 `noticeOccurred`，成功后自动重挂监听）/ `projectRulesState`（与注入侧同一回退顺序，复用 `AIAgent::isUntouchedRulesSeed`）读写 `AGENTS.md`。外部编辑协调：`watchProjectRules` / `unwatchProjectRules` 以 `QFileSystemWatcher` 监听两个候选文件（只存在者可挂），变化发 `projectRulesFileChanged(root)`——**异步投递受 Windows 文件通知延迟影响（AV 可扣数秒甚至更久），页面按内容比较分流，迟到/重复/自身写入都安全**；`watchedRulesFiles` 是挂载集探针（同步断言用，不测异步投递）。三个 Q_INVOKABLE 追加在类尾（AOT 索引约定）。
6. **检查点/撤销**：agent 完成工具或撤销后发 `fileCheckpointsChanged` → controller → VM（仅活动标签更新属性）→ `undoFileCheckpoint(id)` 转 `AIAgent::undoFileCheckpoint`；`fileCheckpointDetail` 返回 JSON 详情。
7. **知识库**：`importDocument` 文本同步读取 / PDF 后台提取 → `importExtractedText`（内容 hash 去重 → `uniqueDocumentName`）→ `startIngestion`（QtConcurrent）→ `RagService::documentPersisted`（main.cpp，队列到 GUI 线程）→ `SqliteMemory`；`RagService::changed` → `refreshKnowledge`（先更新属性再 NOTIFY，避免 QML 读到旧列表）。
8. **设置写路径**：QML → `SettingsService` setter → `persistValue/persist` → `SettingsRepository`（QSettings，org/app 均为 `NexusAI`）→ 信号 → main.cpp 的 `applySettings` 把步数/超时/只读/沙箱/代理/搜索/embedding 重新注入所有 agent。偏好路径：QML → `EditBuffer.stage/commit` → `PreferenceStore.saveBatch` → `prefs/v1/` → `ChangeBus.notify` → `ThemeManager`/`I18nManager`/`AgentVM` 各自订阅。
9. **Git**：`GitVM.requestX()` 返回 requestId → `GitService` 在池线程执行（写操作 60s 超时）→ `*Ready(requestId, …, error)`；写成功后自动 `refreshStatus/refreshRepoInfo`；切 `projectRoot` 递增四代计数器并清空 repo/status，旧应答被丢弃。
10. **数据库恢复编排**：`restoreDatabaseFrom`（校验 integrity → `closeAllTabs` → `sendPostedEvents(DeferredDelete)` → `bumpRestoreGeneration` → `memory.restoreFromBackup` → `databaseRestored` → `openTab`），全程同步，靠同一调用栈回到“单标签稳态”。

## 集成点

- **main.cpp 注入顺序**（顺序本身是契约，依赖在引擎加载前就位）：
  1. `Logger::install()` + 历史回填；
  2. `PreferenceRegistry/ChangeBus/PreferenceStore/FontProbe/LocaleProbe`（必须先于 Settings/Theme，供其注册偏好）；
  3. `I18nManager` 并 `applyLocale(currentLocale())`；
  4. `SettingsService`（QSettings + 迁移 + DPAPI 解密）；
  5. LLM `ProviderSet`（每协议一个实例）；
  6. `SqliteMemory` → Ollama/OpenAI embedding → `RagService`；
  7. local-only 工具闸门（`web_search` + 所有 `mcp__*`）；
  8. 模板 `AIAgent` + `EpisodicService`；
  9. `ThemeManager/ThemeProvider` + 同步 Basic 控件 `QPalette`；
  10. `AgentViewModel`：`setRagService` / `setMemory` / `setKnowledgeContentProvider` / `setModelConfigurator` / `setAgentFactory`；
  11. RAG 持久化接线、`McpManager.loadAndConnectAll()`；
  12. usage sink、6 小时清理定时器、启动自动备份、`startKnowledgeRestore`；
  13. `applySettings()`（步数/超时/只读/sandbox/代理/搜索/embedding/episodic）；
  14. 权限默认 → embedding 热重配 → `agentVM->setAgent(agent)`；
  15. `general.autoStart` 偏好 ↔ HKCU Run 键桥接；
  16. `QQmlApplicationEngine` 创建并 `load("qrc:/qt/qml/NexusAI/src/ui/main.qml")`，托盘与 `taskFinished` 通知在窗口就绪后接线。
- **QML**：`import NexusAI.ViewModels` 后直接使用 `AgentVM`、`Settings`、`GitVM`、`McpManager`、`Theme`、`ThemeManager`、`I18n`、`PreferenceStore`、`PreferenceRegistry`、`EditBuffer`、`ChangeBus`、`SettingsBackup`、`ConnectionTester`、`CommitDrafter`、`FontProbe`、`LocaleProbe`。`main.qml` 绑定 `AgentVM.sidebarWidth/panelWidth/isPanelVisible`、`Theme.*`，窗口几何经 `loadWindowGeometry/saveWindowGeometry`。
- **core**：`AIAgent`（状态机/工具确认/trace/会话/checkpoint）、`IMemory`/`SqliteMemory`、`RagService`、`GitService`、`ToolRegistry`、`Sandbox`、`SettingsRepository`/`SettingsSchema`、`Logger`、`Activity`、`ProjectFileIndex`、`AttachmentClassifier`。
- **infrastructure**：`IAIService`/`LlmProtocolRegistry`（`ConnectionTester`、`CommitMessageDrafter`、main.cpp 的 ProviderSet）、Ollama/OpenAI embedding。
- **测试**：`tests/test_viewmodel.cpp` 覆盖 TraceModel diff/末行定位、输入历史、拖放分类、标签独立性、项目归属隔离、批量导入状态/取消、分叉、导出、LogModel 过滤；`tests/test_settings.cpp` 覆盖 profile 归一/DPAPI 代理认证、schema 覆盖与版本戳、权限默认、上下文窗口优先级、保留/配额。

## 关键文件

| 文件 | 行数 | 说明 |
| --- | --- | --- |
| `AgentViewModel.h/.cpp` | 675 / 1897 | 主门面、标签、项目、知识库、备份恢复、@引用、附件 |
| `SettingsService.h/.cpp` | 438 / 1350 | profile/DPAPI/迁移/代理探测/自动启动 |
| `GitViewModel.h/.cpp` | 187 / 656 | 异步 git 门面 + 四代计数器 + 单写闸门 |
| `ChatSessionController.h/.cpp` | 158 / 310 | 单标签会话状态、agent 信号接线、历史环 |
| `SettingsBackup.cpp` | 397 | 可移植键筛选、白名单 profile、回滚 |
| `McpManager.cpp` | 293 | 连接/工具桥接/延迟退役/退出回收 |
| `CodeHighlighter.cpp` | ~420 | 语言启发式 + 扩展名映射 + 五类 token 着色 + 单行预算 |
| `PreferenceRegistry.cpp` / `PreferenceStore.cpp` | 222 / 114 | 元数据注册 / `prefs/v1` 读写 |
| `TraceModel.cpp` / `LogModel.cpp` | 151 / 227 | trace 增量 diff / 日志环形镜像（上限 5000 行） |
| `ThemeProvider.h` / `ThemeManager.cpp` | 208 / 190 | 设计令牌 / 主题偏好与系统配色 |
| `CommitMessageDrafter.cpp` | 235 | 一次性起草、本地模式拦截、diff 截断 |
| `PdfTextExtractor.cpp` | 89 | 页级文本提取与错误分类 |

## 约束与风险

- **`active()` 无守卫**（直接 `m_tabs.at`）：`stop/newChat/startTask/…` 假定至少一个标签。`closeAllTabs()` 因此必须静默，且调用方必须在同一调用栈内到达 `openTab()`（数据库恢复流程依赖 `sendPostedEvents` 冲刷延迟删除）。
- **单例重复实例**：给 `AgentViewModel`/`SettingsService`/`ConnectionTester` 加默认构造会让引擎绕过 `create()`；`ThemeProvider`/`CitationParser` 用内联静态实例，注意别与 `instance()` 混用。
- **AOT 方法索引**：新增 `Q_INVOKABLE` 只能追加；改动 QML 模块输出目录或提升方法顺序会让旧缓存崩溃（历史事故）。
- **服务不可共享**：LLM service 的单一在途上下文决定了每标签必须独立 ProviderSet；`tabProviderSets/tabMemories/summaryProviders` 在 agent `destroyed` 时清理，否则设置刷新会碰到已死 agent。
- **`*Value()` vs 掩码 getter**：C++ 装配必须用 `webSearchApiKeyValue()/embeddingApiKeyValue()/proxyPasswordValue()`，QML 永远拿不到明文；用错会得到“已配置却报未配置”。
- **DPAPI 是每用户、仅 Windows**：换用户/换机无法解密；非 Windows 明文落盘；加密失败会阻止保存（设计如此）。
- **worker 捕获纪律**：所有 `QtConcurrent` lambda 只捕获值（`rag`/`memory` 是进程级裸指针，永不析构）；捕获 `this` 会在 VM 销毁后悬垂。
- **偏好写穿语义**：`EditBuffer.dirty` 只在失败暂存时短暂为真，QML 不要依赖“批量应用/取消”语义；拆分器拖动写盘由 `m_uiPersistTimer` 400ms 去抖。
- **易失状态**：标签权限模式、输入历史（50 条）、trace 均不持久化；重启后从默认权限与会话记录恢复。
- **缓存/一致性问题**：`@` 文件索引 30s TTL 可能短暂陈旧；`refreshSessions` 依赖任一挂接 agent，全部标签无 agent 时列表为空；`sessionList` 全量返回、由 QML 分组。
- **Git 只读边界**：不 fetch/checkout/push；HEAD 已发布到远端时拒绝改写历史（amend/undo），`discardPath` 先把未跟踪文件备份到 `discardBackupRoot`。
- **备份/恢复限制**：`SettingsBackup` 不含凭据与 SQLite 数据，导入后需重启；数据库恢复会关闭所有标签并停止任务，恢复前必须通过 integrity 检查。
- **日志模型上限**：会话镜像最多 5000 行，被环与镜像同时淘汰的日志不可恢复；捕获定时器 1s，面板关闭期间仍持续采集。
