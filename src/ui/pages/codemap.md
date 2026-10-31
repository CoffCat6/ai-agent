# src/ui/pages 架构地图

## 职责

本目录包含主聊天页与「设置中心」的全部内容页：

| 文件 | 角色 | 是否由 SettingsCenterDialog 装载 |
|---|---|---|
| AgentChatPage.qml | 主聊天工作区（顶栏 / 会话标签 / 轨迹视口 / 输入区） | 否，由 main.qml 直接实例化 |
| ModelConfigPage.qml | 模型 provider/profile/模型列表（支持从端点拉取）/思考强度/上下文预算；芯片可拖拽排序（`moveProfile`，`DropArea` 落点 + `reflowTick` 复排），底栏「复制」生成不带密钥的新配置 | 是，分类 `model` |
| AgentBehaviorPage.qml | 单次任务步数预算、超时、自定义系统提示词（原 GeneralPage 的模型行为区块） | 是，分类 `agent` |
| NetworkPage.qml | HTTP 代理模式（跟随系统/手动/不使用）、主机端口、认证、连接测试 | 是，分类 `network` |
| LogsPage.qml | 日志级别、保存目录、单文件大小上限、保留文件数、最近日志（尾部读取/过滤）、诊断包导出 | 是，分类 `logs` |
| KnowledgePage.qml | 知识库嵌入向量服务（Ollama / OpenAI 兼容）、模型、地址、API Key | 是，分类 `knowledge` |
| InterfacePage.qml | 主题、字体（名称搜索 + 等宽字体约束）、语言、窗口默认布局、减少动效（S4.7） | 是，分类 `interface` |
| GeneralPage.qml | 仅剩 `general` 分类的声明式偏好（托盘/自启/通知）与「关于 NexusAI」（构建信息、数据目录、诊断包、开源许可） | 是，分类 `general` |
| SecurityPage.qml | 读取沙箱、项目写入、黑名单、仅本地/内网访问 | 是，分类 `security` |
| ShortcutsPage.qml | 全局快捷键逐动作录制/取消/恢复（docs/31 N2）：动作表与校验在 `ShortcutMap`，本页只负责录制交互与提示；录制期间把 `ShortcutMap.recording` 置真以让窗口级 `Shortcut` 让位，切页/关对话框/销毁时收回 | 是，分类 `shortcuts` |
| McpPage.qml | MCP 服务器增删、启停、重新连接、连通状态、配置导入/导出（同名冲突预览 + 策略）；新增/编辑走 `McpServerEditor` 弹窗（含 stdio 命令、环境变量、端点 URL、请求头） | 是，分类 `mcp` |
| ToolsPage.qml | 内置工具（默认折叠）/ MCP 工具分组启停、名称/说明搜索、联网搜索、默认权限模式 | 是，分类 `tools` |
| MemoryPage.qml | 长期记忆（episodic）浏览/搜索/遗忘、审计与 Token 保留天数 | 是，分类 `memory` |
| UsageStatsPage.qml | token/费用/配额统计、请求明细（可导出 CSV） | 是，分类 `usage`（绑定 `dialog.opened`） |
| ActivityLogPage.qml | usage_log 操作/网络活动时间线 | 是，分类 `activity`（绑定 `dialog.opened`） |
| SettingsBackupPage.qml | 设置 JSON 导入导出、数据库备份/恢复、启动时自动备份与保留份数 | 是，分类 `backup` |
| DataPage.qml | docs/31 S4.5 N5：数据管理总览——DB/WAL 体积与各表行数（`AgentVM.requestDataStats()` 异步统计，`dataStatsReady` 回填）、按保留天数立即清理 / 清空用量 / VACUUM（三个动作全部二次确认，`storageConfirmDialog`）、最近备份展示 + 立即备份/打开备份目录 | 是，分类 `storage`（绑定 `dialog.opened`，重开重新拉统计） |
| ProjectRulesPage.qml | 项目列表 + 项目规则（AGENTS.md）就地编辑与注入状态 | 是，分类 `projects`（绑定 `dialog.opened`） |
| PromptAssetsPage.qml | 提示词与指令：三段式管理（系统提示预设/斜杠指令/会话模板），行内编辑表单；套用与展开逻辑不在本页（TopBar 菜单 / InputConsole 发送链 / Sidebar 模板菜单）。头部工具栏另有「导出预设包…」「导入预设包…」（`PresetPackTransfer`）：导出只写两组资产 + 当前禁用工具与默认权限模式；导入先 `previewPack` 展示四组计数、同名冲突清单（最多 6 条 + “等 N 条”）与策略下拉（默认全部重命名），确认后才落盘，包内禁用工具与权限模式始终应用（不参与逐条冲突）。**布局陷阱（回归测试 promptAssetsPageShowsCreateButtons 锁定）**：ScrollView 内容列的宽度必须写 `width: assetsScroll.availableWidth`（限定 id）——裸 `availableWidth` 在子项作用域解析不到，绑定失效后宽度回退到未折行文本的隐式宽度（≈860px），整页被推出对话框右侧、全部交互控件不可见但 `visible=true`；换行/省略的 Text 还须 `Layout.minimumWidth: 0`，否则未折行隐式宽度成为布局最小宽度、布局拒绝收缩。是，分类 `prompts`（绑定 `dialog.opened`） |

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
  （避免统计/时间线停留在首次加载的旧值）；`storage`（数据管理）同理；其余页切走分类即销毁；
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
   400ms 防抖。走 `SettingsField` 的字段默认**打字停顿 600ms 即提交**（docs/31 S4.8，
   `commitDebounce` 默认 600；回车/失焦仍立即提交；validator 中间态不落盘；modelRow
   因提交会并进 models 列表而显式归零），并须把 `lastKnownValue` 声明式绑到已存值——
   只有真正改过的文本才会在销毁时提交（否则页面重建会把原值再写一遍）。密钥字段统一
   `secret + ignoreEmptySubmit + clearOnSubmit`：留空即不改（且强制不防抖），写完清空
   输入框，只能由页面的「清除密钥」按钮删除。
