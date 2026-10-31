# src/viewmodels/ — ViewModel 层（QML 门面 + 会话/设置编排）

## 职责

- 本目录是 MVVM 的 **VM 层**：把 core（ReAct/记忆/RAG/Git/MCP）与 infrastructure（LLM/embedding）的能力包装成 QML 可直接绑定/调用的对象，不承担任何视觉、布局或动画职责（那些在 `src/ui/`）。
- 三类职责并存：
  1. **应用级门面单例**：`AgentVM`、`Settings`、`GitVM`、`McpManager`、`Theme`、`I18n`、`PreferenceStore`、`SettingsBackup`、`PresetPackTransfer` 等，经 `QML_SINGLETON` 暴露给整个 QML 树。
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
- 切换标签不打断任务（后台标签继续跑自己的 trace）；`loadSession()` 在活动标签 busy 时改走 `openConversation()`（可能新开 controller 承载会话）。
- 关标签：先 `stop()`（agent 封口未答复的工具调用）再 `deleteLater()`；最后一个标签不可关。`closeAllTabs()` 静默清空（不 emit），并连同清空 LRU 与未读表，供数据库恢复使用。

### 2b. 单对话视图（docs/30，P0 已实施）

面向用户的形态从「标签栏」迁到「侧栏驱动单窗口」：主区永远只渲染活动会话，
所有会话在侧栏可见，后台有任务的行显示动画图标。引擎层不变（仍是一会话一
controller 一 agent 一连接），因此下面这层是**呈现语义**的收敛点：

- `openConversation(id)`：已被 controller 承载 → `activateTab`（不打断任务）；
  否则就地复用活动 controller（**须空闲且无草稿**，否则是新建）→ 加载并激活。
  空闲时就地复用是硬要求：每个 controller 一条 SQLite 连接 + 最多 800 条 trace
  节点恢复，若浏览历史也新建，翻几条旧会话就撞上限。
- `closeConversation(id)`：停任务 + 释放 controller，**不删数据**；关掉最后一个时
  就地重置为空白对话（而非像 `closeTab` 那样拒绝）。
- `sessionList()` 在读取端叠加运行时键 `open/active/busy/hasDraft/unread`——不写进
  `m_sessionList` 快照，因为 busy/unread 随任务变化而数据库快照不变。任一键变化都发
  `sessionListChanged`。
- 上界 `kMaxOpenConversations = 12`，淘汰只针对 LRU 里**空闲且无草稿**的会话（其次
  是最久未用的空白对话）；活动/busy/有草稿的永不淘汰，全钉住时 `noticeOccurred` 拒绝。
- 草稿归属：有草稿的 controller 不能被就地换内容，否则草稿会串到另一条会话上。

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
| `SettingsService` | 本目录 | 强类型属性 + QSettings 直读直写（`SettingsRepository`）+ profile 管理 + DPAPI + 迁移；写成功发 `settingSaved(key)`，失败发 `persistenceError(message)` |
| `PreferenceRegistry` | 本目录 | 从 `SettingsSchema` 载入 `CategoryGroupMeta`/`CategoryMeta`/`PreferenceMeta`，提供分组与分类查询（`categoryGroups/categoriesInGroup/categoryGroup`）、元数据、默认值、`normalize/validate/coerce` |
| `PreferenceStore` / `EditBuffer` / `ChangeBus` | 本目录 | 偏好键 `prefs/v1/<key>` 的读写/批量写/重置 + 写穿缓冲 + 变更广播；`saveBatch` 成功发 `saved(keys)`，失败发 `persistenceError`，`resetBatch` 不发 `saved` |
| `SettingsCatalog` / `SettingsValueFormat` | 本目录 | 字段级目录 + 值快照。`SettingsValue::inspect/displayOf` 统一偏好与 M1 键的 current/default/modified/摘要（密钥只回答已配置/未配置）；`displayValue(key, value)` / `sameValueForKey` 让导入 diff 复用同一套格式化与比较口径。目录在其上提供 `describe/isModified/modifiedCount/resetValue` 与加权搜索（标题 100 > 关键词 60 > 键名 40 > 分类 20，带高亮）。`revision` 是 QML 绑定的失效源（偏好 `saved` + 服务 `settingSaved`） |

