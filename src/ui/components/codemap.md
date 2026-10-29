# src/ui/components/ — QML 组件层索引

## 职责

- 本目录是 NexusAI 展示层的**可复用控件 + 业务组件**层，位于「壳层（`main.qml`）／页面（`pages/`）／右栏（`panels/`）」与 C++ ViewModel 单例之间。共 55 个 QML/JS 文件（约 650 KB），包含四类东西：① 无状态视觉基元（按钮、图标、对话框外壳、Toast、分隔条）；② 展示型部件（代码块、diff、目录树、表格、芯片）；③ 业务复合组件（消息气泡、过程卡片、审阅面板、Git 面板、任务清单）；④ 对话框族（设置中心、工具确认、审计日志、运行日志、知识库、快捷键，以及 8 个 Git 对话框）。
- **职责边界**：本层只做展示与交互编排。所有状态、持久化、网络、文件系统与 Git 动作都调用 `NexusAI.ViewModels` 单例（`AgentVM`/`GitVM`/`Settings`/`PreferenceStore`/`PreferenceRegistry`/`EditBuffer`/`Theme`/`ThemeManager`/`I18n`/`CommitDrafter`/`CitationParser`/`CodeHighlighter`）。组件内几乎没有业务规则，只有「怎么显示、怎么点」。
- **分层依赖方向**：`main.qml`/`pages/`/`panels/` → 业务组件 → 展示部件 → 基元控件；反向不成立（组件不引用页面，只通过 `parent: Overlay.overlay` 挂到覆盖层，或由宿主注入回调/数据）。

## 组件分组

### 布局与导航

| 组件 | 职责 | 关键依赖 |
| --- | --- | --- |
| `Sidebar.qml`（83 KB，最大文件） | 左栏：Logo/新建、项目树 + 本地对话树（共享 `sessionRow` delegate）、跨会话搜索（200ms 防抖）、归档筛选、导入项目、行内重命名/置顶/删除 + 「…」溢出菜单（归档/导出/结束对话）、底栏（日志/审计/设置/知识库）。**docs/30 起是会话切换的唯一入口**：行点击走 `openConversation`，行内 14px 状态槽表达 busy（旋转）/ 等待确认（静态警告）/ 未读（品牌点）三态 |新对话按钮右缘的模板窄条（`tmplMA` + `SessionTemplateMenu`，与主点击区分离）：按 `Settings.sessionTemplates` 开新会话，应用动作统一走 `AgentVM.startFromSessionTemplate`；`Accessible` 语义挂在窄条上（"从会话模板开始"）。  `AgentVM.projects/sessionList/openConversation/closeConversation/searchMessages/importProject/renameSession/setSessionPinned/setSessionArchived/exportSession`、`Settings.activeProjectId`、`ThemeManager.isDark` |
| `TopBar.qml`（44 KB） | 顶栏：侧栏开关、项目归属徽标（空标签可切换，已有会话转只读）、**当前会话标题**（docs/30：标签栏删除后的唯一常显标题位，弹性 + elide + 悬停全文）、**会话模板入口**（`preset` 图标，无模板时隐藏；`Ctrl+[` 折叠侧栏后仍可达）、状态点 + `stateText`、token 占用条/累计/费用 + 上下文构成浮层、任务清单与审阅入口（各自带角标）、右栏开关。提示词预设不在顶栏：它为 40px 高度塞不下预设名，已移到输入框下方的选项行（`PromptPresetSelector`）。审阅按钮在 `unreviewedFileCount > 0` 时变品牌色并显示待看文件数，关闭审阅对话框即清零 | `AgentVM.state/stateText/stateBusy/usedTokens/maxTokens/tokenUsageRate/sessionTokensUsed/lastCostCents/contextComposition()/sessionTokenBreakdown()/todoList/fileCheckpoints/unreviewedFileCount/acknowledgeFileReview()/toggleSidebar/togglePanel` |
| `SplitBar.qml` | 竖向分隔条：拖拽发 `dragged(delta)`、双击 `resetRequested()`、`dragActive` 让宿主暂停宽度动画 | 纯几何 + `Theme.brandPrimary`；宿主（`main.qml`）负责 clamp 与写回 `AgentVM.sidebarWidth/panelWidth` |
| `SettingsCenterDialog.qml` | 设置中心：搜索框 + 分组分类导航 + 16 个分类页 `Loader` 懒加载（900×680，导航 200px）+ 底栏（恢复默认/关闭）+ 恢复确认对话框；底栏按钮仅当前分类有可复位键或声明了 `requestResetDefaults()` 时出现，按分类调用对应页/领域的复位入口 | `PreferenceRegistry.categoryGroups/categoriesInGroup/categories/preferencesInCategory/categoryName/categoryOrder`、`PreferenceStore`、`EditBuffer`、`settings-search.js`、绝对 qrc URL `qrc:/qt/qml/NexusAI/src/ui/pages/*.qml` |
| `CategoryNav.qml` | 设置分类左侧导航（200 px，`Flickable` 可滚动）：按 `categoryGroups()` 分四组渲染，组标题 + 34px 分类项，当前项左侧 3px 品牌条 | `PreferenceRegistry.categoryGroups()/categoryGroupName()/categoriesInGroup()/categoryName()`、`I18n.currentLocale`（触发语言切换重算） |

### 对话渲染

