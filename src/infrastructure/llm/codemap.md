# src/infrastructure/llm/ — LLM 线协议适配层（5 种协议 / 一份契约）

## 职责

把 Agent 层统一使用的 **OpenAI wire format**（`messages` + `assistant.tool_calls`（arguments 为 JSON 字符串）+ `role:"tool"`/`tool_call_id` + 可选 `images` data URL 数组）翻译成各家线协议，并把流式响应归一化为 `LLMResponse` 与 5 个信号：

- `IAIService.h`：抽象契约 + `LlmProfileConfig`（一次性的活动 profile 快照）。
- `LLMServiceBase.h/.cpp`：共享管道——单在途请求上下文、按行切分流、finished 兜底梯子、一次透明重试。
- `LlmProtocolRegistry.h/.cpp`：线协议 ↔ 实现的唯一映射表（描述符 + 工厂）。
- `OpenAIService`（`/chat/completions`，SSE）、`OpenAIResponsesService`（`/responses`，SSE）、`AnthropicService`（`/v1/messages`，SSE）、`GeminiService`（`:streamGenerateContent?alt=sse`）、`OllamaService`（`/api/chat`，NDJSON）。

**不含**：profile 的持久化与清洗（`viewmodels/SettingsService` + `core/utils/SamplingParams`）、历史裁剪与 ReAct 循环（`core/agent/AIAgent`）、token 估算与计费（`core/utils/TokenEstimator`/`PricingCalculator`）、服务按 profile 换绑（`main.cpp`）。

## 设计

### IAIService 契约（流式 + 取消 + 错误）
`chatComplete(QJsonArray messages, QJsonArray tools)` / `cancel()` / `setApiKey|setModel|setBaseUrl` / `configure(LlmProfileConfig)`（默认空实现，测试替身可无视 profile）/ `model() const`（审计行身份）。
信号：`requestStarted(quint64)`、`responseReady(LLMResponse)`、`streamingChunk(QString)`、`reasoningChunk(QString)`、`errorOccurred(QString)`。
**思考正文走独立信号**：若复用 `streamingChunk`，模型私密推理会写进 Agent 正在构建的答案节点。

`LlmProfileConfig` 刻意只放「已清洗值」：`baseUrl/apiKey/model/temperature/maxOutputTokens/numCtx/reasoningEffort`。字段缺失或清空即**无效 `QVariant`**，语义是「请求体里不发该字段」（严格端点会拒空字段）。消费方由各协议 `applyProfileParams` 决定：`openai-chat`/`openai-responses`/`anthropic-messages`/`gemini-generate` 读 `temperature + maxOutputTokens + reasoningEffort`；`ollama-chat` 读 `temperature + numCtx + reasoningEffort`（输出上限在该协议无对应字段）。装配层因此**不需要按协议字符串分支**。

### 协议注册表（provider 与 protocol 是两个正交轴）
`LlmProtocolDescriptor{key, provider, label, defaultBaseUrl, defaultModel, models, contextWindows, envApiKeyVar, local, createService}`，注册顺序即 UI 候选顺序、也是「厂商默认协议」：
`openai-chat`(openai, gpt-4o-mini) → `openai-responses`(openai, 排第二，因为多数兼容网关只实现 chat) → `anthropic-messages` → `gemini-generate` → `ollama-chat`(local=true，无 env key)。
`resolve(key, provider)`：key 已知用 key；否则该 provider 第一条；再否则 `openai-chat`，**永不返回空**——因此旧 profile（只有 provider）与手改坏的值都能落到确定实现。端点路径（`/chat/completions` 等）**不进表**，与各 Service 放在一起避免两处漂移；表只回答「用哪个实现、默认值是什么、是否本地」。

### LLMServiceBase 的一次请求
`LLMRequestContext`（每次 `chatComplete` 新建，`QSharedPointer`）：`requestId`、`reply`、`request`、`requestBody`、`attempt`、`startMs`、`lineBuffer`、`rawBody`、`content`、`reasoning`、`tokensUsed`、`doneReceived`、`responseEmitted`/`errorEmitted`/`cancelled`、`provider`（协议私有的 `ProviderRequestState`）。
所有回调捕获 `ctx` 并与 `m_current` 做**身份校验**：被取代的请求即使还在跑，也只能 `deleteLater()`，绝不污染新请求的累积器或发信号。`ProviderRequestState::reset()` 是重试路径的钩子——必须清掉上一轮已累积的 tool 调用片段，否则 `streamResult()` 会重复上报。

