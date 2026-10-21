# src/ui/pages 架构地图

## 职责

本目录包含主聊天页与「设置中心」的全部内容页：

| 文件 | 角色 | 是否由 SettingsCenterDialog 装载 |
|---|---|---|
| AgentChatPage.qml | 主聊天工作区（顶栏 / 会话标签 / 轨迹视口 / 输入区） | 否，由 main.qml 直接实例化 |
| ModelConfigPage.qml | 模型 provider/profile/模型列表/思考强度/上下文预算 | 是，分类 `model` |
| InterfacePage.qml | 主题、字体、语言、窗口默认布局 | 是，分类 `interface` |
| GeneralPage.qml | 声明式偏好、步数/超时/系统提示词、日志、代理、嵌入服务 | 是，分类 `general` |
| SecurityPage.qml | 读取沙箱、项目写入、黑名单、仅本地/内网访问 | 是，分类 `security` |
| McpPage.qml | MCP 服务器增删、启停、连通状态 | 是，分类 `mcp` |
| ToolsPage.qml | 单工具启停、联网搜索、默认权限模式 | 是，分类 `tools` |
| MemoryPage.qml | 长期记忆（episodic）浏览/搜索/遗忘、审计与 Token 保留天数 | 是，分类 `memory` |
| UsageStatsPage.qml | token/费用/配额统计与请求明细 | 是，分类 `usage`（绑定 `dialog.opened`） |
| ActivityLogPage.qml | usage_log 操作/网络活动时间线 | 是，分类 `activity`（绑定 `dialog.opened`） |
| SettingsBackupPage.qml | 设置 JSON 导入导出、数据库备份/恢复 | 是，分类 `backup` |

注意：任务描述里的「记忆页 = 知识库文档导入/删除/检索」与代码不符。MemoryPage 管理的是
会话摘要型长期记忆；知识库文档的导入/删除/重新嵌入在 `panels/ContextPanel.qml` 中，
检索由模型通过知识库工具完成。详见下文约束与风险。

## 设计

### 设置中心的装载方式

`SettingsCenterDialog.qml` 不用 StackLayout，而是为每个分类放一个 `Loader`：

- `active: dialog.currentCategory === "<id>"`，`visible: active`；
- `source` 是 `qrc:/qt/qml/NexusAI/src/ui/pages/<Page>.qml`；
- 分类 id 与顺序由 `SettingsSchema::categories()` 定义，经
  `PreferenceRegistry.categories()/categoryName()` 暴露；CategoryNav 用 Repeater 渲染；
- `usage` 与 `activity` 额外与 `dialog.opened` 相与，关闭即销毁、重开重新查询
  （避免统计/时间线停留在首次加载的旧值）；其余页切走分类即销毁；
- 搜索态（`searchField.text` 非空）只隐藏分类页包裹层，Loader 状态保留，跳转后原位恢复；
- 对话框 860×620，左侧 180px 导航，底栏只有「恢复默认」（声明式偏好分类可见）与「关闭」，
  没有「保存/取消」。`onClosed` 只做 `EditBuffer.rollback()`，丢弃校验失败留下的 staged 残值。

`openCategory(id)` 设定 `explicitCategory=true` 后打开；`onOpened` 在非显式指定时恢复
`PreferenceStore.load("settings.lastCategory")`。打开入口包括 Ctrl+,、侧栏设置、
AgentChatPage 空态的「配置模型」、ModelSelector、`AgentVM.modelSettingsRequested`。

### 保存时机

1. **即时直写 SettingsService**：SecurityPage、ModelConfigPage、McpPage、ToolsPage、
   MemoryPage、UsageStatsPage、SettingsBackupPage、GeneralPage 的 SettingsService 字段。
   每个控件在 `onToggled/onActivated/onValueModified/onEditingFinished` 写属性。
2. **即时写穿 EditBuffer**：InterfacePage 与 GeneralPage 的声明式偏好调用
   `EditBuffer.stage(key, value)`；`EditBuffer::stage` 校验后立即 `saveBatch`，
   `staged` 只在失败或“值等于已存值”时短暂存在，因此仍是即时保存而非批量提交。