| 组件 | 职责 | 关键依赖 |
| --- | --- | --- |
| `TranscriptViewport.qml` | 轨迹滚动容器：`Flickable + Column + Repeater`（非虚拟化，行高需精确），自绘滚动条、吸底跟随 `pinnedToBottom`、`scrollToIndex(idx)` 定位 | 只用宿主注入的 `model`/`delegate`/`scrollBarColor`；**刻意不 import NexusAI** |
| `MessageNavigator.qml` | 对话区左侧用户消息导轨：每条 = 一次提问，线长按正文全长三档（<20→6px / <80→10px / 其余 14px），active/hover 时 +4px 并配色过渡；高度跟随记录数、上限视口高 60%，装不下时等比压缩间距（线长不变）；悬停经**唯一共享** ToolTip 显示序号/时间/正文预览；点击透出 trace 行号 | `entries`（= `AgentVM.userMessageIndex`）、`activeIndex`（页面按滚动位置算）、`viewportHeight`；`jumpRequested(row)` |
| `TraceNodeRouter.qml` | 按 `modelData.type` 路由到 5 个组件（`user_message`/`summary`/中间节点/`metrics`/`error`），单 `Loader` 只实例化命中的组件；计算 `detailKey`、顶部留白、高亮 | 页面注入 `detailExpandedFn`/`toggleDetailFn`（折叠态存页面级 `detailState`）、`showProcess` |
| `UserMessageBubble.qml` | 右对齐用户气泡、圆角尾、时间戳、悬停操作条（复制/编辑重发）、内联 `TextArea` 编辑态、附件芯片行 | `AgentVM.stateBusy/traceCount/lastTraceIndexOfType/editAndResend`、`CopyButton`、`AttachmentChip` |
| `AssistantMessage.qml`（23 KB） | 助手回复：头像列 + Markdown 正文 + 流式光标 + 悬停条（复制/从此处分支/重新生成）；非流式时把「散文/代码」拆段，散文再按 `[文档名]` 切出引用芯片 | `CitationParser.parse`（唯一实现，C++）、`AgentVM.knowledgeDocuments/openKnowledgeDocument/branchFromAnswer/regenerate/traceCount` |
| `ProcessCard.qml` | 思考/工具调用/工具结果/context 四类共用卡片：状态徽章、折叠摘要、`git_status` porcelain 结构化徽标、`waiting_confirm` 前置确认按钮 | `AgentVM.approveToolCall`、`CodeBlock`、`AppButton` |
| `MetricsLine.qml` | 任务指标弱化单行（细线 + 灰字） | `modelData.title` |
| `ErrorCard.qml` | 错误卡：正文 + 重试本轮 / 模型设置 / 复制 | `AgentVM.modelConfigured/state/stateBusy/regenerate/openModelSettings` |
| `CodeBlock.qml` | 代码块：语言标签栏（围栏信息串优先，回落自动检测）+ 复制按钮 + 语法高亮正文 | `CodeHighlighter`（C++ `QSyntaxHighlighter`）、`CopyButton` |
| `CopyButton.qml` | 复制按钮 + 1.5s「已复制」反馈 | `AgentVM.copyToClipboard` |
| `AppToast.qml` | 顶部瞬时通知条：语义色边框、自动隐藏、点击关闭、可带一个动作按钮（撤销） | `parent: Overlay.overlay`、`z: 100` |
| `AttachmentChip.qml` | 附件单元：图片缩略图 或 文件芯片（140px 截断+悬停全名），可开删除按钮 | `kind/source/name/removable`；气泡内与输入区预览共用 |
| `ReferenceChipRow.qml` | 输入草稿里 `@token` 的实时校验条（绿=会随消息发送 / 红=原因），横向 Flickable | `AgentVM.inspectFileReferences` 的 `checks` 数组 |

### 输入

| 组件 | 职责 | 关键依赖 |
| --- | --- | --- |
| `InputConsole.qml`（31 KB） | 多行输入：草稿双向同步、附件预览条、`@` 项目文件补全（无焦点 Popup + ListView）、引用状态条、输入历史 ↑/↓、Enter 发送 / Shift+Enter 换行 / Ctrl+V 贴图、发送前经 `AgentVM.expandSlashCommand(text, Settings.slashCommands)` 展开斜杠指令、发送/停止按钮 | `AgentVM.draftText/draftAttachments/restoreDraft/startTask/stop/historyPrev/historyNext/activeFileMention/listProjectFiles/resolveFileReferences/inspectFileReferences/loadImageDataUrl/loadTextFileAttachment/clipboardImageDataUrl` |
| `ModelSelector.qml` | 底部向上弹的模型/配置下拉（三段：当前配置模型列表 → 配置列表 → 管理模型配置…），切换后提示上下文窗口 | `AgentVM.chatModelProfile/selectChatModel/stateBusy`、`Settings.profiles/modelContextWindow` |
| `PermissionModeSelector.qml` | 权限模式下拉（只读/变更确认/自动编辑/完全访问），按风险着色；完全访问需二次确认对话框 | `AgentVM.permissionMode*` 常量、`AppButton`、`DialogBackground` |
| `PromptPresetSelector.qml` | 会话级提示词预设选择器：胶囊回显当前套用的预设名（未套用显示"提示词·默认"，来源预设被删则标黄并注明"预设已删除"），向上弹出主题化列表（分区标题 + 当前状态 + "无预设（使用全局系统提示词）"+ 每条预设的名称与两行提示词摘要 + 空态 + 「管理预设…」）。`manageRequested` 信号经 AgentChatPage→main 打开设置中心 prompts 分类 | `AgentVM.activePromptPresetId/activePromptPresetName/applyPromptPreset/clearPromptPreset`、`Settings.promptPresets/systemPromptOverride`、`DialogBackground` |
| `SessionTemplateMenu.qml` | 会话模板菜单（独立组件，三个宿主共用：侧栏窄条 / 标签栏「+」右键 / 空态）。`Popup` + `DialogBackground` + 主题化行项；每行"名称 + 配置·权限"两行摘要，配置被删时明写「配置已删除」。`openBelow(trigger)`/`openAbove(trigger)` 手工算场景坐标并夹进窗口（Popup 没有 `popup(item,x,y)`/`mapToItem`）。应用动作一律调 `AgentVM.startFromSessionTemplate` | `Settings.sessionTemplates/profiles`、`AgentVM.startFromSessionTemplate/newLocalChat`、`DialogBackground` |
| `ThinkingEffortSelector.qml` | 思考强度下拉（默认/off/low/medium/high），未配置模型时整体禁用 | `AgentVM.chatModelProfile.reasoningEffort/setChatReasoningEffort/modelConfigured` |
| `ToolConfirmDialog.qml` | 高危工具确认：警告侧条 + 工具名 + 变更预览/调用参数 + 允许/拒绝 | `AgentVM.pendingConfirmation/approveToolCall`；`CodeBlock` |
| `ShortcutsDialog.qml` | 快捷键速查表（两列静态表，与 `main.qml` 的 `Shortcut` 一一对应） | 纯静态数据 |

