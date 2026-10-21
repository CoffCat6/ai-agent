# src/core/rag/ — 本地知识库：分块、嵌入、向量检索与引用

## 职责

本目录实现「导入文档 → 分块 → 向量化 → 内存向量检索 → 以 `[doc:名#chunk]` 引用回灌模型」的完整链路：

- `DocumentRetriever.h`：单文档的分块 + 批量嵌入 + 每文档 `VectorStore` + 相关性门控。
- `VectorStore.h`：内存向量索引（余弦相似度 + `partial_sort` top-k）。
- `RagService.h/.cpp`：多文档门面（线程安全 ingest/search/remove/clear）、预算控制、持久化导出与恢复。
- `KnowledgeChunk.h`：落盘 chunk 记录 + 内容哈希 + 「持久化块可用性」判定。
- `KnowledgeSearchTool.h`：对模型暴露的 `search_knowledge_base`（`Safe`，自动执行）。

**不含**：PDF 文本抽取在 `viewmodels/PdfTextExtractor`（`Qt6::Pdf` 依赖 `Qt6::Gui`，为保持 `nexus_core` 零 UI 依赖而外置）；SQLite 持久化在 `core/memory/SqliteMemory`；worker 编排在 `main.cpp` 与 `AgentViewModel`。

## 设计（模式/抽象/关键类型）

### 三层结构
`RagService`（门面 + 互斥） → `DocumentRetriever`（每文档一个，持有自己的 `VectorStore`） → `VectorStore`（纯内存，裸 `QVector<Entry>`）。`RagService::Document{name, chars, contentHash, shared_ptr<DocumentRetriever>}`；用 `shared_ptr` 是为了让检索在锁外扫描时，即使 `removeDocument()/clear()` 摘下条目，检索器对象仍存活（docs/25 B5）。

### 分块策略（`DocumentRetriever::chunkDocument`）
- 字符级而非空白切分：`ChunkSize = 512` 字符、`ChunkOverlap = 64`，因此无空格的中文文档不会塌成单块（旧空白切分器曾导致嵌入请求超时）。
- 优先句界：在窗口后半段（`start + 256` 到 `end`）反向查找 `\n . ! ? ; : 。！？；… ，、`，找到则在句界切断，否则硬切。
- 每块 `.simplified()`（折叠空白）后丢弃空块；`start = max(end - overlap, start + 1)` 保证推进不退化。
- PDF 导入路径复用同一 `chunkDocument` 生成 `chunkTexts`，再以 `addChunkedDocument(prechunked, …)` 避免二次分块（docs/25 B7）；`RagService::ingestDocument` 在分块前用 `size/(512-64)+1` 预判 `MaxChunksPerDocument`，避免白跑一遍。

### 嵌入策略（全篇单一模式）
- 一个文档只做**一次批量** `BatchEmbedFn(chunks)`；后端是 `OllamaEmbedding::embedBatch`（`/api/embed`，每批 32 条，120s 传输超时，传输错误重试 1 次，4xx 不重试）或 `OpenAICompatibleEmbedding`。
- 任何不一致（返回条数不符 / 任一向量为空 / 调用失败）→ **整篇**降级为 `pseudoEmbed`，绝不混维度：`m_usesPseudo`、`m_embeddingDim = 128`。
- `pseudoEmbed`：128 维位置相关字符哈希 `(char*31 + i) % 128` 累加后 L2 归一；无语义，只保证离线可检索。
- 查询向量：`RagService::searchDetailed` 每次检索只算**一次**真实向量（所有真实模式文档共享），伪向量本地现算；真实向量走 `m_queryCache`（`QHash + QQueue`，上限 32，FIFO 淘汰，`setEmbedder` 时清空）。

### 检索与门控
- `DocumentRetriever::search(realVec, pseudoVec, k)`：伪模式库用伪向量、`score > 0` 才留；真实模式库要求 `realVec` 非空且维度等于 `m_embeddingDim`，否则**直接返回空**（FIX-302），并通过 `score >= RealMinRelevanceScore(0.15)` 过滤。
- `VectorStore::search`：逐条算余弦，`std::partial_sort` 只维护 top-k 前缀（避免 4096 块全排序）；元数据里回填 `score`。
- `RagService::finishSearch`：锁内只复制 retriever 快照，锁外逐文档 top-k → 合并 → 按 score 降序 → 取全局 top-k。
- `SearchOutcome{passages, hasDocuments, embeddingUnavailable}`：区分「库里没匹配」与「检索根本没跑起来」（存在真实向量文档但查询向量取不到 ⇒ 嵌入服务离线/维度漂移）。

### 预算与返回码（`RagService`）
- `MaxChunksPerDocument = 512`（超限 `-1`）、`MaxTotalChunks = 4096`（超限 `-3`，可由 `setMaxTotalChunks` 覆盖，供测试）。
- 总预算按「先摘掉同名旧文档，只计增量」核算；`0` = 空/不可用，`-2` = 恢复代际已被 `clear()/removeDocument()` 作废。
- 恢复代际 `m_restoreGeneration`：启动恢复 / 身份变更重嵌入的每个 worker 步骤都绑定开始时快照，避免「用户删库后 worker 又把文档塞回来」。

