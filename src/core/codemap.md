# src/core/ — 领域核心层索引

## 职责

- `nexus_core` 是不依赖 Qt6::Gui 的纯业务核心：ReAct 编排、会话/轨迹持久化、工具抽象与注册、RAG 检索、Git 读取、MCP 协议、设置/Token/定价等横切能力。
- 只定义接口与领域逻辑；LLM/embedding 网络实现属于 `nexus_infra`，QML 暴露与 UI 生命周期属于 `nexus_viewmodels`/`nexus_ai`。
- 构建事实：`add_library(nexus_core STATIC ...)`，PUBLIC 链接 `Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Network`；`nexus_infra` PUBLIC 依赖 core，`nexus_viewmodels`/`nexus_ai` 再向上消费。

## 设计（模式/抽象/关键类型）

- **接口驱动**：`IMemory`（持久化）、`ITool`（能力）在 core；`IAIService`（LLM 协议）在 infra。核心只按 OpenAI wire format 交换 `QJsonArray` 消息，不感知供应商差异。
- **组合根在装配层**：`main.cpp` 创建 `SqliteMemory`、`RagService` 与每 tab 的 `AIAgent`，并通过 setter 注入；`ToolRegistry`/`McpManager` 以单例接入，`GitService` 由 `GitViewModel` 或内置 git 工具按需构造。core 内不承担进程级装配。
- **异步边界**：跨线程用 `QtConcurrent` + 队列信号；SQLite 连接线程绑定，落库统一回 GUI 线程；长耗时工具/向量生成在池线程。
- **可选能力**：`IMemory` 的会话书签、审计、轨迹、checkpoint、token 统计等均有默认 no-op，裁剪后端仍可编译并静默降级。
- **配置单一事实源**：`SettingsSchema` 定义键、默认值、钳制范围；`SettingsRepository` 负责 QSettings 身份与读写；`SettingsDefinition` 提供 UI 元数据。

| 子目录 | 一句话职责 | 索引 |
| --- | --- | --- |
| `agent/` | ReAct 主循环、工具确认、预算/取消/会话恢复与情节记忆摘要注入。 | [agent/codemap.md](agent/codemap.md) |
| `memory/` | `IMemory` 抽象 + `SqliteMemory`（消息、轨迹、情节、审计、checkpoint、token、项目的 SQLite 实现）。 | [memory/codemap.md](memory/codemap.md) |
| `tools/` | `ITool`/`ToolLevel` 契约、`ToolRegistry` 注册与启停策略、`Sandbox` 路径边界、`web_search`/`http_fetch` 实现。 | [tools/codemap.md](tools/codemap.md) |
| `rag/` | `RagService` 多文档知识库门面：切块、embedding、余弦检索与持久化恢复。 | [rag/codemap.md](rag/codemap.md) |
| `git/` | `GitService` 以参数列表调用 git，提供 status/log/diff/stage/commit/stash/blame/graph 等只读与显式写操作。 | [git/codemap.md](git/codemap.md) |
| `mcp/` | MCP JSON-RPC 客户端、stdio/HTTP 传输、`mcp_servers.json` 配置与 `ITool` 适配器。 | [mcp/codemap.md](mcp/codemap.md) |
| `schedule/` | 定时任务（docs/50）的调度规格：三种形态的解析/校验/下一次触发纯函数，DST 语义按 QDateTime 固化。 | [schedule/codemap.md](schedule/codemap.md) |
| `utils/` | Token 估算、设置 schema、日志/Activity、定价、代理、`@` 项目文件索引、data URL/附件分类等横切工具。 | [utils/codemap.md](utils/codemap.md) |

## 数据与控制流（具体调用序列、状态机）

1. 启动：`main.cpp` 初始化 `SettingsRepository`/`SettingsService` → 建 `SqliteMemory`（主 agent 共享，tab agent 各自 `setOwnedMemory`）→ `RagService` 注入 embedder → `ToolRegistry` 注册内置工具，`McpManager` 注册 MCP 适配器 → 每个 tab 创建 `AIAgent` 并接 `IAIService`、`EpisodicService`。
2. 一次对话：viewmodels → `AIAgent::startTask()` → `IAIService::chatComplete()` → `ToolRegistry::findShared()` + `ITool::execute()`（池线程）→ 结果回灌 → `IMemory::saveMessage/saveTraceNode/logAudit` → `responseReady` 回到 viewmodels/QML。
3. 新会话注入：`EpisodicService::retrieveRelevantSummariesAsync()` → `IMemory::searchEpisodic` + `searchEpisodicSemantic` → 合并排序 → 系统提示追加摘要；任务完成后 `generateAndStoreSummary()` 异步写回 `saveEpisodic`。
4. 知识库：viewmodels 触发导入 → `RagService::ingestDocument()` → `DocumentRetriever` 切块/embedding → 结果经信号由 viewmodels 持久化；恢复时 `restorePersistedDocument()` 不重算向量。
5. MCP：读取 `mcp_servers.json` → `McpClient` 建传输并 `initialize`/`tools/list` → 每个远端工具包装为 `mcp__<server>__<tool>` 注册；调用时 `McpToolAdapter::execute()` 排队到 GUI 线程发 `tools/call`。
6. Git：`GitService` 只读命令默认 `GIT_OPTIONAL_LOCKS=0`；任何写操作必须由 UI 明确确认，history rewrite 拒绝已发布提交；命令带超时与输出上限，参数不经过 shell。
7. 设置：`SettingsSchema::intSetting/boolSetting` 读取带默认与钳制；`SettingsService` 通过 `SettingsRepository` 持久化并广播变更，`main.cpp` 把步数、超时、上下文窗口、权限默认值、local-only 策略推给各 agent。
8. Token/成本：`TokenEstimator` 做字符启发式估算与上下文压缩；`AIAgent::recordTokenUsage()` 落 `logTokenUsage`，`PricingCalculator` 折算费用，聚合由 `IMemory` 的 token/cost 统计接口完成。