### 变更与 Git（文件最多的一组）

| 组件 | 职责 | 关键依赖 |
| --- | --- | --- |
| `FileTree.js` | 纯路径树工具库（`.pragma library`）：路径规范化、`commonDir`、插入/聚合/扁平化为 folder+file 行 | 无（dependency-free，便于测试直接驱动） |
| `ChangeTree.qml` | 扁平化目录树（`ListView` 虚拟化）：折叠/展开/全展全折、目录计数与增删行统计、Git 状态字母、行内动作按钮 | `FileTree.js`、`Theme`、宿主注入的 `actionFor/fileColorFor/statusColorFor` 回调 |
| `FileChangesPanel.qml` | 审阅面板：`AgentVM.fileCheckpoints` → 目录树；行内「确认」（`performAccept`，标记 accepted 不可再撤销）、详情面板「确认+撤销」双判定（互斥：已确认不可撤销，已撤销不可确认）、「全部确认」/「全部撤销」（弹窗列路径，`undoAllFileCheckpoints` 跳过 accepted/undone/truncated 并如实报告 `{reverted,skipped,failed}`）、展开详情。`splitAxis` 决定树与 diff 是上下堆叠（`"vertical"`，窄栏用）还是左右并排（`"horizontal"`，对话框用）；`showHeader` 关掉时由宿主打印汇总 | `AgentVM.fileCheckpoints/fileCheckpointDetail/undoFileCheckpoint/acceptFileCheckpoint/acceptAllFileCheckpoints/undoAllFileCheckpoints/stateBusy`、`SplitBar`。行内操作按钮是「确认」（`performAccept`），撤销移到详情面板（破坏性操作需先看 diff）；「全部撤销」走 `undoAllDialog`（列出待处理路径 + 标注不可还原项），执行后按 `{reverted,skipped,failed}` 如实报告 |
| `FileChangeDetailDialog.qml` | 单文件改动详情：本会话文件树 + diff，统一/并排切换、上一/下一文件、确认 + 撤销（`acceptOffered`/`undoOffered` 互斥：已确认不可撤销，已撤销不可确认）。根类型是 `Item`（曾是模态 `Dialog`），由 `FileChangesPanel` 内嵌托管；`selectHint` 随 `sideBySide` 改措辞；`onCheckpointsChanged` 同步自身 `checkpoint` 快照，否则判定按钮会滞后一拍 | `DiffView`、`ChangeTree`、`AgentVM.fileCheckpointDetail` |
| `KnowledgeDialog.qml` | 知识库管理对话框（全局，与日志/审计/设置同属侧栏工具条入口）。表格式列表：文档 / 段数 / 字符 / 状态 / 操作列对齐；头部含总量摘要与导入进度；空态给出导入指引。**URL 导入**：「从 URL 导入」按钮展开内联地址行（回车提交 / Esc 收起），走 `AgentVM.importFromUrl`（池线程 `WebContent::fetchUrl`，4MB wire / 400k 字上限）→ 复用 `importExtractedText` 管线（`sourcePathOverride` 传 URL——URL 侧只构造显示名的 `QFileInfo`，其 `absoluteFilePath()` 会解析成 CWD+文件名的**伪造本地路径**，修复前复制源路径得到 build/v2-6 之类不存在的路径）；**文档过滤框**（按名称，空态区分"库为空"与"无匹配"）与**复制源路径**按钮。**Qt 6.9 var 属性坑（本文件实测）**：C++ `setProperty` 写 var 属性发 changed 信号但不失效 QML 依赖绑定（docCount 停留旧值）；Popup 内 ListView 对内联块绑定的 JS 数组返回值静默丢弃——过滤用 `refilter()` 单写者 + `docList.model = out` 直推；测试注入走 QML 侧 `setDocuments()`**source_path 支持**：导入时记录源文件绝对路径（`RagService::ingestDocument` 4 参重载 → `documentPersisted` 信号 → `knowledge_documents.source_path` 列，`upsertKnowledgeDoc` 空值不覆盖旧路径——restore/re-embed 不带路径），有路径的文档显示「打开源文件」按钮（`ProjectFileVM.openAbsolute`，同样清 `ELECTRON_RUN_AS_NODE`）；注意 `RegQueryValueExW`/空 QString 绑定为 SQL NULL 的坑见 SqliteMemory**从 `ContextPanel` 迁出**：知识库是全局数据，而右侧栏其余分区都只在项目会话中存在；且 220–660px 列宽放不下"名称+降级徽标+段数字符+两个操作按钮" | `AgentVM.knowledgeDocuments/knowledgeBusy/batchImport*/importDocuments/removeDocument/reembedDocument/retryFailedBatchImports/cancelBatchImport` |
| `ProjectFilesPanel.qml` | 右侧栏「文件」分区：只读项目文件浏览。一次只读一层（`ProjectFileVM.children`），目录默认折叠、展开时才加载下一层；支持名称过滤（**非空即切到全项目搜索**：`ProjectFileVM.search` → `ProjectFileIndex::searchPaths` 递归整棵树，过滤规则与 `listDir` 完全一致，故"树里看得见的一定搜得到"；命中列表选中后清查询并 `revealInTree` 回到树，搜索答"在哪"，树仍是导航本体）、折叠全部、选中预览（面板内可折叠，默认 320px、可拖 `SplitBar` 调节高度并在 100px/可用空间之间夹紧、双击复位、**不持久化**，`TextArea` 只读 + `CodeHighlighter` 语法高亮（`sourceName: previewName` 按扩展名定语言，未识别扩展名保持纯文本、不猜；`.qml` 近似为 JS/TS；超 64KB 或单行超 8KB 关闭高亮并在 notices 说明）。左侧行号槽：`Column` + `Repeater` 每**逻辑行**一条 Text + 细分隔线。换行模式下条目高度 = 该逻辑行的视觉行数 × 行高，续行留空（与代码编辑器一致）；视觉行数由 `CodeHighlighter::visualLineSpans(wrapWidth)` 给出——取自 `QTextLayout::lineCount()`，**必须显式传视口宽度**（document 自身 textWidth 不跟随编辑器，实测编辑器折了 11 行而 document 仍报每块 1 行）；`QTextLayout` 惰性布局，故该函数内部自建 layout 且需设 `QTextOption::WrapAnywhere`（不设则 301 字符在 400px 下仍只出 1 行）。行高由 `contentHeight / lineCount` 反推（TextEdit 无 `lineHeight`），滚动偏移取 `previewScroll.contentItem.contentY`（TextArea 自身被撑到内容大小、无 contentY）。NoWrap 下条目高度退回一行，行数取 `previewArea.lineCount`（尾随换行的空末行照常编号）；「行号」按钮开关，默认开；换行开关默认关（代码看缩进更清楚），开启时须把编辑器宽度绑定到视口否则永不折行、同时关闭横向滚动条）、双击/按钮用系统程序打开（失败时走 `ProjectFileVM.openWithKnownEditor` 应急路径：从注册表读 `.md` UserChoice 与常见编辑器 ProgId 的 `shell\open\command` 直接拉起，绕开损坏的文件关联——实测案例：某 Electron 编辑器的 Node 运行时把 .md 当 JS 入口执行而崩溃。**根因**是 `ELECTRON_RUN_AS_NODE=1`——从 VSCode 系终端启动本应用会继承该变量，shell/子进程再传给 Electron 处理器，使其退化为纯 Node 并把文档当脚本执行；故正常路径与应急路径都会清掉该变量（`cleanChildEnvironment` / `QProcessEnvironment.remove`））；「复制绝对路径」按钮（`AgentVM.copyToClipboard`）；「在文件树中定位」按钮（`revealInTree`）先展开全部祖先再重建行并 `positionViewAtIndex(Center)`，找不到时在 `errorText` 说明。不显示 git 状态（那是 GitPanel 职责），不做任何写操作 | `ProjectFileVM.children/preview/openInDefaultApp/clearCache`（底层 `ProjectFileIndex::listDir`） |
| `ReviewDialog.qml` | 审阅对话框：把审阅从 220–640px 侧栏搬进大对话框（diff 在侧栏里不可读）。树在左、diff 占满剩余宽高，`splitAxis: "horizontal"`；头部打印文件数与 ±行数，折叠/展开按钮上移至此；`onClosed` 调 `AgentVM.acknowledgeFileReview()` 清零顶栏角标（`reviewAcknowledger` 为测试注入点） | `FileChangesPanel`（`splitAxis/showHeader`）、`AgentVM.fileCheckpoints/activeProjectRoot/acknowledgeFileReview()` |
| `TodoDialog.qml` | 任务清单对话框：待办列表同样因宽度移出侧栏。内容驱动高度（3 项 ≈240px 卡片，25 项封顶 540px 并内部滚动），列表置于内凹 list well 中；`surfaceColor` 让底部渐隐终止色跟随真实底色 | `TodoListPanel`（`surfaceColor/showHeader`）、`AgentVM.todoList` |
| `DiffView.qml` | unified diff 渲染器：自解析 @@ hunk 与行号，unified/split 双模式，增删行着色，超大差异提示；split 模式中缝 `splitXFor()` 为唯一几何来源（内容自适应 `autoSplitRatio` + 可拖动 + 两侧 `minColumnText` 夹紧） | 纯解析，无外部依赖 |
| `GitPanel.qml`（64 KB） | 右栏 Git 页：仓库识别、已暂存/未暂存两棵 `ChangeTree`、提交说明编辑器（Ctrl+Enter 提交）、提交历史（分页 + grep/作者筛选）、对比基准挑选、本地恢复操作（reset/amend/undo/stash/discard），并托管 8 个 Git 对话框实例 | `GitVM.projectRoot/statusEntries/repoInfo/requestXxx()/operationBusy/lastBackupPath`、`AgentVM.fileCheckpointsChanged`（防抖刷新）、`CommitDrafter` |
| `GitWorkingDiffDialog.qml` | 工作区单文件 diff（staged / unstaged / untracked），头部含打开/复制路径/历史/丢弃；支持按 hunk 部分暂存 | `GitVM.requestWorkingDiff/requestDiffHunks/stageHunks/unstageHunks/openInDefaultApp/absolutePath` |
| `GitCommitDialog.qml` | 单提交详情：元信息 + 改动文件树 + 选中文件 diff | `GitVM.requestCommitFiles/requestCommitDiff`、`ChangeTree`、`DiffView` |
| `GitActionDialog.qml` | 一切破坏性/改写历史动作的**统一确认面**（reset/amend/undo/stash/discard），可带必填说明文本 | 由 `GitPanel` 通过 `openWith({title, details, editable, requireText, …})` 配置 |
| `GitFileHistoryDialog.qml` | 单文件提交历史（30 条，跟随重命名） | `GitVM.requestFileHistory` |
| `GitBranchDialog.qml` | 只读分支视图：本地分支 + 上游漂移状态 + 远端 + 标签 | `GitVM.requestBranchView` |
| `GitCompareDialog.qml` | 任意两提交对比（A→B，可交换方向）：差异文件树 + 选中文件 diff | `GitVM.requestCompareFiles/requestCompareDiff` |
| `GitBlameDialog.qml` | 逐行归属（blame），点击行打开该提交详情 | `GitVM.requestBlame` |
| `GitGraphDialog.qml` | 多分支提交图（渲染 git `--graph` 原文，带提交的行可点） | `GitVM.requestGraph` |

