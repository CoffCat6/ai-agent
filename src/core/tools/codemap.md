# src/core/tools/ — 工具契约、注册表、沙箱与联网工具

## 职责

本目录是「模型可调用能力」的定义与实现边界：

- `ITool.h`：全部工具的唯一抽象契约（`ToolDefinition` / `ToolResult` / `ToolCallInfo` / `FileCheckpoint`）。
- `ToolRegistry.cpp/.h`：工具注册表单例，内建 11 个文件/shell/git/todo 工具（`FileReadTool`、`ListDirTool`、`FileWriteTool`、`EditFileTool`、`ShellExecTool`、`GlobTool`、`GrepTool`、`GitStatusTool`、`GitDiffTool`、`GitCommitTool`、`TodoWriteTool`），并负责 schema 暴露、用户禁用持久化、策略拦截。
- `Sandbox.h` + `SandboxWin.cpp`：路径级安全策略（系统目录、黑名单、读写允许层、workspace 边界、Windows reparse point 解析）。
- `WebSearchBackend.h/.cpp`：搜索后端抽象（DuckDuckGo HTML / 智谱 / DeepSeek）、结果结构与 `WebSearchProviderRegistry`。
- `WebSearchTool.h`、`NetworkFetchTool.h`：两个联网工具（header-only，`web_search` / `http_fetch`）。
- `SubAgentTool.h/.cpp`：`spawn_agent` 派生器（角色档案、并发闸、线程局部进度钩子；目录快照在 GUI 线程取）。`setContextLimit()` 随 profile 快照刷新子代理的上下文窗口（docs/35 P4.1，0 = 不压缩）；并发闸排队时每 250ms 发 `phase="queued"` 进度事件（P4.5）。
- `ProposeEditTool.h/.cpp`（docs/34 P3.1）：子代理专用的 `propose_edit`——锚点式 find/replace 提案，**永不落盘**。校验（项目根内、UTF-8 文本、单条 find 恰好命中一次、同调用内按顺序模拟）通过后推进线程局部 `SubAgentProposalSink`；Runner 在报告末尾渲染 `## PROPOSALS` 段，主代理用 `edit_file` 应用。工具不进全局注册表，只有角色白名单显式点名才授予（`SubAgentTool::execute` 的能力门）；配额常量在头文件（3 条/调用、20 条/运行、4000 字符 find、8KB replace、1MiB 文件）。

**不含调度**：真正的调用分发、确认流程、并发编排在 `core/agent/AIAgent.cpp`（`processNextToolCall` / `dispatchToolExecution` / `finishToolExecution`）。本目录只提供「可被调用的对象」与被调用的安全/网络实现。

## 设计（模式/抽象/关键类型）

### ITool 契约（`ITool.h`）
- `definition()` 返回 `ToolDefinition{name, description, parameters(JSON Schema), level, autoEditSafe, readOnly}`；每次调用都会重新构造（`ToolRegistry::listAll` 也逐次调用，无缓存）。
- `execute(args)` 是纯同步阻塞语义，运行在 `QtConcurrent` 池线程；返回 `ToolResult{success, output, error, checkpoint, source, metadata}`。`source` 是「这次请求打到了哪个外部服务」（只有 `web_search` 填充，值 = 后端 `providerKey()`），经 `AIAgent::appendTraceNode` 的白名单进 `trace_nodes.metadata`，会话恢复后仍可显示。`metadata`（docs/34 P3.2）是结构化附加数据，下游同样按白名单挑键（目前只有 spawn_agent 填 `subagentProposals`）。
- 协作式取消：`using Cancellation = std::shared_ptr<std::atomic<bool>>`。`execute(args, cancelled)` 默认实现只在入口检查一次；`cancel(cancelled)` 默认置位原子并转调无参 `cancel()`。契约要求 `cancel()` 只允许翻原子/关句柄，**不得阻塞、不得跨线程触碰 execute 的非原子状态**。
- `generatePreview(args)` 在 GUI 线程、派发前调用，只用于确认弹窗预览（`write_file`/`edit_file` 生成 unified diff，`git_commit` 生成 staged/工作区摘要），必须快且不阻塞。
- `FileCheckpoint`：G4 撤销/审计载荷。写入前捕获原内容（>1MiB 记 `truncated`），写入后补 `diff`、`addedLines`/`removedLines`、`afterHash`(SHA-256)。checkpoint 随 `ToolResult` 回到 GUI 线程再由 agent 落库（`QSqlDatabase` 线程绑定）。

