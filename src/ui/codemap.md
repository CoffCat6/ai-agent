# src/ui/ — QML 展示层索引

## 职责

- `main.qml` 是唯一窗口根（`ApplicationWindow`），负责整体壳层：左 Sidebar、中 AgentChatPage、右 ContextPanel、全局对话框/Toast、快捷键、窗口几何与关闭行为、窗口级文件拖放。
- `components/` 放可复用控件与业务组件，`pages/` 放设置中心分类页，`panels/` 放右侧上下文面板，`components/charts/` 是 Canvas 自绘图表。
- 只做展示与交互编排；所有状态/持久化/网络/文件系统动作都调用 `NexusAI.ViewModels` 单例。

## 设计

- **三层结构**：窗口壳（`main.qml`）→ 页面/面板（`pages/`、`panels/`）→ 组件（`components/`）。`AgentChatPage` 内部再分 `TopBar`、`TranscriptViewport`、`InputConsole` + 模型选项行（`SessionTabs` 已随 docs/30 删除）。
- **单例入口**：`Theme`（只读设计令牌）、`ThemeManager`（明暗切换）、`AgentVM`（打开集合/轨迹/输入/状态）、`Settings`、`PreferenceStore`/`ChangeBus`/`PreferenceRegistry`、`I18n`、`EditBuffer`、`GitVM`、`McpManager`。
- **响应式壳层**：`RowLayout` 中两个 `SplitBar` 可拖拽；侧栏 180–560px（默认 260），右面板 220–640px（默认 300），聊天区最小 320px；折叠动画 220ms，拖拽时 `enabled: !dragActive` 保持跟手。折叠后宽度为 0 但 `visible` 延续到动画结束，两侧悬浮拉手（24×56、`z:20`）始终可召回。
- **对话框装载**：`SettingsCenterDialog`、`OnboardingDialog`、`AuditDialog`、`LogPanel`、`KnowledgeDocDialog`、`ShortcutsDialog` 全部 `parent: Overlay.overlay`；设置页由 `Loader` 按分类懒加载（`qrc:/qt/qml/NexusAI/src/ui/pages/*.qml`）。
- **主题入口**：QML 侧只读 `Theme.*` 令牌；`Ctrl+D` 切 `ThemeManager.isDark`；C++ 另把同一组颜色同步进 `QPalette` 供 Basic 控件使用。
- **图表约束**：`DonutChart`/`LineTrendChart`/`BarCompareChart` 用 QML `Canvas` 自绘，禁止引入 Qt Charts；`charts/` 子目录经 QTP0004 NEW 平铺注册进 `NexusAI` 模块。
- **i18n**：界面文本用 `qsTr`；语言切换由 C++ `I18nManager::localeChanged → engine.retranslate` 强制重求值。

## 数据与控制流