### 设置与知识库

| 组件 | 职责 | 关键依赖 |
| --- | --- | --- |
| `SettingsField.qml` | 设置页字段行：128px 定宽标签 + 输入框 + 密文显隐切换；失焦/回车提交，销毁时按 `commitOnDestroy` 补交 | `Theme`、`EditBuffer`（由宿主页面接线） |
| `SettingCard.qml` | 设置分组卡：标题 + 副标题 + 内容插槽（`default property alias content: body.data`） | `Theme` |
| `ParamSelectRow.qml` | 参数下拉行：[自动] + 预设 + [自定义…]，自定义时露出输入框；值↔索引双向映射 | 纯受控组件，`submitted(value)` 出参 |
| `PreferenceControl.qml` | 声明式偏好通用控件：按 `PreferenceRegistry.preferenceMeta()` 的类型自动选 Switch/ComboBox/Slider+SpinBox/TextField，错误来自 `EditBuffer.validationError` | `PreferenceRegistry`、`PreferenceStore`、`EditBuffer` |
| `AuditDialog.qml` | 工具活动审计（SQLite）：按事件类型筛选、5s 自动刷新、剪贴板导出、日志路径提示 | `AgentVM.auditLog(200)/logFilePath/copyToClipboard` |
| `KnowledgeDocDialog.qml` | 知识库文档详情：统计 + 分块预览，支持按 `targetChunk` 高亮定位（引用溯源入口） | `AgentVM.documentChunks/knowledgeDocuments`、`CopyButton` |
| `LogPanel.qml` | 运行日志面板：类别/级别/日期/关键词四重筛选、吸底跟随、复制可见日志、打开日志目录 | `AgentVM.logModel`（filterCategory/filterLevel/filterDate/filterText/availableDates/toClipboardText/clearView/polling） |
| `TodoListPanel.qml` | 任务清单（`todo_write` 工具产出）只读视图：优先级色条、状态图标、完成计数、可折叠 | `AgentVM.todoList` |
| `UsageDetailTable.qml`（25 KB） | 用量统计「请求明细」表：筛选/排序/拖拽调列宽/横向滚动/加载更多 | `rows`/`total` 由调用方注入，组件只发 `loadMore()` |
| `settings-search.js` | 设置搜索的手工索引与匹配/去重/排序逻辑（`.pragma library`，无 Qt 依赖） | 由 `SettingsCenterDialog` 与 `PreferenceRegistry` 条目合并使用 |