### 三级权限与两个正交标记
`enum class ToolLevel { Safe, RequiresConfirm, Dangerous }`：
- `Safe`：只读，直接执行（read_file/list_dir/glob/grep/git_status/git_diff/todo_write/search_knowledge_base）。
- `RequiresConfirm`：写状态，逐次确认（write_file/edit_file/shell_exec/git_commit/web_search/http_fetch/MCP 工具）。
- `Dangerous`：**永不暴露给模型**（`toolNamesForModel` 与 AIAgent 的 toolsArray 都过滤）。

`Dangerous` 之外还有两层「禁用」语义，与本目录强相关：
- 用户禁用：`ToolRegistry::setToolEnabled` → 持久化 `tools/disabled`（`SettingsRepository`）。
- 批量替换（docs/31 S4.6 预设包导入）：`setDisabledTools(names)` 只保留已注册工具名（未注册/已断开的 MCP 工具丢弃），集合不变不写盘，一次写盘一次 `toolsChanged`；写失败恢复原集合并 `qWarning`。`disabledToolNames()` 返回当前被禁用的已注册工具名（大小写不敏感升序），供导出与断言。
- 策略拦截：`setPolicyBlocked`（localOnly 隐私门控，仅内存，不落盘，避免覆写用户开关）。
- 最终判定 `isToolUsable = isToolEnabled && !isPolicyBlocked`。

`autoEditSafe` / `readOnly` 是给会话 `PermissionMode` 用的能力声明：AutoEdit 只放行 `autoEditSafe`（write_file/edit_file/web_search/http_fetch），PlanOnly 只额外放行 `readOnly`（web_search/http_fetch），`shell_exec`、`git_commit`、MCP 工具在任何自动模式下仍需确认。

### ToolRegistry
- 单例 `instance()`，构造即 `registerBuiltins()` + `loadDisabledFromSettings()`。
- 双索引：`m_ownedTools`（`std::vector<std::shared_ptr<ITool>>`，顺序即 UI/schema 顺序）+ `m_tools`（`QHash<name, shared_ptr>`，查表）。
- `registerTool` 接受 `shared_ptr`（`unique_ptr` 隐式转换，旧调用点无需改）。
- `find()` 裸指针（仅安全读）；`findShared()` 供派发路径在池线程执行期间持有强引用；`takeTool()` 用于 MCP 断开时摘除工具并交还所有权。
- 名称集合 `toolNamesForModel()` 供模型纠错提示（未知工具时回显可用名单）。
- `toolsChanged()` 信号驱动 UI 刷新（注册/摘除/禁用/策略变更）。

