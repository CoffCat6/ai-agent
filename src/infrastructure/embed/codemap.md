# src/infrastructure/embed/ — 嵌入后端（本地 Ollama / OpenAI 兼容）+ 退出中断

## 职责

为知识库（`core/rag`）与长记忆摘要（`core/agent/EpisodicService`）提供**同步阻塞式**文本向量化，两份可互换实现 + 一个进程级退出标志：

- `OllamaEmbedding`：Ollama 原生 `POST /api/embeddings`（单条）与 `POST /api/embed`（批量）。
- `OpenAICompatibleEmbedding`：OpenAI 兼容 `POST /v1/embeddings`（覆盖 OpenAI、SiliconFlow、智谱、LiteLLM 代理、Ollama 的兼容端点）。
- `EmbedShutdown.h`：`requestEmbeddingShutdown()` / `embeddingShutdownRequested()` 的原子闸门。

**不含**：分块策略、伪向量降级、维度记录、余弦检索与持久化（全在 `core/rag/DocumentRetriever` + `RagService`）；后端选择与配置注入（`main.cpp` 的 `applyEmbeddingConfig`）。

## 设计

### 一致的方法面（两个后端故意同构）
`setBaseUrl` / `setModel`（OpenAI 侧另有 `setApiKey`）/ `model()` / `embed(QString)` / `embedBatch(QStringList)` / `static constexpr BatchSize = 32` / `splitBatches()` / 信号 `embedProgress(done, total)`。main.cpp 因此只需一个原子开关就能在两者之间切换，装配层不出现后端分支。

- **单条**：`embed()` → 返回 `QVector<float>`，失败返回空容器（空即失败，没有错误码/异常）。
- **批量**：`embedBatch()` → `QVector<QVector<float>>`，内部按 32 条分页；每批成功 emit `embedProgress(done, total)`；**任一批失败/协议不一致 → 返回空容器**，让调用方整篇降级（`DocumentRetriever` 的「一篇文档只用一种模式」契约）。

### 线程模型：配置加锁、每调用快照、栈上网络对象
- setter 可能在 GUI 线程，`embed*` 在 worker 线程（文档导入 / 启动恢复 / 重嵌入、摘要检索），`QString` 并发读写会撕裂 → `m_configMutex` 保护配置，**每个调用入口在锁内快照** `baseUrl/model/apiKey`；批量整轮只快照一次（用户中途改配置只影响下一次导入）。
- 不用成员 `QNetworkAccessManager`：每次调用在栈上新建 `QNetworkAccessManager`，与栈上的 `QEventLoop`、`QTimer` 一起天然属于调用线程，避免跨线程 QObject 误用。
- 阻塞等待：`loop.exec()`，`reply::finished → loop.quit()`。

### 超时 / 重试 / 失败语义
| | Ollama | OpenAI 兼容 |
|---|---|---|
| 单条超时 | 30s | 60s |
| 批量超时（每页） | 120s | 300s |
| 单条重试 | 无 | 无 |
| 批量重试 | 每批最多 2 次尝试；仅传输错误/5xx；4xx fail-fast；第二次前 `msleep(500)` | 同左 |

这些超时**不再是退出路径的下限**：等待循环里另有一个 200ms 轮询定时器（见下）。

协议层错误一律 fail-fast（重试无意义）：批量返回条数 ≠ 请求条数、任一向量为空、OpenAI 的 `data[i].index` 非法/越界/重复 → 直接返回空容器。

### 身份（向量空间版本号）
`OllamaEmbedding::model()` 返回裸模型名；`OpenAICompatibleEmbedding::model()` 返回 **`"openai:" + m_model`**。这个前缀是刻意的：main.cpp 把 `model()` 写进 `rag->setEmbeddingModelName()`，持久化 chunk 的可用性检查按字符串比较，切 provider 会判定旧 chunk 失效并触发全库重嵌入，而不是把两套不兼容的向量混在一个库里。