### 通用控件

| 组件 | 职责 | 关键依赖 |
| --- | --- | --- |
| `AppButton.qml` | 主按钮（32 高、圆角 6、按压缩放、focus 环、可高亮） | `Theme`；`signal clicked()` |
| `IconButton.qml` | 32×32 图标按钮：悬停着色 + Tooltip 600ms；`flipOnIconChange` 开启后 icon 变化时做 Y 轴翻转（压扁→中点换图→展开，共 180ms），默认关闭。**Item 没有 scaleX**：横向压扁必须用 `transform: Scale`，用等比 `scale` 会把图标缩成一个点而不是压扁。 | `AppIcon`；`Theme` |
| `HWheelScroll.qml` | Shift+滚轮横滚：**MouseArea 覆盖层**（`acceptedButtons: NoButton`，`anchors.fill: parent`），`onWheel` 里 Shift 时对解析出的 `flickTarget.contentX` 写入**自夹取后的值**（0 ≤ cX ≤ contentWidth−width）并接受事件，否则 `accepted=false` 放行给 Flickable 纵向滚动。**为什么不是 WheelHandler**（探针实测 Qt 6.9）：`orientation: Horizontal` 只响应自带横向增量的设备，鼠标 Shift+滚轮是纵向增量永远不匹配；`property: "contentX"` 重定向只累计 rotation 从不写属性。Qt Quick 原生不把 Shift+滚轮转横向（`QQuickFlickable::wheelEvent` 仅按 angleDelta 轴向分发）。**三个踩坑实录（全部探针/日志实证）**：① 声明在 ScrollView 里的 Item 的 visual parent 仍是 ScrollView（Pane，无 contentX），真正的 Flickable 是 `parent.contentItem`；② Flickable 子项的 QML `parent` 是 contentItem（普通 Item，无 contentX），QObject::parent 才是 Flickable——所以必须沿视觉父链爬找，鸭子判定 contentX；③ **程序化写 contentX 不被夹取**——到端点后继续写会推出负值/越界，立即触发原生橡皮筋回弹动画，回弹每帧覆写后续平移，表现为「有弹性、必须点击才复原」，且写完 `accepted=true` 后事件不会链接到外层竖滚（到头即停）。平移前还须 `cancelFlick()` 打断进行中的拖拽/回弹动画（否则动画同样逐帧覆写 contentX）。**覆盖层必须放在 Flickable/ScrollView 的直接子级、内容声明之后（最顶层），且不能放进 Row/Column 定位器**（定位器禁止 fill 锚点，覆盖层尺寸归零无法命中——实测踩坑）。NoButton 不拦按压/拖拽/文本选择。已挂 3 处：ProjectRulesPage 规则编辑器、UsageDetailTable 外层横滚 Flickable、ProjectFilesPanel 预览（仅 NoWrap 模式 `enabled`）；纵向纯列表不加。滚轮手势无法自动化测试（合成事件不驱场景投递），行为靠真机验收 | `MouseArea` |
| `AppIcon.qml` | Canvas 自绘矢量图标库（24×24 网格，约 30 个图标：`menu/close/add/chevron-*/warning/copy/folder/file/send/stop/search/export/layers/pin/archive/undo/plus/minus/branch/history/blame/sidebar-collapse/sidebar-expand/panel-collapse/panel-expand/process-collapse/process-expand`…） | `Theme.textPrimary` 等令牌；刻意用 Canvas（Shapes 在软件渲染后端不渲染） |
| `DialogBackground.qml` | 对话框统一外壳（`bgSecondary` + 细边 + 8 圆角），作为 `background:` 使用 | `Theme` |
| `charts/{DonutChart,LineTrendChart,BarCompareChart}.qml` | Canvas 自绘图表（Token 占比环图 / 趋势线 / 对比柱），**禁止引入 Qt Charts** | 数据由调用方注入；色板循环用 `Theme` 语义色 |

