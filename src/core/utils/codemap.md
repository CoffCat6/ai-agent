# src/core/utils/ — 架构地图

## 职责

`nexus_core`（静态库，Qt Core/Sql/Concurrent/Network，无 UI 依赖）中的横向基础层，为 Agent、基础设施与 ViewModels 提供 5 类能力：

1. **设置单一事实源**：`SettingsDefinition.h`（POD 元数据）→ `SettingsSchema`（键账本：默认值/类型/范围/枚举/加密/废弃标记）→ `SettingsRepository`（唯一持久化边界）。
2. **可观测性**：`Logger`（qInstallMessageHandler 落盘 + 进程内环形缓冲）与 `Activity`（分类事件门面，日志 + `usage_log` 双通道）。
3. **计量**：`TokenEstimator`（上下文 token 估算/压缩/分桶）、`PricingCalculator`（token → 分，整数计价）。
4. **项目上下文**：`ProjectFileIndex`（'@' 提及解析、文件扫描、引用校验与读取）。
5. **输入与网络规范化**：`AttachmentClassifier`、`DataUrlUtils`、`SamplingParams`、`NetworkProxyConfig`。

## 设计

**设置三层结构，职责不重叠。** `SettingsDefinition.h` 只放结构体与枚举（`PreferenceType`/`PreferenceConstraint`/`PreferenceMeta`/`CategoryMeta`/`SettingValueType`/`SettingsKeyDef`），无逻辑、无 Qt 依赖除 QVariant。`SettingsSchema` 是"权威账本"：`categoryGroups()` 4 个导航分组（模型与对话 / 外观与常规 / 知识与内容 / 数据与诊断），`categories()` **18 个分类**（model…backup + `shortcuts` + `storage`，带全局 order、`groupId` 与 `QT_TRANSLATE_NOOP` 显示名）、`preferences()` **20 条** UI 偏好（含 `settings.collapsedGroups`：设置导航分组折叠状态；`ui.shortcuts`：快捷键覆盖项 JSON；`general.onboarded`：首次启动向导标记；`ui.settingsDialogWidth/Height`：设置中心尺寸记忆——三者均无分类=隐藏）、`registeredKeys()` = **59 个静态 `keyDef` + 20 个 `prefs/v1/<key>` 派生键（共 79）**；其中 `schemaVersion`（内部版本戳）与 `theme/dark`（legacy）不计入用户可见账本，故用户可枚举 M1 键为 **57**，加 20 个偏好 = **77 个用户可见设置键**。全部函数内 `static` 惰性构造，无锁只读。`kSchemaVersion = 4`（v2 模型列表用户自管，v3/v4 归一 contextTokens 旧默认值）。

**键命名空间**：顶层业务键（`modelProfiles`、`activeProfileId`、`contextTokens`、`agent/*`、`security/*`、`embedding/*`、`logging/*`、`webSearch/*`、`network/*`、`memory/*`、`ui/*`、`tools/disabled`、`projects/activeId`、配额与 `usage/alertPercent`、`schemaVersion`）用扁平 `section/name`；UI 偏好统一 `prefs/v1/` 前缀。`theme/dark` 保留为 `legacy = true`，`ui.themeMode` 用 `legacyKeys = {"theme/dark"}` 声明迁移来源。

**持久化**：`SettingsRepository` 全部静态方法，每次调用新建 `QSettings(QSettings::defaultFormat(), UserScope, "NexusAI", "NexusAI")`（Windows 即注册表 `HKCU\Software\NexusAI\NexusAI`）。写入前对**已登记**键调 `SettingsSchema::isValidValue`（类型、有限数、整数性、int 范围、枚举白名单、required/maxLength），未登记键直接放行。批量写/删会先快照旧值，`sync()` 失败时做 best-effort 回滚（注释明示 QSettings 非原子）。

**备份调度**（docs/31 S4.2 N6）：`BackupSchedule.h` 是 header-only 纯函数命名空间——五档间隔 `{0,1,6,24,168}` 小时；`normalizeBackupIntervalHours()` 取**不超过**输入值的最大档（负数/0 → 0，即关闭），`backupIntervalMs()` 输出毫秒（0 = 调用方停表）。`SettingsService` 在加载、setter、`setValueForSetting` 三处统一归一后再落盘，所以导入的旧配置/手工改注册表不会让 UI 出现选不中的档位；`main.cpp` 的启动备份与定时备份共用同一个 worker，失败静默期 ≥ 一个间隔且受 `notice.enabled` 门控。

**默认值读取三段式**收敛为 `SettingsSchema::intSetting/boolSetting`（存储值 → schema 默认 → `clampInt`），配额键曾双源（AIAgent::checkQuota 与 SettingsService::load），现统一入口。