### OpenAI 端点归一与空 key 策略
`embeddingsUrl()`：去尾斜杠；已以 `/v1` 结尾 → `+/embeddings`，否则 `+/v1/embeddings`（避免 `.../v1/v1/embeddings` 或根路径少一段）。
key 为空**仍然发请求**（部分自建网关免鉴权），让服务端用 401 说话；日志用 `Q_LOGGING_CATEGORY(lcEmbedding, "nexus.embedding")` 每进程只警告一次（`QAtomicInt::testAndSetAcquire`，`embed()` 与 `embedBatch()` 共用）。

### 关闭时序（EmbedShutdown 解决什么）
- **问题**：三个后台嵌入 worker（启动知识库恢复、provider/model 切换后的全库重嵌入、文档导入）跑在**被丢弃的 `QtConcurrent` future** 上，没有任何代码能取消它们；worker 在全局线程池线程里用 `QEventLoop` 同步等 HTTP（外部服务批量超时最长 300s）。`main()` 返回后的静态析构阶段，全局线程池 `waitForDone()` 会一直等下去 → 窗口消失、进程挂在后台。
- **方案**：GUI 线程在 `QCoreApplication::aboutToQuit` 里 `requestEmbeddingShutdown()`（`storeRelease(1)`，单向闸门，生产路径不复位）；两个后端在四个位置检查（入口、每批开始、每次重试前、等待循环里 200ms 轮询）——置位则 `reply->abort()` + `loop.quit()`、立即返回空、不再发起新批；`main.cpp` 的两个后台 `for` 循环也据此 `break`，不再白算降级向量。
- **线程安全**：`QAtomicInt` 的 release/acquire 保证 GUI 线程置位对 worker 可见；`inline` 函数内的 `static` 在所有编译单元共享同一实例（ODR），同时靠 magic static 规避静态初始化顺序问题。`resetEmbeddingShutdownForTesting()` 仅供 `test_embedding` 在同一进程内复用标志。

## 数据与控制流

```
main.cpp 启动
  ├─ new OllamaEmbedding(nullptr) / new OpenAICompatibleEmbedding(nullptr)   // 进程级，绝不 delete
  ├─ applyEmbeddingConfig()：两个后端都 setModel/setBaseUrl(+setApiKey)
  │    ├─ SettingsService::embeddingUsesExternalProvider(provider, localOnlyMode)
  │    └─ openAiActive.storeRelease(usesExternal)      // 原子开关，只切指针不换实例
  ├─ embedText  = openAiActive ? openai->embed(t)    : ollama->embed(t)
  ├─ embedTexts = openAiActive ? openai->embedBatch  : ollama->embedBatch
  └─ rag->setEmbedder/setBatchEmbedder；EpisodicService::setEmbedder

rag 导入 worker → DocumentRetriever::addChunkedDocument
  └─ 一次 embedBatch(chunks) → 32 条/页 → 每页 POST → embedProgress
       ├─ 全部成功 → 真实向量（维度 = vectors[0].size()）
       └─ 任一失败/为空 → 空容器 → 整篇 pseudoEmbed（128 维），绝不混维度

退出：aboutToQuit → requestEmbeddingShutdown()
  └─ 等待循环 200ms 轮询 → reply->abort() → embed*() 返回 {} → worker 尽快收手
```

## 集成点