## 设计

- **主题来源唯一**：`Theme`（C++ `ThemeProvider` 注册为 QML 单例）提供颜色、间距（`spaceXs…spaceXl`）、圆角、字号、字体、`maxContentWidth`、`avatarSize`、`bubbleMaxRatio` 与 `formatTime/formatDateTime/formatLogTime` 等格式化函数；明暗切换由 `ThemeManager.isDark` 驱动 `themeChanged` 重算。组件内**几乎不硬编码色值**——残留例外见「约束与风险」。
- **复用层次**（自底向上）：`AppIcon/AppButton/IconButton/CopyButton/DialogBackground/SplitBar/AppToast` → `CodeBlock/AttachmentChip/ReferenceChipRow/SettingCard/SettingsField/ParamSelectRow/PreferenceControl/DiffView/ChangeTree/UsageDetailTable` → `ProcessCard/AssistantMessage/UserMessageBubble/FileChangesPanel/GitPanel/TodoListPanel/LogPanel` → 各类 `Dialog`。基元几乎都是「Rectangle + MouseArea + 文本」自绘，不依赖 `QtQuick.Controls` 的默认皮肤——凡引了 `QtQuick.Controls` 的（`IconButton`/`AppToast`/`CategoryNav`/`InputConsole`/`PermissionModeSelector` 等）也都自绘 `background`/`contentItem`/`indicator`，只取 `ToolTip`/`Popup`/`Menu` 的行为。
- **四种 ViewModel 绑定模式**：
  1. *读属性直绑*：`AgentVM.traceModel` → `TranscriptViewport.model`；`AgentVM.fileCheckpoints` → `FileChangesPanel.checkpoints`；`AgentVM.todoList` → `TodoListPanel.todos`。
  2. *写属性直赋*：`AgentVM.permissionMode = mode`、`AgentVM.draftText = text`。
  3. *动作调用*：`AgentVM.startTask/approveToolCall/undoFileCheckpoint/regenerate/copyToClipboard`。
  4. *异步请求 + token 比对*：Git 系列一律 `token = GitVM.requestXxx(...)`，回填 signal 里先比对 `requestId !== token` 则丢弃，防止过期响应覆盖新数据。`GitPanel` 另维护 `logToken/stashToken/branchToken/draftDiffToken` 区分用途。
- **注入式依赖（为可测）**：`FileChangesPanel.detailProvider`、`FileChangeDetailDialog.detailFor`、`ChangeTree.actionFor/fileColorFor/statusColorFor`、`TraceNodeRouter.detailExpandedFn/toggleDetailFn` 均由宿主注入，测试可替换成桩。
- **可访问性约定**（docs/25 D1/D2 一致落地）：所有可点元素 `activeFocusOnTab: true` + `Keys.onReturnPressed/Keys.onSpacePressed` + `Accessible.role/name/description/onPressAction`；**悬停操作条的 visible 条件必须包含 `activeFocus`**，否则聚焦后鼠标移开按钮消失导致焦点漂移；根 Item 上的 `Accessible.name` 不会传给真正获得焦点的子控件，所以内部控件各自补名（`PreferenceControl` 注释明确）；角色语义化：`Button/RadioButton/ComboBox/MenuItem/ListItem/PageTab/StaticText/Alert/Notification/Separator/Grouping`。
- **大列表与性能策略**（按场景选型，不是随意选择）：
  - `ListView` 虚拟化：`ChangeTree`、`DiffView`、`UsageDetailTable`、`AuditDialog`、`KnowledgeDocDialog`、`LogPanel`、Git 的 blame/graph 列表。
  - `Flickable + Repeater`（非虚拟化）：`GitBranchDialog`、`GitFileHistoryDialog`、`TodoListPanel` —— 注释写明原因：**Layout 内的 ListView 会塌成 0 高度而永远不创建 delegate**。
  - `Repeater + Column`（非虚拟化）：`TranscriptViewport` —— 行高必须精确，滚动条不能有估算误差。
  - **按需实例化**：`TraceNodeRouter` 用单 `Loader` 只建命中的组件；`ProcessCard` 的正文区 `Loader.active = bodyVisible`（折叠态不为不可见文本付费）；`AssistantMessage` 仅在非流式且含围栏/引用时才走拆分路径。
  - **算法层面**：`Sidebar.sessionGroups` 一次 O(会话数) 遍历产出分组，避免每个项目节点各 filter 一遍（O(项目×会话)）；`UsageDetailTable` 用 ListView 而非 Repeater，避免「加载更多」时全量重建 delegate。
