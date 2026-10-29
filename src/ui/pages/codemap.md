# src/ui/pages 架构地图

## 职责

本目录包含主聊天页与「设置中心」的全部内容页：

| 文件 | 角色 | 是否由 SettingsCenterDialog 装载 |
|---|---|---|
| AgentChatPage.qml | 主聊天工作区（顶栏 / 会话标签 / 轨迹视口 / 输入区） | 否，由 main.qml 直接实例化 |
| ModelConfigPage.qml | 模型 provider/profile/模型列表/思考强度/上下文预算 | 是，分类 `model` |
| AgentBehaviorPage.qml | 单次任务步数预算、超时、自定义系统提示词（原 GeneralPage 的模型行为区块） | 是，分类 `agent` |
| NetworkPage.qml | HTTP 代理模式（跟随系统/手动/不使用）、主机端口、认证、连接测试 | 是，分类 `network` |
| LogsPage.qml | 日志级别、保存目录、单文件大小上限、保留文件数 | 是，分类 `logs` |
| KnowledgePage.qml | 知识库嵌入向量服务（Ollama / OpenAI 兼容）、模型、地址、API Key | 是，分类 `knowledge` |
| InterfacePage.qml | 主题、字体、语言、窗口默认布局 | 是，分类 `interface` |
| GeneralPage.qml | 仅剩 `general` 分类的声明式偏好（托盘/自启/通知）与「关于 NexusAI」 | 是，分类 `general` |
| SecurityPage.qml | 读取沙箱、项目写入、黑名单、仅本地/内网访问 | 是，分类 `security` |
| McpPage.qml | MCP 服务器增删、启停、连通状态 | 是，分类 `mcp` |
| ToolsPage.qml | 单工具启停、联网搜索、默认权限模式 | 是，分类 `tools` |
| MemoryPage.qml | 长期记忆（episodic）浏览/搜索/遗忘、审计与 Token 保留天数 | 是，分类 `memory` |
| UsageStatsPage.qml | token/费用/配额统计与请求明细 | 是，分类 `usage`（绑定 `dialog.opened`） |
| ActivityLogPage.qml | usage_log 操作/网络活动时间线 | 是，分类 `activity`（绑定 `dialog.opened`） |
| SettingsBackupPage.qml | 设置 JSON 导入导出、数据库备份/恢复 | 是，分类 `backup` |
| ProjectRulesPage.qml | 项目列表 + 项目规则（AGENTS.md）就地编辑与注入状态 | 是，分类 `projects`（绑定 `dialog.opened`） |
| PromptAssetsPage.qml | 提示词与指令：三段式管理（系统提示预设/斜杠指令/会话模板），行内编辑表单；套用与展开逻辑不在本页（TopBar 菜单 / InputConsole 发送链 / Sidebar 模板菜单）。**布局陷阱（回归测试 promptAssetsPageShowsCreateButtons 锁定）**：ScrollView 内容列的宽度必须写 `width: assetsScroll.availableWidth`（限定 id）——裸 `availableWidth` 在子项作用域解析不到，绑定失效后宽度回退到未折行文本的隐式宽度（≈860px），整页被推出对话框右侧、全部交互控件不可见但 `visible=true`；换行/省略的 Text 还须 `Layout.minimumWidth: 0`，否则未折行隐式宽度成为布局最小宽度、布局拒绝收缩。是，分类 `prompts`（绑定 `dialog.opened`） |

注意：任务描述里的「记忆页 = 知识库文档导入/删除/检索」与代码不符。MemoryPage 管理的是
会话摘要型长期记忆；知识库文档的导入/删除/重新嵌入在 `panels/ContextPanel.qml` 中，
检索由模型通过知识库工具完成。向知识库生成索引所用的**向量服务配置**（provider/model/
baseUrl/API key）在 `KnowledgePage.qml`，二者职责不同。详见下文约束与风险。

## 设计

### 设置中心的装载方式

`SettingsCenterDialog.qml` 不用 StackLayout，而是为每个分类放一个 `Loader`：

- `active: dialog.currentCategory === "<id>"`，`visible: active`；
- `source` 是 `qrc:/qt/qml/NexusAI/src/ui/pages/<Page>.qml`；
- 分类 id 与顺序由 `SettingsSchema::categories()` 定义，经
  `PreferenceRegistry.categories()/categoryName()` 暴露；导航按
  `SettingsSchema::categoryGroups()` 的四组渲染（`CategoryNav` 可滚动）；