4. **无持久化状态**：连接测试结果、日期错误/分页游标、Activity 本地筛选、
   episodes 缓存只在页面生命周期内有效。

### 版面原语（P2 + 视觉底座）

设置页不再各写一套「标题 + 说明 + 控件」的排版，统一用以下组件：

- `SettingsPage`：整页滚动骨架（页头 + `horizontalMargin`/`contentSpacing` + `AppScrollBar`）。页面只声明内容，子项不要再加左右 `Layout.*Margin`；搜索目录需要锚定页头时传 `headerObjectName`（与 `SettingsCatalog` 条目 id 相同）。
- `SettingsPageHeader`：统一页头（H2 标题 + caption 说明），固定页头页面直接把它放在顶部。
- `SettingsSection`：扁平分组（大标题 + 可选说明 + 内容列），用于长页面的分段；
- `SettingRow`：左列标签/说明、右列控件的单行布局，控件宽度由自身
  `Layout.preferredWidth` 决定（开关、下拉、SpinBox 都走这里）；
- `SettingsField`：定宽标签 + 单行输入框（可选密文/等宽/防抖/销毁兜底），
  需要整行铺满的输入项走这里；`statusText/statusOk` 在输入框右侧显示
  「已配置/未配置」徽标，密钥字段用它表达当前是否已存有凭据（不要塞进 placeholder）；
- `StatusLine`：测试类操作的内联结果行（`busy/success/warning/danger`），
  进行中状态点带脉冲，替代各页自写 `Text` + 条件颜色；
- `SettingCard`：带边框的独立设置卡，内部间距与圆角已 token 化。

已迁到 `SettingsPage` 的整页滚动页面：General、AgentBehavior、Interface、Security、
Network、Logs、Knowledge、Tools、Mcp、SettingsBackup。Memory/Usage/Activity/ProjectRules/
PromptAssets/ModelConfig 因需要固定页头 + 内部列表滚动，保留各自滚动结构，但页头统一为
`SettingsPageHeader`，字号/间距/圆角/输入框表面全部改用 `Theme` 令牌（`fontSizeMicro`、
`radiusField`、`radiusBadge`、`space*`）。说明文字一律 caption + `textSecondary`，不再叠加
`opacity`；输入框统一 `surfaceHover` 底 + `radiusField`，聚焦时品牌色 2px 边框。
新页面**不要**再手写标签列与像素间距。

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
  `monthlyCostQuotaCents`、`strictQuotaMode`、`usage/alertPercent`（经
  `Settings.quotaAlertPercent`，0 = 关闭提醒）；统计数据来自 `token_usage` 表。
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
  结果经 `testFinished(ok, message)` 回填到 `StatusLine`（busy/success/danger）；
  测试模型默认当前模型，用户选择后 `testModelPinned` 固定。`Settings.endpointOptions(provider)`
  决定协议芯片，`Settings.modelContextWindow(apiProtocol, model)` 决定能力表窗口。
  API Key 输入框带「已配置/未配置」徽标（`SettingsField.statusText`）。
  「从端点拉取模型」按钮（`modelFetchModels`）调
  `ConnectionTester.fetchModels(apiProtocol, baseUrl, apiKey)`：按协议走
  `/models`、`/v1/models`、`/v1beta/models`、`/api/tags`，结果经
  `modelsFetched(ok, models, message)` 回填；成功时把新模型逐个
  `Settings.addModelToProfile(id, name)` 去重追加，并显示「共 N 个，新增 M 个」。
  服务地址为空时按钮禁用并提示先填端点。
