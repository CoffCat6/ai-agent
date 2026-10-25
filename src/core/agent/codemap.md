# src/core/agent/ — ReAct 代理核心与情节记忆

## 职责

- `AIAgent` 承担一次用户请求的完整生命周期：消息落库 → 组装系统提示/上下文 → 调 LLM → 执行工具并回灌结果 → 收敛为最终回答；同时维护轨迹（`m_traceModel`）、状态机、Token/步数/时间预算、会话恢复与审计。
- 工具确认：`Safe` 直接执行，`RequiresConfirm` 在 `ConfirmChanges` 下挂起等待 `approveToolCall()`，`Dangerous` 永不进入请求的 tools schema。
- `EpisodicService` 在任务完成后异步生成会话摘要并落库；新会话启动时异步检索相关摘要，拼进系统提示（仅新会话，不用于 `resumeSession`）。
- 本目录只定义领域类型与编排逻辑；网络协议、SQLite、具体工具实现分别由 `IAIService`、`IMemory`、`ITool` 注入，不直接依赖 Qt UI（`nexus_core` 不链接 Qt6::Gui）。

## 设计（模式/抽象/关键类型）

- **组合 + 依赖注入**：`AIAgent` 持有 `IAIService*`、`ToolRegistry*`、`IMemory*`、`EpisodicService*`；`setLLMService()` 重连五路信号，换服务时先 `stop()` 终止在途任务。`setOwnedMemory()` 为每个 tab 接管独立 `SqliteMemory`，析构时先把 `EpisodicService` 的 memory 摘掉再释放。
- **每 tab 一个 agent**：`main.cpp` 的 `agentFactory` 创建 tab agent，各自拥有 memory 与协议服务；`ChatSessionController` 桥接 QML 与该 agent。
- **状态机**：`AgentState{Idle, Thinking, AwaitingToolConfirm, ExecutingTool, Summarizing, Completed, Error}`，由 `setState()` 去重后发 `stateChanged`。`finishTask()` 同步连发 `Summarizing`→`Completed`，不等待情节摘要。
- **并发模型**：LLM 回调在 GUI 线程；工具用 `QtConcurrent::run` + `QFutureWatcher` 在池线程执行，结果回 GUI 线程续跑。`Sandbox::ExecutionScope(projectRoot)` 把项目根钉在派发时快照。
- **失效令牌**：`m_epoch` 在 `startTask/stop/resetSession/restartTaskFromAnchor` 递增；工具完成回调若 `epoch != m_epoch` 只把节点标 `cancelled`，不续循环。`m_activeRequestId` 丢弃被新请求取代的 LLM 响应。
- **工具生命周期**：`m_inFlightTools`（call id → `shared_ptr<ITool>`）与 `m_toolCancellations`（call id → `shared_ptr<atomic<bool>>`）让 `stop()` 能协作取消并保证工具在注销后仍存活到 `execute()` 返回。
- **权限**：`PermissionMode{ConfirmChanges, AutoEdit, FullAccess, PlanOnly}`，默认 `ConfirmChanges`；内存态不持久化，由 `ChatSessionController` 下发。`permissionModeToString/FromString/isValidPermissionMode` 提供稳定字符串与 QML int 校验。
- **核心类型**：`LLMResponse`（content/toolCalls/tokensUsed/requestId/分类 token/reasoning）、`ToolCallInfo`、`ToolDefinition`（`level`、`autoEditSafe`、`readOnly`）、`ToolResult`（output/error/checkpoint）、`FileCheckpoint`。
- **预算四件套**：`m_maxIterations`（默认 30，钳制 3–1000）、`m_maxToolExecutions`（`max(16, 2×步数)`）、`m_contextLimit`（默认 128000，钳制 2048–1048576）、`m_taskTimeoutMs`（默认 300000，0 关闭）。
- **轨迹**：`appendTraceNode()` 白名单字段落 `m_traceModel` 并 `IMemory::saveTraceNode()`；`order` 单调递增，恢复会话时按 `loadTraceNodes()` 重建。流式正文进 `summary` 节点，思考进独立 `thinking` 节点（上限 64 KiB，永不回灌模型）。
- **闲聊无独立开关**：没有 chat-mode 枚举/属性；“闲聊直接回答”由系统提示中的 small-talk 规则、`collapseOldToolRounds()`（只保留最近 2 轮工具往返，旧的替换为中性 assistant 说明）和重复调用终止器共同实现。