### 持久化契约（`KnowledgeChunk.h`）
- `KnowledgeChunk{index,total,text,embedding,dim,pseudo,page}`；`page` 为 1 基 PDF 页码，非 PDF 为 `-1`，落 `knowledge_chunks.page`。
- `knowledgeContentHash(content)` = SHA-256；`persistedChunksUsable(storedHash, storedModel, content, currentModel)` 要求**内容哈希与嵌入模型身份双匹配**，否则重嵌入（不同模型的向量空间不可混用）。
- `exportChunks()` 直接导出**已存向量**，恢复路径 `addPrecomputedDocument()` 零网络 IO、位级一致。

### 引用标记与工具输出（`KnowledgeSearchTool`）
- 每条命中前缀 `[doc:<原始文档名>#<0 基 chunk_index>]`，随后 `<knowledge_passage source="…">` 包裹（SEC-502：文档内容视为不可信数据，明确告知模型不得当指令）。
- 文档名用于标记时只压掉 `\r\n`（保留原始值以便 UI 按名精确匹配）；`source` 属性中 `"`→`'`、`<`→`(`、`>`→`)`。标记用字符串拼接而非 `.arg()`，防止名字中的 `%N` 被吃掉。
- `k` 夹在 1–10（默认 4）；`knowledgeBaseNonEmpty()` 供 AIAgent 决定是否注入引用约定。

## 数据与控制流

### 导入（`AgentViewModel::importDocument` / `importDocuments`）
1. Worker 线程 `readAndIngestFile(path, rag)`：`.pdf` → `extractPdfText()`（`PdfExtractResult{status, text, pageTexts, detail}`，区分 `OpenFailed/Encrypted/Corrupted/NoText`）；其余按 UTF-8 读。
2. 内容去重：`knowledgeContentHash` → `rag->containsContentHash()` 命中即 `duplicate`；命名冲突用 `uniqueDocumentName()` 生成 `a (2).txt`。
3. `startIngestion` → `QtConcurrent::run(rag->ingestDocument(name, content, pageTexts))`，GUI 侧 `QFutureWatcher` 收结果码（`ok` / `degraded`（pseudo）/ `failed`）。
4. `ingestDocument`：可选先 chunk + `mapChunksToPages`；`buildRetriever`（锁外做网络嵌入）→ `exportChunks` → 锁内预算检查 + 替换同名 → `emit changed()` + `emit documentPersisted(name, content, chunks)`。
5. `main.cpp` 以 `&app` 为 context 连接 `documentPersisted`（跨线程排队到 GUI）→ `memory->upsertKnowledgeDoc(...)` + `replaceKnowledgeChunks(...)`。

### PDF 页码映射（`RagService::mapChunksToPages`）
把各页文本 `simplified()` 后用空格拼成 `full` 并记录每页起始偏移；对每个 chunk（同样 `simplified`）从 `cursor - ChunkOverlap` 起做子串定位，命中后二分（倒序线性）找所属页 = `i+1`；整体近似 O(全文)。跨页 chunk 记起点页；匹配不到记 `-1`（UI 不显示页码）。

### 启动恢复（`main.cpp::startKnowledgeRestore`）
GUI 线程一次性快照 `loadKnowledgeDocRecords()` + `loadKnowledgeChunks()` → 池线程逐文档：`persistedChunksUsable(...)` 且 `restorePersistedDocument()` 成功则零嵌入恢复，否则 `ingestRestoredDocument()` 重新分块+嵌入；每步校验 `restoreGeneration`，`embeddingShutdownRequested()` 时提前收手（退出卡死修复）。

### 检索（模型调用）
`KnowledgeSearchTool::execute` → 空库直接回「空库」文案 → `searchDetailed(query, k)` → 查询向量（缓存/锁外嵌入）→ `embeddingUnavailable` 检测 → `finishSearch` 合并排序 → 无命中则 `No passages matched '<q>'`；有命中则逐条格式化带引用标记的块。

### 身份切换重嵌入（`main.cpp::applyEmbeddingConfig`）
`identity = openai:"<model>" 或 ollama model` 与 `rag->embeddingModelName()` 不同 ⇒ 后台遍历 `loadKnowledgeDocRecords()` 调 `ingestRestoredDocument`（走 `documentPersisted` 回写），旧向量作废。

## 集成点

