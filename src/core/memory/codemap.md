# src/core/memory/ — 持久化架构地图

> 代码事实来源：`IMemory.h`（422 行）、`SqliteMemory.h`（303 行）、`SqliteMemory.cpp`（3818 行）。
> 本文只描述本目录的契约与实现，数字/行为均取自代码。

## 职责

- 定义应用唯一的持久化抽象 `IMemory`，并由 `SqliteMemory` 用单文件 SQLite 实现。
- 负责：会话与消息、时间线（trace）节点、episodic 记忆、知识库文档与分块向量、工具审计、token/成本明细、
  结构化 usage 事件、文件写前检查点、项目归属、数据库备份/巡检/恢复、各类保留期清理。
- 不负责：向量化本身（`RagService`/`KnowledgeChunk` 提供 chunk 与 embedding）、UI 文案与布局、
  LLM 上下文组装（`AIAgent` 消费本模块数据后自行决定注入策略）。
- 默认库文件：`QStandardPaths::AppDataLocation + "/nexusai_memory.db"`；构造参数可覆盖（测试用 `:memory:`）。

## 数据模型（表结构表格）

单库 16 张表（含 2 张 FTS5 虚拟表），全部由 `createTables()` 以 `IF NOT EXISTS` 幂等建立。

| 表 | 主键 / 键 | 关键列 | 用途 |
|---|---|---|---|
| `messages` | `id` AUTOINCREMENT | `session_id, role, content, timestamp, metadata, workspace_id` | 线性会话历史；`metadata` JSON 内嵌 `images`/`files`（无独立列） |
| `messages_fts` | FTS5 `rowid`=`messages.id` | `content`，`tokenize='trigram'` | 跨会话全文检索；触发器 `messages_fts_ai/_ad` 同步，无 update 触发器 |
| `sessions` | `id` TEXT（UUID） | `title, created_at, updated_at` + 迁移补列 `project_id, pinned, archived, deleted_at` | 侧栏会话列表；软删除与归档 |
| `projects` | `id` TEXT（UUID） | `name, root_path, created_at` | 导入文件夹即项目；不是目录表，只存根路径 |
| `episodic` | `id` AUTOINCREMENT | `session_id, summary, tags, embedding BLOB, timestamp` | 长期记忆条目；索引 `idx_episodic_ts`、`idx_episodic_session` |
| `episodic_fts` | 外部内容 FTS5（`content='episodic'`） | `summary, tags` | 摘要检索；触发 `episodic_ai/_ad`，`rowid` 对齐 `episodic.id` |
| `trace_nodes` | `(session_id, node_id)` | `node_order, type, title, status, args, result, reasoning, timestamp, message_id, metadata, workspace_id` | 恢复会话时的可视化时间线（thinking/tool/summary） |
| `file_checkpoints` | `id` AUTOINCREMENT | `path, before_content BLOB, existed, truncated, byte_size, request_id, created_at, undone_at, diff_text, added_lines, removed_lines, after_hash` | write_file/edit_file 写前快照，支持单条/按文件组撤销 |
| `knowledge_documents` | `name` TEXT | `content, imported_at, content_hash, embedding_model, source_path` | 知识库原文 + 增量重建判据 + **源文件路径**（2026-10-24：导入记录绝对路径；upsert 对空路径 COALESCE 保护旧值；注意空 QString 经 addBindValue 绑定为 SQL NULL，NOT NULL 列须绑显式空串） |
| `knowledge_chunks` | `(doc_name, chunk_index)` | `chunk_total, text, embedding BLOB, dim, pseudo, page` | 分块与向量；`page=-1` 表示无页码 |
| `audit_log` | `id` AUTOINCREMENT | `session_id, event, tool_name, arguments, success, detail, timestamp, request_id, latency_ms, tokens, model, workspace_id` | 工具/请求审计；`arguments` 超 10000 字符截断后落库 |
| `token_usage` | `id` AUTOINCREMENT | `request_id, session_id, model, timestamp`, 3 方向 × 多来源 token 列, `cost_cents, latency_ms` | 每次 LLM 请求一行；聚合统计的唯一精确来源 |
| `usage_log` | `id` AUTOINCREMENT | `ts, category, action, target, request_id, detail` | `Activity` 门面的操作/网络事件流水（仅元数据） |
| `model_pricing` | `model` TEXT | `input/output/cache_hit_price_cents, effective_date, notes` | 单价（分/1M token），启动时 `INSERT OR IGNORE` 预置 11 个模型 |
| `scheduled_tasks` | `id` TEXT（UUID） | `name, prompt, profile_id, profile_name, schedule_kind, schedule_value, permission_mode, enabled, notify, last_fired_at, created_at, updated_at` | docs/50 定时任务定义；`schedule_value` 存 `ScheduleSpec::toJson()` 紧凑串，编辑不洗 `last_fired_at`/`created_at` |
| `scheduled_task_runs` | `id` AUTOINCREMENT | `task_id, session_id, status, trigger_source, started_at, finished_at, error, summary` | 执行历史；每任务保留最近 30 条（插入时按 id 清理），删任务级联删除；`trigger_source` 避开 SQL 关键字 TRIGGER |