## 数据与控制流（具体调用序列、状态机）

1. `startTask(text, images, files)`：拒绝 `m_active`、空输入、无 `IAIService`、配额 strict 拒绝。新会话建 UUID、`beginSession`、`setSessionProject`、清零累计 token，发 `sessionChanged/sessionsChanged`；随后重置循环字段并 `++m_epoch`。
2. 追加 user 消息（`images` 为 data URL，`files` 为结构化字段），`saveMessage()` 回填主键，`appendTraceNode("user_message")`。
3. 新会话且配置了 `EpisodicService`：`retrieveRelevantSummariesAsync()`，`m_active=true` 占位，登记一次性 `retrievalReady` 连接与 `kEpisodicLaunchTimeoutMs=200ms` 定时器；回调按 requestId 与 epoch 校验后 `resumeTaskLaunch(injection)`。超时或 `requestId==0` 则无注入启动。
4. `launchTaskTail(fresh, injection)`：`effectiveSystemPrompt()`（内置/用户 override + 项目块 + **G3 项目规则块** + 知识库引用约定）+ 摘要注入。系统提示只在 `fresh` 会话组装一次，会话中途改规则不会重发，`prepend` 为 system 消息；先按预算预压缩再 `notifyTokens()`；启动任务超时定时器，进入 `processIteration()`。
5. `processIteration()`：若 `m_iteration >= m_maxIterations`，首次触发 `m_forcedFinal=true` 并注入“禁止再调工具”的请求级 `m_forcedFinalPush`（只进本次请求副本，不进历史）；否则 `++m_iteration`。`collapseOldToolRounds()` 后构建 tools schema（排除 `Dangerous`、`isToolUsable()` 为假、`toolAllowedForModel()` 为假者；forced-final 时为空），以 schema token 预留调 `applyContextBudget()`。追加 running 的 `thinking` 节点，`setState(Thinking)`，把 `files` 文本折叠进请求副本，`chatComplete(requestMessages, toolsArray)`。
6. 流式：`IAIService::streamingChunk` → 首个增量创建 `summary` 节点，100ms 节流刷新标题；`reasoningChunk` 累积到 `m_reasoningText` 并刷新 `thinking` 节点。agent 不显式请求流式，是否流式由协议实现决定；非流式时无增量，最终由 `onLLMResponse` 定稿。
7. `onLLMResponse()`：校验 requestId → 记录延迟/`recordTokenUsage()` → 定稿 thinking（优先 Provider 的完整 `reasoning`）→ 失败走 `failTask()`。成功则把 assistant 消息（无正文时 content=null，含 `tool_calls`）追加进历史；空/重复 tool_call id 用全局 `call_N` 重写。
8. 无工具调用：若流式节点存在则就地定稿，`finishTask(content, streamed=true)`；否则新建 `summary` 节点后完成。`finishTask()` 写 `task_complete` 审计、`metrics` 节点、非空回答的 assistant 消息、`touchSession()`，触发情节摘要生成，发 `sessionsChanged/responseReady`，状态 `Summarizing`→`Completed`。
9. 有工具调用：先做重复轮检测（signature = 每个 `name:compactArgs;`）。检测到重复（`m_repeatToolCount==1`）时：正文 ≥40 字则封口并完成；否则跳过执行、为每个调用写 `[Repeated call skipped]` 的 tool 消息并重入 LLM；`m_repeatToolCount>=2` 直接以现有正文收敛。
10. `processNextToolCall()`：逐个取出 pending。未注册工具→失败节点 + 可用工具名 + `unknown_tool` 审计；被禁用/策略拦截→`tool_disabled`；`PlanOnly` 且非 `Safe` 且 `!readOnly`→`tool_blocked`。自动放行规则：`Safe` 恒真；`RequiresConfirm` 在 `FullAccess` 全放行、`AutoEdit` 仅 `autoEditSafe`、`PlanOnly` 仅 `readOnly`；`Dangerous` 永不自动放行。需确认时先把已收集的并行安全调用派发掉，`generatePreview()` 生成预览，追加 `waiting_confirm` 节点，`setState(AwaitingToolConfirm)`，发 `toolConfirmRequired` 后返回。
11. 自动放行的调用收集为并行批次；派发前检查 `m_toolExecutions >= m_maxToolExecutions`，超限则为剩余调用写“预算耗尽”tool 消息、置 `m_forcedFinal`，等所有在途工具结束后再 `processIteration()`。
12. `dispatchToolExecution()`：再次校验可用性；预算检查；节点置 running；`setState(ExecutingTool)`；`++m_toolExecutions`；`findShared()` 取强引用，建 `atomic<bool>` 取消标志并登记 epoch；`QtConcurrent::run` 执行 `execute(args, cancelled)`。完成回调清理登记，epoch 不符则标 cancelled，否则 `finishToolExecution()`。
13. `finishToolExecution()`：节点 success/failed、`tool_result` 节点（截断 1500 字）、tool 消息（截断 6000 字）、`tool_execute` 审计（自动放行标注模式，MCP 名称解析出 `mcp_server`）、写文件 checkpoint、`todo_write` 刷新 `m_todoList`；所有并行调用结束后 `processNextToolCall()`。
14. `approveToolCall(id, approved)`：id 必须等于 `m_confirmNodeId`。拒绝→cancelled 节点 + “用户拒绝”tool 消息 + `tool_declined` 审计 + 继续循环；批准时若已切到 `PlanOnly` 且工具会改状态→拒绝执行；否则 `tool_approved` 审计后带原节点 id 派发。
15. `stop()`：`m_active=false`、`++m_epoch`、停任务定时器；对 `m_inFlightTools` 逐个 `cancel(cancellation)` 并清表；把 pending、待确认与 `m_currentExecutingCall` 封口为 `[Not executed] ...` tool 消息（并行在途的其余调用见风险）；确认节点置 cancelled；`IAIService::cancel()`；收尾 thinking/summary 节点；`Idle` + `touchSession()`；保留已有轨迹，不发 `responseReady`。
16. 恢复/重生成：`resumeSession()` 仅空闲可用，重建 system 消息，`loadMessages(200)` 只回放 user/assistant（工具证据按 MEM-701 不重灌），优先 `loadTraceNodes()` 重建轨迹，否则由消息合成；按新窗口压缩，`sessionTokensUsed` 从 `sessionTokenBreakdown()` 恢复。`regenerateLast()` 先 `rewindLastTurn(false)`（`truncateLastTurn()` 事务失败则内存不动），再 `restartTaskFromAnchor()`；`editLastUserMessage()` 以 `removeUser=true` 截断后带原图片/附件重新 `startTask()`。
17. 上下文预算：`applyContextBudget(reserved)` 用 `capacity = limit - reserved`，再除以估算校准系数交给 `TokenEstimator::compressToFit()`；发生压缩时追加 `context` 节点。窗口变大且空闲时 `restoreFullHistoryIfAvailable()` 重载最多 2000 条消息。`notifyTokens()` = 消息估算 × 滚动校准系数 + tools schema 预留；校准系数由真实 `promptTokens / 原始估算` 的 EWMA（α=0.3，钳制 0.5–3.0）维护，新会话归零。
18. 配额与计费：`startTask/regenerateLast/editLastUserMessage` 先 `checkQuota()`；`quotaStatus()` 对比 `SettingsSchema` 的日/月 token/费用上限，超限发 `quotaWarning`，strict 模式拒绝。`recordTokenUsage()` 拆分输入/输出/缓存桶、按真实值比例校准、可选 `PricingCalculator` 计费、`logTokenUsage()` 落库、累加 `m_sessionTokensUsed` 并发 `sessionTokensChanged/costChanged`。
19. 超时：`onTaskTimeout()` 只在 active 时执行，先 `stop()` 再 `finishTask("The task timed out ...")`，保证 UI 不悬空。
20. 情节记忆：`finishTask()` fire-and-forget `generateAndStoreSummary(sessionId, m_messages)`。`EpisodicService` 取非 system 非空消息拼“用户/助手”转录（非 user 角色统一标为助手，含 tool 结果；整体截 4000 字），用独立 LLM 请求生成 ≤500 字中文摘要；同刻只保留一个 pending 请求，旧的丢弃并发 `summaryStored(sessionId,false)`；响应按 requestId 严格配对，成功后 `saveEpisodic()` 先落库、立刻发 `summaryStored(true)`，再由 `QtConcurrent` 生成 embedding 并排队回 `updateEpisodicEmbedding()`；默认 90s 超时清理悬挂连接。
21. 检索：`retrieveRelevantSummariesAsync()` 在无 embedder 时同步完成 FTS 检索并 emit；有 embedder 时把向量网络调用放池线程，DB 扫描与余弦合并留在服务线程，`retrievalReady` 回传。`retrieveMerged()` = `searchEpisodic()`（score 1.0）∪ `searchEpisodicSemantic()`（余弦分），按 id 去重、分数降序取 topK；`buildInjection()` 生成 `## 历史会话摘要（episodic memory）` 列表，累计超过 1500 token 即停止，只有标题时返回空串。