### 结束处理与兜底梯子（`handleReplyFinished`）
1. 读掉剩余字节，切行；**没有尾换行的残余也当作最后一行**（SSE 末事件 / NDJSON 末记录）。
2. 重试判定（见下）。
3. 写一条 `Activity::usage` 网络结果（status/duration/bytes/attempt，不含响应体）。
4. `streamComplete(ctx)` 为真 → `streamResult(ctx)` → `responseReady`。
5. 否则解析整包 JSON：`extractApiError` 非空 → `errorOccurred("... error (HTTP n): msg")`；`isCompleteBody` 且无网络错误 → `parseBody`（服务端忽略了 stream 标志）。
6. 有网络错误 → `Network error: ...`；否则 `Empty or invalid response from <label>`。

### 超时 / 重试 / 取消
- 超时只用 `QNetworkRequest::setTransferTimeout`（硬上限，无额外定时器）：Chat/Responses/Anthropic/Gemini **120s**，Ollama **300s**（本地模型慢）。
- 重试（REL-1001）：`attempt==0` 且未取消、且 `content`/`reasoning` 皆空，且（瞬时网络错误 `TimeoutError|RemoteHostClosed|TemporaryNetworkFailure|NetworkSessionFailed|UnknownNetworkError`，或 HTTP `>=500`/`429`）→ `attempt=1`，`resetStreamState()`，**以同一 requestId 重发**。只重试一次，无退避；已流出任何文本就不重试（否则 UI 闪回且重复计费）。
- 取消：`cancel()` 只置 `cancelled` + `reply->abort()`，没有 cancelled 信号；清理全交给 finished。

### 各协议的请求构造与流解析差异
| 协议 | 端点 / 鉴权 | 请求要点 | 流式 | 完成判定 |
|---|---|---|---|---|
| openai-chat | `{base}/chat/completions`，`Bearer` | `stream_options.include_usage=true`；`max_tokens`；`reasoning_effort`；`tools`+`tool_choice=auto`；带图 user 轮 → `content:[{text},{image_url}]`（无图保持字符串，非标 `images` 字段剥掉） | SSE `data:`，`[DONE]` 置 done；usage 在空 `choices` 末帧（先取再 return）；`delta.content`/`reasoning_content`/`tool_calls[index]` | done \|\| content \|\| toolCalls |
| openai-responses | `{base}/responses`，`Bearer` | `store=false`、`truncation=auto`、每轮全量重发 `input`；system/developer → 顶层 `instructions`；`tool_calls`→`function_call` item、`role:tool`→`function_call_output`（靠 `call_id` 配对，错一个即 400）；tool 扁平 + `strict:false`；`max_output_tokens` 钳 ≥16；`reasoning.effort` | SSE 但**只看载荷里的 `type`，忽略 `event:` 行**：`output_text.delta`、`reasoning_summary_text.delta`/`reasoning_text.delta`、`error`（顶层 message/code）、`output_item.added/done`、扁平 `function_call`（第三方网关）、`function_call_arguments.delta/done`、`completed/incomplete`（usage 在 `response.usage`）、`failed` | done \|\| content \|\| calls |
| anthropic-messages | `{base}/v1/messages`，`x-api-key` + `anthropic-version: 2023-06-01` | `max_tokens` **必填**（未配置兜底 4096）；system 折进顶层 `system`；tool 结果 run-merge 成一条 user 的多个 `tool_result`；assistant `tool_calls`→`tool_use`（arguments 字符串 → `input` 对象）；连续同角色纯文本 `\n\n` 合并；开头非 user 轮删除；图片 → base64 `image` 块 | 按 `type`：`message_start`（input + cache 拆分）、`content_block_start`（tool_use 起槽）、`content_block_delta`（`text_delta`/`thinking_delta`/`input_json_delta`/`signature_delta` 忽略）、`message_delta`（output 累积）、`message_stop` | done \|\| content \|\| toolUses |
| ollama-chat | `{base}/api/chat`，**无鉴权头** | `tools` 剥掉 `function` 包装；`think`(bool)；`options{temperature,num_ctx}`；images→裸 base64；assistant content 不能 null；tool_calls arguments 字符串→对象；role tool 去掉 `tool_call_id` | **NDJSON**：整行即 JSON，无 `data:` 前缀 | 只认 `done=true` |
| gemini-generate | `{base}/v1beta/models/{model}:streamGenerateContent?alt=sse`，`x-goog-api-key` **请求头**（放 `?key=` 会把密钥带进日志/代理） | 模型名先 `trim` 再过 `[A-Za-z0-9._:~-]` 白名单（拼进 URL path）；`contents`（assistant→model）、`systemInstruction`、`generationConfig`、`tools:[{functionDeclarations}]`；`sanitizeSchema` 把 MCP 完整 JSON Schema 降到 Gemini 子集，降级不动的工具整条跳过 | SSE `data:`：`candidates[0].content.parts` 里 `functionCall` / `thought:true` / `text`；`finishReason` 置 done；`promptFeedback.blockReason` 报拦截 | done \|\| content \|\| toolCalls |

