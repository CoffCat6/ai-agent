# src/ui/ — QML 展示层索引

## 职责

- `main.qml` 是唯一窗口根（`ApplicationWindow`），负责整体壳层：左 Sidebar、中 AgentChatPage、右 ContextPanel、全局对话框/Toast、快捷键、窗口几何与关闭行为、窗口级文件拖放。
- `components/` 放可复用控件与业务组件，`pages/` 放设置中心分类页，`panels/` 放右侧上下文面板，`components/charts/` 是 Canvas 自绘图表。
- 只做展示与交互编排；所有状态/持久化/网络/文件系统动作都调用 `NexusAI.ViewModels` 单例。

## 设计

- **三层结构**：窗口壳（`main.qml`）→ 页面/面板（`pages/`、`panels/`）→ 组件（`components/`）。`AgentChatPage` 内部再分 `TopBar`、`TranscriptViewport`、`InputConsole` + 模型选项行（`SessionTabs` 已随 docs/30 删除）。
- **单例入口**：`Theme`（只读设计令牌）、`ThemeManager`（明暗切换）、`AgentVM`（打开集合/轨迹/输入/状态）、`Settings`、`PreferenceStore`/`ChangeBus`/`PreferenceRegistry`、`I18n`、`EditBuffer`、`GitVM`、`McpManager`。
- **响应式壳层**：`RowLayout` 中两个 `SplitBar` 可拖拽；侧栏 180–560px（默认 260），右面板 220–640px（默认 300），聊天区最小 320px；折叠动画 220ms，拖拽时 `enabled: !dragActive` 保持跟手。折叠后宽度为 0 但 `visible` 延续到动画结束，两侧悬浮拉手（24×56、`z:20`）始终可召回。
- **对话框装载**：`SettingsCenterDialog`、`AuditDialog`、`LogPanel`、`KnowledgeDocDialog`、`ShortcutsDialog` 全部 `parent: Overlay.overlay`；设置页由 `Loader` 按分类懒加载（`qrc:/qt/qml/NexusAI/src/ui/pages/*.qml`）。
- **主题入口**：QML 侧只读 `Theme.*` 令牌；`Ctrl+D` 切 `ThemeManager.isDark`；C++ 另把同一组颜色同步进 `QPalette` 供 Basic 控件使用。
- **图表约束**：`DonutChart`/`LineTrendChart`/`BarCompareChart` 用 QML `Canvas` 自绘，禁止引入 Qt Charts；`charts/` 子目录经 QTP0004 NEW 平铺注册进 `NexusAI` 模块。
- **i18n**：界面文本用 `qsTr`；语言切换由 C++ `I18nManager::localeChanged → engine.retranslate` 强制重求值。

## 数据与控制流

