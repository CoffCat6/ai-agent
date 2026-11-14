# 51 - 知识库 RAG 与项目文件变更联动（增量导入）

| | |
|---|---|
| **状态** | **批 A 已实施（2026-11-14）**，含一处设计修订（见 D3 备注）：批 A/B 合并为文件级 watcher 方案 |
| **范围** | 已导入知识库、且带 `source_path` 的文档，在源文件变化后保持同步 |
| **前置** | 全量只读调研（RagService / DocumentRetriever / SqliteMemory knowledge 表 / ProjectFileIndex / file_checkpoints），见 §2 |

## 1. 背景与目标

知识库的文档导入是**一次性快照**：导入后源文件怎么变，知识库都停留在导入那一刻。
真实使用中两类高频场景会立刻踩到：

1. **Agent 改了项目文档**（写类子代理 propose_edit / edit_file 更新 README、设计
   文档、代码注释），下次 `search_knowledge_base` 检索到的仍是旧内容——模型拿旧
   知识答新问题，用户难以察觉；
2. **用户在外部编辑器改了已导入的文件**，知识库同样无感知。

目标：源文件变化后，知识库文档**可发现、可更新、不重复**；未变化的文档零开销
（不重嵌入）。明确**不做的**：目录级递归监听、自动导入新文件（仍由用户显式导入，
见 §7）、全库自动重嵌。

## 2. 现状盘点（为什么不能直接"再加个 watcher"）

- **导入管线**：`AgentViewModel::importExtractedText`（AgentViewModel.cpp:2636）
  → 内容 SHA-256 去重 → `startIngestion` → `RagService::ingestDocument`
  （RagService.cpp:195）→ `documentPersisted` → `memory->upsertKnowledgeDoc` +
  `replaceKnowledgeChunks`。**同内容跳过；同路径不同内容 → `uniqueDocumentName`
  改名共存**（RagService.cpp:90）——同一磁盘文件改内容后重导会变成两个文档。
- **路径感知**：`Document.sourcePath` 只是个字符串（RagService.h:145），无
  mtime、无 size、无任何再同步逻辑；`knowledge_documents` 表有 `source_path`
  列但无索引、无变更字段。
- **文件监听**：全仓唯一的 QFileSystemWatcher 是项目规则监视（rules watcher，
  AgentViewModel.cpp:2363）；`ProjectFileViewModel` 明确 "no watcher by
  design"（ProjectFileViewModel.h:96）。
- **现成的"Agent 改了哪些文件"数据源**：`file_checkpoints` 表 + `fileCheckpoints
  Changed` 信号（写前 before_content + after_hash，AIAgent.cpp:3997）——GitPanel
  已用它做 500ms 防抖刷新（GitPanel.qml:698），防抖范式可直接复用。
- **重嵌通道**：`reembedDocument`（AgentViewModel.cpp:2677）+ `startIngestion`
  已是"取内容 → 同名重嵌"的完整通道，批量 worker 范式见 main.cpp:424-440
  （换模型重嵌），`embedProgress` 进度线现成。

## 3. 关键设计决策

- **D1 覆盖语义以 `source_path` 为键**：带 source_path 的文档重新导入/更新时，
  同路径 → **覆盖替换**（chunks 整批重嵌，`dropDocumentLocked` 按名替换已支持），
  不再走 `uniqueDocumentName` 改名分支；改名分支只保留给无路径来源（URL/粘贴）。
  理由：知识库文档与磁盘文件应当一一对应，"同名不同内容共存"对文件类来源是
  反直觉的（旧版本会持续污染检索）。
- **D2 变更判定用 mtime+size 快速路径**：`knowledge_documents` 加
  `file_mtime_ms`/`file_size` 列；同步扫描先比这两项，未变直接跳过（零嵌入
  开销）；变化才读全文、比对内容 hash、决定重嵌。mtime+size 在 Windows 与
  Linux 的常规编辑器保存下都可靠更新，且比全量 hash 便宜两个数量级。