3. **文本输入**：多数在失焦/回车提交，并用 `Component.onDestruction` 兜底（Loader
   切分类不触发 `editingFinished`）；长文本（系统提示词、代理主机、嵌入地址）另有
   400ms 防抖；SettingsField/ParamSelectRow 用 `commitOnDestroy` 补同一语义。
4. **无持久化状态**：连接测试结果、日期错误/分页游标、Activity 本地筛选、
   episodes 缓存只在页面生命周期内有效。

### 各页对应的 schema 键组

- `model`：`modelProfiles`（JSON 配置数组）、`activeProfileId`；另读 `contextTokens`
  作为上下文预算的最终回退值。profile 内部字段（kind/provider/apiProtocol/baseUrl/
  apiKey/model/models/temperature/maxOutputTokens/numCtx/reasoningEffort/contextLength）
  不是独立 schema 键，由 SettingsService 合并写入 `modelProfiles`。
- `interface`：`prefs/v1/ui.themeMode`、`ui.fontSans`、`ui.fontMono`、`ui.baseFontSize`、
  `ui.locale`、`ui.defaultSidebarWidth`、`ui.defaultPanelWidth`、
  `ui.defaultSidebarExpanded`、`ui.defaultPanelVisible`、`ui.restoreWindowOnStartup`。
- `general`：`prefs/v1/window.closeToTray`、`prefs/v1/general.autoStart`、
  `prefs/v1/notice.enabled`；`agent/maxSteps`、`agent/taskTimeoutMinutes`、
  `agent/systemPromptOverride`、`logging/level`、`logging/directory`、
  `logging/maxFileSizeMb`、`logging/retentionFiles`、`network/proxyMode`、
  `network/proxyHost`、`network/proxyPort`、`network/proxyAuth`（加密）、
  `embedding/provider`、`embedding/model`、`embedding/baseUrl`、`embedding/apiKey`（加密）。
- `security`：`security/allowFullDiskRead`、`security/allowProjectWrite`、
  `security/localOnlyMode`、`security/allowLocalNetworkFetch`、`security/readDenyList`。
- `tools`：`tools/disabled`（经 ToolRegistry 持久化）、`webSearch/provider`、
  `webSearch/apiKey`（加密）、`webSearch/searxBaseUrl`、`agent/defaultPermissionMode`。
- `memory`：`memory/episodicMemoryEnabled`、`memory/auditRetentionDays`、
  `memory/tokenRetentionDays`；记忆正文在 SQLite，不在 SettingsSchema。
- `usage`：`dailyTokenQuota`、`dailyCostQuotaCents`、`monthlyTokenQuota`、
  `monthlyCostQuotaCents`、`strictQuotaMode`；统计数据来自 `token_usage` 表。
- `activity`：不写任何键，只读 `usage_log`。
- `backup`：`SettingsBackup` 的可移植键白名单 + `memory/backupKeepCount`（只读展示）；
  数据库备份走 AgentVM。
- `mcp`：不使用 SettingsSchema，配置在 `<appData>/mcp_servers.json`。
- AgentChatPage：不使用 schema 键，只读 AgentVM。

## 数据与控制流

### AgentChatPage

`ColumnLayout` 自上而下：TopBar → SessionTabs → 轨迹区 → 输入区。轨迹区用
`TranscriptViewport`（`model: AgentVM.traceModel`），delegate 为 `TraceNodeRouter`；
展开状态存在 viewport 的 `detailState` 中，避免 delegate 重建丢失。输入区由
`InputConsole` + `Flow{ModelSelector, ThinkingEffortSelector, PermissionModeSelector}`
组成。空态按 `AgentVM.modelConfigured` 切换文案并给出「配置模型」按钮；
`settingsRequested` 由 main.qml 接到 `openCategory("model")`。
`AgentVM.pendingConfirmation.count` 驱动「有待确认」浮标；`ChangeBus` 的
`search.revealMessage` 经 `Qt.callLater` 用 `traceModel.rowOfMessageId` 定位并高亮 2 秒；
`AgentVM.attachmentsDropped` 把拖入附件转交 `inputConsole.appendAttachments`。