索引：`idx_messages_session_ts(session_id,timestamp)`、`idx_sessions_pinned_updated(pinned,updated_at)`、
`idx_episodic_ts`、`idx_episodic_session`、`idx_audit_ts`、`idx_trace_nodes_session(session_id,node_order)`、
`idx_file_checkpoints_session`、`idx_usage_ts`、`idx_token_usage_ts/model/session`、
`idx_scheduled_tasks_enabled`、`idx_scheduled_task_runs_task(task_id,started_at,id)`。
已退役索引 `idx_messages_session_parent` 在启动时 `DROP`。

时间语义：所有时间戳均为 `QDateTime::...toMSecsSinceEpoch()`（**本地时区的 epoch ms**）；按天聚合用
`date(timestamp/1000,'unixepoch','localtime')`。

## 设计（模式/抽象/关键类型）

- **接口隔离 + 可选能力默认实现**：`IMemory` 只有约 20 个纯虚函数，其余 40+ 能力（trace、项目、检查点、
  用量、备份、恢复、软删除…）都以 **默认 no-op / 空结果** 的虚函数给出，注释明确说明「保持替代后端可编译」。
  因此新增能力通常不改破坏性契约，但也意味着**漏实现不会编译失败，只会静默降级**。
- **单实现、单连接、连接名唯一**：每次 `SqliteMemory` 实例 `addDatabase("QSQLITE", "nexus_memory_<uuid>")`，
  析构 `close → 置空句柄 → removeDatabase`（MEM-702 修过连接泄漏）。并行会话页各持一个实例连同一文件。
- **PRAGMA 基线**：`busy_timeout=5000`、`journal_mode=WAL`、`synchronous=NORMAL`；WAL 使读不阻塞写，
  但 `.db` 旁有 `-wal/-shm`，因此备份必须走 `VACUUM INTO` 而不是文件拷贝。
- **版本闸门是粗粒度整数**：`PRAGMA user_version`，现状为 `3`。只有 `0`（老库/新库）才做 FTS 回填与
  `ANALYZE`；只有 `<3` 才跑消息树拉平。其余迁移靠 `PRAGMA table_info` 探测列是否已存在，**每次打开都执行**，
  以幂等换取无需逐版本脚本。
- **JSON/Map 作为边界格式**：读接口返回 `QJsonObject/QJsonArray`（消息、trace、episodic）或
  `QVariantMap/QVariantList`（统计、搜索、项目），键为驼峰；写接口接受 `QJsonObject`/`QVariantMap`。
  列不是整段 JSON（trace_nodes 是列映射表），读端必须重建形状并做字段白名单提升。
- **会话模型 = 线性历史 + 分叉新会话**：树形版本（`parent_id`/`head_message_id`/`message_variants`）已被
  `migrateConversationModel()` 一次性拉平；分叉是「复制到锚点为止的消息 + 按序号切 trace」生成新 UUID 会话。