- `usage` 与 `activity` 额外与 `dialog.opened` 相与，关闭即销毁、重开重新查询
  （避免统计/时间线停留在首次加载的旧值）；其余页切走分类即销毁；
- 搜索态（`searchField.text` 非空）只隐藏分类页包裹层，Loader 状态保留，跳转后原位恢复；
- 对话框 900×680，左侧 200px 分组导航，底栏只有「恢复默认」与「关闭」，没有「保存/取消」。
  「恢复默认」按当前分类分派：声明式偏好调用 `EditBuffer.resetToDefault(keys)`；页面自绘
  字段调用该页导出的 `requestResetDefaults()`；两者都没有时按钮隐藏。`onClosed` 只做
  `EditBuffer.rollback()`，丢弃校验失败留下的 staged 残值。

`openCategory(id)` 设定 `explicitCategory=true` 后打开；`onOpened` 在非显式指定时恢复
`PreferenceStore.load("settings.lastCategory")`。打开入口包括 Ctrl+,、侧栏设置、
AgentChatPage 空态的「配置模型」、ModelSelector、`AgentVM.modelSettingsRequested`。

### 保存时机

1. **即时直写 SettingsService**：SecurityPage、ModelConfigPage、McpPage、ToolsPage、
   AgentBehaviorPage、NetworkPage、LogsPage、KnowledgePage、MemoryPage、UsageStatsPage、
   SettingsBackupPage、GeneralPage 的 SettingsService 字段。
   每个控件在 `onToggled/onActivated/onValueModified/onEditingFinished` 写属性。
2. **即时写穿 EditBuffer**：InterfacePage 与 GeneralPage 的声明式偏好调用
   `EditBuffer.stage(key, value)`；`EditBuffer::stage` 校验后立即 `saveBatch`，
   `staged` 只在失败或“值等于已存值”时短暂存在，因此仍是即时保存而非批量提交。
3. **文本输入**：多数在失焦/回车提交，并用 `Component.onDestruction` 兜底（Loader
   切分类不触发 `editingFinished`）；长文本（系统提示词、代理主机、嵌入地址）另有
   400ms 防抖。走 `SettingsField` 的字段由 `commitDebounce`/`commitOnDestroy` 承担同一
   语义，并须把 `lastKnownValue` 声明式绑到已存值——只有真正改过的文本才会在销毁时
   提交（否则页面重建会把原值再写一遍）。密钥字段统一 `secret + ignoreEmptySubmit +
   clearOnSubmit`：留空即不改，写完清空输入框，只能由页面的「清除密钥」按钮删除。
4. **无持久化状态**：连接测试结果、日期错误/分页游标、Activity 本地筛选、
   episodes 缓存只在页面生命周期内有效。

### 版面原语（P2）

设置页不再各写一套「标题 + 说明 + 控件」的排版，统一用三个组件：

- `SettingsSection`：扁平分组（大标题 + 可选说明 + 内容列），用于长页面的分段；
- `SettingRow`：左列标签/说明、右列控件的单行布局，控件宽度由自身
  `Layout.preferredWidth` 决定（开关、下拉、SpinBox 都走这里）；
- `SettingsField`：定宽标签 + 单行输入框（可选密文/等宽/防抖/销毁兜底），
  需要整行铺满的输入项走这里。

`SettingCard` 仍保留给「带边框的独立设置卡」（ToolsPage 的工具卡、LogsPage 的日志
目录块等），两者只做排版、不读写设置。Interface/Security/Tools/Memory/General 的字段
已全部改为 section + row；AgentBehavior/Network/Logs/Knowledge 中的相应区块也同步改用
同一组原语，因此新页面**不要**再手写标签列与像素间距。

### 各页对应的 schema 键组

- `model`：`modelProfiles`（JSON 配置数组）、`activeProfileId`；另读 `contextTokens`
  作为上下文预算的最终回退值。profile 内部字段（kind/provider/apiProtocol/baseUrl/
  apiKey/model/models/temperature/maxOutputTokens/numCtx/reasoningEffort/contextLength）
  不是独立 schema 键，由 SettingsService 合并写入 `modelProfiles`。