1. 启动：`Component.onCompleted` 读 `AgentVM.restoreWindowOnStartup` 与 `loadWindowGeometry()`，恢复位置/尺寸或最大化；`trayAvailable` 由 `main.cpp` 注入。随后 `Qt.callLater` 检查 `Onboarding.shouldShow()`（无模型配置且 `general.onboarded` 未写）→ `OnboardingDialog.openOnboarding()`，三步走完或显式跳过才写标记。
2. 会话渲染：`AgentChatPage` 把 `AgentVM.traceModel` 交给 `TranscriptViewport`，delegate 为 `TraceNodeRouter`（按节点类型路由到用户/助手/过程/错误卡片）；展开态存在页面级 `detailState`，不随 delegate 重建丢失。左侧 `MessageNavigator` 导轨按 `AgentVM.userMessageIndex` 画出每次提问，点击跳到对应 trace 行。
3. 过程节点开关：`AgentVM.showProcess`（持久化于 `ui/showProcess`）由 TopBar 右侧的按钮控制，`AgentChatPage` 只读并透传给 `TraceNodeRouter`。它原先是一个悬浮在对话区右上角的胶囊按钮，会遮挡正文且与"某条消息的展开"混淆——搬到 TopBar 后与任务清单/审阅/面板同排。
3. 输入：`InputConsole` 负责草稿、附件、`@` 提及补全、历史浏览与发送；`ModelSelector`/`ThinkingEffortSelector`/`PermissionModeSelector` 读写 `AgentVM`/`Settings`。
3b. 会话切换（docs/30，P0+P1 已实施）：**侧栏是唯一入口，主区只渲染活动会话**。侧栏点击 → `AgentVM.openConversation(id)`；右侧「结束对话」（右键菜单 / hover 操作条，后者仅在 `open === true` 时出现）→ `closeConversation(id)`；`Ctrl+W` → `closeActiveConversation()`；`Ctrl+Tab`/`Ctrl+Shift+Tab` → `cycleConversation(±1)`（跳过空白对话）。行内 14px 状态槽三态互斥：busy 旋转图标 › 等待确认静态警告 › 未读点。顶栏中部显示当前会话标题，并带会话模板入口（`Ctrl+[` 折叠侧栏后仍可达）。
4. 拖放：窗口根级 `DropArea`（最后声明、铺满全窗）只判断 `hasUrls` 并把原始 `drop.urls` 交给 `AgentVM.handleDroppedFiles()`；VM 发 `attachmentsDropped`，`AgentChatPage` 转给 `inputConsole.appendAttachments()`。
5. 设置：`Sidebar.settingsRequested`/Ctrl+,/托盘激活走普通 `open()`（`onOpened` 恢复上次分类 `settings.lastCategory`）；`AgentVM.modelSettingsRequested`/AgentChatPage「配置模型」/`settingsCategoryRequested` 定向 `openCategory(id)`；关闭时 `EditBuffer.dirty` 则回滚；搜索与「恢复默认」可见性由 C++ 单例 `SettingsCatalog` 提供（字段级结果，点选后跳转分类并滚动高亮目标 `objectName`）。
6. 面板：`ContextPanel` 只剩"Git / 知识库"两态 `StackLayout`（`activeSection` 0/1，Git 仅在 `activeProjectRoot` 非空时出现，隐藏时回落到知识库页）。审阅与任务清单已移出侧栏——两者都需要侧栏给不了的宽度，分别改为 `ReviewDialog` / `TodoDialog`，由 `TopBar` 按钮与 `Ctrl+R` / `Ctrl+T` 打开；知识库 `FileDialog` 多选后 `AgentVM.importDocuments()`。后台子代理（docs/35 P4.3）同样是顶栏入口 + 独立 `BackgroundTasksDialog`（仅在有排队/运行中任务时出现，角标=进行中数），面板读 `AgentVM.backgroundSubAgents`，行内 1s 本地时钟推进耗时、按需取报告，支持单任务/全部停止与清除已结束。
7. 通知：`errorOccurred`/`noticeOccurred` 分别驱动错误/成功 Toast；删除会话经 `Sidebar.sessionDeletedUndoable` 弹 10 秒撤销条，调用 `AgentVM.undoDeleteSession()`。
7b. 会话切换（docs/30，P0+P1 已实施）：**侧栏是唯一入口，主区只渲染活动会话**。侧栏点击 → `AgentVM.openConversation(id)`；右侧「结束对话」（右键菜单 / hover 操作条，后者仅在 `open === true` 时出现）→ `closeConversation(id)`；`Ctrl+W` → `closeActiveConversation()`；`Ctrl+Tab`/`Ctrl+Shift+Tab` → `cycleConversation(±1)`（跳过空白对话）。行内 14px 状态槽三态互斥：busy 旋转图标 › 等待确认静态警告 › 未读点。顶栏中部显示当前会话标题，并带会话模板入口（`Ctrl+[` 折叠侧栏后仍可达）。
8. 引用定位：`ChangeBus` 的 `search.revealMessage` 在 `AgentChatPage` 中 `Qt.callLater` 查 `traceModel.rowOfMessageId`，滚动并高亮 2 秒。
9. 快捷键（窗口级）：序列唯一来源是 `ShortcutMap.bindings`（docs/31 N2，动作表/默认值/冲突校验都在 `ShortcutMap.cpp`）；设置页录制时 `ShortcutMap.recording` 让全部窗口级 `Shortcut` 暂时失效，速查表由同一张动作表生成，不再手抄两份。
10. 关闭：`onClosing` 先 `AgentVM.saveWindowGeometry(...)`；当 `PreferenceStore.load("window.closeToTray") === true && trayAvailable` 时 `close.accepted = false` 并 `hide()`，否则真正退出（真正退出入口仍是托盘菜单）。