- SecurityPage：两个 `SettingsSection`（文件读取沙箱 / 隐私门），每项是 `SettingRow` +
  无文字 CheckBox（行标签即 `Accessible.name`）；CheckBox 直接写 `Settings.*`；黑名单增删以
  `slice()` 复制后整体赋回 `Settings.readDenyList`，去重按 Windows 大小写不敏感语义。
  判定顺序由后端执行：系统目录封锁 → 黑名单 → 允许层（用户目录 ∪ 当前项目），
  页面文案与之一致。黑名单为空时提示用 `EmptyState`，外层 `Item` 在空列表时给 34px
  占位高度——ListView 空时 `contentHeight` 为 0，直接 `Layout.preferredHeight:
  Math.min(contentHeight, 160)` 会让提示与下方输入行重叠。
- McpPage：`Repeater model: McpManager.servers`，每项
  `{name, transport, endpoint, enabled, state, toolCount, tools, command, args, url, env, headers}`；
  已连接服务器在卡片下部显示 `tools` 名预览（一行省略、悬停看全量），排查工具来源
  不必切到工具页；`tools` 为空时整行隐藏。
  `state` 2=已连接、3=失败、其余=连接中/未启用。头部「添加服务器…」与卡片上的
  铅笔按钮都打开 `McpServerEditor`（`openForAdd()` / `openForEdit(modelData)`），
  它把字段解析成 `{name, transport, command, args, env, headers, url}` 后交给
  `McpManager.addServer` / `updateServer`；编辑走 `updateServer(原名称, 新配置)`，
  在原条目上改名并整条重建 client——**不要**用「删了重加」，那会让该服务器提供的
  工具全部掉线、并把启用状态重置。刷新按钮调 `McpManager.reconnect(name)`。
  `setEnabled`、`removeServer` 即时生效并写回 mcp_servers.json。
- McpPage 导入/导出（docs/31 N7）：「导出配置…」走 `FileDialog`（SaveFile，`.json`）
  调 `McpManager.exportServers(path)`，结果文案区分「有凭据未包含」与普通成功；
  「导入配置…」选文件后先 `previewServerImport(path)` 打开确认弹窗——摘要行
  （`mcp/importSummary`）报告可导入/同名冲突/含凭据条数，策略下拉
  （`mcp/importPolicy`：跳过/覆盖/重命名）只对同名项生效，列表 `mcp/importList`
  逐条显示名称、同名徽标、非法原因与端点；确认后才 `importServers(path, policy)`
  落盘并停用未启用项。**导入的服务器一律禁用**，用户必须逐台手动启用（凭据不可导出，
  启用前先补全 env/headers）。顶部状态行 `transferMessage` 显示导入/导出结果，
  失败转危险色。