1. 启动：`Component.onCompleted` 读 `AgentVM.restoreWindowOnStartup` 与 `loadWindowGeometry()`，恢复位置/尺寸或最大化；`trayAvailable` 由 `main.cpp` 注入。
2. 会话渲染：`AgentChatPage` 把 `AgentVM.traceModel` 交给 `TranscriptViewport`，delegate 为 `TraceNodeRouter`（按节点类型路由到用户/助手/过程/错误卡片）；展开态存在页面级 `detailState`，不随 delegate 重建丢失。左侧 `MessageNavigator` 导轨按 `AgentVM.userMessageIndex` 画出每次提问，点击跳到对应 trace 行。
3. 输入：`InputConsole` 负责草稿、附件、`@` 提及补全、历史浏览与发送；`ModelSelector`/`ThinkingEffortSelector`/`PermissionModeSelector` 读写 `AgentVM`/`Settings`。
3b. 会话切换（docs/30，P0+P1 已实施）：**侧栏是唯一入口，主区只渲染活动会话**。侧栏点击 → `AgentVM.openConversation(id)`；右侧「结束对话」（右键菜单 / hover 操作条，后者仅在 `open === true` 时出现）→ `closeConversation(id)`；`Ctrl+W` → `closeActiveConversation()`；`Ctrl+Tab`/`Ctrl+Shift+Tab` → `cycleConversation(±1)`（跳过空白对话）。行内 14px 状态槽三态互斥：busy 旋转图标 › 等待确认静态警告 › 未读点。顶栏中部显示当前会话标题，并带会话模板入口（`Ctrl+[` 折叠侧栏后仍可达）。
4. 拖放：窗口根级 `DropArea`（最后声明、铺满全窗）只判断 `hasUrls` 并把原始 `drop.urls` 交给 `AgentVM.handleDroppedFiles()`；VM 发 `attachmentsDropped`，`AgentChatPage` 转给 `inputConsole.appendAttachments()`。
5. 设置：`Sidebar.settingsRequested`/`AgentVM.modelSettingsRequested`/`Ctrl+,` 都走 `settingsCenterDialog.openCategory("model")`；关闭时 `EditBuffer.dirty` 则回滚；搜索合并 `PreferenceRegistry` 元数据与 `settings-search.js` 手工索引，并记住 `settings.lastCategory`。
6. 面板：`ContextPanel` 只剩"Git / 知识库"两态 `StackLayout`（`activeSection` 0/1，Git 仅在 `activeProjectRoot` 非空时出现，隐藏时回落到知识库页）。审阅与任务清单已移出侧栏——两者都需要侧栏给不了的宽度，分别改为 `ReviewDialog` / `TodoDialog`，由 `TopBar` 按钮与 `Ctrl+R` / `Ctrl+T` 打开；知识库 `FileDialog` 多选后 `AgentVM.importDocuments()`。
7. 通知：`errorOccurred`/`noticeOccurred` 分别驱动错误/成功 Toast；删除会话经 `Sidebar.sessionDeletedUndoable` 弹 10 秒撤销条，调用 `AgentVM.undoDeleteSession()`。
7b. 会话切换（docs/30，P0+P1 已实施）：**侧栏是唯一入口，主区只渲染活动会话**。侧栏点击 → `AgentVM.openConversation(id)`；右侧「结束对话」（右键菜单 / hover 操作条，后者仅在 `open === true` 时出现）→ `closeConversation(id)`；`Ctrl+W` → `closeActiveConversation()`；`Ctrl+Tab`/`Ctrl+Shift+Tab` → `cycleConversation(±1)`（跳过空白对话）。行内 14px 状态槽三态互斥：busy 旋转图标 › 等待确认静态警告 › 未读点。顶栏中部显示当前会话标题，并带会话模板入口（`Ctrl+[` 折叠侧栏后仍可达）。
8. 引用定位：`ChangeBus` 的 `search.revealMessage` 在 `AgentChatPage` 中 `Qt.callLater` 查 `traceModel.rowOfMessageId`，滚动并高亮 2 秒。
9. 快捷键（窗口级）：`Ctrl+,` 设置、`Ctrl+L` 审计日志、`Ctrl+B` 右面板、`Ctrl+[` 侧栏、`Ctrl+D` 主题、`Ctrl+/` 速查表。
10. 关闭：`onClosing` 先 `AgentVM.saveWindowGeometry(...)`；当 `PreferenceStore.load("window.closeToTray") === true && trayAvailable` 时 `close.accepted = false` 并 `hide()`，否则真正退出（真正退出入口仍是托盘菜单）。

## 集成点

- C++ 单例与属性：`AgentVM`（traceModel/sessionList/projects/sidebarWidth/panelWidth/pendingConfirmation/todoList/fileCheckpoints…）、`Theme`/`ThemeManager`、`Settings`、`PreferenceStore`、`ChangeBus`、`I18n`、`EditBuffer`、`GitVM`、`McpManager`、`ConnectionTester`。
- qrc 路径固定为 `qrc:/qt/qml/NexusAI/src/ui/main.qml`（URI `NexusAI` + 源目录结构）；设置页在 QML 内以绝对 qrc URL 加载。
- 测试：`tests/CMakeLists.txt` 把 `src/ui/**/*.qml` 与 `settings-search.js`、`FileTree.js` 重新打进测试资源，`qmllint_gate` 对 `nexus_ai`/`nexus_viewmodels` 的响应文件做静态检查；图表/Todo/审阅/引用行有 offscreen 渲染回归。

## 关键文件