- `main.cpp`：唯一装配点；`applyEmbeddingConfig` 由 `embeddingModelChanged` / `embeddingBaseUrlChanged` / `embeddingProviderChanged` / `embeddingApiKeyChanged` / `localOnlyModeChanged` 五个信号触发重放；两个后端的 `embedProgress` 都连到 `AgentViewModel::setEmbedProgress`（只有活动后端会发）。
- `core/rag/RagService` + `DocumentRetriever`：消费 `BatchEmbedFn`，负责「批失败 → 整篇 pseudo」与维度记录；`KnowledgeSearchTool` 间接消费。
- `core/agent/EpisodicService`：用单条 `embed()`（`retrieveRelevantSummariesAsync` 在 `QtConcurrent` 线程里算查询向量）。
- `viewmodels/SettingsService`：`embeddingUsesExternalProvider(provider, localOnlyMode)` 是选择开关的纯函数；`embeddingApiKeyValue()` 才是真实 key（`embeddingApiKey()` 是掩码 getter，恒空——用它注入必然 401）；外部后端被 localOnlyMode 拦下时回退本地并 `qWarning`。
- `core/utils/NetworkProxyConfig.h`：栈上 NAM 走应用级 proxy factory；loopback 目标（本地 Ollama）强制绕过代理。
- CMake：属于 `nexus_infra`，链接 `nexus_core` + `Qt6::Network`。

## 关键文件

| 文件 | 关键内容 |
|---|---|
| `OllamaEmbedding.h` | 方法面、`BatchSize=32`、`m_configMutex`、默认 `localhost:11434` / `nomic-embed-text` |
| `OllamaEmbedding.cpp` | 栈上 NAM+EventLoop+200ms 退出轮询、批量分页与单次重试、`/api/embeddings` 与 `/api/embed` 两套解析 |
| `OpenAICompatibleEmbedding.h` | `"openai:"` 身份前缀、`setApiKey`、`embeddingsUrl()` |
| `OpenAICompatibleEmbedding.cpp` | URL 归一、空 key 每进程一次告警、按 `data[i].index` 归位、60s/300s 超时 |
| `EmbedShutdown.h` | `QAtomicInt` 闸门、`inline` 函数内 static、测试复位口 |

## 约束与风险

- **同步阻塞 API**：内部跑 `QEventLoop`，只能在允许事件循环的 worker 线程调用；在 GUI 线程调用会直接卡界面（导入路径已全部走 `QtConcurrent`）。
- **空返回即失败**：没有错误码/维度信息，调用方无法区分「服务离线」「4xx」「返回空向量」；`DocumentRetriever` 只能整篇降级 pseudo。若调用方漏判空容器，后续余弦检索会静默失效。
- **批量是全有或全无**：任一批失败丢弃已算好的前几批（不做部分保留），大批量文档在长尾失败时重算成本高。
- **OpenAI 批量依赖 `index`**：部分网关返回乱序（已按 index 归位），但不带 `index`（`-1`）时直接 fail-fast → 整篇降级。
- **两个后端的 model 同值**：`applyEmbeddingConfig` 给两者设的是同一个 `settings->embeddingModel()`；身份只靠 `"openai:"` 前缀区分。若用户把 Ollama 模型名填进 OpenAI 后端（或反之），发出的报文本身不会被本地校验拦住。
- **重试策略很浅**：固定 500ms、最多一次、无指数退避；429 属于 4xx 直接 fail-fast，用户侧表现是「整篇 pseudo」，需要手动重新导入。
- **退出时返回空 ≠ 故障**：`abort()` 后 `embedBatch` 返回 `{}`，与真实故障同形。正在处理的最后一篇文档仍可能被判为 pseudo 模式并尝试落库（`documentPersisted` 是投递到 GUI 线程的队列连接，退出过程中可能已不再派发）。
- **EmbedShutdown 单向且进程级**：置位后同一进程内所有嵌入永久短路；只有测试可复位，生产代码严禁调用。
- 两个后端的 `splitBatches` 各自实现（内容相同），改动分页语义需同步两处；`BatchSize` 也是两份常量。

## 相关

上级索引：[../codemap.md](../codemap.md) ｜ LLM 层：[../llm/codemap.md](../llm/codemap.md) ｜ 降级与检索：[../../core/rag/codemap.md](../../core/rag/codemap.md) ｜ 装配：[../../main.cpp](../../main.cpp)