- `SettingsService` 管“AI 管线/安全/网络/日志/配额”这类**有强类型属性、会被 main.cpp 消费**的键（`agent/*`、`security/*`、`logging/*`、`webSearch/*`、`network/*`、`embedding/*`、`memory/*`、`projects/*`），键直接落 QSettings。
- `Preference*` 管“设置页可枚举、需要显示名/约束/重置”的偏好（`ui.*` 主题/字体/语言/布局默认、`general.*`、`window.closeToTray` 等），物理键加 `prefs/v1/` 前缀；`legacyKeys` 支持旧键迁移（如 `ui.themeMode` 由布尔 `isDark` 转换）。
- `EditBuffer` 是 `PreferenceStore` 的校验闸门：`stage()` 用 registry 约束校验后**立即** `saveBatch`（写穿），`dirty` 仅瞬时；校验失败保留暂存值供字段错误标签使用，`rollback()` 丢弃。写穿成功时记录"提交前的落盘状态"（`undoKey/undoHadPrevious/undoPrevious`，只保留最后一次；撤销点先于 `saveBatch` 登记，失败则恢复旧记录），`undoLast()` 有旧值写回、无旧值删物理键，且只回写一次；复位入口（`resetToDefault/resetAllToDefault`）与成功 `commit()` 清空记录，密钥/未登记键拒绝撤销。
- **复位口径**：行级复原与「恢复全部设置」经 `SettingsCatalog::resetValue()`。偏好走 `EditBuffer.stage(key, default)`，M1 键走 `SettingsService::setValueForSetting()`；`secret`（加密键、`network/proxyUsername|Password`）一律拒绝。网络页整页 `resetNetworkToDefaults()` 仍会清空代理凭据（页面确认弹窗已明示），不属于新增入口的语义。目录里**所有**有默认值且有 setter 的 M1 键都必须登记为 `kPage`（安全开关/黑名单、用量配额、默认权限、联网搜索 provider 已补齐）——登记成 `kNone` 会让行内圆点无复原按钮、导航徽标不计、底栏复位跳过它们，而「恢复全部设置」的确认文案又承诺会复位。`resetValue` 对偏好键以"staged 里是否残留"判定成功，写盘失败不再误报成功。
- **值比较**：`SettingsValue::sameValue` 对 `Double/Any` 类型（如 `security/readDenyList` 的 `QVariantList`）先按 metatype 直接比 `QVariant`。用 `toString()` 比较会让任何列表改动都判成"未修改"（`QVariant(QVariantList)` 的字符串恒为空）。
- **徽标代价**：`modifiedCount()` 的结果按 `revision` 缓存。导航每次写入都会让全部分类各查一次，不缓存就是十几遍全表 `inspect`。
- **保存回执契约**：`SettingsService::settingSaved` 只在 `persistValue`/`persist` 真正写盘成功后发出（装载/迁移不经过，故启动时不会误报“已保存”），参数是本次写入的键；`PreferenceStore::saved` 在 `ChangeBus` 通知后发出本次批量写入的键列表，`resetBatch` 刻意不发。设置中心底栏据此显示短暂「已保存」——经 `SettingsCatalog.entry(key).category` 只认当前分类的键，避免其他分类/后台写入在屏幕上闪回执。

### 6. 加密、备份与迁移