**加密字段**只打标记，不在本层实现：`webSearch/apiKey`、`embedding/apiKey`、`network/proxyAuth` 三个键 `encrypted = true`。实际 DPAPI 在 `viewmodels/SettingsService.cpp`：`dpapi:v1:` 前缀 + `CryptProtectData(熵描述 "NexusAI.apiKey")` + Base64；解密失败视为未设置；加密失败**拒绝回退明文**，直接丢弃。**旧明文只按明文读取、不回写加密**：load 时无 `dpapi:v1:` 前缀的值原样返回（`SettingsService.cpp:227`），源码注释里的 "migrated on load" 实际未发生写回，密钥仍以明文留在原存储，直到用户下次保存该键。`proxyAuth` 是 username+password 的整块 JSON，整体加密。非 Windows 编译单元里 protect/unprotect 退化为恒等（明文）。

**LogTailReader**（docs/31 O6）：`readLogTail(path, options)` 只 `seek` 到文件尾部窗口读取
（`maxBytes` 夹紧 64 KB–8 MB），超大日志不整读；从窗口中间起读时丢弃半行。级别沿用
`[ts] [LEVEL]` 行头解析为 `severityRank`（0–4），解析不出级别的续行（异常堆栈）**继承上一行**，
避免级别过滤误杀上下文。关键字大小写不敏感子串；超长行截断到 2,000 字符加 `…`；保留最近
`maxLines`（1–2,000）条，`matched` 是窗口内匹配总数，`truncated` 表示窗口前还有未读内容。
`path` 为空时回落到 `Logger::logFilePath()` → 日志目录里最新的 `nexus-*.log`。

**Logger**：`install()` 先读 `logging/level`、`logging/directory`、`logging/maxFileSizeMb`、`logging/retentionFiles`（均带 `clampInt`），`NEXUSAI_LOG_LEVEL` 环境变量覆盖级别，然后 `qInstallMessageHandler`。单文件 `nexus-YYYYMMDD.log`，超限滚动为 `nexus-YYYYMMDD-NNNN.log`；`pruneOldLogs` 按 mtime 保留 `retentionFiles` 个并强制保留当前活动文件。行格式 `[yyyy-MM-dd hh:mm:ss.zzz] [LEVEL] [category] message`，仅 WARN/CRIT 追加 ` (file:line)`；`levelName` 输出 `DEBUG/INFO/WARN/CRIT/FATAL`。级别比较必须走 `severityRank()`（Qt6 中 `QtInfoMsg=4 > QtFatalMsg=3`，声明序非严重度序）。全部状态（QFile、路径、阈值、环形缓冲、seq）由单个 `QMutex g_mutex` 保护，handler 在锁内做文件追加 + `fflush` + stderr 镜像，Fatal 后 `abort()`。环形缓冲 `RingCapacity = 5000`，`entriesSince(seq)` 拉取式快照；`backfillFromFile()` 从最新日志尾部 256 KB 反向正则解析，标记 `historical = true`、默认上限 2500、按 message 文本去重后重新分配 seq。

**Activity**：7 个 `QLoggingCategory`（`nexus.app/user/agent/tool/rag/network/db`）。`report()` 发 `qCInfo(...).noquote()`，contextId 以 `[id]` 拼进消息（复用 LogPanel 关键字过滤，不扩 schema）；`usage()` = report + 注入的 `UsageSink`（`main.cpp` 注入，`QMetaObject::invokeMethod` 排队到 GUI 线程 → `SqliteMemory::addUsageEvent` → `usage_log`）。隐私契约写死：只传 metadata（action/target id/sizes），禁止 prompt、文档正文、API key。

**TokenEstimator**（header-only 启发式）：非 CJK 4 字符/token，CJK 1.5 字符/token（`(cjk*2+2)/3`），非空至少 1；CJK 区间 = 0x2E80–0x9FFF、F900–FAFF、3000–303F、FF00–FFEF。单消息 = role + content + 4 开销 + 每图 1000 + `files[].content` + 每个 tool_call（8 + name + arguments）。`estimateToolsSchema` 用 compact JSON 全文估算（每次请求都随行，必须预留）。`compressToFit` 保留首条 system、插入省略标记（预留 24 token）、从某条 **user** 消息起保留最大后缀（避免孤儿 tool 消息破坏配对契约），无可用 user 时原样返回。`compressToolRoundsToFit`（docs/35 P4.1）是子代理专用变体：子代理只有一条 user（goal），`compressToFit` 在其历史上等于不压缩；该变体保留 system+goal，把「assistant + 其后连续 tool」视为不可拆单元整轮丢弃，插入带省略轮数的 user 提示，仍超预算时对最大 tool 结果反复减半（下限 400 字符 + 截断标记，严格递减防死循环），可选出参报告丢弃轮数/截断条数。`splitContextTokens` 输出 systemPrompt/conversation/toolResults/images/toolsSchema/usedTokens/totalTokens，约定四桶之和 == `estimateTokens(messages)`。