- `main.cpp`：构造进程级 `OllamaEmbedding` / `OpenAICompatibleEmbedding`，`embedText` / `embedTexts` 闭包按 `shared_ptr<QAtomicInt>` 切换后端（localOnly 时强制本地）；`rag->setEmbedder/setBatchEmbedder/setEmbeddingModelName`；注册 `KnowledgeSearchTool`；`documentPersisted` → SQLite；数据库恢复后重跑 `startKnowledgeRestore`。
- `core/agent/AIAgent`：`knowledgeBaseNonEmpty()`（`dynamic_cast` 回查已注册工具）决定系统提示词是否注入引用约定；`toolAllowedForModel` 在 localOnly 下仅当模型为本地时才允许 `search_knowledge_base`。
- `viewmodels/AgentViewModel`：`importDocument(s)` / `clearKnowledge` / `reembedDocument` / `documentChunks` / `openKnowledgeDocument(name, chunk)`；`knowledgeBusy`、嵌入进度属性；`documentRemoved` / `knowledgeCleared` 信号触发 SQLite 删除。
- `core/memory/SqliteMemory`：`knowledge_docs` + `knowledge_chunks`（含 `page` 列迁移）、`loadKnowledgeDocRecords`/`loadKnowledgeChunks`/`replaceKnowledgeChunks`。
- UI 引用链路：`viewmodels/CitationParser.h` 解析 `[doc:名#N]`（取最后一个 `#`，非法 chunkId 退化为 -1）→ QML 渲染 chip → 点击弹 `KnowledgeDocDialog`（`AgentVM.documentChunks` 展示分块，可定位到 `chunk`）。
- 测试：`tests/test_rag.cpp`（分块/伪嵌入降级/预算/恢复代际/页码映射/引用标记/快照并发）、`tests/test_embedding.cpp`（批量与重试）、`tests/resources/sample.pdf`。

## 关键文件

| 文件 | 规模 | 内容 |
|---|---|---|
| `DocumentRetriever.h` | 260 行 | 分块、批量嵌入 + 整篇 pseudo 回退、相关性门控、导出/预计算恢复 |
| `VectorStore.h` | 104 行 | 余弦相似度、`partial_sort` top-k、`chunks()`/`entries()` |
| `RagService.h` | 172 行 | 门面 API、预算常量、`SearchOutcome`、代际与查询缓存 |
| `RagService.cpp` | 466 行 | ingest/restore/search 实现、锁边界、`mapChunksToPages` |
| `KnowledgeChunk.h` | 47 行 | 持久化记录、内容哈希、可用性判定 |
| `KnowledgeSearchTool.h` | 139 行 | `search_knowledge_base` schema、`[doc:…]` 输出、离线错误文案 |

## 约束与风险

- **伪嵌入会静默掩盖故障**：批量嵌入任一批失败即整篇降级为 128 维哈希向量，检索只是词面重叠；只靠 `documents()[].pseudo` / `usesPseudoEmbedding()` 在 UI 标「degraded」，模型侧无感知。
- **相关性阈值是占位值**：`RealMinRelevanceScore = 0.15` 未按模型标定（源码注释自述 placeholder）；换嵌入模型后可能出现「该命中却不命中」或反之。
- **恢复路径丢页码**：`ingestRestoredDocument` 只传 `content`，不传 `pageTexts`，因此身份切换重嵌入后的 PDF 文档 chunk `page` 变 `-1`，引用定位退化（原持久化行被 `replaceKnowledgeChunks` 覆盖）。
- **查询缓存不随模型身份失效**：`setEmbeddingModelName()` 不清 `m_queryCache`，只有 `setEmbedder()` 清；运行中切换 embedding provider/model（只翻 `openAiActive`）后，缓存里的旧维度向量会让 `acceptsQueryVector` 失败，进而误报 `embeddingUnavailable`。
- **名称即主键**：文档以 `sourceName` 为唯一标识，无重命名 API；`ingestDocument` 本身不做内容去重（去重在 `AgentViewModel` / 批量导入侧调用 `containsContentHash`）。
- **`setMaxTotalChunks` 不受锁保护**：`m_maxTotalChunks` 直写，测试用；若运行中调整存在与 ingest 的竞争（实际仅测试路径调用）。
- **检索是线性扫描**：每查询 O(总块数 × 维度)，4096 块上限是其可行前提；虽然扫描在锁外，但仍占用调用线程（工具池线程；若 GUI 直接调用 `search()` 会卡 UI）。真实向量库模式下每次查询至少一次网络往返（未命中缓存时）。
- **`documentPersisted` 从 worker 线程发出**：任何新消费者必须用排队连接，否则会跨线程碰 SQLite（main.cpp 已用 `&app` context 兜住）。
- **预算拒绝在恢复路径是「静默跳过」**：`restorePersistedDocument` 超预算只 `qWarning` 并返回 false，文档仍留在 SQLite，用户可能以为已加载；启动日志的 skip 计数是唯一线索。
- **引用标记的已知边界**：文档名含 `]` 时标记提前闭合（`[doc:a]b.txt#0]`），模型照抄会退化为纯文本 chip；含 `#` 无碍（取最后一个 `#`）。源码明确拒绝转义 `]`，以保「原始名往返」契约。
- **`chunkDocument` 依赖 `simplified()` 对齐**：页码映射要求分块输入与建库输入严格同为 `content.trimmed()`，任何前导/尾随空白差异都会让页号漂移或落到 `-1`。
- **进程生命周期对象**：`RagService` 及嵌入后端被三个可能仍在跑的后台 worker（启动恢复 / 全量重嵌入 / 导入）持有裸指针，故刻意不 parent、不析构；新增使用者必须遵守同一纪律。