- API key（模型 profile / web search / embedding / 代理认证）用 Windows DPAPI `CryptProtectData`（范围 `NexusAI.apiKey`）加密，落盘前缀 `dpapi:v1:`；非 Windows 退化为明文。加密失败时 `persist()` 直接中止并 `persistenceError`，绝不静默写明文。
- 掩码契约：`apiKey()`/`webSearchApiKey()`/`embeddingApiKey()`/`proxyPassword()` 对 QML 恒返回空串，只暴露 `*KeySet`；真实值经非属性、非 invokable 的 `*Value()` 出口给 main.cpp（历史上 main.cpp 误用掩码 getter 导致 401/“未配置”）。
- profile 以一条 JSON blob（`modelProfiles`）+ `activeProfileId` 存储；`load()` 做 schema 版本迁移（v2 清空存量模型列表、v4 归一 contextTokens 默认值），旧单 provider 键迁移为 profile 后删除明文 `apiKey`/`provider`。
- `LogTail` 是唯一读日志文件的地方（`LogModel` 只读进程内环形缓冲）：只读尾部窗口，级别沿用 `Logger` 的 `severityRank`，续行继承上一行级别；QML 只负责展示与过滤参数，不要把日志内容缓存进单例。
- `SettingsBackup` 只导出**可移植**键：排除 `security/*`、`ui/*`、`encrypted`、`legacy`、`modelProfiles`、`agent/defaultPermissionMode`、`projects/activeId`、`tools/disabled` 等；格式 `nexusai-settings` v1，profiles 用白名单字段，MCP server 一律 `enabled=false`，导入后需重启。分类导出（`exportSettings(path, categories)`）是在白名单上再叠加分类过滤：导出键是物理键（偏好带 `prefs/v1/`），查目录前先换回逻辑键；`profiles` 归 `model`、`mcpServers` 归 `mcp`，未选中的段不读取、不校验（坏掉的 MCP 配置不会挡住纯模型导出）。`previewImport()` 另返回 `changes`（只含变化/新增键，≤200 条 + `changesTotal`/`changesTruncated`），格式与设置中心一致。
- 数据库备份/恢复是另一条链：`AgentVM.backupDatabaseNow/previewDatabaseBackup/restoreDatabaseFrom` 走 `SqliteMemory` 的 VACUUM INTO 快照，不经过 `SettingsBackup`。
- `PresetPackTransfer` 是**可分享的起步配置**，与备份的分工不同：格式 `nexus-preset-pack` v1，只带 `promptPresets` / `slashCommands` / `tools/disabled` / `agent/defaultPermissionMode` 四组可移植资产（预设只写 `{name,prompt}`、指令只写 `{name,templ,note}`，**id 与任何凭据/profile 都不导出**），导入后立即生效、无需重启。`previewPack()` 只读返回计数、同名冲突清单（≤200 + `conflictsTotal`）、`changes` 与 warnings；`importPack(path, policy)` 的 `overwrite/skip/rename` 只作用于同名条目（预设名/指令词，大小写不敏感、指令词归一），禁用工具与权限模式是单值全局配置始终应用。写盘走 `SettingsService::applyPromptAssets()`（两组列表一次落盘、失败回滚第一组）与 `ToolRegistry::setDisabledTools()`（过滤未注册工具、集合不变不写盘、失败恢复原集合）；资产 → 工具 → 权限模式任一失败按逆序回滚。包内工具名全部未注册时本机工具状态保持不变，权限模式必须是 schema 选项之一，否则整包拒绝。

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
- 成本/配额：`costChanged` 只转发活动标签；`quotaWarning` 由 controller 直接转发，`strictQuotaMode` 的拒绝逻辑在 agent 侧；`quotaAlert`（阈值提醒，未超限）在门面层按 `(日期, quotaType)` 去重后再发给 `main.cpp` 弹托盘气泡。

## 类清单（表格）