**PricingCalculator**：单位"分 / 1M token"，全程 `qint64` 整数，`ceil((in-cache)*inPrice + cache*cachePrice + out*outPrice) / 1e6`；缓存命中量从 input 剥离并单独计价，负数钳 0；`model` 形参不参与公式（`Q_UNUSED`）。`model_pricing` 表（model / input_price_cents / output_price_cents / cache_hit_price_cents / effective_date / notes）由 `SqliteMemory::initDatabase` 末尾 `refreshCache` 一次性载入函数局部静态 `QHash` + 独立 `QMutex`；未命中一律 0（本地 Ollama 与未知模型免费但仍计 token）。

**ProjectFileIndex**（`NexusAI::ProjectFileIndex` 纯函数命名空间，无状态、无缓存）：`scan` 递归（`NoSymLinks` + `Hidden`），跳过 23 个硬编码目录名 + `cmake-build-*`/`build-*` 前缀，只收白名单后缀（约 90 种）或无后缀白名单名（Makefile/Dockerfile/CMakeLists.txt/.env.example 等），上限默认 20000，输出 `/` 分隔的相对路径。`complete` 四级评分（basename 前缀 0 > basename 包含 1 > 路径前缀 2 > 路径包含 3），同分按路径长度再字典序。`mentionAt` 处理光标处 '@'（前一字符须空白/起始，光标须在 token 边界）与 `"引号路径"`；`mentionPaths` 抽取全部 token 并剥离尾部中英文标点、去重。`resolveReferences` 限 8 文件 / 24 KB 单文件 / 96 KB 总量，拒绝绝对路径、`..`、Windows 盘符相对路径 `C:foo`、canonical 逃逸 root、敏感文件与含 `\0` 的二进制，超限追加 `[... truncated ...]`；`checkReferences` 只做路径校验（32 项上限）返回 `no-project|missing|outside|sensitive`，供输入框实时标红。

**文件浏览扩展（2026-10-24 批次）**：`listDir(root, relPath)` 一次只读一层，目录优先 + 各组字典序，供右侧栏懒展开树（`ProjectFileVM.children`）；**过滤规则与 `scan` 有意不同**——只排除 ignore 目录与敏感文件，**不套后缀白名单**（浏览器要看见 README/LICENSE/.yaml 等非索引文件）。`readFilePreview(root, relPath, maxBytes=512KB)` 返回 `{text, bytes, truncated, binary, error}`：多读 1 字节探测截断、NUL 字节（前 8KB 内）判二进制、非 UTF-8 回退本地编码。`searchPaths(root, query, maxResults=50)` 递归全项目，相对路径大小写不敏感子串匹配，**规则与 `listDir` 完全一致**（刻意不复用 `scan`，否则"树里看得见但搜不到"），上限 50 条 + 2 秒时长熔断（网络盘/病态目录降级为部分结果）。三者共用 `resolveUnderRoot`：拒绝绝对路径、`..` 逃逸、Windows 盘符相对路径。**符号链接不是"一律不跟随"**：`readFilePreview` 在词法校验后再做 canonical 解析（`canonicalForContainment`，Windows 用 `GetFinalPathNameByHandleW`），目标仍在 root 内才放行、指向根外返回 `outside`，读取始终走 canonical 路径而非链接本身（`ProjectFileIndex.cpp:440-475`）。

**WebContent**（header-only 命名空间，`NetworkFetchTool` 与知识库 URL 导入共用）：`fetchUrl(cancelFlag, url, allowPrivateTargets, timeout, maxBodyBytes, maxResultChars)` 单一入口，内含 SSRF 预检（`hostIsAllowed` 异步 DNS 切片轮询）、ManualRedirectPolicy（不跟随重定向，3xx 以 `Redirected` + location 上抛）、wire 上限切片轮询取消；`htmlToText` 最小 HTML→文本（script/style 剔除、标签剥离、实体解码、非 UTF-8 回退本地编码——**非完整解析器**，导航噪音会保留，检索可接受）。TooLarge **不是失败**：返回已读正文 + 截断注记（工具层契约，`test_provider_lifecycle httpFetchSizeCapTruncates`）。