- **软删除 + 硬回收**：`deleteSession` 只写 `deleted_at`；查询一律带 `COALESCE(deleted_at,0)=0`；
  到期由 `purgeDeletedSessions` 在一个事务里对每条会话执行 5 条 DELETE。
- **检索双路径**：关键词 ≥3 字符走 trigram FTS（整词加双引号当短语，CJK 可子串命中）；<3 字符退化
  `LIKE '%kw%'` 并先 `COUNT(*)` 限流行数。snippet 由 C++ 管线生成（去 markdown → 压平空白 → 居中窗口 →
  最后注入 `U+0001/U+0002` 高亮标记）。
- **向量存储为 float32 BLOB**：`embeddingToBlob/embeddingFromBlob` 直接 memcpy；语义检索对**最新 500 条**
  （`kSemanticCandidateCap`）做内存余弦重排，返回带 `score`。
- **金额整数化**：价格单位「分/1M token」，`kPerMillion` 做整数向上取整，避免浮点误差；
  `PricingCalculator::refreshCache(m_db)` 在 `initDatabase()` 末尾载入进程级缓存。

## 数据与控制流（典型调用序列）

1. **启动 / 建库**：构造函数 → `openConnection()` → PRAGMA → `initDatabase()`：
   `createTables` → 各列迁移 → `migrateConversationModel(user_version)` → `migrateKnowledgeSchema`
   → FTS 迁移 → `seedModelPricing` → `refreshCache` → 建索引 →（`user_version==0` 时 `ANALYZE`）→ `user_version=3`。
2. **写一轮对话**：`beginSession(id,title)`（`INSERT OR REPLACE`）→ `saveMessage`（返回新 `id`，失败 0；
   空 `sessionId` 直接 0）→ FTS 触发器同步 `messages_fts` → 流式过程中 `saveTraceNode`（同一 `node_id` 幂等
   upsert，`node_order` 只在首次插入生效）→ `touchSession` → `logAudit("task_complete", …, tokens, model)`
   + `logTokenUsage({...})`。
3. **恢复会话**（`AIAgent::resumeSession`）：重置内存态 → 注入 system prompt →
   `loadMessages(id, 200)`（内层 DESC 取最新窗口、外层 ASC 还原顺序；只保留 user/assistant，丢弃空 assistant）→
   `loadTraceNodes(id)`（`ORDER BY node_order ASC`）→ trace 为空时用消息文本重建 user_message/summary →
   从最后一条 `summary` 节点恢复 `m_lastResponse` → 上下文超限则立即压缩。
   **工具证据只进时间线，不回灌 LLM 上下文**（MEM-701 契约）。
4. **跨会话搜索**：`searchMessages(kw, limit, includeArchived)` 两趟：第一趟按 `session_id` 分组取
   `COUNT(*)`/`MAX(timestamp)` 并 `ORDER BY MAX(timestamp) DESC`（`limit` 限制会话数）；第二趟用
   `ROW_NUMBER()` 同一次扫描算出「user 优先的最佳片段」与「最新命中 id」，再 JOIN 取正文，C++ 侧统一生成 snippet。
5. **截末轮 / 分叉**：`truncateLastTurn` 在**一个事务**内删 `node_order >= 最后 user_message 节点`
   的 trace 再删消息；`forkSessionAtAnswer` 先建会话，再在事务内复制 `messages(id<=anchor)`、按
   `summary` 序号切 `trace_nodes`、继承源会话 `project_id`，失败则 `hardDeleteSessionRows(newId)` 清理半成品。
6. **清理**：`main.cpp` 启动即跑一次 `runPrunes`，并挂 6 小时 `QTimer`：`pruneUsage(30,10000)`、
   `pruneAudit(auditRetentionDays=30)`、`pruneTokenUsage(tokenRetentionDays=90)`、
   `pruneFileCheckpoints(now-30d)`、`purgeDeletedSessions(now-30d)`。`keepDays<=0` 表示永久保留。
   docs/31 S4.5：`pruneAudit`/`pruneTokenUsage` 提升为 `IMemory` 虚接口，数据管理页可按保留天数**立即**触发；
   同批新增 `clearTokenUsage()`（清空 token_usage）与 `vacuumDatabase()`（本连接 VACUUM，
   其它连接持锁时 BUSY 返回 false，调用方提示稍后重试）。
