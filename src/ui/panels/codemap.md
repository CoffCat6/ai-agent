# `src/ui/panels/` — 上下文面板（右侧栏）

本目录只含 `ContextPanel.qml`——主窗口右侧栏的唯一实现。它是**容器型面板**：自身不做业务逻辑，只负责在「文件 / Git」两个等宽子标签间切换视图。

## 设计

- 骨架：`Rectangle(root, color=Theme.bgSecondary, clip:true)` → 左侧 1px 分隔线 → `ColumnLayout(margins 20, spacing 12, opacity=root.contentFade)` → 标签行 → `StackLayout(currentIndex: activeSection)`。
- **两个分区，都是项目专用**：
  - 索引 0 = `GitPanel`（Git 状态 / 历史 / 提交）
  - 索引 1 = `ProjectFilesPanel`（只读项目文件浏览）
- `property int activeSection: 0` 是**面板局部状态**，不持久化、不进 VM。写成普通属性而非绑定：回落逻辑会对它赋值，赋值会打断绑定。
- `contentFade = AgentVM.isPanelVisible ? 1 : 0`，折叠时内容淡出（`Behavior on opacity` 150ms）。
- 本地会话（`activeProjectRoot` 为空）时两个标签都隐藏，由 `contextPanelNoProject` 覆盖层说明原因——不擅自改动用户的面板可见性设置，但明确告知"这里为什么是空的"。`onActiveProjectIdChanged` 把 `activeSection` 归零，使回到项目时落在稳定的 Git 分区。

## 迁移历史（重要）

右侧栏曾同时容纳**审阅树、任务清单、知识库、Git、项目文件**五项，后全部迁出前三项，理由是同一类：

| 迁出项 | 现位置 | 原因 |
|---|---|---|
| 审阅（文件改动 diff） | `ReviewDialog`（顶栏按钮 / `Ctrl+R`） | 220–660px 放不下可读的并排 diff |
| 任务清单 | `TodoDialog`（顶栏按钮 / `Ctrl+T`） | 同上，列表需要宽度 |
| 知识库 | `KnowledgeDialog`（侧栏左下工具条） | **既是宽度问题，也是作用域问题**：知识库是全局的（`RagService` 进程内单例），而右侧栏其余分区都只在项目会话中存在——本地会话时会出现"面板里只剩一个知识库"的错位 |

**因此不要再往本面板添加非项目作用域的功能。** 判断标准很简单：这个功能在没有项目的本地会话里是否有意义？若无，它不属于这里。

## 与知识库的关系

本文件**不再包含任何知识库 UI**。知识库的导入 / 移除 / 重新嵌入 / 批量进度全部在 `src/ui/components/KnowledgeDialog.qml`，入口是侧栏左下工具条（与日志、审计、设置并列，四者都是全局功能）。

引用徽标 → 文档详情这条链路与面板无关，走 `AgentVM.openKnowledgeDocument()` → `knowledgeDocumentOpenRequested` → `main.qml` 的 `KnowledgeDocDialog`。

## 代码级约束

- **宽度下限 220px**：`ui/panelWidth` 的 schema 范围是 220–640。新增头部控件必须给 `elide` 或固定宽度。
- **分区索引与标签按钮一一对应**，改动其一必须同步另一处。
- 面板本身不下拉 `GitVM` / `ProjectFileVM` 的数据，由各子面板自理。