### usage / reasoning 归一化
`LLMResponse{content, toolCalls, tokensUsed, success, error, requestId, promptTokens, completionTokens, cachedTokens, cacheReadTokens, cacheCreationTokens, reasoning}`。
- `tokensUsed`：Chat/Responses 用 `total_tokens`；Anthropic `input+output`；Ollama `prompt_eval_count+eval_count`；Gemini `prompt+candidates+thoughts`。拿不到 usage 就留 0（Agent 侧走估算），**不做客户端兜底计算**。
- 缓存桶：Chat 只填 `cachedTokens`（`prompt_tokens_details.cached_tokens`）；Responses 同样只填 `cachedTokens`（`input_tokens_details.cached_tokens`，其 `input_tokens` 已含缓存，再填 cacheRead 会双计）；Anthropic 另立 `cacheReadTokens`/`cacheCreationTokens`。
- 思考正文：Chat `reasoning_content`；Anthropic `thinking` 块 / `thinking_delta`（`signature_delta` 丢弃）；Ollama `message.thinking`；Gemini `thought:true` 的 part。全部只进 `reasoning`/`reasoningChunk`。

### thinking effort 参数
统一档位（`SamplingParams::optionalReasoningLevel` 归一后只有 `off|low|medium|high`，其余 → 无效 QVariant = 不发）：
- Chat：`reasoning_effort`，`off→"none"`，low/medium/high 原样，其他返回空串（不发）。
- Responses：`reasoning.effort`，`off→"none"`，另外防御性接受 `minimal/xhigh/max`。
- Anthropic：`thinking{type:enabled,budget_tokens}`，low 2048 / medium 8192 / high 16384；`ceiling = max_tokens-1024`、下限 1024，留不出 → 0（宁可无思考也不发必 400 的报文）；启用思考时 `temperature` 一律不发（API 要求 =1）。
- Gemini：`thinkingConfig.thinkingBudget`，同档位表，再按 `maxOutputTokens-1024` 钳制；`off` 只是「不指定 thinkingConfig」，不是真正关闭（2.5/3 系列本身是思考模型）。
- Ollama：只有顶层布尔 `think`，`off→false`，low/medium/high→`true`，未设置 → 不发。

### 代理注入
Service 不自己配代理：`LLMServiceBase` 只建一个默认 proxy factory 的 `QNetworkAccessManager`；`main()` 用 `QNetworkProxyFactory::setApplicationProxyFactory(new ConfigurableProxyFactory)` 全局安装一次，所有 LLM/embedding/搜索/MCP HTTP/`http_fetch` 一致生效，loopback 目标由工厂强制绕过（本地 Ollama/网关经代理会连接失败）。

## 数据与控制流

```
profile QVariantMap
  └─ main.cpp profileConfigOf()（SamplingParams 清洗成可能无效的 QVariant）
       └─ service->configure(cfg) → 三件套落基类 + applyProfileParams(协议私有参数)
            └─ chatComplete(messages, tools)
                 ├─ 协议转换 + buildRequestBody（或 Gemini/Anthropic 内联组装）
                 ├─ startReply：++requestId → createRequestState() → emit requestStarted
                 └─ postReply：NAM->post → readyRead/finished（身份校验闭包）
                      readyRead  → onReadyRead → lineBuffer → processIncoming（按 '\n' 切）
                                    └─ processLine（各协议）→ streamingChunk / reasoningChunk
                      finished   → handleReplyFinished → [重试 | responseReady | errorOccurred]
```

## 集成点

- `main.cpp`：`LlmProtocolRegistry::createServiceSet(&app)` 是进程级「模板集」；每个标签页 Agent 与每个 `EpisodicService`（长记忆摘要）各建一套独立 service set，避免前台流式与摘要互相覆盖；`setModelConfigurator` 按 `profile["apiProtocol"]` 取值 → `configure(profileConfigOf(profile))` → `target->setLLMService()`；`model`/`baseUrl` 为空则传 `nullptr`；`setContextLimit(settings->resolveContextLimitForProfile(profile))` 与 `setModelLocal(LlmProtocolRegistry::isLocal(...))`（local-only 云门控）。
- **`IAIService` 消费者不止 `AIAgent`**：`core/agent/AIAgent`（主循环）、`core/agent/EpisodicService`（长记忆摘要）、`core/tools/SubAgentTool` + `core/agent/SubAgentRunner`（子代理，经 `main.cpp` 的服务工厂）、`viewmodels/ConnectionTester`（设置页测试连接）、`viewmodels/CommitMessageDrafter`（生成提交信息）各持独立服务实例。`AIAgent` 按 `requestStarted` 的 id 丢弃被取代/取消的回复，把 `reasoningChunk` 与 `streamingChunk` 写进不同节点。
- `viewmodels/ConnectionTester`、设置页：默认端点/模型/协议候选来自描述符（避免各处 if-else 判 provider 字符串）。
- `core/utils/NetworkProxyConfig.h`：代理工厂定义处；本目录只依赖「默认工厂」这一隐式约定。