- `interface`：`prefs/v1/ui.themeMode`、`ui.fontSans`、`ui.fontMono`、`ui.baseFontSize`、
  `ui.locale`、`ui.defaultSidebarWidth`、`ui.defaultPanelWidth`、
  `ui.defaultSidebarExpanded`、`ui.defaultPanelVisible`、`ui.restoreWindowOnStartup`。
- `general`：`prefs/v1/window.closeToTray`、`prefs/v1/general.autoStart`、
  `prefs/v1/notice.enabled`。
- `agent`：`agent/maxSteps`、`agent/taskTimeoutMinutes`、`agent/systemPromptOverride`。
- `network`：`network/proxyMode`、`network/proxyHost`、`network/proxyPort`、
  `network/proxyAuth`（用户名 + DPAPI 加密密码）。
- `logs`：`logging/level`、`logging/directory`、`logging/maxFileSizeMb`、
  `logging/retentionFiles`。
- `knowledge`：`embedding/provider`、`embedding/model`、`embedding/baseUrl`、
  `embedding/apiKey`（DPAPI 加密；复位默认不清空，需在页面显式「清除密钥」）。
- `security`：`security/allowFullDiskRead`、`security/allowProjectWrite`、
  `security/localOnlyMode`、`security/allowLocalNetworkFetch`、`security/readDenyList`。
- `tools`：`tools/disabled`（经 ToolRegistry 持久化）、`webSearch/provider`、
  `webSearch/zhipuApiKey` / `webSearch/deepseekApiKey`（各自加密，按 provider 分桶）、`agent/defaultPermissionMode`。
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
- SecurityPage：两个 `SettingsSection`（文件读取沙箱 / 隐私门），每项是 `SettingRow` +
  无文字 CheckBox（行标签即 `Accessible.name`）；CheckBox 直接写 `Settings.*`；黑名单增删以
  `slice()` 复制后整体赋回 `Settings.readDenyList`，去重按 Windows 大小写不敏感语义。
  判定顺序由后端执行：系统目录封锁 → 黑名单 → 允许层（用户目录 ∪ 当前项目），
  页面文案与之一致。
- McpPage：`Repeater model: McpManager.servers`，每项
  `{name, transport, endpoint, enabled, state, toolCount}`；`state` 2=已连接、
  3=失败、其余=连接中/未启用。`addServer` 解析 stdio 命令行为 command+args 或 http URL；
  `setEnabled`、`removeServer` 即时生效并写回 mcp_servers.json。
- ToolsPage：`AgentVM.toolRegistryModel` 每项
  `{name, description, level(safe/requires_confirm/dangerous), enabled,
  blockedByPolicy, source(builtin/MCP)}`；Switch 调 `AgentVM.setToolEnabled`，
  由 ToolRegistry 写 `tools/disabled` 并刷新模型。`blockedByPolicy` 只做徽标，
  不改变 Switch 的用户层状态。联网搜索 provider 是 `SettingRow` + ComboBox，API Key 是
  `SettingsField`（密文、留空不改、提交后清空）+「清除密钥」按钮，key 已配置时只显示
  `webSearchApiKeySet` 的占位文案，不回显明文。
- AgentBehaviorPage：步数 `SettingsField`（`IntValidator` 3–1000、`ignoreEmptySubmit`、
  `commitOnDestroy`；非法值在 `onSubmitted` 内被丢弃）、超时为 `SettingRow`，均直接写
  `Settings.maxSteps/taskTimeoutMinutes`；系统提示词经 400ms 防抖写
  `Settings.systemPromptOverride`；页头显示 `Settings.maxSteps` 与超值的实时回显，
  `requestResetDefaults()` → `Settings.resetAgentToDefaults()`。
- NetworkPage：代理字段直写 `Settings.proxyMode/proxyHost/proxyPort/proxyUsername/
  proxyPassword`；主机/用户名用 `SettingsField`（等宽 + 400ms 防抖 + 销毁兜底），端口为
  `SettingRow` + SpinBox；「测试代理」调用 `Settings.testProxy()`，结果在页面内联显示；
  密码为掩码 getter，页面只读 `proxyPasswordSet`，清空需显式「清除密码」。
- LogsPage：级别/目录/大小/保留数直写 `Settings.log*`；目录用 `FolderDialog` 选择并可
  恢复默认；`requestResetDefaults()` → `Settings.resetLoggingToDefaults()`。