- ToolsPage：`AgentVM.toolRegistryModel` 每项
  `{name, description, level(safe/requires_confirm/dangerous), enabled,
  blockedByPolicy, source(builtin/MCP)}`；Switch 调 `AgentVM.setToolEnabled`，
  由 ToolRegistry 写 `tools/disabled` 并刷新模型。`blockedByPolicy` 只做徽标，
  不改变 Switch 的用户层状态。列表按 `source` 拆成两个 `SettingsSection`：
  「内置工具」默认折叠（badge 显示总数/已启用数，避免十几行铺满首屏），
  「MCP 工具」单独成组、无工具时显示前往 MCP 页的提示；行组件是 `ToolRow.qml`。
  页面顶部 `toolsSearch` 输入框按「名称/说明」子串过滤两组（`builtinTools`/`mcpTools`
  是过滤后的派生数组，`toolRegistryModel.length` 才是全量），命中时自动展开内置组；
  两组都没有匹配时显示 `toolsSearchEmpty` 提示，清空搜索恢复完整列表。
  联网搜索 provider 是 `SettingRow` + ComboBox，API Key 是
  `SettingsField`（密文、留空不改、提交后清空、`statusText` 显示已配置）+「清除密钥」按钮；
  key 已配置时只显示 `webSearchApiKeySet` 的占位文案，不回显明文。测试按钮在请求期间改文案
  「测试中…」，结果写入 `StatusLine`（busy/success/warning）。
- AgentBehaviorPage：步数 `SettingsField`（`IntValidator` 3–1000、`ignoreEmptySubmit`、
  `commitOnDestroy`；非法值在 `onSubmitted` 内被丢弃）、超时为 `SettingRow`，均直接写
  `Settings.maxSteps/taskTimeoutMinutes`；系统提示词经 400ms 防抖写
  `Settings.systemPromptOverride`；页头显示 `Settings.maxSteps` 与超值的实时回显，
  `requestResetDefaults()` → `Settings.resetAgentToDefaults()`。
- NetworkPage：代理字段直写 `Settings.proxyMode/proxyHost/proxyPort/proxyUsername/
  proxyPassword`；主机/用户名用 `SettingsField`（等宽 + 400ms 防抖 + 销毁兜底），端口为
  `SettingRow` + SpinBox；「测试代理」调用 `Settings.testProxy()`，请求期间按钮文案
  「测试中…」，结果写入 `StatusLine`（busy/success/warning）；
  密码为掩码 getter，页面只读 `proxyPasswordSet` 并在输入框右侧显示「已配置/未配置」，
  清空需显式「清除密码」；「恢复本页默认值」会一并清空代理用户名与密码，确认框有专门提示。
- LogsPage：级别/目录/大小/保留数直写 `Settings.log*`；目录用 `FolderDialog` 选择并可
  恢复默认；`requestResetDefaults()` → `Settings.resetLoggingToDefaults()`。
  「最近日志」面板（docs/31 O6）走 `LogTail.read(path="", maxLines, minLevel, keyword)`，
  级别/行数/关键字只是本页查看状态、**不写设置**；关键字输入 300 ms 防抖，「跟随最新」
  用 2 秒 `Timer`（仅 `visible` 时运行，`positionViewAtEnd()` 只在开启时执行）。
  列表等宽字体按级别着色，状态行显示匹配数/读取字节/截断标记，失败显示 `LogTail` 的 error。
  「导出诊断包」用 `FolderDialog` 选父目录，`SettingsBackup.exportDiagnostics(dir)`
  新建 `nexus-diagnostics-<yyyyMMdd-HHmmss>/`（同秒重复追加 `-2`），内含
  `settings.json`（与「导出设置」同一套脱敏规则）、`logs/`（复制 `nexus-*.log`，
  单个 >32 MB 跳过）、`environment.txt`（版本 / Qt / OS / 架构 / 日志目录 / 已复制数）。
  Qt 无公开 zip 写入 API，故导出目录而非压缩包；成功返回目录绝对路径，失败走 `lastError`。
- KnowledgePage：嵌入 provider/model 用 `SettingRow` + ComboBox，baseUrl 用 `SettingsField`
  （等宽 + 400ms 防抖），API key 用 `SettingsField`（密文 + 留空不改 + 提交后清空 +
  `statusText` 已配置徽标）+ 清除按钮，全部直写 `Settings.embedding*`；切换到外部 provider
  时提示文档内容会发送到所配置端点，纯本地模式下提示请求会被拦截并回退 Ollama；
  `requestResetDefaults()` → `Settings.resetEmbeddingToDefaults()`（保留密钥）。
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
  请求明细表头「导出 CSV」把 `detailTable.viewRows`（筛选 + 排序后的当前视图，
  不是整库）交给 `AgentVM.exportUsageCsv(path, rows)`：UTF-8 BOM + CRLF +
  RFC 4180 转义，`= + - @` 开头的单元格前置单引号防公式注入；空行/写失败
  返回 false 并在表头旁显示错误提示。