## 集成点

- C++ 单例与属性：`AgentVM`（traceModel/sessionList/projects/sidebarWidth/panelWidth/pendingConfirmation/todoList/fileCheckpoints…）、`Theme`/`ThemeManager`、`Settings`、`PreferenceStore`、`SettingsCatalog`、`ChangeBus`、`I18n`、`EditBuffer`、`GitVM`、`McpManager`、`ConnectionTester`、`ShortcutMap`、`Onboarding`。
- qrc 路径固定为 `qrc:/qt/qml/NexusAI/src/ui/main.qml`（URI `NexusAI` + 源目录结构）；设置页在 QML 内以绝对 qrc URL 加载。
- 测试：`tests/CMakeLists.txt` 把 `src/ui/**/*.qml` 与 `FileTree.js` 重新打进测试资源，`qmllint_gate` 对 `nexus_ai`/`nexus_viewmodels` 的响应文件做静态检查；图表/Todo/审阅/引用行有 offscreen 渲染回归；设置搜索/目录覆盖由 `test_settings` 的 C++ 用例锁定。

## 关键文件

| 分类 | 文件 | 说明 |
| --- | --- | --- |
| 壳层 | `main.qml` | 窗口、三分栏、对话框/Toast、快捷键（含 Ctrl+W / Ctrl+Tab）、DropArea、几何/关闭。侧栏/面板的开关**只**在 TopBar（`sidebar-collapse`/`sidebar-expand` 状态图标），main.qml 不再有浮动的中间把手 |
| 聊天页 | `pages/AgentChatPage.qml` | TopBar + 轨迹（折叠态按会话分桶）+ 输入区与模型选项 |
| 轨迹 | `components/TranscriptViewport.qml`、`TraceNodeRouter.qml` | 滚动/跟随/定位与节点类型路由 |
| 输入 | `components/InputConsole.qml`、`ModelSelector.qml`、`ThinkingEffortSelector.qml`、`PermissionModeSelector.qml` | 发送、附件、提及、历史与参数选择 |
| 侧栏/右栏 | `components/Sidebar.qml`、`panels/ContextPanel.qml` | 会话树/搜索 + 会话运行状态槽（busy/等待确认/未读）；Git/文件（审阅与任务清单已移入对话框） |
| 设置 | `components/SettingsCenterDialog.qml`、`CategoryNav.qml`、`PreferenceControl.qml`、`SettingsPage.qml`、`SettingsPageHeader.qml`、`AppScrollBar.qml`、`SettingsSection.qml`、`SettingRow.qml`、`SettingsField.qml`、`SettingCard.qml` | 搜索（`SettingsCatalog` 字段级结果 + 页内滚动高亮）、四组可滚动分类导航（图标 + 可折叠分组，「数据与诊断」默认收起、当前分类所在组自动展开；对话框宽度 <900 时导航收成 56px 图标条）、偏好控件、按分类复位的底栏。视觉底座：`SettingsPage`（整页滚动 + 页头 + 边距 + 主题滚动条）/`SettingsPageHeader`（统一标题层级），`SettingsSection`（扁平分组）/`SettingRow`（左标签右控件）/`SettingsField`（输入行），`SettingCard` 留给带边框的独立卡片；字号/间距/圆角/输入框表面统一走 `Theme` 令牌 |
| 设置页 | `pages/GeneralPage`、`AgentBehaviorPage`、`NetworkPage`、`LogsPage`、`KnowledgePage`、`InterfacePage`、`ModelConfigPage`、`SecurityPage`、`McpPage`、`ToolsPage`、`MemoryPage`、`UsageStatsPage`、`ActivityLogPage`、`SettingsBackupPage`、`ProjectRulesPage`、`PromptAssetsPage` | 16 个分类，由 `Loader` 按分类懒加载 |
| 业务组件 | `AssistantMessage.qml`、`UserMessageBubble.qml`、`ProcessCard.qml`、`ErrorCard.qml`、`ToolConfirmDialog.qml`、`TodoListPanel.qml`、`TodoDialog.qml`、`BackgroundTasksDialog.qml`、`FileChangesPanel.qml`、`ChangeTree.qml`、`GitPanel.qml` | 消息/工具确认/审阅/Git；ProcessCard 含子代理过程日志（subagentLog 展开）与底部「收起」入口；BackgroundTasksDialog 是后台子代理管理面 |