## 集成点（依赖/被消费方/信号槽/接口）

- **IAIService**（`src/infrastructure/llm`）：`chatComplete(messages, tools)`、`cancel()`、`model()`；信号 `requestStarted(quint64)`、`responseReady(LLMResponse)`、`streamingChunk(QString)`、`reasoningChunk(QString)`、`errorOccurred(QString)`。`AIAgent` 只认 OpenAI wire format（tool_calls 参数字符串、tool 消息带 `tool_call_id`），协议转换在 infra。
- **重试边界**：`AIAgent` 收到错误/失败响应只 `failTask()`，不自动重试；瞬态超时的一次透明重试在 `LLMServiceBase`（REL-1001，保留 requestId）。
- **ToolRegistry / ITool**：`listAll()`、`find()`、`findShared()`、`isToolUsable()`、`isPolicyBlocked()`、`toolNamesForModel()`；`ITool::definition()`、`execute(args, Cancellation)`、`cancel(Cancellation)`、`generatePreview()`。注册表是进程单例，`mcp__<server>__<tool>` 由 `McpToolAdapter` 适配为 `RequiresConfirm`。
- **IMemory**：消息 `saveMessage/loadMessages/truncateLastTurn`、会话 `beginSession/touchSession/listSessions/delete/restore/rename/setSessionProject`、轨迹 `saveTraceNode/loadTraceNodes`、情节 `saveEpisodic/searchEpisodic/searchEpisodicSemantic/updateEpisodicEmbedding`、审计 `logAudit/auditLog`、checkpoint、token 统计与配额 `logTokenUsage/quotaStatus/sessionTokenBreakdown` 等。所有方法都有默认 no-op，memory 为空时 agent 降级为纯内存会话。
- **EpisodicService**：信号 `summaryStored(sessionId,bool)` → agent `episodicChanged`；`retrievalReady(requestId,injection)` 供 `startTask` 一次性连接。装配层为每个 agent 配独立的摘要 LLM service，避免与前台流式请求互相顶替。
- **消费方**：`ChatSessionController`（每 tab）转发 `startTask/approveToolCall/stop/regenerateLast/权限模式`，聚合 `state/trace/tokens`；`AgentViewModel` 暴露 QML 单例属性；`main.cpp` 注入 settings 的步数、超时、上下文窗口、系统提示 override 与 local-only 门控，项目根由 `AgentViewModel` 随切换推送。
- **agent 信号**：`stateChanged/traceChanged/responseReady/toolConfirmRequired/errorOccurred/tokensChanged/contextLimitChanged/sessionChanged/sessionsChanged/quotaWarning/costChanged/sessionTokensChanged/todoListChanged/fileCheckpointsChanged/episodicChanged`。
- **构建**：`nexus_core`（Qt6::Core/Sql/Concurrent/Network，无 Gui），`nexus_viewmodels` 与 `nexus_ai` 消费。