**NetworkProxyConfig**：值对象 `NetworkProxyConfig`（Mode System/Manual/Off + host/port/username/password，`manualUsable()` = 非空 host 且 port ∈ 1..65535）。`ConfigurableProxyFactory` 是应用级 `QNetworkProxyFactory`，`main.cpp` 装一次，`setConfig` 走 `QMutex`。决策顺序：目标 host 是 loopback（`localhost`/`127.0.0.1`/`::1`/`[::1]`/`*.localhost`）→ **永远 NoProxy**（本地 Ollama/网关）；Off → NoProxy；Manual 且可用 → `HttpProxy` + 可选凭据；Manual 不可用 → NoProxy（不静默回退系统代理）；System → `systemProxyForQuery()`，为空补 NoProxy。只支持 HTTP 代理，无 SOCKS。

**AttachmentClassifier / DataUrlUtils / SamplingParams**：均为零 IO 纯函数。分类按 `QFileInfo::suffix()` 小写匹配 image 6 种（png/jpg/jpeg/webp/bmp/gif）与 text 约 42 种，其余含无扩展名 → Unsupported，`kindToString` 返回 `image|text|unsupported`。data URL 仅接受 `data:image/` 且含 `;base64,`，`parseDataUrl` 切出 mimeType 与 Base64 载荷（不解码、不校验 Base64 合法性）。`SamplingParams` 把脏值归一为**非法 QVariant**（语义"未设置"，Provider 省略字段而非发 0）：`optionalDouble`（有限非负）、`optionalPositiveInt`（>0 且 ≤ INT_MAX，防 4294967296 回绕成 0）、`optionalReasoningLevel`（off/low/medium/high，其余归一为空）。

## 数据与控制流

- **设置读**：`SettingsService::load()`（启动）→ `SettingsRepository::value` → 默认值回退 → schema 迁移（`schemaVersion < 4` 时执行 v2/v3/v4 迁移并写回版本戳）→ 分发到各服务。
- **设置写**：QML/服务 → `SettingsService::persistValue`（含 DPAPI）→ `SettingsRepository::writeValue/writeBatch` → 校验 → `QSettings::setValue` + `sync` → 失败回滚 + 错误串回传（上层 `persistenceError` 信号）。`proxyAuthChanged` 等再经 `proxyConfigChanged` → `main.cpp::applySettings` → `proxyFactory->setConfig`。
- **日志**：任意线程 `qCDebug/qCInfo/...` 或 `Activity::report` → messageHandler（锁内过滤 severityRank → 格式化 → 入环 → 追加文件/滚动/清理 → stderr）→ `LogModel` 轮询 `revision()`/`entriesSince()` 拉到 QML `LogPanel`。
- **活动**：`Activity::usage` → ① qCInfo 进行日志链；② UsageSink 跨线程排队 → `usage_log`（按 `memory/auditRetentionDays`/`tokenRetentionDays` 定期清理）。
- **计量**：AIAgent 请求前 `estimateTokens(messages) + estimateToolsSchema(tools)` 判定预算 → `compressToFit`；响应后 `getPricing(model, ...)` + `calculateCost(...)` 写 `token_usage`/统计页。
- **'@' 引用**：QML `InputConsole` → AgentVM `mentionAt/complete`（基于后台扫描的 `m_projectFiles`，30 s TTL）→ 提交时 `resolveReferences` 读内容拼进请求 → `checkReferences` 供 Chip 预览。
- **附件**：拖拽/选择 → `AttachmentClassifier::classify` 分派 → ViewModels 读盘/压缩 → `DataUrlUtils::parseDataUrl` 在 Ollama/Gemini/Anthropic/OpenAI 系服务里拆回 mimeType+Base64。

## 集成点

`src/main.cpp`：`Logger::install()`（:237）、`Logger::backfillFromFile()`（:240）、`ConfigurableProxyFactory` 注册（:227-228）、`Activity::setUsageSink`（:625-638）、代理/日志设置的 `applySettings` 回调（:713、:776）。`viewmodels/SettingsService.cpp`：DPAPI、schema 迁移、`proxyConfig()`、设置持久化的唯一上层。`core/agent/AIAgent.cpp`：TokenEstimator 预算/压缩/分桶与 PricingCalculator 计费（:2122-2123）。`core/memory/SqliteMemory.cpp`：`refreshCache`（:209）、`model_pricing` 建表/种子、`usage_log`。`viewmodels/AgentViewModel.cpp`：ProjectFileIndex 全部 API + 附件分派。`infrastructure/llm/*`：DataUrlUtils。测试：`test_logger`、`test_settings`(超大套)、`test_settings_backup`、`test_settings_edit_lifecycle`、`test_token_estimator`、`test_project_files`、`test_mention_interaction`、`test_network_proxy`、`test_sampling_params`、`test_attachment_classifier`、`test_data_url_utils`。