- **模态与焦点管理**：统一 `background: DialogBackground {}`；`parent: Overlay.overlay`；尺寸 `Math.min(W, parent.width - 24)` / `Math.min(H, parent.height - 80)` 自适应；`closePolicy` 统一 `CloseOnEscape | CloseOnPressOutside`（破坏性确认只留 `CloseOnEscape`）；弹出式选择器一律 `focus: true` + 菜单项 `activeFocusOnTab` + `nextItemInFocusChain()` 做 ↑↓ 导航；`@` 补全 Popup 刻意 `focus: false`（键盘留在输入框）。

## 数据与控制流

1. **轨迹渲染**：`AgentVM.traceModel` → `AgentChatPage` 把 `model` 与 `delegate` 注入 `TranscriptViewport` → delegate 为 `TraceNodeRouter` → 按 `modelData.type` 路由到 `UserMessageBubble`/`AssistantMessage`/`ProcessCard`/`MetricsLine`/`ErrorCard`。折叠态不存 delegate 内，而是页面级 `detailState` 经回调读写，切会话不丢。搜索命中定位走 `AgentVM.traceModel.rowOfMessageId` → `transcript.scrollToIndex(row)` → 2 秒高亮复位。左侧导轨是第二条定位入口：`TraceModel::userMessageIndex()` 产出 `{row,messageId,preview,length,timestamp}`，页面用 `transcript.activeIndexIn(rows)` 按像素位置算出"当前在第几条"，点击时 `scrollToIndex(row)` + 弱高亮（`highlightStrength: 0.06`，搜索命中是 `0.10`）。
2. **输入与发送**：`InputConsole` 维护草稿（写 `AgentVM.draftText/draftAttachments`，并监听其变更回填）→ `sendCurrentText()` 合并附件 + `@` 引用解析 → `AgentVM.startTask(text, images, files)` → 轨迹模型更新 → `TranscriptViewport` 吸底跟随（发送时显式 `requestFollow()` 解钉）。
3. **审阅（Agent 写入的文件）**：`AgentVM.fileCheckpoints` → `FileChangesPanel` → `ChangeTree`（`FileTree.js` 建树）→ 行点击 → `FileChangeDetailDialog` → `DiffView`；撤销走 `AgentVM.undoFileCheckpoint(id)`，超限文件（`truncated`）只记路径、不可撤销/不可 diff。
4. **Git**：`ContextPanel` 把 `AgentVM.activeProjectRoot` 传给 `GitPanel`，后者写 `GitVM.projectRoot` 固定仓库位置；面板自身只做「读状态 + 组装命令 + 发请求」，写操作一律经用户显式动作 → `GitActionDialog` 二次确认 → `GitVM` 执行 → `onOperationFinished` 回写提示并刷新日志/分支。`AgentVM.fileCheckpointsChanged` 触发防抖刷新（Agent 写文件后自动更新仓库视图）。
5. **设置**：`SettingsCenterDialog` → `CategoryNav`（`PreferenceRegistry.categoryGroups()` → `categoriesInGroup()`）→ `Loader` 按分类懒加载 `pages/*.qml`（usage/activity 两页绑定 `dialog.opened`，关闭即销毁以免统计停在旧值）→ 声明式偏好走 `PreferenceControl` + `EditBuffer.stage`；搜索合并 `PreferenceRegistry` 元数据与 `settings-search.js` 手工索引。关闭时 `EditBuffer.dirty` → `rollback()`。底栏「恢复默认」按当前分类分派：声明式偏好走 `EditBuffer.resetToDefault(keys)`，页面自绘字段调用该页的 `requestResetDefaults()`。
6. **通知**：`AgentVM.errorOccurred/noticeOccurred` → `main.qml` 的两个 `AppToast`（error/notice 纵向错开）；`Sidebar.sessionDeletedUndoable` 信号 → 带「撤销」动作的第三个 Toast → `AgentVM.undoDeleteSession()`。

## 集成点