- ActivityLogPage：`AgentVM.listUsageEvents(500)` 全量拉取（id DESC），
  QML 侧按类别精确匹配 + 关键词对 action/target/detail 不区分大小写过滤；
  刷新按钮手动重拉，页面重建时也会拉一次。
- SettingsBackupPage：`SettingsBackup.exportSettings` / `previewImport` /
  `importSettings`。导出卡片可先选范围（`exportableCategories()`，
  空数组 = 全量；`profiles` 归 `model`、`mcpServers` 归 `mcp`）；
  恢复前弹预览：分类摘要、**逐键差异**（`changes`，分类小标题 +
  `当前 → 备份`）、被跳过的密钥、警告；
  「自动备份」卡直接绑 `Settings.backupOnStartup` / `Settings.backupKeepCount`
  / `Settings.backupIntervalHours`
  （`objectName` 为 `memory/backupOnStartup`、`memory/backupKeepCount`、
  `memory/backupIntervalHours`，`SettingsCatalog` 登记为可见字段级条目，
  可按「备份保留」「定时」等关键词搜到）；间隔是五档 ComboBox（关闭 /
  1 小时 / 6 小时 / 1 天 / 7 天），`onActivated` 写小时数，
  `Connections.onBackupIntervalHoursChanged` 反向刷新，导入/复位也能同步；
  页脚「恢复默认」走 `requestResetDefaults()` → `Settings.resetBackupToDefaults()`；
  `AgentVM.backupDatabaseNow` / `previewDatabaseBackup` / `restoreDatabaseFrom`
  处理 SQLite 快照；导入后 MCP 服务器保持禁用并要求重启确认高风险权限。
  数据库卡片下方的快照列表来自 `AgentVM.databaseBackups()`（最新在前，自动/
  手动徽标 + 时间/大小），预览/恢复复用同一个确认弹窗，删除走
  `AgentVM.deleteDatabaseBackup(path)` 并二次确认——路径校验在后端，只允许
  删除备份目录的直接子文件。

## 集成点

- `main.qml`：实例化 `AgentChatPage` 与 `SettingsCenterDialog`，把
  `settingsRequested` / `modelSettingsRequested` 统一到 `openCategory("model")`，
  并把 `ContextPanel.documentDetailsRequested` 接到 `KnowledgeDocDialog.openFor`。
- 单例：`Settings`、`PreferenceStore`、`PreferenceRegistry`、`SettingsCatalog`、
  `EditBuffer`、`AgentVM`、`McpManager`、`ConnectionTester`、`SettingsBackup`、`Theme`、
  `I18n`、`ChangeBus`。页面之间不直接互相引用，全部经单例与信号解耦。
- CategoryNav 切换时写 `settings.lastCategory`；`jumpToCategory` 先清空搜索再写同一键。
  分组折叠状态另外写在偏好键 `settings.collapsedGroups`（逗号分隔的组 id，
  见 `CategoryNav.persistCollapsedGroups`）。
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
  `SettingsPage.qml`、`SettingsPageHeader.qml`、`AppScrollBar.qml`、
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
- 行级复原与底栏「恢复全部设置」是 2026-10-30（docs/31 S1）新增入口，统一经
  `SettingsCatalog.resetValue()`：偏好走 `EditBuffer.stage`，M1 键走
  `SettingsService.setValueForSetting`；`secret` 条目（加密键、`network/proxyUsername|Password`）
  一律拒绝，与红线 1 对齐。上面的网络页整页复位仍保留原有的清空凭据语义。
  底栏「恢复本页默认值」的分派顺序是：页面 `requestResetDefaults()` → 声明式偏好 →
  `resetCatalogEntries()` 逐条 `resetValue`。**最后一个分支专门服务安全 / 工具 / 用量页**
  （只有 SettingRow 自绘控件、没有页面级复位函数）；新增这类页面时不用补函数，只要键在
  `SettingsCatalog` 里登记为 `page`。
- 设置行/偏好行右侧的「已修改」槽宽固定（`SettingRow` 24px、`PreferenceControl` 20px），
  可见性读 `SettingsCatalog.revision` + `describe(key)`；新增行若需要徽标，写
  `settingKey`（默认取 `objectName`）即可，不要自己另算默认值。
- LogsPage 的「最近日志」是**只读查看器**：只读文件尾部窗口（`LogTailReader`，默认 1 MB），
  级别/行数/关键字都是页面内状态，不登记 schema/catalog 键；需要新过滤维度时先想清楚
  是否值得新增设置键，不要顺手把查看参数持久化。它独立于 `AgentVM`，不走 agent 栈。