| 分类 | 文件 | 说明 |
| --- | --- | --- |
| 壳层 | `main.qml` | 窗口、三分栏、对话框/Toast、快捷键（含 Ctrl+W / Ctrl+Tab）、DropArea、几何/关闭。侧栏/面板的开关**只**在 TopBar（`sidebar-collapse`/`sidebar-expand` 状态图标），main.qml 不再有浮动的中间把手 |
| 聊天页 | `pages/AgentChatPage.qml` | TopBar + 轨迹（折叠态按会话分桶）+ 输入区与模型选项 |
| 轨迹 | `components/TranscriptViewport.qml`、`TraceNodeRouter.qml` | 滚动/跟随/定位与节点类型路由 |
| 输入 | `components/InputConsole.qml`、`ModelSelector.qml`、`ThinkingEffortSelector.qml`、`PermissionModeSelector.qml` | 发送、附件、提及、历史与参数选择 |
| 侧栏/右栏 | `components/Sidebar.qml`、`panels/ContextPanel.qml` | 会话树/搜索 + 会话运行状态槽（busy/等待确认/未读）；Git/文件（审阅与任务清单已移入对话框） |
| 设置 | `components/SettingsCenterDialog.qml`、`CategoryNav.qml`、`PreferenceControl.qml`、`settings-search.js` | 搜索、四组可滚动分类导航、偏好控件、按分类复位的底栏 |
| 设置页 | `pages/GeneralPage`、`AgentBehaviorPage`、`NetworkPage`、`LogsPage`、`KnowledgePage`、`InterfacePage`、`ModelConfigPage`、`SecurityPage`、`McpPage`、`ToolsPage`、`MemoryPage`、`UsageStatsPage`、`ActivityLogPage`、`SettingsBackupPage`、`ProjectRulesPage`、`PromptAssetsPage` | 16 个分类，由 `Loader` 按分类懒加载 |
| 业务组件 | `AssistantMessage.qml`、`UserMessageBubble.qml`、`ProcessCard.qml`、`ErrorCard.qml`、`ToolConfirmDialog.qml`、`TodoListPanel.qml`、`FileChangesPanel.qml`、`ChangeTree.qml`、`GitPanel.qml` | 消息/工具确认/审阅/Git |
| 图表/表格 | `charts/{DonutChart,LineTrendChart,BarCompareChart}.qml`、`UsageDetailTable.qml`、`DiffView.qml`、`CodeBlock.qml` | Canvas 图表、明细表、diff/代码块 |
| 基础控件 | `AppButton.qml`、`AppIcon.qml`、`AppToast.qml`、`IconButton.qml`、`DialogBackground.qml`、`SplitBar.qml`、`SettingsField.qml`、`SettingCard.qml` | 统一外观与交互基元 |

## 约束与风险

- **不要在 QML 解析拖放路径**：`drop.urls` 元素没有 `toLocalFile()`，必须原样交给 C++；`DropArea` 必须最后声明且铺满窗口，否则命中层被内容遮挡。
- **Loader 生命周期**：设置页切换分类会销毁页面，仅靠 `editingFinished` 会丢输入——依赖 `EditBuffer` 防抖 + `Component.onDestruction`；新增页面需沿用同一保存契约。
- **托盘不可用**：`trayAvailable == false` 时绝不能"关闭到托盘"，否则窗口无召回入口。
- **快捷键冲突**：`Ctrl+/` 与部分输入法的 `/` 组合、`Ctrl+[` 与编辑器缩进习惯可能冲突；窗口级 `Shortcut` 会抢占输入焦点，改动需回归 `InputConsole`。
- **qrc/注册漂移**：新增 QML 文件必须加入根 `CMakeLists.txt` 的 `QML_FILES`；`charts/` 依赖 QTP0004 平铺，移动目录会改 qrc 路径并导致加载失败。
- **qmllint 噪声**：门禁只把 `Error:` 级判失败，历史 warning 会打印但不阻断；不要据此认为 QML 完全干净。
- **性能**：`Sidebar.qml`、`GitPanel.qml`、`UsageStatsPage.qml` 均为数千行级组件，且大量绑定/委托；新增每帧绑定需注意 trace 滚动与 Canvas 重绘开销。