| 类名 | 角色 | 关键 QML 接口 |
| --- | --- | --- |
| `AgentViewModel` | 主门面（`AgentVM`）：打开集合管理 + 全局会话/项目/知识库/布局/工具/统计 | `startTask`、`stop`、`approveToolCall`、`openConversation`、`closeConversation`、`clearConversationUnread`、`loadSession`、`newChat`、`regenerate`、`editAndResend`、`branchFromAnswer`、`listProjectFiles`、`importDocument(s)`、`undoFileCheckpoint`、`databaseBackups`（最新在前的快照列表）、`deleteDatabaseBackup`（只删 `<app-data>/backups` 直接子文件）、`openDatabaseBackupsFolder`、`restoreDatabaseFrom`、`requestDataStats`/`pruneByRetentionNow`/`clearUsageNow`/`vacuumDatabaseNow`（docs/31 S4.5 数据管理页：统计走 QtConcurrent + 自包含短连接，`dataStatsReady` 回 GUI；清理/清空/VACUUM 复用 IMemory 虚接口）、`exportUsageCsv(path, rows)`（UTF-8 BOM + CRLF + RFC 4180 + 公式注入防护，QSaveFile）；属性 `state/traceModel/sessionList/tabModel/knowledgeDocuments/...` |
| `ChatSessionController` | 单标签会话状态与 agent 适配（非 QML 类型） | 无（C++ 侧：`attachAgent`、`startTask`、`stop`、`loadSession`、`permissionMode`） |
| `TraceModel` | trace 增量 `QAbstractListModel`（非 QML 类型） | 经 `AgentVM.traceModel` 使用；roles `id/type/title/status/args/result/timestamp/modelData`；`userMessageIndex()`（纯函数）产出左侧导轨的数据 |
| 视图偏好 | `AgentVM.showProcess`（持久化 `ui/showProcess`，默认 true） | 过程节点（思考/工具调用/结果/context）是否展开；TopBar 的开关写它，`AgentChatPage` 只读 |
| `MessageNavigator` 数据源 | `AgentVM.userMessageIndex`（`Q_PROPERTY`，NOTIFY `traceChanged`）= `[{row,messageId,preview,length,timestamp}]` | `row` 是 trace 行号而非数组下标；`length` 用正文全长（导轨线长靠它，用 preview 长度会让长消息与短消息画成一样长） |
| `LogModel` | 应用内日志列表模型（会话内追加、过滤/日期） | `AgentVM.logModel`（CONSTANT）；`filterLevel/filterText/filterCategory/filterDate`、`clearView()`、`toClipboardText()` |
| `ChangeBus` | 偏好变更广播总线 | `notify(key, value)`；信号 `changed(key, value)` |
| `EditBuffer` | 偏好校验写穿缓冲（单例） | `stage/commit/rollback/resetToDefault/undoLast`、属性 `dirty/staged/firstInvalidKey/validationError/canUndo/undoKey` |
| `SettingsService` | 设置与模型 profile 门面（`Settings`），DPAPI/迁移；三组用户自建文本资产（`promptPresets`/`slashCommands`/`sessionTemplates`，JSON blob 持久化，提示词与指令页管理，备份白名单自动携带；首次启动经 `maybeSeedPromptAssets()` 种入4 预设 + 4 指令 + 3 模板（固定 id `default-*`，键为 "[]" 视为已清空不回灌）） | `profiles/activeProfile`、`addProfile/updateProfile/selectModelForProfile`、`addPromptPreset/addSlashCommand/addSessionTemplate` 及各自 update/remove、`testProxy`、按领域拆分的 `resetAgentToDefaults/resetLoggingToDefaults/resetNetworkToDefaults/resetEmbeddingToDefaults/resetMemoryToDefaults/resetBackupToDefaults`（各自只回写本页 schema 键，`embedding/apiKey` 不随复位清空）、`setValueForSetting(key, value)`（目录单键复位入口，拒绝加密键与代理凭据）、`duplicateProfile(id, newName)`（复制除 `apiKey` 外的全部字段，新 UUID 插在源配置后，不改启用项）、`moveProfile(from, to)`（最终位置语义，只发 `profilesChanged`）、`applyPromptAssets(presets, commands)`（预设包导入的一次性替换：结构校验后两组列表各写一次，第二组失败恢复第一组）、`modelContextWindow`、信号 `settingSaved/persistenceError` |
| `SettingsCatalog` | 字段级目录 + 值快照（`SettingsCatalog` 单例） | `entries/entry/hasEntry/search/resetMode`、`describe/isModified/modifiedCount/resetValue`、属性 `revision`（偏好 `saved` + 服务 `settingSaved` 时 +1） |
| `SettingsValueFormat` | `SettingsValue::inspect/displayOf/displayValue/sameValueForKey` 纯函数层 | 统一偏好与 M1 键的 `SettingValueSnapshot{known,secret,configured,stored,modified,current,defaultValue}`；密钥/代理/联网搜索锚点只输出已配置状态；`displayValue` 渲染不在存储里的值（导入备份里的候选值），`sameValueForKey` 供导入 diff 判断是否变化；`ui.shortcuts` 的摘要只显示「N 项自定义 / 全部默认」，不把 JSON 原文塞进值行 |
| `SettingsBackup` | 可移植设置导入导出（不含凭据/SQLite）+ 诊断包 | `exportSettings(path, categories)`（空分类 = 全量）、`exportableCategories()`、`previewImport`（含逐键 `changes`）、`importSettings`、`exportDiagnostics(dir)`（返回新建目录绝对路径或空串）、`lastError`、`restartRequired`。导出 JSON 与诊断包的 `settings.json` 共用 `buildExportPayload(categories)` 的脱敏规则；诊断包另含 `logs/` 与 `environment.txt` |
| `PresetPackTransfer` | 可分享预设包（预设/指令/禁用工具/默认权限模式；无凭据与 profile，导入立即生效） | `exportPack(path)`、`previewPack(path)` → `{valid,error,presets,commands,disabledTools,permissionMode,currentPermissionMode,conflicts,conflictsTotal,changes,warnings}`、`importPack(path, policy)`（`overwrite/skip/rename`，默认由 UI 取 rename）、`lastError`；`parsePack()` 统一校验 format/version/大小/条目上限/包内重名/未知权限模式 |
| `LogTail` | 日志页「最近日志」的只读尾部读取入口（单例） | `read(path, maxLines, minLevel, keyword)` → `{ok,path,error,lines,matched,truncated,bytesRead}`；包住 `core/utils/LogTailReader`，未知/空级别按"全部"处理。刻意独立于 `AgentVM`（不碰会话/数据库，不需要 agent 栈），也不写任何设置键 |
| `PreferenceRegistry` | 偏好元数据注册表（来自 SettingsSchema） | `hasPreference/defaultValue/displayName/preferenceMeta/categories` |
| `PreferenceStore` | `prefs/v1/<key>` 持久化 + legacy 回退（单例） | `load/hasStoredValue/save/saveBatch/reset/resetBatch`、信号 `saved/persistenceError` |
| `ThemeManager` | 主题/字体状态与系统配色跟随；`reduceMotion`（无障碍，经 ChangeBus 订阅 `ui.reduceMotion`） | `themeMode/isDark/fontSans/fontMono/baseFontSize/reduceMotion` |
| `ThemeProvider` | 设计令牌门面（`Theme`，纯头文件、颜色/间距/圆角/字号/动效/时间格式化）。字号档位 `fontSizeNano/Micro/Caption/Body/H2/H1` 全部随「基础字号」缩放（各有下限），QML 不再写死 `font.pixelSize`；动效统一取 `motionFast/Base/Slow`（`NOTIFY motionChanged`，「减少动效」开启时归零——绝不能改回 CONSTANT，32 处绑定靠它即时刷新） | `bgPrimary/textPrimary/brandPrimary/...`、`fontSizeNano/...`、`motionFast/Base/Slow`、`formatDateTime/formatTime` |
| `FontProbe` | 字体可用性与平台默认字体探测 | `isAvailable/isFixedPitch/systemDefaultSans/systemDefaultMono/availableFamilies/fixedPitchFamilies`；等宽集合在 C++ 侧缓存（`isFixedPitch` 会加载字体文件，搜索框逐字过滤会反复触发） |
| `AppInfo` | 关于页的构建信息与数据目录（docs/31 N10） | `appVersion/qtVersion/compiler/architecture/osVersion/dataDir/licenses`（全部 CONSTANT）+ `openDataDir()`；值来自 `QCoreApplication`、`QT_VERSION_STR`、`QSysInfo`、`QStandardPaths`，页面不得自行计算 |
| `ShortcutMap` | 全局快捷键动作表 + 用户覆盖（`ui.shortcuts`，docs/31 N2） | 12 个动作的 `bindings`（`main.qml` 的 `Shortcut` 专用）、`actions`（标题/描述/默认与当前序列/`modified`，设置页与速查表共用）、`modifiedCount`、`recording`；`assign(actionId, sequence)` 做 PortableText 规范化 + 裸键/纯修饰键拒绝 + 与其它动作**当前生效**序列查重，返回 `{ok,error,conflict}`；`resetAction/resetAll` 删覆盖项（全空时连存储键一起 `reset`）；`sequenceFromKey(key, modifiers)` 供 QML 录制转 PortableText。改回默认值即删键，存储里只会留下真正的自定义项 |
| `Onboarding` | 首次启动向导的触发判定与标记（docs/31 N1） | `shouldShow()` = `general.onboarded` 未写且 `Settings.profiles` 为空；`complete()`（完成/跳过共用，已写过不重复写盘）、`isComplete()`。只做判定与落盘，三步交互在 `OnboardingDialog.qml` |
| `LocaleProbe` | 可用语言枚举（`:/i18n` 资源扫描） | `availableLocales()` |
| `I18nManager` | 翻译器装载与运行时切语言（`I18n`） | `currentLocale/availableLocales`、`applyLocale(locale)` |
| `ConnectionTester` | 一次性最小请求测试连接 + 从端点拉取模型列表（不动实时管线） | `test(protocol, baseUrl, apiKey, model)`；`fetchModels(protocol, baseUrl, apiKey)`（独立 GET，20s 超时，按协议选列举端点与鉴权：OpenAI 系 `/models` Bearer、Anthropic `/v1/models` x-api-key、Gemini `/v1beta/models?key=`、Ollama `/api/tags`）；静态纯函数 `modelsUrl` / `parseModelList`（均可无网络单测）；信号 `testFinished(ok, message)`、`modelsFetched(ok, models, message)` |
| `GitViewModel` | 异步 Git 门面（`GitVM`）：只读历史/分支/blame + 显式写操作 | `refreshStatus`、`requestRepoLog/DiffHunks/Blame/Graph`、`stageFiles/commitStaged/undoLastCommit/discardPath/stashPush` |
| `ProjectFileViewModel` | 右侧栏文件浏览门面（`ProjectFileVM`）：一层目录懒读 + 预览 + 打开，按 relPath 缓存（切根失效、失败不缓存），只读无写 | `children/preview/absolutePath/clearCache`、`openInDefaultApp`（清 `ELECTRON_RUN_AS_NODE`）、`openWithKnownEditor`（注册表探测 MarkText/VSCodium/VSCode 的 shell\open\command 应急拉起）、`openAbsolute`（**按 scheme 分流**：http(s) → 浏览器 openUrl，本地 → fromLocalFile——URL 源此前走 fromLocalFile 得无效 URL 静默打不开；判定与执行分离为 `openAbsoluteRoute` 探针，GUI-less 测试可断言路由）、`canonicalAbsolute`（知识库源文件用，不受项目根约束） |
| `CommitMessageDrafter` | 一次性“从暂存 diff 起草提交信息”（`CommitDrafter`） | `draft(stagedDiff, recentSubjects)`、`cancel()`；信号 `drafted` |
| `McpManager` | MCP server 运行时管理（`McpManager`）：连接/工具桥接/持久化 | `addServer/updateServer/removeServer/setEnabled/reconnect`、`servers`（含 `command/args/url/env/headers` 供编辑预填，已连接项另带 `tools` 工具名列表供卡片预览）、`lastError`；`addServer` 返回 `bool`（重名/空名直接 false + lastError），`updateServer(原名称, 配置)` 在原条目上改名并重建 client（保留启用状态，缺省不改）；`exportServers(path)` / `previewServerImport(path)` / `importServers(path, policy)` 返回 map（成功与错误同口径），导入后整表落盘、停用未启用项并写活动记录 `mcp.config.export/import` |
| `CodeHighlighter` | 代码块正则高亮 + 语言识别（QML 元素，非单例）。语言来源二选一：`sourceName` 扩展名映射（文件预览；未知扩展名保持纯文本不猜），无名字走内容启发式（聊天代码块）；`visualLineSpans(wrapWidth)` 从 `QTextLayout` 取每逻辑行视觉行数（须显式传视口宽，惰性布局需内部自建且设 WrapAnywhere）；单行 >8KB 跳过高亮 | `document/text/sourceName/languageName/visualLineSpans/hasHighlightedText` |
| `CitationParser` | `[文档名]` / `[doc:名#chunk]` 解析唯一实现 | `parse(text)`、`hasCitation(segments)` |
| `PdfTextExtractor` | 自由函数 `extractPdfText` + `PdfExtractStatus`/`PdfExtractResult` | 无（C++/测试使用） |