## 关键文件

| 文件 | 关键内容 |
|---|---|
| `IAIService.h` | `LlmProfileConfig`、5 信号契约、`configure`/`model` 默认可选实现 |
| `LLMServiceBase.h` | `ProviderRequestState`/`LLMRequestContext`/`resetStreamState`、8 个协议钩子 |
| `LLMServiceBase.cpp` | `startReply`/`postReply`/`processIncoming`/`handleReplyFinished`、瞬时错误白名单、REL-1001 重试 |
| `LlmProtocolRegistry.h/.cpp` | `LlmProtocol::k*` 常量、5 条描述符、`resolve`/`providers`/`isLocal`/`createServiceSet` |
| `OpenAIService.h/.cpp` | `reasoningEffortParam`、`accumulateName`/`accumulateArguments`（容错重发式网关）、`convertMessagesForOpenAI` |
| `OpenAIResponsesService.h/.cpp` | `convertMessagesToInput`/`instructionsFromMessages`/`convertTools`、`callKey`/`slotFor`/`finalizeCalls` 三键归位 |
| `AnthropicService.h/.cpp` | `convertMessages`（system 折叠 + tool_result run-merge）、`thinkingBudgetTokens` |
| `OllamaService.h/.cpp` | NDJSON 解析、`thinkFlagForLevel`、`parseToolCalls`（新/旧两种形状 + 合成 id） |
| `GeminiService.h/.cpp` | `sanitizeSchema`/`sanitizeSchemaNode` 子集降级、`buildStreamUrl`/`sanitizeModelName`、`clampedThinkingBudget`、`isBlockedFinishReason` |
| `tests/test_protocol_registry.cpp` | 描述符完整性、`resolve` 三条回落、厂商默认协议、service set 覆盖 |
| `tests/test_llm_parsers.cpp` | 各协议纯函数/流式驱动（`Testable*` 暴露受保护入口）、usage 捕获、通道分离、重试累积清理 |

## 约束与风险

- **单在途请求**：`m_current` 只保留最后一个 ctx；并发 `chatComplete` 会静默让前一个请求「失联」（不会被 abort，只在 finished 时 `deleteLater`），其响应被丢弃。调用方必须一次只发一条，或用不同 service 实例。
- **取消没有专门语义**：`cancel()` 后 finished 仍走兜底梯子——已积累内容会作为成功 `responseReady` 发出（部分答案），空内容则报 `Network error: Operation canceled`。消费方必须靠 `requestId`/自身 generation 丢弃。
- **重试无退避**，`429` 立即重发一次，可能再吃一次限流；且只覆盖 `attempt==0` 的首次失败。
- **Responses 不回传 reasoning item**（含 `encrypted_content`）：思考模型多轮工具调用时推理不连续；补上要动 `LLMResponse` 与历史表结构（头注释已登记为独立一步）。
- **Responses 无状态**：`store=false` + 每轮全量重发 `input`，token 成本随轮数线性上升，完全依赖 AIAgent 的裁剪。
- **Anthropic `input_json_delta` 归到「最后一个 tool_use 槽」**，不校验 content block index：多工具交错流理论上会串参数。`signature_delta` 被丢弃（不回放历史，无影响）。
- **思考预算静默降级**：Anthropic/Gemini 输出上限太小时 `thinkingBudget=0`，只有 `qWarning`，用户看不到「思考没生效」。
- **Gemini 的 `off` 不是关闭思考**，服务端默认动态思考仍会消耗 `thoughtsTokenCount`（且计入 `tokensUsed`）。
- **Gemini schema 降级是丢约束而非失败**：`pattern/minimum/additionalProperties` 等被裁掉，参数合法性只能靠工具层自己校验；不可降级的工具被静默跳过（仅日志）。
- **空 usage 不兜底**：任何被截断/中途失败的流都可能 `tokensUsed=0`，计费与校准口径依赖 Agent 侧估算。
- **API key 校验策略不一致**：Chat/Responses 对 `localhost/127.0.0.1` 免 key；Anthropic/Gemini 无本地豁免，空 key 直接 `errorOccurred`（不发请求）。
- 新增一种协议只需在 `descriptors()` 加一条 + 一个 Service；一旦在别处出现 `if (provider == ...)`，就说明绕过了这张表。

## 相关

上级索引：[../codemap.md](../codemap.md) ｜ 嵌入后端：[../embed/codemap.md](../embed/codemap.md) ｜ Agent 消费方：[../../core/agent/codemap.md](../../core/agent/codemap.md) ｜ 代理：[../../core/utils/codemap.md](../../core/utils/codemap.md)