- **D3 两条变更源（实施修订：批 A/B 合并）**：原计划分两批（先 checkpoint
  事件联动、后 watcher），实施时发现 `fileCheckpointsChanged` 只镜像**活动
  tab** 且携带全量列表（增量匹配既复杂又漏非活动 tab）；而**文件级
  QFileSystemWatcher 一个机制同时覆盖两类触发源**——Agent 写盘与外部编辑
  落盘的形态完全相同。最终实现：
  - `AgentViewModel::m_knowledgeWatcher` 文件级监听所有 `source_path`
    （refreshKnowledge 后 rearm，导入/删除/恢复自动跟随）；
  - `fileChanged` → 600ms 防抖 → `syncKnowledgePaths`（偏好键
    `knowledge.autoSyncSourceFiles` 门控，默认开）；
  - KnowledgeDialog「同步源文件」按钮走同一 `syncKnowledgePaths`（全量扫描）；
  - worker 线程扫描：mtime+size 快速路径 → 内容 hash 比对 → 相同只刷元数据
    （`documentFileInfoPersisted` 信号跨线程回 GUI 持久化）、不同
    `ingestDocument` 按名覆盖重嵌；源缺失跳过（不自动删除）。
  - `recordKnowledgeFileInfo`：导入/重嵌完成回调里记录快照；启动恢复
    （main.cpp startKnowledgeRestore）从 DB 回填 `setDocumentFileInfo`，
    快速路径跨重启生效。
- **D4 防抖与并发纪律全部收在 AgentViewModel 层**：单实例、GUI 线程、已有
  checkpoint 镜像与 rules watcher；`RagService` 保持无定时器纯服务（锁纪律与
  预算门顺序 REL-1003 不动）。重嵌走既有 QtConcurrent worker 链路，`EmbedShutdown`
  闸门、`fitsBudgetLocked` 预算预检原样生效。
- **D5 源消失是第三状态，不自动删除**：同步扫描发现 `source_path` 不存在 →
  行内标记"源已消失"，由用户决定删除。理由：文件移动/重命名是常见操作，自动
  删除知识库内容不可逆（重导入成本高，PDF 页文本尤其如此）。

## 4. 数据模型

```sql
-- probe-and-add（照 knowledge content_hash/embedding_model 的幂等 ALTER 惯例）
ALTER TABLE knowledge_documents ADD COLUMN file_mtime_ms INTEGER NOT NULL DEFAULT 0;
ALTER TABLE knowledge_documents ADD COLUMN file_size INTEGER NOT NULL DEFAULT 0;
CREATE INDEX IF NOT EXISTS idx_knowledge_documents_source_path
    ON knowledge_documents(source_path);
```

- `KnowledgeDocRecord`（SqliteMemory.cpp:1432）与 `RagService::Document`
  （RagService.h:138）同步携带两列；`documentPersisted` 信号**不加参**（载荷经
  `documents()` 全量投影上抛，避免三个发射点连锁改签名）。
- `upsertKnowledgeDoc` 的 source_path COALESCE 守护（:1296）保留——重嵌路径
  不传 sourcePath 时不洗掉旧值；覆盖更新路径显式传新值。

## 5. 阶段计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| **A（本批）** | D1 覆盖语义（replaceByPath）+ §4 数据列 + Agent 写文件联动（checkpoint 防抖 → 匹配 source_path → 重嵌）+ KnowledgeDialog「同步源文件」按钮 + 行内源状态 | test_rag 新增：同路径不同内容覆盖不改名、mtime 未变跳过重嵌、Agent 写联动触发一次重嵌（防抖合并）；test_knowledge_dialog：同步按钮与源状态展示 |
| **B（登记）** | 文件级 QFileSystemWatcher 外部编辑联动 | 外部保存文件后 N 秒自动重嵌 |

## 6. 测试计划

- `test_rag`：覆盖替换不产生 `(2).txt)` 分身；mtime/size 未变跳过（嵌入调用
  计数断言）；预算门在覆盖路径同样生效（REL-1003 回归）。
- `test_memory`：新列迁移幂等（二次建库/老库升级）；source_path 索引存在性。
- `test_knowledge_dialog`：同步按钮触发 AgentVM 调用、三态源状态展示、
  源消失不自动删除。
- 手工验收：导入项目 README → 子代理改写 → 知识库行状态变"已过期"→ 打开
  会话问知识库内容，回答引用新文本。

## 7. 明确不做

- **目录递归监听 / 自动导入新文件**：导入必须是显式动作（用户选文件/拖放/URL），
  自动画目录会把"知识库"变成"全盘索引"，预算（4096 段）会被无关文件吃光；
- **内容 hash 全量扫描**：mtime+size 快速路径足够，hash 只在 mtime 变化后算；
- **删除联动**：源消失不自动删（D5）；
- **批 B 之外的调度器**：不做通用的"知识库刷新定时器"（与定时任务体系无耦合）。