- KnowledgePage：嵌入 provider/model 用 `SettingRow` + ComboBox，baseUrl 用 `SettingsField`
  （等宽 + 400ms 防抖），API key 用 `SettingsField`（密文 + 留空不改 + 提交后清空）+ 清除
  按钮，全部直写 `Settings.embedding*`；切换到外部 provider 时提示文档内容会发送到所配置
  端点，纯本地模式下提示请求会被拦截并回退 Ollama；`requestResetDefaults()` →
  `Settings.resetEmbeddingToDefaults()`（保留密钥）。
- MemoryPage：「记忆与保留」`SettingsSection`（Switch + 两个保留天数 SpinBox，均为
  `SettingRow`）直写 `memory/*`；记忆列表走 `listEpisodic(500)` /
  `searchEpisodic(keyword, 500)` / `removeEpisodic(id)` / `clearEpisodic()`；
  `AgentVM.episodicChanged` 触发重新查询。搜索框 `onTextChanged` 立即查询（无防抖），
  计数与空态随结果变化。
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
- 单例：`Settings`、`PreferenceStore`、`PreferenceRegistry`、`SettingsCatalog`、
  `EditBuffer`、`AgentVM`、`McpManager`、`ConnectionTester`、`SettingsBackup`、`Theme`、
  `I18n`、`ChangeBus`。页面之间不直接互相引用，全部经单例与信号解耦。
- CategoryNav 切换时写 `settings.lastCategory`；`jumpToCategory` 先清空搜索再写同一键。
- `PreferenceRegistry.categories()` 同时驱动导航与恢复默认；`SettingsCatalog` 提供
  搜索/定位/复位元数据（偏好自动派生，页面自绘项在 `SettingsCatalog.cpp` 静态表登记，
  条目 id 必须与页面上同名字段的 `objectName` 一致）。

## 关键文件

- `src/ui/pages/AgentChatPage.qml`
- `src/ui/pages/ModelConfigPage.qml`
- `src/ui/pages/GeneralPage.qml`
- `src/ui/pages/AgentBehaviorPage.qml`
- `src/ui/pages/NetworkPage.qml`
- `src/ui/pages/LogsPage.qml`
- `src/ui/pages/KnowledgePage.qml`
- `src/ui/pages/InterfacePage.qml`
- `src/ui/pages/SecurityPage.qml`
- `src/ui/pages/McpPage.qml`
- `src/ui/pages/ToolsPage.qml`
- `src/ui/pages/MemoryPage.qml`
- `src/ui/pages/UsageStatsPage.qml`
- `src/ui/pages/ActivityLogPage.qml`
- `src/ui/pages/SettingsBackupPage.qml`
- `src/ui/pages/ProjectRulesPage.qml`
- `src/viewmodels/AgentViewModel.h`（`loadProjectRules` / `writeProjectRules` /
  `projectRulesState`）、`src/core/agent/AIAgent.cpp`（`projectRulesBlock`）
- `src/ui/components/SettingsCenterDialog.qml`、`CategoryNav.qml`、
  `SettingsSection.qml`、`SettingRow.qml`、`SettingsField.qml`、
  `SettingCard.qml`、`PreferenceControl.qml`
- `src/core/utils/SettingsSchema.cpp`、`src/viewmodels/EditBuffer.cpp`、
  `src/viewmodels/SettingsService.h`、`src/viewmodels/McpManager.h`、
  `src/core/memory/IMemory.h`

## 约束与风险

- MemoryPage 与任务描述的知识库职责不一致：知识库导入/删除/检索不在本页；
  若按描述改动会误改语义。知识库 UI 的单一入口是 ContextPanel；向量服务配置在
  KnowledgePage（`embedding/*`），不要把两页合并。
- 设置中心「恢复默认」在底栏（带确认弹窗），按当前分类分派到 `EditBuffer.resetToDefault`
  或页面的 `requestResetDefaults()`。各页复位入口只回写本领域 schema 键：`resetNetwork*`
  会清空代理用户名/密码，`resetEmbedding*` 刻意保留 `embedding/apiKey`（密钥只能在页面
  显式清除），其余页不得越界写别人的键。2026-10-25 已移除页头的重复复位按钮。