## 数据与控制流

1. **发送与流式**：QML Composer → `AgentVM.startTask(text, images, files)` → `configureModel(active())` + 图片压缩 → `ChatSessionController::startTask`（历史环/Usage）→ `AIAgent::startTask` → `stateChanged` / `traceChanged`（全量 JSON）→ `TraceModel::applyDelta` → 门面仅对活动标签 `emit traceChanged` → ListView 只收到插入/变更行。
2. **工具确认**：`AIAgent::toolConfirmRequired` → controller → VM 队列 + `pendingConfirmation` + 全局信号 → 确认对话框 → `AgentVM.approveToolCall(id, ok)` → 按 id 找回 controller 并校验 `pendingConfirmationId` → `AIAgent::approveToolCall`。
3. **会话切换/恢复**：Sidebar → `openConversation(sessionId)`（docs/30；旧的 `loadSession` 保留为内部入口，搜索定位 `revealMessage` 走它）→ 已被承载则 `activateTab`，否则就地复用空闲无草稿的 controller / 守上限后新建 → 按 `sessionList` 查到的 `projectId` 先 `applyProjectTo` 再 `controller.loadSession`（busy 时拒绝）→ `AIAgent::resumeSession` → `sessionChanged`/`traceChanged` → 重建 trace、checkpoints、刷新共享会话列表，清未读并推进 LRU。
4. **会话派生操作**：`newChat`→`agent.resetSession`；`regenerate`→`agent.regenerateLast`；`editAndResend`→`agent.editLastUserMessage`（整轮替换，含持久化）；`branchFromAnswer`→`memory.forkSessionAtAnswer(sessionId, summary 的 rank, 唯一标题)`→`refreshSessions`→`loadSession`（源会话不动）。
5. **项目与 `@` 引用**：`importProject(path)` → canonicalize → `memory.createProject` → `refreshProjects` → 仅当活动标签尚无会话时绑定归属；`listProjectFiles(query)` 读 30s TTL 缓存，过期则后台 `ProjectFileIndex::scan`；`resolveFileReferences(text)` 把 `@path` 变成 `{kind:"file",name,content}` 附件；`inspectFileReferences` 逐 token 返回 `no-project/missing/outside/sensitive`。
5b. **项目规则（G3/R3）**：设置页经 `loadProjectRules` / `writeProjectRules`（失败发 `noticeOccurred`，成功后自动重挂监听）/ `projectRulesState`（与注入侧同一回退顺序，复用 `AIAgent::isUntouchedRulesSeed`）读写 `AGENTS.md`。外部编辑协调：`watchProjectRules` / `unwatchProjectRules` 以 `QFileSystemWatcher` 监听两个候选文件（只存在者可挂），变化发 `projectRulesFileChanged(root)`——**异步投递受 Windows 文件通知延迟影响（AV 可扣数秒甚至更久），页面按内容比较分流，迟到/重复/自身写入都安全**；`watchedRulesFiles` 是挂载集探针（同步断言用，不测异步投递）。三个 Q_INVOKABLE 追加在类尾（AOT 索引约定）。
6. **检查点/撤销**：agent 完成工具或撤销后发 `fileCheckpointsChanged` → controller → VM（仅活动标签更新属性）→ `undoFileCheckpoint(id)` 转 `AIAgent::undoFileCheckpoint`；`fileCheckpointDetail` 返回 JSON 详情。
7. **知识库**：`importDocument` 文本同步读取 / PDF 后台提取 → `importExtractedText`（内容 hash 去重 → `uniqueDocumentName`）→ `startIngestion`（QtConcurrent）→ `RagService::documentPersisted`（main.cpp，队列到 GUI 线程）→ `SqliteMemory`；`RagService::changed` → `refreshKnowledge`（先更新属性再 NOTIFY，避免 QML 读到旧列表）。
8. **设置写路径**：QML → `SettingsService` setter → `persistValue/persist` → `SettingsRepository`（QSettings，org/app 均为 `NexusAI`）→ `settingSaved/persistenceError` 信号 → main.cpp 的 `applySettings` 把步数/超时/只读/沙箱/代理/搜索/embedding 重新注入所有 agent。偏好路径：QML → `EditBuffer.stage/commit` → `PreferenceStore.saveBatch` → `prefs/v1/` → `ChangeBus.notify` → `saved/persistenceError` 信号与 `ThemeManager`/`I18nManager`/`AgentVM` 各自订阅。
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
- **QML**：`import NexusAI.ViewModels` 后直接使用 `AgentVM`、`Settings`、`GitVM`、`McpManager`、`Theme`、`ThemeManager`、`I18n`、`PreferenceStore`、`PreferenceRegistry`、`EditBuffer`、`ChangeBus`、`SettingsBackup`、`ConnectionTester`、`CommitDrafter`、`FontProbe`、`LocaleProbe`、`LogTail`、`AppInfo`、`ShortcutMap`、`Onboarding`。`main.qml` 绑定 `AgentVM.sidebarWidth/panelWidth/isPanelVisible`、`Theme.*`，12 个窗口级 `Shortcut` 的 `sequence` 全部来自 `ShortcutMap.bindings[...]`（唯一事实源，录制期间统一 `enabled: !ShortcutMap.recording`），窗口几何经 `loadWindowGeometry/saveWindowGeometry`；启动完成时 `Onboarding.shouldShow()` 为真则 `Qt.callLater(openOnboarding)`。
- **core**：`AIAgent`（状态机/工具确认/trace/会话/checkpoint）、`IMemory`/`SqliteMemory`、`RagService`、`GitService`、`ToolRegistry`、`Sandbox`、`SettingsRepository`/`SettingsSchema`、`Logger`、`Activity`、`ProjectFileIndex`、`AttachmentClassifier`。
- **infrastructure**：`IAIService`/`LlmProtocolRegistry`（`ConnectionTester`、`CommitMessageDrafter`、main.cpp 的 ProviderSet）、Ollama/OpenAI embedding。
- **测试**：`tests/test_viewmodel.cpp` 覆盖 TraceModel diff/末行定位、输入历史、拖放分类、标签独立性、项目归属隔离、批量导入状态/取消、分叉、导出、LogModel 过滤；`tests/test_settings.cpp` 覆盖 profile 归一/DPAPI 代理认证、schema 覆盖与版本戳、权限默认、上下文窗口优先级、保留/配额；`tests/test_shortcuts.cpp` 覆盖快捷键默认表、落盘/改回默认删键、冲突与不安全键拒绝、行级/全部复位、外部复位、坏 JSON 回落、键事件转 PortableText；`tests/test_onboarding.cpp` 覆盖首次启动触发、完成/跳过标记只写一次、已有 profile 不触发、隐藏偏好与备份排除。