### Sandbox（静态工具类，`= delete` 构造）
判定顺序固定（优先级高→低）：
1. `\\.\` / `\\?\` device 前缀直接拒（`SystemPath`）；
2. 相对路径先锚定 `resolvePath()`（项目激活时以项目根为基准）；
3. `cleanPath()` 规范化：Windows 走 `sandboxNativeFinalPath()`（`CreateFileW` + `GetFinalPathNameByHandleW`，穿透 junction/symlink，因为 Qt 的 `canonicalFilePath()` 在 Windows 不穿透），POSIX 走 `canonicalFilePath`；对「尚不存在的尾部」做词法拼接，保证「校验路径 == 实际触碰路径」；
4. 系统目录硬封锁（`C:\Windows`、`C:\Program Files*`、`/etc`、`/usr` …）；
5. 黑名单（绝对拒绝层，先于任何允许规则）：内置 `~/.ssh`、`~/.gnupg`、`~/.aws`、`*.pem/*.key/*.ppk/*.kdbx` + 用户追加项（目录前缀 / glob / 相对目录名）；
6. 允许层：读 = 用户目录（Documents/Downloads/Desktop/AppData）∪ 当前项目目录，或 FullDisk 全盘；写 = 上述 + Home（项目目录仅在 `projectWrite` 显式开启后加入）。
- 拒绝原因 `DenyReason` 由 `denialHint()` 转成可直接拼进 `ToolResult::error` 的文案。
- `currentProjectDir()`：线程局部 `ExecutionScope` 快照优先，回落到全局活动项目；四个策略静态量由 `QReadWriteLock(Recursive)` 保护（GUI 线程写、池线程读）。**任何新开的工具执行线程（如子代理内部并发 worker，docs/35 P4.2）都必须在 lambda 内重建 `ExecutionScope`**，否则相对路径丢失主代理钉住的项目根。

### 联网工具
- `WebSearchBackend` 无状态、`search()` 为 `const`；可选 `requiresApiKey()`、`notReadyReason()`、`errorReason` 出参——把「没配置」「请求失败」「零结果」三态分开。
- `WebSearchProviderRegistry`：后端元数据表（`key`/`label`/`requiresApiKey`/`apiKeySettingKey`）+ `create()` 工厂 + `defaultProviderKey()`。**设置 UI 下拉、`SettingsSchema` 的 options 与 `main.cpp` 的工厂全部读这张表**，新增后端只改一处。
- `DuckDuckGoBackend`（默认，免密钥）：抓 `html.duckduckgo.com/html/`，正则解析 `result__a`/`result__snippet`，解包 `uddg=` 跳转；`looksLikeAntiBotChallenge()` 识别 202 + `anomaly.js` 反爬页——不识别的话挑战页会被误报成「没找到结果」。
- `ZhipuSearchBackend`：POST `https://open.bigmodel.cn/api/paas/v4/web_search`，Bearer；`search_query`/`search_engine`/`search_intent` 三项必填（漏 `search_intent` 直接 400）；结果 `search_result[]`，链接字段是 `link` 不是 `url`；`search_query` 硬上限 70 字符，超长在发请求前拦截。
- `DeepSeekSearchBackend`：POST `https://api.deepseek.com/v1/tools/search`，Bearer（与对话 API 同一把 key）；`need_content=false` 是刻意的——`content` 是整页正文会撑爆 6000 字符预算，映射 `summary`（600~800 字符，官方定位「给模型阅读」）回退 `snippet`。
- `WebSearchTool`：`RequiresConfirm` + `autoEditSafe` + `readOnly`；`max_results` 夹到 1–10（默认 5），总输出 6000 字符，单条 snippet 截 800；配置（backend/apiKey/timeout）通过 `std::mutex` 快照，**backend 可在飞行中被替换而不悬垂**。所有返回路径（含失败）都把后端的 `providerKey()` 写进 `ToolResult::source`，界面据此显示检索来源徽章。
- `NetworkFetchTool`：GET-only，`RequiresConfirm` + `autoEditSafe` + `readOnly`；SSRF 预检（字面量地址直接判，域名走异步 `QHostInfo` + 200ms 切片 DNS loop），拒绝 loopback/RFC1918/CGNAT/link-local/ULA/多播等；`ManualRedirectPolicy`（3xx 显式报失败并回显 Location）；响应体 1MiB 线上截断、正文 6000 字符；HTML→文本为极简正则剥离，UTF-8 失败回退 local8Bit。

## 数据与控制流

1. **注册**：`ToolRegistry::instance()` 首次触达 → 11 个内建工具入库；`main.cpp` 再追加 `search_knowledge_base`、`http_fetch`、`web_search`（后两者保留裸指针以便设置变更时热更新）；`McpManager` 动态注册 `mcp__<server>__<tool>`，断开时 `takeTool`。
2. **schema 暴露**：`AIAgent::processIteration` 遍历 `listAll()` → 过滤 `Dangerous` / `!isToolUsable` / `!toolAllowedForModel`（localOnly 门控）→ 拼 OpenAI `{type:function, function:{name,description,parameters}}`；schema token 计入上下文预算（`estimateReservedTokensForNextRequest` 同口径）。
3. **调用分发**（AIAgent）：LLM `tool_calls` → `processNextToolCall` 依次判定「未注册 → 回显可用工具名」「被禁用/策略拦截 → 结构化错误」「PlanOnly 且非 Safe/非 readOnly → 拒绝」「`autoApproved` → 入并行队列，否则发 `toolConfirmRequired` 并挂起」。
4. **执行**：`dispatchToolExecution` 取 `findShared`（保活）+ 新建 `shared_ptr<atomic<bool>>` 取消令牌 → `QtConcurrent::run` 内构造 `Sandbox::ExecutionScope(projectRoot)` 后 `tool->execute(args, cancelled)`；结果经 `QFutureWatcher` 回 GUI 线程 `finishToolExecution`（写 trace、tool 消息、审计、checkpoint 落库、todo 同步）。
5. **并发**：同一轮多个自动放行工具并行派发；`m_inFlightTools` 记录在飞工具，`m_toolCancellations` 记录令牌；`stop()` 遍历调用 `cancel(token)`（只翻原子）；全部完成才 `processNextToolCall`，保证工具结果按轮次成批回灌。
6. **超时/裁剪**：`shell_exec` 1–60s（默认 10s），200ms 切片轮询取消/超时，输出 8MiB 上限后 `taskkill /T /F` 杀进程树，回灌 8000 字符；`http_fetch` 1–60s（默认 15s）；`web_search` 5–60s（默认 30s）；AIAgent 统一把工具结果裁到 6000 字符（trace 1500）。

## 集成点

- `core/agent/AIAgent`：唯一的调度者；依赖 `ITool`/`ToolRegistry`/`PermissionMode`。
- `viewmodels/AgentViewModel`：`refreshToolRegistry()` 把 `listAll()` 映射成 QML 模型（name/description/level/enabled/blockedByPolicy/source）；`setToolEnabled` 直通注册表。
- `main.cpp`：注入 `Sandbox::setReadPolicy/setProjectWriteEnabled/setUserDenyList`、`fetchPtr->setAllowPrivateTargets`、`webPtr->setApiKey/setBackend`（provider 工厂）、`setPolicyBlocked`（localOnly → `web_search` + 所有 `mcp__*`）。
- `core/mcp/McpToolAdapter`：把 MCP 工具包装成 `RequiresConfirm` + `readOnly=false` 的 `ITool`。
- `core/git/GitService`：git 工具的实际执行体；`core/utils/Activity`：联网工具的 usage 轨迹；`core/memory/SqliteMemory`：审计与 checkpoint 的落库方。
- 设置层：`SettingsRepository` / `SettingsSchema` 键 `tools/disabled`、`security/*`、`webSearch/*`。

## 关键文件

| 文件 | 规模 | 内容 |
|---|---|---|
| `ITool.h` | 113 行 | 契约、`ToolLevel`、`FileCheckpoint`、取消协议、preview |
| `ToolRegistry.h` | 85 行 | 双索引注册表、双层禁用、`findShared`/`takeTool`、批量禁用 |
| `ToolRegistry.cpp` | 1507 行 | 11 个内建工具 + `unifiedDiff` + checkpoint 捕获/收尾 + 注册表实现 |
| `Sandbox.h` | 418 行 | 路径策略、黑名单、`ExecutionScope`、`cleanPath`、锁 |
| `SandboxWin.cpp` | 51 行 | `GetFinalPathNameByHandleW` 解析 reparse point（`\\?\` 前缀归一） |
| `WebSearchBackend.h/.cpp` | 161 / 534 行 | 后端抽象 + 注册表 + DDG/智谱/DeepSeek 实现 |
| `WebSearchTool.h` | 175 行 | `web_search` 定义、快照配置、结果格式化 |
| `NetworkFetchTool.h` | 334 行 | `http_fetch`、SSRF 白/黑名单、切片取消、HTML→文本 |

## 约束与风险

- **沙箱只管路径，不管命令**：`shell_exec` 的命令内容不做白名单，只靠 `RequiresConfirm`/`PermissionMode` + 环境变量剥离（`*API_KEY*`/`*TOKEN*`/`*SECRET*`/`*PASSWORD*`）兜底；模型可用 `cmd /c` 读写沙箱允许层内任何文件。
- **策略静态量是进程级**：`s_readPolicy`/`s_projectWrite`/`s_userDenyList` 影响所有 tab；`ExecutionScope` 只固定项目根，不固定读写策略。`validatePath` 在持锁期间做文件系统解析，属于毫秒级阻塞。
- **`isDenied` 每次校验都复制默认黑名单并追加用户项**（`QStringList` 分配/比较），高频 grep/glob 场景下有可测开销。
- **`registerTool` 同名覆盖不清理 `m_ownedTools`**：重复注册同名工具会让 `listAll()` 出现重复条目（MCP 重连路径依赖 `takeTool` 先摘除，若顺序出错会暴露该缺陷）。
- **注册表无锁**：`QHash/QSet` 假定只在 GUI 线程读写；`findShared()` 也在 GUI 线程调用，池线程只持有已取出的 `shared_ptr`。
- `web_search` 的取消标志是单成员 `std::atomic`，同一 `WebSearchTool` 实例被多 tab 共享时，`cancel()` 语义是「取消该实例当前/下一次调用」，只在 `execute` 入口重置；真正的并发隔离依赖 AIAgent 的 `Cancellation` 令牌（`WebSearchTool` 只读 `m_cancelRequested`，令牌取消不传导到后端）。
- 三个搜索后端的 HTTP 往返统一走 `sendRequest()`（`QNetworkAccessManager` + `QEventLoop`），**必须在非 GUI 线程调用**；超时被强制夹到 5–60s，调用方传入更小值无效。凭据一律走 `Authorization: Bearer` 头（不再有把 key 放进 JSON body 的后端）。
- 搜索密钥**按 provider 分桶**（`webSearch/zhipuApiKey` / `webSearch/deepseekApiKey`，各自 DPAPI 加密）：共用一个键会把 A 平台的密钥发给 B 平台。`SettingsService::webSearchApiKeyValue()` 读的是**当前选中 provider** 那一把，切换 provider 时必须补发 `webSearchApiKeyChanged` 并在同一个 `applySettings` 里成对执行 `setApiKey` + `setBackend`。
- `http_fetch` 明确不覆盖 DNS rebinding，也不做内容类型过滤（二进制会被当文本处理）；代理由全局 `QNetworkProxyFactory` 决定，SSRF 判定基于解析结果而非实际出口。
- 文件类工具全靠 `kMaxReadFileBytes`(8MiB)/`kMaxWriteFileBytes`(8MiB)/`kMaxFileChars`(20000)/`kMaxOutputChars`(8000) 限流；`read_file` 的字符截断发生在整文件读入后，只有字节上限是读前拒绝。
- `edit_file` 是字节级精确匹配（不归一化换行），CRLF 文件用 LF 的 `old_string` 会报「not found」——这是刻意契约（`editFileCrlfBoundary` 测试固定）。docs/34 P3.3：未命中时 `relocateHint` 附确定性重定位建议（CRLF/LF 差异、空白归一化后唯一行号、首行行号、通用重读建议），探测失败不猜、语义仍为失败；见 `editFileNotFoundSuggestsRelocation`。
- `NetworkFetchTool.h` 未列入 `nexus_core` 的 CMake 源列表（header-only，由 `main.cpp` 直接 include 进 `nexus_ai`），修改时不会触发 core 重编译依赖链。
