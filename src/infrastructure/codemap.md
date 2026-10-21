# src/infrastructure/ — 外部服务适配层（LLM 线协议 / 嵌入后端）

本目录是 `nexus_core` 与外部 HTTP 服务之间的**唯一出口**：core 定义领域类型（`LLMResponse`、`ToolCallInfo`、`KnowledgeChunk`），infra 负责把它们翻译成各家的线上报文，并归一化回来。

构建位置：`CMakeLists.txt` 的 `nexus_infra` 静态库（`LLMServiceBase`/5 个 Service/注册表/两个 Embedding 后端/`EmbedShutdown.h`），`target_link_libraries(nexus_infra PUBLIC nexus_core Qt6::Network)`，`include` 根为 `src/`。零 UI 依赖（QML/Quick 只在 `nexus_viewmodels` 之上）。

## 子目录

### [llm/](llm/codemap.md) — 5 种线协议，一份 `IAIService` 契约

- **契约**：`chatComplete(messages, tools)` + `cancel()` + `configure(LlmProfileConfig)`；5 个信号（`requestStarted(requestId)`、`responseReady`、`streamingChunk`、`reasoningChunk`、`errorOccurred`）。思考正文走独立通道，永不混进答案。
- **共享管道**：`LLMServiceBase` 持有唯一的 `LLMRequestContext`（身份校验的闭包 → 旧请求不能污染新请求）、按行切流、finished 兜底梯子（流式结果 → 整包 JSON → API 错误 → 网络错误 → 空响应）、REL-1001 一次透明重试（仅瞬时错误/5xx/429 且尚未流出文本，重试前 `resetStreamState()` 清 provider 侧 tool 累积）。
- **协议注册表**：`LlmProtocolRegistry` 是「协议 → 实现/默认端点/默认模型/上下文窗口/是否本地/环境变量」的唯一映射；`provider` 与 `apiProtocol` 是两个正交轴，`resolve()` 三条回落（key → 厂商默认 → `openai-chat`）永不返回空。加协议只加一条描述符。
- **实现**：`OpenAIService`（Chat Completions，SSE）、`OpenAIResponsesService`（`/responses`，无状态、按载荷 `type` 分派、`call_id` 配对工具结果）、`AnthropicService`（Messages，system 折叠 + `tool_use`/`tool_result` 转换 + `thinking.budget_tokens`）、`GeminiService`（`streamGenerateContent?alt=sse`，`functionCall`/`thought`/`functionResponse` 转换 + JSON Schema 子集降级）、`OllamaService`（`/api/chat`，NDJSON，`think` 布尔 + `num_ctx`）。
- **统一口径**：无效 `QVariant` = 不发该字段；每个请求 `setTransferTimeout` 硬上限（云 120s / 本地 Ollama 300s）；usage 归一为 `tokensUsed + prompt/completion/cached/cacheRead/cacheCreation` 六个桶，拿不到就留 0 由 Agent 估算；`reasoning_effort`/`thinkingBudget`/`think` 由同一套内部档位（off/low/medium/high）折算，并按输出上限钳制避免 400。
- **代理**：Service 只建默认 factory 的 `QNetworkAccessManager`；`main()` 安装的 `ConfigurableProxyFactory` 全局生效，loopback 强制绕过。

### [embed/](embed/codemap.md) — 两个可互换嵌入后端 + 退出中断

- **同构方法面**：`embed()` / `embedBatch()`（32 条/页、`embedProgress` 每批上报）/ `model()`；`OllamaEmbedding`（`/api/embeddings`、`/api/embed`，30s/120s）与 `OpenAICompatibleEmbedding`（`/v1/embeddings`，60s/300s，`"openai:"` 身份前缀防向量空间混用）。
- **线程纪律**：配置由 `QMutex` 保护、每调用快照；每次调用在**栈上**建 NAM/EventLoop/QTimer（可在 worker 线程安全调用，但会阻塞该线程）。
- **失败语义**：任一批失败或协议不一致（条数不符/空向量/非法 index）→ 返回空容器，由 `core/rag` 整篇降级为 128 维 pseudo 向量，绝不混维度。
- **`EmbedShutdown.h`**：`aboutToQuit` 置位的进程级 `QAtomicInt` 闸门；后端在入口/每批/每次重试/200ms 轮询处检查，置位即 `abort` 并返回空——解决「被丢弃的 `QtConcurrent` worker 让静态析构期线程池 `waitForDone()` 永久等待、窗口消失但进程挂后台」的退出卡死。

## 与其他层的关系

| 依赖方向 | 说明 |
|---|---|
| `main.cpp` → 本目录 | 装配：`createServiceSet()` 按协议建服务集（进程模板 + 每标签页 + 每摘要服务），`configure(profileConfigOf(profile))` 一次落配置；嵌入后端进程级常驻、只切原子开关 |
| `core/agent/AIAgent` → `IAIService` | 只依赖契约与信号，按 `requestId` 丢弃被取代的回复 |
| `core/rag` / `EpisodicService` → 嵌入闭包 | 只拿到 `std::function`，不知道后端是 Ollama 还是 OpenAI |
| 本目录 → `core/utils` | `Activity`（网络日志/用量）、`DataUrlUtils`（图片 data URL）、`LLMResponse`/`ToolCallInfo`（`core/agent/LLMTypes.h`） |
| 本目录 → 网络 | `Qt6::Network` + `core/utils/NetworkProxyConfig.h` 的应用级 proxy factory（不直接持有代理配置） |

## 阅读顺序建议

1. `llm/IAIService.h` + `llm/LLMServiceBase.h` —— 先看懂请求上下文、钩子与兜底梯子。
2. `llm/LlmProtocolRegistry.cpp` —— 一张表看清有哪几种协议、谁是本地、默认值是什么。
3. 按需读某个 Service 的 `buildRequestBody` / `processLine` / `streamResult` 三件套。
4. `embed/EmbedShutdown.h` → 两个后端的 `embedBatch` —— 退出时序是这块最容易踩的坑。
5. 回归护栏：`tests/test_protocol_registry.cpp`（注册表归一化）与 `tests/test_llm_parsers.cpp`（各协议转换/解析/usage/通道分离）。

## 明确不在本目录

设置持久化与清洗（`viewmodels/SettingsService`、`core/utils/SamplingParams`）、历史裁剪与上下文上限（`AIAgent` + `SettingsService::resolveContextLimitForProfile`）、token 估算与计费（`core/utils/TokenEstimator`、`PricingCalculator`）、向量检索与持久化（`core/rag`、`core/memory/SqliteMemory`）、重嵌入/恢复的 worker 编排（`main.cpp`、`AgentViewModel`）。