- **宿主**：`main.qml`（Sidebar、两个 SplitBar、SettingsCenterDialog、AuditDialog、LogPanel、KnowledgeDocDialog、3 个 AppToast、ShortcutsDialog、窗口级 DropArea、窗口级 Shortcut）；`pages/AgentChatPage.qml`（TopBar、TranscriptViewport、InputConsole、ModelSelector、ThinkingEffortSelector、PermissionModeSelector、PromptPresetSelector、SessionTemplateMenu）；`panels/ContextPanel.qml`（TodoListPanel、FileChangesPanel、GitPanel）；`pages/*.qml`（SettingCard、SettingsField、ParamSelectRow、PreferenceControl、UsageDetailTable、charts/*）。
- **C++ 单例**：`AgentVM`、`GitVM`、`Settings`、`PreferenceStore`、`PreferenceRegistry`、`EditBuffer`、`Theme`/`ThemeManager`、`I18n`、`CommitDrafter`、`CitationParser`、`CodeHighlighter`。
- **模块注册**：根 `CMakeLists.txt` 的 `qt_add_qml_module(nexus_ai … QML_FILES)` 逐个列出本目录文件；`charts/` 因 `QTP0004 NEW` 平铺注册进 `NexusAI` 模块（移动目录会改 qrc 路径）。设置页通过绝对 qrc URL 加载，不依赖相对路径。
- **测试锚点**：`tests/CMakeLists.txt` 把 `src/ui/**/*.qml` 与两个 JS 重新打进测试资源；组件广泛使用 `objectName` 作为自动化锚点（`inputConsole`、`messageInput`、`mentionPopup`、`mentionRow`、`changeTree`、`fileChangeRow`、`fileRowAction`、`diffView`、`diffSplitRow`、`detailDiffView`、`commitFileTree`、`gitBlameRow`、`gitGraphRow`、`gitActionMessage`、`gitCommitMessage`、`fileChangeCountBadge` 等）。

## 下拉/弹出触发器的开合约定

所有「点一次开、再点一次关」的触发器必须把判断放在 **`onPressed`**，并且弹层
用 `closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside`。

原因：若用 `onClicked`/`onReleased` + `CloseOnPressOutside`，弹层会在鼠标按下/
释放的那一刻先被外部点击策略关掉，处理器读到的 `opened` 恒为 `false`，toggle
于是又打开一次——用户看到的是"点第二次还是开着，只能点别处关"。

已按此约定修正：`ModelSelector`、`ThinkingEffortSelector`、
`PermissionModeSelector`、`PromptPresetSelector`、`TopBar` 项目徽标、
`SessionTemplateMenu.toggleBelow()`（三个宿主）、`Sidebar` 会话行「…」。
新增下拉时照抄，回归用例见 `test_chat_workflows::dropdownTriggersToggleClosedOnSecondClick`、
`test_prompt_preset_qml::secondClickOnTriggerClosesPopup`、
`test_session_template_qml::secondClickOnTriggerClosesMenu`。

原生 `ComboBox`（设置页 `ParamSelectRow` / `PreferenceControl` / `LogPanel`）
自带正确的开合行为，无需处理。

## 约束与风险

- **代码级硬约束**：`AssistantMessage.qml` **禁止出现正则字面量与裸反引号**（历史上导致 qmlcachegen 内存失控），围栏标记用 `String.fromCharCode(96)` 动态拼接；`DiffView.qml` 的 hunk 正则必须用 `new RegExp(...)` 构造（qmllint 6.9.0 在「正则字面量 + 循环」的函数上会无限增长）。改动这两个文件前先读文件头注释。
- **拖放只能在窗口根级**：`drop.urls` 的元素是 QML URL 包装对象，**没有 `toLocalFile()`**，必须原样交给 C++；组件内部再放 `DropArea` 收不到事件（分派只给首个接受者），因此 `InputConsole` 内不放 DropArea，改由 `main.qml` 根级解析后经 `AgentVM.attachmentsDropped` 推入 `appendAttachments()`。
- **`TranscriptViewport` 刻意不 import NexusAI**（tests/qml 以相对路径直接加载，保持 dependency-free），因此它拿不到 `Theme` 令牌，滚动条颜色靠宿主注入 `scrollBarColor`。
- **Layout 管理的 item 上写 `anchors` 是未定义行为**（每次启动告警 "Detected anchors on an item that is managed by a layout"），`InputConsole` 的 `AttachMenuItem` 已改用 `Layout.alignment`。
- **覆盖式 MouseArea 的声明顺序即层级**：必须先声明（在下层），否则吞掉子按钮点击。`Sidebar` 项目头、`FileChangesPanel` 头部都踩过这个坑（注释保留为回归提示）。
- **Popup Menu 在 `Repeater` delegate 内会被焦点抢占秒关**，`Sidebar` 的置顶/归档因此改为直连 `Rectangle + MouseArea` 按钮；不要图省事改回 Menu。
- **`SettingsField`/`ParamSelectRow` 的 `commitOnDestroy`**：设置页挂在 `Loader` 上，切分类直接销毁且不触发 `editingFinished`，编辑中的值会静默丢失；但无条件自动提交又会把半截模型名灌进模型列表，故提交策略由使用方显式开启。密码字段读 `text` 而非 `displayText`（后者是掩码）。
- **Git 请求必须做 token 比对**：所有 `GitVM.requestXxx()` 的回填 signal 都先比对 `requestId`，否则旧响应会覆盖新数据；`GitVM.projectRoot` 是全局单例状态，由 `GitPanel` 写入，跨面板使用需注意当前仓库。
- **主题不是全 CONSTANT**：颜色类令牌带 `themeChanged` 通知，间距/圆角/字号是 CONSTANT；明暗切换时不能缓存色值（如 `AppIcon` 需 `requestPaint()`）。
- **硬编码色值残留**（本应统一走 Theme）：`AuditDialog` 的徽章底色（`Qt.rgba(0,1,0.58,…)` 等三处）、`DiffView` 的 diff 底色 alpha（`0.18`/`0.10` 两档字面量按 `Theme.isDark` 分支选）、`AttachmentChip` 删除按钮的 `#ffffff`。
- **共享空数组/稳定引用**：`Sidebar.noSessions`、`sessionGroups` 回落空数组必须引用稳定，否则 `Repeater` 会因「新数组身份」反复重建 delegate。
- **抽样范围**：本图基于全量精读 40 个文件与分段精读 `Sidebar.qml`（74 KB）、`GitPanel.qml`（64 KB）、`UsageDetailTable.qml`、`AssistantMessage.qml`、`InputConsole.qml`、`TopBar.qml`；`charts/` 仅精读 `DonutChart.qml` 前 70 行（`LineTrendChart`/`BarCompareChart` 结构未逐行确认）；`Sidebar` 中段会话行/右键菜单/搜索结果段与 `GitPanel` 中段恢复操作/历史分页段为抽样阅读。