## 关键文件

| 文件 | 内容 |
| --- | --- |
| `SettingsDefinition.h` | 设置元数据 POD（类型/约束/键定义/分类） |
| `SettingsSchema.{h,cpp}` | 键账本 76 项、默认值、`isValidValue`/`clampInt`、`intSetting`/`boolSetting`、`kSchemaVersion=4` |
| `SettingsRepository.{h,cpp}` | QSettings 持久化边界、批量读写、失败回滚 |
| `Logger.{h,cpp}` | 消息处理器、日滚动/容量滚动、保留策略、5000 条环形缓冲、历史回填 |
| `LogTailReader.{h,cpp}` | 日志文件尾部只读窗口（默认 1 MB）：级别阈值（续行继承）/ 关键字 / 行数上限 / 超长行截断；`newestLogFile()` |
| `Activity.{h,cpp}` | 7 个日志分类、`report`/`usage`、`UsageSink` 注入 |
| `TokenEstimator.h` | 启发式估算、`compressToFit`、`compressToolRoundsToFit`、`splitContextTokens`（header-only） |
| `PricingCalculator.{h,cpp}` | 分/1M token 整数计价、`model_pricing` 进程级缓存 |
| `ProjectFileIndex.{h,cpp}` | 扫描/补全/'@'解析/引用校验与读取；文件浏览三 API（`listDir` 一层目录、`readFilePreview` 预览、`searchPaths` 全项目搜索，规则见上） |
| `NetworkProxyConfig.{h,cpp}` | 代理模式快照 + 应用级 `QNetworkProxyFactory`、loopback 绕过 |
| `AttachmentClassifier.{h,cpp}` | image/text/unsupported 扩展名分派 |
| `DataUrlUtils.{h,cpp}` | `data:image/...;base64,` 识别与拆分 |
| `SamplingParams.h` | 采样参数防御性归一（header-only，未列入 CMake SOURCES） |

## 约束与风险

- **账本纪律**：`writeBatch` 只校验 `definition()` 命中的键，未登记键静默放行，拼写错误不会被发现；`isValidValue` 对未知键返回 `false`，两处口径不一致。
- **QSettings 非原子**：回滚是 best-effort，进程崩溃可能留下半批写入；每次访问都新开 QSettings 并 `sync()`，高频写有 IO 成本。
- **加密只在 ViewModels 层**：绕过 `SettingsService` 直接 `writeValue("webSearch/<provider>ApiKey", ...)` 会落明文；非 Windows 平台 `encrypted` 标记形同虚设。
- **Logger 全局锁 + 同步文件 IO**：handler 持锁做 write/flush/stderr，日志风暴会串行化所有线程（含 GUI）；单行超 `maxFileBytes` 时按字节 `left()` 截断，可能切断 UTF-8 多字节序列。
- **回填去重按 message 文本**：同文不同时的日志被静默丢弃；回填项重新分配 `seq`，环形缓冲内 `seq` 顺序与 `timestamp` 不再单调。
- **Token 估算非真实分词**：CJK 区间不含扩展 B 平面（代理对按两个 char 计），图片固定 1000、消息固定 +4，只适合预算/触发压缩，不能作为计费依据。
- **计价缓存不自动失效**：`refreshCache` 仅在 `initDatabase` 调用，设置里改价后需显式刷新；`calculateCost` 忽略 `model`，调用方必须先取价。
- **ProjectFileIndex 无缓存/无 .gitignore**：忽略规则是硬编码名单，`scan` 全量递归（20000 上限），大仓靠 AgentVM 的 30 s TTL + `QtConcurrent` + generation 防旧结果；`resolveReferences` 在调用线程读文件；敏感文件仅按文件名判定，不含内容扫描。
- **代理能力边界**：仅 HTTP 代理（无 SOCKS/HTTPS 区分），Manual 配置非法时直接直连而非回退系统代理；loopback 绕过基于目标 host，指向内网非 loopback 地址仍会走代理。
- **纯函数模块的轻微脆弱点**：`DataUrlUtils` 不验证 Base64 合法性、`mimeType = mid(5, ...)` 依赖 `"data:"` 字面长度；`AttachmentClassifier` 把 gif 归入图片链路，但压缩侧可能只取静帧；`SamplingParams.h` 未登记进 `nexus_core` 的 CMake SOURCES（仅被 main/tests 包含）。