## 关键文件

- `AIAgent.h` / `AIAgent.cpp`：ReAct 主循环、确认、预算、流式、会话、审计、配额、项目规则注入（约 2440 行）。
- `AgentState.h`：7 状态枚举 + `Q_DECLARE_METATYPE`。
- `LLMTypes.h`：`LLMResponse`，含 requestId、分类 token、`reasoning`。
- `PermissionMode.h`：四种会话权限、字符串映射、QML int 校验。
- `EpisodicService.h` / `.cpp`：摘要生成/存储、FTS+语义合并检索、注入文本构造、embedding 异步回写。

## 约束与风险

- **线程约束**：`IMemory` 的 SQLite 连接线程绑定，持久化只能在 GUI 线程；工具在池线程，只能通过 `ToolResult` 把 checkpoint 带回。新增工具不得在 `execute()` 内直接写库。
- **取消/封口缺口**：并行派发时 `m_currentExecutingCall` 只记录最后一个调用；`stop()` 只封口它和 pending/confirm，其余在途调用的 tool 消息不会补写，历史可能留下多个 `tool_calls` 只有部分 `tool` 响应的非法序列。恢复/重生成前的 `stop()` 同样受影响。
- **Dangerous 仅在 schema 层过滤**：`dispatchToolExecution()`/`approveToolCall()` 不再校验 `ToolLevel::Dangerous`；当前内置工具未注册 Dangerous，若未来注册，人工批准即可执行。
- **步数不等于工作量**：`m_maxToolExecutions` 只能随 `setMaxIterations()` 按 2× 缩放，无独立设置入口；一轮可声明任意多调用，执行上限是唯一硬闸。
- **上下文开关不一致**：`setContextLimit()` 注释称 0 关闭压缩，但 `qBound(MinContextTokens=2048, ...)` 使 0 无法生效；真正的关闭分支只在 `applyContextBudget()` 的 `m_contextLimit <= 0` 守卫里，实际不可达。
- **无独立闲聊模式**：所谓“闲聊”完全依赖提示词与历史折叠；弱模型仍可能在小招呼叫工具，只能靠重复轮终止器兜底，非确定性行为无硬保证。
- **情节检索时序**：无 embedder 时 `retrieveRelevantSummariesAsync()` 在返回前同步 emit `retrievalReady`，而 `AIAgent` 在拿到 requestId 后才建立连接，信号会丢失并退化为 200ms 超时后无注入启动；生产装配始终注入 embedder，测试/裁剪装配需注意。
- **摘要一致性**：`summaryStored(true)` 在 embedding 生成前发出，UI 可能先看到无向量的摘要，语义检索要等异步回写后才命中；同刻只允许一个摘要请求，新请求会丢弃旧会话的摘要（旧请求发 `false`）。
- **错误处理**：LLM 失败即 `failTask()`，不重试、不回滚已写入的消息/轨迹；工具失败只作为 tool 消息回灌，不终止任务。所有失败路径都依赖 `m_active` 与 epoch 防重入。
- **会话恢复裁剪**：`resumeSession()` 只取最近 200 条 user/assistant；工具证据只存在于轨迹，不进入模型上下文（MEM-701）。扩大到 2000 条仅在窗口变大且空闲时触发。
- **Token 估算**：预算判断基于字符启发式，依赖每会话 EWMA 校准；首请求前误差最大，`contextComposition()` 的桶与 `notifyTokens()` 必须保持同一校准口径，否则 UI 数字互相矛盾。
- **G3 项目规则只在 fresh 会话生效**：`effectiveSystemPrompt()` 仅在 `launchTaskTail(fresh=true)` 与 `resumeSession()` 里调用。同一会话内改了 `AGENTS.md`，下一条消息**不会**带上新规则，必须开新会话/重载会话。设置页的「生效中」是读文件时的状态，不代表当前会话已注入。
- **G3 种子模板判定依赖精确 hash**：`AIAgent::isUntouchedRulesSeed()`（公开静态）用 SHA256 比对去掉行尾换行、且 **CRLF/CR 归一化为 LF** 后的模板（行尾不算内容），并容忍 BOM。任何实质编辑（哪怕只加一个空格）都会让模板变成「有内容」并开始注入。判定逻辑已收敛到该单一函数，`AgentViewModel::projectRulesState()` 复用它，不再有两处副本。
- **G3 只写 `AGENTS.md` 不写 `.nexus/rules.md`**：读取支持两个候选路径，保存只针对 `AGENTS.md`。项目若只用 `.nexus/rules.md`，设置页会显示其内容但保存会另建 `AGENTS.md`，两个文件并存时 `AGENTS.md` 优先。
- **G3 8KB 上限是硬截断**：超出部分丢弃并追加 `[truncated]`，不做分段注入；超长规则文件的有效内容因此可能完全落在被丢弃的后半段。截断点会回退到 UTF-8 字符边界（切在多字节字符中间会产生 U+FFFD，进而误触发 local8Bit 回退把整个文件按 GBK 解成乱码）。