## 集成点（依赖/被消费方/信号槽/接口）

- **上游**：`nexus_infra` 实现 `IAIService`（OpenAI/Anthropic/Ollama 等）与 embedding，向 core 提供 `LLMResponse` 与流式/推理信号；`nexus_viewmodels` 通过 `ChatSessionController`/`AgentViewModel` 调用 agent 并把状态转 QML 属性。
- **下游系统边界**：SQLite（`SqliteMemory`）、外部进程（git、MCP stdio）、HTTP（web search、http_fetch、MCP HTTP）、文件系统（工具 + checkpoint）。
- **共享单例**：`ToolRegistry::instance()` 被所有 tab agent 共用；工具启停（用户禁用层）持久化到 `tools/disabled`，local-only 等策略拦截只存内存，二者经 `isToolUsable()` 合并。
- **跨层契约**：`LLMTypes.h` 的 `LLMResponse` 被 infra 与 core 共用；`PermissionMode` 字符串形式被 `SettingsService` 持久化，QML 侧以 int 传递；`AgentState`/`LLMResponse` 注册 metatype 供跨线程信号转发。
- **被消费方**：`nexus_ai` 可执行文件链接 core/infra/viewmodels；QML 模块 `NexusAI.ViewModels` 只暴露 ViewModel，不直接依赖 core 类型（除注册过的元类型）。

## 关键文件

- `agent/AIAgent.{h,cpp}`：核心编排入口；`agent/EpisodicService.{h,cpp}`：摘要与检索。
- `memory/IMemory.h`、`memory/SqliteMemory.{h,cpp}`：持久化契约与实现。
- `tools/ITool.h`、`tools/ToolRegistry.{h,cpp}`、`tools/Sandbox.h`：能力/权限/路径边界。
- `rag/RagService.{h,cpp}`、`rag/DocumentRetriever.h`、`rag/VectorStore.h`、`rag/KnowledgeSearchTool.h`。
- `git/GitService.{h,cpp}`；`mcp/McpClient.{h,cpp}`、`mcp/McpTransport.h`、`mcp/McpToolAdapter.{h,cpp}`、`mcp/McpConfig.{h,cpp}`；`schedule/ScheduleSpec.{h,cpp}`（docs/50 定时任务调度规格）。
- `utils/TokenEstimator.h`、`utils/SettingsSchema.{h,cpp}`、`utils/SettingsRepository.{h,cpp}`、`utils/PricingCalculator.{h,cpp}`、`utils/ProjectFileIndex.{h,cpp}`、`utils/Logger.{h,cpp}`、`utils/Activity.{h,cpp}`。

## 约束与风险

- **Gui-free 是硬约束**：任何需要 `QImage`/`Qt6::Pdf`/控件的逻辑必须留在 `nexus_viewmodels`；core 只接收已压缩的 data URL 或纯文本附件。
- **线程亲和**：`SqliteMemory` 的 `QSqlDatabase` 只能在打开它的线程使用；工具/embedding 在池线程时不得落库，必须把数据带回 GUI 线程。
- **静默降级**：`IMemory` 大量方法是默认 no-op，注入空 memory 或裁剪后端时功能会消失而不报错；新增调用方需自行判断能力。
- **设置键漂移**：`SettingsSchema::registeredKeys()`、`SettingsService` 与 `test_settings` 互为锁定；新增键必须同步，否则备份校验/默认值会漏。
- **安全边界**：`ProjectFileIndex` 拒绝绝对路径、`..` 越界、指向根外的符号链接与敏感/二进制文件；`GitService` 不跳过 hooks、不重写已发布历史、参数不经 shell；但这些是约定，不替代操作系统沙箱。
- **外部依赖失败**：MCP 子进程崩溃有有限重连；git/HTTP/embedding 失败多以错误字符串或空结果返回，上层需区分“无结果”与“服务不可用”（`RagService::searchDetailed()` 即为此设计）。
- **测试**：`tests/` 覆盖 agent、memory、tools、rag、git、mcp、settings；子目录细节以对应 `codemap.md` 与源码为准。