ProcessCard 的 `subagentLog` 判型注意（2026-11-02 修复，回归 `test_process_card_qml`）：嵌在 modelData（QVariantMap）里的 QVariantList 经 ListView role 进 QML 后是 sequence wrapper——length/索引正常但 `Array.isArray()` 返回 false，判型必须用 undefined/null + length；且 role 数据必须走 ListView 路径测（setProperty 注入会转成原生 JS Array，复现不了）。docs/34 P3.2 的 `subagentProposals`（spawn_agent 结果节点上的 `[{path, rationale, count}]`）走同一判型与同一 role 路径，卡片头显示「N 个提案」徽章（总数 = 各 count 之和），展开区 `subagentProposalsBody` 列出路径/理由/处数并把报告正文（含 diff）附在下方。
| 图表/表格 | `charts/{DonutChart,LineTrendChart,BarCompareChart}.qml`、`UsageDetailTable.qml`、`DiffView.qml`、`CodeBlock.qml` | Canvas 图表、明细表、diff/代码块 |
| 基础控件 | `AppButton.qml`、`AppIcon.qml`、`AppToast.qml`、`IconButton.qml`、`DialogBackground.qml`、`SplitBar.qml` | 统一外观与交互基元 |

## 约束与风险

- **不要在 QML 解析拖放路径**：`drop.urls` 元素没有 `toLocalFile()`，必须原样交给 C++；`DropArea` 必须最后声明且铺满窗口，否则命中层被内容遮挡。
- **Loader 生命周期**：设置页切换分类会销毁页面，仅靠 `editingFinished` 会丢输入——依赖 `EditBuffer` 防抖 + `Component.onDestruction`；新增页面需沿用同一保存契约。文本字段优先用 `SettingsField` 的 `commitDebounce`/`commitOnDestroy`（并绑定 `lastKnownValue` 避免重复提交原值）；密钥字段用 `secret + ignoreEmptySubmit + clearOnSubmit`，读 `text` 而非 `displayText`。
- **托盘不可用**：`trayAvailable == false` 时绝不能"关闭到托盘"，否则窗口无召回入口。
- **快捷键冲突**：`Ctrl+/` 与部分输入法的 `/` 组合、`Ctrl+[` 与编辑器缩进习惯可能冲突；窗口级 `Shortcut` 会抢占输入焦点，改动需回归 `InputConsole`。序列的唯一事实源是 `ShortcutMap.bindings`（docs/31 N2），不要在 `main.qml` 里再写死；新增/改动全局键须同步 `ShortcutMap` 动作表与 `ShortcutsDialog` 可见性，并保证录制时（`ShortcutMap.recording`）窗口级 `Shortcut` 全部让位，否则录制 `Ctrl+]` 会先触发旧的「右侧面板开关」。
- **qrc/注册漂移**：新增 QML 文件必须加入根 `CMakeLists.txt` 的 `QML_FILES`；`charts/` 依赖 QTP0004 平铺，移动目录会改 qrc 路径并导致加载失败。
- **qmllint 噪声**：门禁只把 `Error:` 级判失败，历史 warning 会打印但不阻断；不要据此认为 QML 完全干净。
- **性能**：`Sidebar.qml`、`GitPanel.qml`、`UsageStatsPage.qml` 均为数千行级组件，且大量绑定/委托；新增每帧绑定需注意 trace 滚动与 Canvas 重绘开销。