- 设置中心的分类 Loader 采用"访问过即保留"（`visitedCategories`，上限 5 页），
  关闭对话框时清空。新增页面必须写成
  `active: dialog.opened && dialog.pageActive("<id>")`（是否保留实例）+
  `visible: dialog.currentCategory === "<id>"`（是否当前页）**两条独立绑定**：
  `visible: active` 会让所有访问过的页面同时 `anchors.fill` 叠在一起（S1 真实回归，
  `test_chat_workflows::settingsCategoryPagesNeverOverlap` 锁定）。每个 Loader 的
  `objectName` 统一是 `settingsPageLoader.<id>`，测试靠它枚举可见页。
- 新增设置页必须同时：加入 `CMakeLists.txt` 的 `QML_FILES`、在 `SettingsCenterDialog`
  增加同名 `Loader`、在 `SettingsSchema::categories()` 登记分类（含 `groupId`）、在
  `src/viewmodels/SettingsCatalog.cpp` 登记页面自绘项（id 与控件 `objectName` 一致，
  `test_settings` 会断言 schema 键全覆盖），并提供 `requestResetDefaults()`（如该页有可复位
  的 SettingsService 字段）。
- InterfacePage 的值读 `EditBuffer.staged` 再回落 `PreferenceStore`，若未来把
  `stage()` 改回纯暂存，页面会表现为“能改但不落盘”；当前依赖 EditBuffer 的写穿语义。
- GeneralPage 的「关于 NexusAI」（docs/31 N10）：版本/Qt/编译器/架构/系统/数据目录
  全部绑定 `AppInfo` 的 CONSTANT 属性，页面不自行计算；「打开数据目录」调
  `AppInfo.openDataDir()`（失败显示危险色提示），「导出诊断包」复用
  `SettingsBackup.exportDiagnostics`（与日志页同一套文案与"新目录不覆盖"语义），
  「开源许可」弹窗按 `AppInfo.licenses` 渲染。测试 `GeneralPageHarness` 直接比对
  页面文本与 `AppInfo`，防止换工具链后界面还报旧值。
- 字体下拉（docs/31 O9）：字体列表动辄上百项，`ui/fontSearchField` 的输入写
  `page.fontFilter`，正文下拉用 `allFontFamilies.filter()`、等宽下拉用
  `FontProbe.fixedPitchFamilies()`（C++ 侧缓存，避免每敲一个字都 `isFixedPitch`
  加载字体文件）。等宽列表会把手选过的非等宽旧值保留在顶部，避免显示空白；
  两列都为空时显示 `ui/fontSearchEmpty`。`FontPageHarness` 用例锁定"模型真的
  换成过滤后的数组"，而不只是 FontProbe 返回正确。
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
  按分类导出只是在这份白名单上再叠加分类过滤，不能借分类参数绕过排除项；
  导入差异（`changes`）也只遍历备份里已通过白名单的键。
- 导入预览的差异行由真实 `SettingsBackupPage` 渲染，回归在
  `test_settings_page_qml::importPreviewRendersPerKeyChanges`
  （`tests/qml/BackupPageHarness.qml`）。Repeater 的 delegate 没有 QObject
  父链（只有 `setParentItem`），`findChildren` 找不到，断言必须沿
  `childItems()` 或 `Repeater.itemAt()`。
- 各页大量使用 `Component.onDestruction` 兜底提交，若页面被非 Loader 方式复用需重新评估。
  新建文本字段优先用 `SettingsField`（`commitDebounce`/`commitOnDestroy`/`lastKnownValue`
  已封装该契约）；手写 `TextField` 时必须自己补 `Component.onDestruction`，且密钥类字段
  只能读 `text`，不能读 `displayText`（掩码）。
- 设置页的滚动、页头、标签列、间距与标题层级统一由 `SettingsPage`/`SettingsPageHeader`/
  `SettingsSection`/`SettingRow`/`SettingsField` 承担；新增页面不要再复制像素级
  `Layout.*Margin`、自绘页头或硬编码字号（视觉底座前的现状）。`test_chat_workflows` 的
  `settingsPagesInstantiateWithoutWarnings` 会逐个 Loader 装载这些页面并断言无 QML 警告。
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