### 设置页与后端

- ModelConfigPage：`editingId` 独立于 `Settings.activeProfileId`，编辑不等于启用。
  `Settings.updateProfile(id, {key: value})` 单点合并并重算模型一致性；
  `Settings.addProfile(kind)` / `removeProfile` / `setActiveProfile` /
  `selectModelForProfile` / `addModelToProfile` / `removeModelFromProfile`。
  测试连接调用 `ConnectionTester.test(apiProtocol, baseUrl, apiKey, testModel)`，
  结果经 `testFinished(ok, message)` 回填；测试模型默认当前模型，用户选择后
  `testModelPinned` 固定。`Settings.endpointOptions(provider)` 决定协议芯片，
  `Settings.modelContextWindow(apiProtocol, model)` 决定能力表窗口。
- SecurityPage：CheckBox 直接写 `Settings.*`；黑名单增删以 `slice()` 复制后整体
  赋回 `Settings.readDenyList`，去重按 Windows 大小写不敏感语义。判定顺序由后端
  执行：系统目录封锁 → 黑名单 → 允许层（用户目录 ∪ 当前项目），页面文案与之一致。
- McpPage：`Repeater model: McpManager.servers`，每项
  `{name, transport, endpoint, enabled, state, toolCount}`；`state` 2=已连接、
  3=失败、其余=连接中/未启用。`addServer` 解析 stdio 命令行为 command+args 或 http URL；
  `setEnabled`、`removeServer` 即时生效并写回 mcp_servers.json。
- ToolsPage：`AgentVM.toolRegistryModel` 每项
  `{name, description, level(safe/requires_confirm/dangerous), enabled,
  blockedByPolicy, source(builtin/MCP)}`；Switch 调 `AgentVM.setToolEnabled`，
  由 ToolRegistry 写 `tools/disabled` 并刷新模型。`blockedByPolicy` 只做徽标，
  不改变 Switch 的用户层状态。
- MemoryPage：`listEpisodic(500)` / `searchEpisodic(keyword, 500)` /
  `removeEpisodic(id)` / `clearEpisodic()`；`AgentVM.episodicChanged` 触发重新查询。
  搜索框 `onTextChanged` 立即查询（无防抖），计数与空态随结果变化。
- UsageStatsPage：`refresh()` 一次拉四组聚合
  （`tokenStatsDetailed` / `costStats` / `quotaStatus` / `tokenStatsBySource`），
  再 `loadDetailPage(true)` 拉 `tokenUsageRecords` 首页；分页 50 行，`loadMore`
  追加。时间范围统一换算为 `[from, to)`：今日为本地 0 点~次日 0 点，7/30 天为滚动窗口，
  「全部」用 `from=1` 规避聚合接口 `(0,0)` 回退 30 天的口径差，自定义结束日取次日 0 点。
  数据映射：`byModel → modelSeries → donutSeries/barSeries`，`byDay` 先 `reverse()`
  再取 `MM-dd`，费用按分转元；来源占比分母用各来源 token 之和而非 `totalTokens`
  （工具 schema 口径不一致会让占比超过 100%）。
- ActivityLogPage：`AgentVM.listUsageEvents(500)` 全量拉取（id DESC），
  QML 侧按类别精确匹配 + 关键词对 action/target/detail 不区分大小写过滤；
  刷新按钮手动重拉，页面重建时也会拉一次。
- SettingsBackupPage：`SettingsBackup.exportSettings` / `previewImport` /
  `importSettings`，恢复前弹预览（分类、被跳过的密钥、警告）；
  `AgentVM.backupDatabaseNow` / `previewDatabaseBackup` / `restoreDatabaseFrom`
  处理 SQLite 快照；导入后 MCP 服务器保持禁用并要求重启确认高风险权限。