- 新增设置页必须同时：加入 `CMakeLists.txt` 的 `QML_FILES`、在 `SettingsCenterDialog`
  增加同名 `Loader`、在 `SettingsSchema::categories()` 登记分类（含 `groupId`）、在
  `src/viewmodels/SettingsCatalog.cpp` 登记页面自绘项（id 与控件 `objectName` 一致，
  `test_settings` 会断言 schema 键全覆盖），并提供 `requestResetDefaults()`（如该页有可复位
  的 SettingsService 字段）。
- InterfacePage 的值读 `EditBuffer.staged` 再回落 `PreferenceStore`，若未来把
  `stage()` 改回纯暂存，页面会表现为“能改但不落盘”；当前依赖 EditBuffer 的写穿语义。
- UsageStatsPage 单文件约 1250 行，聚合、图表映射、分页、配额编辑耦合在一起；
  四个配额 SpinBox 仍用 `Component.onCompleted` 赋初值，但已配 Settings 信号
  Connections（焦点护栏）回绑外部变更——备份恢复等改动不再显示旧值、也不会被
  下一次 onValueModified 写回。范围跨多年时后端 byDay 最多 366 行，UI 无降采样提示。
- ActivityLogPage 只覆盖最近 500 条，且无自动刷新；它不使用 LogModel。
  LogModel 是运行时日志面板（LogPanel）的模型，两者数据源完全不同，勿混接。
- ModelConfigPage 的 `savedHint` 只表示最近一次字段写入，不代表整页校验通过；
  删除配置依赖 `profiles.length > 1`，且删除后编辑目标回落到 activeProfileId。
- SecurityPage 的黑名单是“追加项”，内置规则不可在 UI 删除；空列表只代表无追加项。
- SettingsBackup 明确排除加密字段、`security/*`、`ui/*`、`modelProfiles`、
  `tools/disabled`、默认权限与自启；恢复后需要用户重新确认这些高风险项。
- 各页大量使用 `Component.onDestruction` 兜底提交，若页面被非 Loader 方式复用需重新评估。
  新建文本字段优先用 `SettingsField`（`commitDebounce`/`commitOnDestroy`/`lastKnownValue`
  已封装该契约）；手写 `TextField` 时必须自己补 `Component.onDestruction`，且密钥类字段
  只能读 `text`，不能读 `displayText`（掩码）。
- 设置页的标签列、间距与标题层级统一由 `SettingsSection`/`SettingRow`/`SettingsField`
  承担；新增页面不要再复制像素级 `Layout.*Margin` 或自绘标签列，否则又会漂移出不一致的
  对齐（P2 前的现状）。`test_chat_workflows` 的 `settingsPagesInstantiateWithoutWarnings`
  会逐个 Loader 装载这些页面并断言无 QML 警告。
- ProjectRulesPage 是项目规则的唯一编辑入口（侧栏项目行不提供入口）。它读写的是
  项目根目录的 `AGENTS.md`（找不到时读 `.nexus/rules.md`，但保存永远写 `AGENTS.md`）。
  编辑区 500 ms 防抖落盘，切换项目/关窗前冲刷；**冲刷目标必须是 `loadedRoot`**——
  `onSelectedProjectChanged` 触发时 `selectedProject` 已是新项目，经它冲刷会把上一个
  项目的文本写进新项目的 `AGENTS.md`（首轮实现的真实数据丢失 bug，勿回退）。
  状态徽标/项目状态点靠 `stateRevision` 计数器重算：`projectRulesState()` 是
  Q_INVOKABLE 无通知信号，不加计数器徽标会在编辑后冻结在旧状态。
  `stateMeta()` 按原始状态串映射颜色，禁止按翻译后文本匹配。
  未编辑的种子模板按 SHA256 判定为「空」，不注入系统提示（共享
  `AIAgent::isUntouchedRulesSeed()`，见 AIAgent.cpp）。
  注入状态只是读取时刻的快照：`active` 不代表下一次任务一定注入（文件可能在外部被改）。
  外部编辑经 `AgentVM.watchProjectRules` 协调（QFileSystemWatcher 监听两个候选文件，
  写盘后自动重挂）：编辑器干净时磁盘变化静默重载；`dirty`（未落盘编辑）时弹
  「重新加载 / 保留我的修改」横幅，而不是被下一次防抖写静默覆盖。
