# src/core/mcp/

## 职责

MCP（Model Context Protocol 2025-03-26）客户端层：把外部 MCP server 的工具变成内置 `ToolRegistry` 里的 `mcp__<server>__<tool>` 工具。

- `McpTransport` 定义 JSON-RPC 2.0 双向消息管道；`StdioTransport`（子进程 + 按行 JSON）与 `HttpTransport`（Streamable HTTP POST 子集）各实现一份。
- `McpClient` 负责协议生命周期：`initialize` → `notifications/initialized` → `tools/list` → `tools/call`，按 id 复用并发请求，带 30 s 超时与取消。
- `McpToolAdapter` 是 `ITool` 桥接器，把一次模型工具调用转成阻塞等待的 `tools/call`。
- `McpConfig` 负责 `<AppData>/mcp_servers.json` 的读写（QVariantMap 形状，避免 core↔viewmodels 类型耦合）。
- `McpConfigTransfer` 负责 MCP 服务器配置的**独立导入/导出文件**（docs/31 N7）：导出脱敏白名单 + 导入预览/冲突策略，纯函数无 UI 依赖。

## 设计（模式/抽象/关键类型）

- **抽象传输 + 工厂选择**：`McpTransport : QObject` 只有 `start/stop/sendMessage` 三个纯虚函数、`State{Disconnected,Connecting,Connected,Failed}`、`messageReceived/stateChanged/errorOccurred` 三个信号；`McpClient` 构造函数按 `config.transport`（`"stdio"`/`"http"`）创建具体实现，未知值则 `connectToServer()` 直接 `connectionFailed`。
- **`McpServerConfig`**（`McpClient.h:15`）：`name, transport, command, args, env, url, headers, enabled`；`headers` 仅用于 HTTP 静态头（如 Authorization）。
- **`McpTransport::dispatchPayload`**：统一 `QJsonDocument::fromJson`，非对象或解析失败 → `errorOccurred("Malformed MCP message: …")` 并丢弃。
- **`McpClient` 并发模型**：`m_nextId` 单调递增 + `QHash<quint64, PendingRequest>`，`PendingRequest{handler, toolHandler, QTimer*}`；响应按 id 派发，天然支持并发调用。`onMessage` 先 `erase` 再回调，允许 handler 重入发新请求。
- **`McpToolAdapter` 线程桥**：adapter 在线程池执行，client/transport 在 GUI 线程；跨线程只经 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`，用 `shared_ptr<PendingCall>{QMutex,QWaitCondition}` 交接结果，`QPointer<McpClient>` 防 client 提前析构。
- **配置与运行时解耦**：磁盘用 QVariantMap，运行时用强类型 `McpServerConfig`；`McpManager` 是二者之间的装配者。

## 数据与控制流

**stdio 传输**（`StdioTransport.cpp`）
1. `start()`：`Connecting`；Windows 下先用 `QStandardPaths::findExecutable` 按 PATH/PATHEXT 解析；`.cmd/.bat`（npx/uvx 之类）改走 `cmd.exe /d /s /c "<inner>"`，用 `setNativeArguments` 绕过 QProcess 自身引号规则，`quoteForCmd` 把内嵌引号翻倍；解析失败则保留原命令让 QProcess 报 `FailedToStart`。
2. 环境 = `systemEnvironment()` 副本 + `config.env`（值语义，必须先在局部变量插入再 `setProcessEnvironment`）。
3. `QProcess::started` → `Connected`；`sendMessage` 写 `compact JSON + '\n'`，进程未运行则 `errorOccurred("MCP server not running")`。
4. `onReadyRead`：累积 `m_lineBuffer`，按 `\n` 切行 → `dispatchPayload`；若缓冲 >8 MiB 且无换行，判协议违规：报错、`m_violationKilled=true`、`Failed`、`kill()`，且不自动重启。
5. stderr 只作诊断：保留尾部（>64 KiB 截到末 32 KiB），`formatError()` 把 ≤2000 字尾部拼到错误消息（UTF-8 失败回退 local8Bit）。
6. `onFinished`：清行缓冲、补读 stderr；`m_stopping` → `Disconnected`；否则报 `exited/crashed`，`MaxRestarts=3` 内按 1 s/2 s/4 s 退避重启（重启后由 client 重跑握手）。
7. `stop()`：`m_stopping=true`、停重启定时器、关 stdin、`terminate()`，3 s 后仍在跑则 `kill()`；析构里再 `kill` + `waitForFinished(2000)` 确保不留孤儿进程。

**HTTP 传输**（`HttpTransport.cpp`）
1. `start()` 直接 `Connected`——每个消息都是自包含 POST，传输层无会话。
2. `sendMessage`：POST `config.url`，`Content-Type: application/json`，`Accept: application/json, text/event-stream`，回显 `Mcp-Session-Id`，叠加静态 headers。
3. `readyRead` → `onSseChunk`：每 reply 累积上限 32 MiB，超限报错并 `setState(Failed)` + `abort()`（让所有 pending 请求立刻失败而不是等 35 s）。`text/event-stream` 按 `data:` 行 + 空行分事件即时派发；`application/json` 只累积。
4. `finished`：从任意响应抓取 `Mcp-Session-Id`；`status<=0` 且有传输错误 → 可读错误；`>=400` → `MCP HTTP error N`；`202` 无 body；然后把 `readyRead` 缓冲与尾部 body 合并解析。
5. `stop()` 只清 session id 并置 `Disconnected`。独立 GET SSE 推送通道与 OAuth 明确未实现（`HttpTransport.h:19-20`）。

**协议生命周期**（`McpClient.cpp`）
1. `connectToServer()` → session `Connecting` + `transport->start()`。
2. transport `Connected` → `runHandshake()`：`initialize{protocolVersion:"2025-03-26", capabilities:{}, clientInfo:{name:"NexusAI",version:"0.2.0"}}`；成功即发 `notifications/initialized`，随后 `refreshTools()`。
3. `refreshTools()`：`tools/list` → 读 `name/description/inputSchema`，置 session `Connected`，发 `toolsChanged()`；首次连接额外发 `connected()`。
4. `callTool()` → `tools/call{name, arguments}`，结果在 `onMessage` 里把 `content[]` 的 `text` 块用 `\n` 拼接，`image` 块替换为 `[image content returned by tool]`，`result.isError` 透传。
5. 超时：每个请求挂 30 s `QTimer`（`RequestTimeoutMs`），超时 → `failRequest()`，同时回调 handler 与 toolHandler 并发 `diagnostic`。
6. 取消：`cancelRequest(id)` 发 `notifications/cancelled{requestId}`，toolHandler 收到 `(true,"","cancelled")` 后移除。
7. 崩溃/失败：transport `Failed` → 所有 pending 以 `isError=true` 结束（避免空成功），session `Failed`，`connectionFailed` 带上最近一次 transport 诊断；transport `Disconnected` → 发 `disconnected()`。
8. `disconnectFromServer()` 先发 `disconnected()` 再清 `m_tools`，因为 `McpManager` 要在该信号里按 `tools()` 反注册。

**工具桥接**（`McpToolAdapter.cpp`）
1. 注册名 `mcp__<server>__<tool>`，非法字符 `[^A-Za-z0-9_-]` 替换为 `_`。
2. `definition()`：description 硬截 1024 字符（第三方不可信文本），`parameters` 直传 `inputSchema`，`level` 强制 `RequiresConfirm`，`readOnly=false`。
3. `execute()`：每次先清 `m_cancelled`（防上次取消毒化下次调用）；投递到 GUI 线程发起 `callTool`，主线程侧 120 ms 条件变量轮询，35 s 总期限（`CallTimeoutMs`，略高于 client 的 30 s）；取消则再发一次 queued `cancelRequest` 并给 3 s 宽限，最终 `fail("cancelled")`。
4. 返回值：`ToolResult::ok(text)` / `fail(text 或 error)`。`cancel()` 只写原子标志，绝不碰 GUI 线程状态。

**配置**（`McpConfig.cpp`）
1. 路径 `<AppDataLocation>/mcp_servers.json`，结构 `{"servers":[...]}`，字段与 `McpServerConfig` 对齐。
2. `load()` 容错：文件缺失/损坏/非对象 → 空列表；`enabled` 缺省 true；`name` 为空条目丢弃。
3. `save()` 用 `QSaveFile`（mkpath → write → commit）原子落盘，失败返回 false 并 qWarning。

**配置导入/导出**（`McpConfigTransfer.cpp`）
1. 导出文件：`{"format":"nexusai-mcp-servers","version":1,"servers":[…],"secretsOmitted":N,"omitted":["env","headers"]}`。
2. `buildExport()` 只写白名单字段 `name/transport/command/args/url`，环境变量与请求头**一律不写**，`enabled` 强制 `false`；含 env/headers 的条目计入 `secretsOmitted`，页面据此提示"凭据未包含"。
3. `previewImportFile()` 只做校验与冲突标记，不改内存：未知字段（未来可能出现的凭据字段）在 `sanitizeServer()` 里丢弃；`name` 为空 / transport 非 stdio|http / stdio 缺 command / http 缺 url 记 `error` 并跳过。
4. `applyImport()` 是纯函数：合法条目逐条合并；同名按策略处理——`skip` 跳过、`overwrite` 替换（仍禁用）、`rename` 用 `"<name> (2)"`、（3）… 保证唯一。`policyFromName()` 不认识的值回退 `skip`，绝不默默覆盖。
5. 导入保留文件里的 env/headers（MCP 凭据本就明文存本地），但导入后全部禁用，必须手动启用。

## 集成点

- `src/viewmodels/McpManager.*`：QML 单例 `McpManager`；`loadAndConnectAll()` 读配置并逐个建 `McpClient`；`connected` → 注册 `McpToolAdapter`（每 server ≤32 个，全局 `mcp__*` ≤64）；`disconnected` → `takeTool`；`connectionFailed`/`diagnostic` → 记录 + qCWarning；`addServer/removeServer/setEnabled/reconnect` 增删改并即时连接/反注册；`exportServers/previewServerImport/importServers` 走 `McpConfigTransfer`，导入后重建 `m_configs`、停用未启用项并 `save()`；`retireClient()` 延迟 40 s 删除（覆盖 35 s 调用 + 3 s 取消宽限），`aboutToQuit` 同步回收，避免 stdio 子进程残留。
- `src/main.cpp:612-618`：启动时 `McpManager::instance()->loadAndConnectAll()`。
- `src/main.cpp:444-463`：`localOnlyMode` 时把 `web_search` 与所有已存在的 `mcp__*` 写入 `ToolRegistry::setPolicyBlocked`；切换本地模式或 profile 时重算。
- `src/core/agent/AIAgent.cpp`：系统提示说明 `mcp__*` 为外部服务；`toolAllowedForModel()` 在 `m_localOnly` 下直接排除 `mcp__*`；审计行把 `mcp__<server>__<tool>` 拆出 `mcp_server` 字段。
- `src/viewmodels/SettingsBackup.cpp`：备份/恢复 `mcp_servers.json`，恢复后 `applyRestoredConfigs()` 重建 client。
- `CMakeLists.txt:83-94`：编入 `nexus_core`，依赖 `Qt6::Network`（HTTP 传输）。
- 测试：`tests/test_mcp.cpp` + `tests/fake_mcp_server_main.cpp`（含 `HttpFakeMcpServer`），覆盖握手、工具调用、错误映射、崩溃重启、并发按 id 关联、SSE、32 MiB 超限、stdout 洪泛、session 头、Windows cmd 包装、stderr 透出；另含 `McpConfigTransfer` 四例（脱敏导出、三种冲突策略、非法输入、落盘回读）。

## 关键文件

- `McpTransport.h/.cpp`（47/27 行）：传输接口、状态机、JSON 帧校验。
- `StdioTransport.h/.cpp`（62/267 行）：`QProcess` 子进程、Windows cmd shim、8 MiB 行缓冲、1/2/4 s 重启、stderr 尾部。
- `HttpTransport.h/.cpp`（51/166 行）：POST、SSE 解析、session 头、32 MiB/32 MiB 上限。
- `McpClient.h/.cpp`（108/295 行）：握手、`tools/list`、`tools/call`、pending 表、30 s 超时、取消。
- `McpToolAdapter.h/.cpp`（52/157 行）：命名、描述截断、跨线程等待、35 s 期限。
- `McpConfig.h/.cpp`（28/88 行）：`QSaveFile` 原子配置。
- `McpConfigTransfer.h/.cpp`（64/264 行）：导出脱敏、导入预览、同名冲突策略、重命名。

## 约束与风险

- **MCP 工具一律需确认**：`level=RequiresConfirm` 且 `readOnly=false`，所以 `PlanOnly` 模式默认拦截；`FullAccess` 会自动放行，这是用户显式选择的风险面。
- **本地模式门禁是快照**：`applyLocalOnlyGate()` 只把"当时已注册"的 `mcp__*` 写入阻断名单；之后新连接的 server 注册的工具不会被追加，存在策略窗口（`main.cpp:451-456` + `ToolRegistry::registerTool` 不重算 policy）。
- **子进程即任意代码执行**：stdio 的 `command/args/env` 完全来自配置文件，无路径或二进制白名单；Windows 上 `.cmd/.bat` 还要经过 `cmd.exe`，引号规则仅覆盖空格/制表/双引号。
- **凭据明文**：`env` 与 HTTP `headers` 以明文写入 `mcp_servers.json`（备份文件同样包含），代码中未做加密或脱敏。
- **两种文件的凭据边界**：整体设置备份（`SettingsBackup`）含 `mcp_servers.json` 全文；N7 的独立导入/导出文件**不含** env/headers，只能迁移服务器形状，凭据需手动补全，二者不可互相替代。
- **超时是双层的**：client 30 s，adapter 35 s。adapter 超时返回失败后，client 的请求仍可能稍后到达并被静默丢弃；`cancel()` 是协作式的，server 不理会 `notifications/cancelled` 时仍要等满宽限。
- **资源上限**：stdio 8 MiB 无换行即杀且不重启；stderr 64 KiB；HTTP 单响应 32 MiB 即 `Failed`（无自动恢复）；重启最多 3 次；工具数每 server 32（超出跳过并告警）；全局 64（超出**只告警、不摘除**，`McpManager.cpp:134-142` 仅 `qCWarning`，已注册的 `mcp__*` 仍留在 `toolNamesForModel()`）。
- **HTTP 为子集**：无 GET SSE 推送通道、无 OAuth、无重连；`start()` 无条件 `Connected`，所以"连接成功"只代表 URL 配置合法。仅接受单个对象响应，JSON-RPC 批量数组会被 `dispatchPayload` 判为畸形。
- **线程边界**：adapter 用阻塞等待占线程池线程最长 35 s；`cancel()` 只置原子位，真正的取消依赖下一次 `execute` 或等待循环观察。
- **配置写入非事务化跨调用**：`McpManager` 先改内存再 `save()`，保存失败只记录 `lastError`，不回滚内存状态。
- **诊断有限**：stderr 只保留尾部；HTTP 错误只报状态码，不读 body。