## 集成点

- `main.qml`：实例化 `AgentChatPage` 与 `SettingsCenterDialog`，把
  `settingsRequested` / `modelSettingsRequested` 统一到 `openCategory("model")`，
  并把 `ContextPanel.documentDetailsRequested` 接到 `KnowledgeDocDialog.openFor`。
- 单例：`Settings`、`PreferenceStore`、`PreferenceRegistry`、`EditBuffer`、
  `AgentVM`、`McpManager`、`ConnectionTester`、`SettingsBackup`、`Theme`、`I18n`、
  `ChangeBus`。页面之间不直接互相引用，全部经单例与信号解耦。
- CategoryNav 切换时写 `settings.lastCategory`；`jumpToCategory` 先清空搜索再写同一键。
- `PreferenceRegistry.categories()` 同时驱动导航、搜索与恢复默认；`settings-search.js`
  只补页面自绘项（模型卡片、联网搜索、数据库备份等）。

## 关键文件

- `src/ui/pages/AgentChatPage.qml`
- `src/ui/pages/ModelConfigPage.qml`
- `src/ui/pages/GeneralPage.qml`
- `src/ui/pages/InterfacePage.qml`
- `src/ui/pages/SecurityPage.qml`
- `src/ui/pages/McpPage.qml`
- `src/ui/pages/ToolsPage.qml`
- `src/ui/pages/MemoryPage.qml`
- `src/ui/pages/UsageStatsPage.qml`
- `src/ui/pages/ActivityLogPage.qml`
- `src/ui/pages/SettingsBackupPage.qml`
- `src/ui/components/SettingsCenterDialog.qml`、`CategoryNav.qml`、
  `settings-search.js`、`SettingsField.qml`、`PreferenceControl.qml`
- `src/core/utils/SettingsSchema.cpp`、`src/viewmodels/EditBuffer.cpp`、
  `src/viewmodels/SettingsService.h`、`src/viewmodels/McpManager.h`、
  `src/core/memory/IMemory.h`

## 约束与风险

- MemoryPage 与任务描述的知识库职责不一致：知识库导入/删除/检索不在本页；
  若按描述改动会误改语义。知识库 UI 的单一入口是 ContextPanel。
- GeneralPage 顶部仍写「不受底部保存/取消影响」，是旧批量保存文案遗留；底栏已无保存按钮。
  该页存在两个恢复默认入口（页头按钮与对话框底栏），二者都调用
  `requestResetDefaults()`，但只有底栏走确认弹窗。
- InterfacePage 的值读 `EditBuffer.staged` 再回落 `PreferenceStore`，若未来把
  `stage()` 改回纯暂存，页面会表现为“能改但不落盘”；当前依赖 EditBuffer 的写穿语义。
- UsageStatsPage 单文件约 1250 行，聚合、图表映射、分页、配额编辑耦合在一起；
  四个配额 SpinBox 用 `Component.onCompleted` 赋初值而非绑定，外部改动不会回显。
  范围跨多年时后端 byDay 最多 366 行，UI 无降采样提示。
- ActivityLogPage 只覆盖最近 500 条，且无自动刷新；它不使用 LogModel。
  LogModel 是运行时日志面板（LogPanel）的模型，两者数据源完全不同，勿混接。
- ModelConfigPage 的 `savedHint` 只表示最近一次字段写入，不代表整页校验通过；
  删除配置依赖 `profiles.length > 1`，且删除后编辑目标回落到 activeProfileId。
- SecurityPage 的黑名单是“追加项”，内置规则不可在 UI 删除；空列表只代表无追加项。
- SettingsBackup 明确排除加密字段、`security/*`、`ui/*`、`modelProfiles`、
  `tools/disabled`、默认权限与自启；恢复后需要用户重新确认这些高风险项。
- 各页大量使用 `Component.onDestruction` 兜底提交，若页面被非 Loader 方式复用需重新评估。