6b. **统计**：`SqliteMemory::collectStats(dbPath)` 静态自包含只读短连接（同 createBackupFile 的
   做法），返回 DB/WAL 体积 + 12 张表行数；绝不触碰 GUI 线程连接，由
   `AgentViewModel::requestDataStats()` 经 `QtConcurrent` 调度、`dataStatsReady` 回 GUI。
7. **备份/恢复**：启动后台 `QtConcurrent::run` → `createBackupFile(dbPath, auto_yyyyMMdd_HHmmss.db)`
   （独立只读连接 + `VACUUM INTO ?`，失败删除半成品）→ `rotateAutoBackups(keepCount=3)`（只动 `auto_*.db`，
   `manual_*` 永不删除）。恢复：`inspectBackupFile` 预览（sessions/messages/knowledgeDocs/newestTimestamp/
   `integrityOk`）→ `restoreFromBackup` 八步：关连接（WAL checkpoint 后旁文件消失）→ `quick_check` →
   复制 `.pre-restore` 锚点 → 暂存 `.restore-tmp` → 删 `-wal/-shm` → remove+rename 换文件 → 重开并重跑
   初始化 → 删锚点；任一失败回滚并尽力重开。

## 集成点（被谁消费）

- `main.cpp`：创建主 `SqliteMemory`（GUI 线程），为每个标签页 `setOwnedMemory(std::make_unique<SqliteMemory>())`；
  连接 `documentPersisted/documentRemoved/knowledgeCleared` → 知识库落库；`Activity::setUsageSink` 用
  `Qt::QueuedConnection` 把任意线程的事件投递到 GUI 线程写 `usage_log`；启动自备份与清理定时器。
- `AIAgent`：持有 `IMemory*`（可 `setMemory` 外部注入或 `setOwnedMemory` 自持），负责 `beginSession`/
  `saveMessage`/`saveTraceNode`/`truncateLastTurn`/`logAudit`/`logTokenUsage`/`addFileCheckpoint`，
  以及 `resumeSession` 的加载顺序。
- `EpisodicService`：`saveEpisodic`、`searchEpisodic` + `searchEpisodicSemantic`（合并打分）、
  `updateEpisodicEmbedding`（异步补写向量）。
- `AgentViewModel`（`setMemory(IMemory*)`）：会话列表/置顶归档重命名删除恢复、项目 CRUD、搜索与导出、
  备份/巡检/恢复、episodic 管理、usage 时间线、全部 token/成本/配额查询、按答案分叉。
- `ChatSessionController`：切会话时触发 `resumeSession`，并把 token 统计转发给 UI。
- `RagService` / `KnowledgeChunk`：知识库持久化契约（`upsertKnowledgeDoc` + `replaceKnowledgeChunks`）。
- `PricingCalculator`：反向读取 `model_pricing` 供成本拆分。
- 编译：`CMakeLists.txt` 仅将三文件列入主 target（无独立库、无 moc）。
- 测试：`tests/test_memory.cpp`（`NEXUSAI_TESTING` 下暴露 `databaseForTest()`），断言覆盖
  WAL、索引存在、老库回填/重盖章、FTS trigram 升级、检查点预算退化、软删/清理原子性、备份往返、
  损坏快照拒绝、snippet 规则、分页与聚合一致性等。

## 关键文件

| 文件 | 内容 |
|---|---|
| `IMemory.h` | 抽象契约与全部可选能力默认实现；语义注释（范围窗口、分页并发、检索聚合）在此定义 |
| `SqliteMemory.h` | 具体类声明、私有迁移/建表函数、`KnowledgeDocRecord`、静态备份 API |
| `SqliteMemory.cpp` | DDL、迁移、全部查询与事务、检索管线、备份/恢复实现 |
| `../../tests/test_memory.cpp` | 行为契约的可执行规格（只读参考，不在本目录） |
| `../../CMakeLists.txt` | 三文件编入主 target |