## 关键文件

| 文件 | 行数 | 说明 |
| --- | --- | --- |
| `AgentViewModel.h/.cpp` | 954 / 2917 | 主门面、标签、项目、知识库、备份恢复、@引用、附件 |
| `SettingsService.h/.cpp` | 627 / 2174 | profile/DPAPI/迁移/代理探测/自动启动/复制排序/预设包资产落盘 |
| `GitViewModel.h/.cpp` | 187 / 656 | 异步 git 门面 + 四代计数器 + 单写闸门 |
| `ChatSessionController.h/.cpp` | 158 / 310 | 单标签会话状态、agent 信号接线、历史环 |
| `SettingsBackup.cpp` | 641 | 可移植键筛选、分类导出、导入差异、白名单 profile、回滚、诊断包 |
| `PresetPackTransfer.h/.cpp` | 57 / 458 | 预设包格式/校验/预览/冲突策略/回滚编排（无凭据、立即生效） |
| `McpManager.cpp` | 563 | 连接/工具桥接/延迟退役/退出回收/配置导入导出门面 |
| `CodeHighlighter.cpp` | ~420 | 语言启发式 + 扩展名映射 + 五类 token 着色 + 单行预算 |
| `LogTail.cpp` | 49 | 日志尾部读取的 QML 门面（级别名 → `severityRank`） |
| `AppInfo.cpp` | 73 | 版本 / Qt / 编译器 / 架构 / 数据目录 / 许可清单（只读） |
| `ShortcutMap.cpp` | 307 | 快捷键动作表、覆盖项读写、冲突/不安全键校验、键事件转 PortableText |
| `Onboarding.cpp` | 45 | 首次启动判定（无配置 + 未标记）与 `general.onboarded` 落盘 |
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