## 约束与风险

- **连接线程绑定**：`QSqlDatabase` 只能在创建它的线程使用；本模块不加锁、不跨线程。所有写路径必须留在
  GUI 线程（`Activity` 用 queued invoke 满足；注释明确检查点「GUI thread only」）。
- **多连接同一文件**：并行标签页各一连接，靠 WAL + 5s `busy_timeout` 缓解 `SQLITE_BUSY`；长事务
  （大批量 purge、检查点写入）仍可能让其他页超时。
- **恢复的前置条件靠调用方**：`restoreFromBackup` 假定没有其他连接打开该文件（UI 需先关闭所有标签页）；
  代码中没有任何强制检查，误用会把其他实例的连接指向被替换的 inode/文件。
- **`beginSession` 用 `INSERT OR REPLACE`**：REPLACE 是删行再插行，未列出的列回到默认值——对已存在的
  会话重复调用会重置 `pinned/archived/project_id/deleted_at`。当前调用点只在新建会话/分叉时使用，属隐含约定。
- **非事务的复合写**：`removeProject`（先 detach 再删项目）、`removeKnowledgeDoc`（先删 chunk 再删文档）、
  `beginSession`+首批消息均无事务；中途失败会留下部分状态。
- **嵌套事务不可用**：`clear()` 与 `purgeDeletedSessions()` 在 `transaction()` 失败（已在事务中）时退化为
  逐条自动提交，注释承认这是「legacy fallback」，原子性不再保证。
- **`deleteMessagesFrom` 不清理 trace_nodes**：只有 `truncateLastTurn`/`clear`/`hardDeleteSessionRows` 删轨迹；
  直接按 id 删消息会留下悬挂 `message_id` 的节点（恢复时可能显示幽灵条目）。
- **硬删不覆盖审计域**：`hardDeleteSessionRows` 只删 messages/episodic/sessions/trace_nodes/file_checkpoints，
  `audit_log`/`usage_log`/`token_usage` 仍保留会话 id 片段，仅靠保留期清理。
- **检索降级是静默的**：DDL 失败只 `qWarning`（无 FTS5 的构建会失去全文检索）；<3 字符且行数超
  `50000`（messages）/`10000`（episodic）时，消息搜索返回 `sessionId="__search_notice__"` 的提示行，
  episodic 搜索直接返回空；语义检索只看最新 500 条，更早条目不可达（有显式 warning）。
- **分页漂移**：`tokenUsageRecords` 用 `LIMIT/OFFSET`，翻页期间新写入会让窗口右移、已展示行在下一页重复
  （`IMemory.h` 已声明，长期方案是锚定分页）。
- **迁移每次启动都跑**：列探测虽幂等，但 `ALTER TABLE`/虚表重建不在同一事务里；`migrateEpisodicFts`
  需靠 `finish()`/作用域规避活动语句导致的 `SQLITE_LOCKED`，改动时极易回归。
- **文档漂移**：`SqliteMemory.h` 仍写着 episodic「currently NOT wired into the agent」（MEM-703），
  但 `AIAgent` 已通过 `EpisodicService` 注入摘要（docs/25 B3），以代码为准。
- **口径双源**：`tokenStats()` 读 `audit_log.task_complete`，`tokenStatsDetailed/costStats/quotaStatus`
  读 `token_usage`；两者独立写入，任何一侧漏记都会让同一页面上的数字互相矛盾。
- **清理数字澄清**：任务简报提到的「10 天 / 30 分钟」在本目录代码中不存在；实际保留策略为上文的
  30 天 usage/30 天审计/90 天 token/30 天检查点/30 天软删窗口，调度间隔 6 小时，均可在设置中改
  （审计与 token 保留天数支持 0 = 永久，上限 3650 天）。
