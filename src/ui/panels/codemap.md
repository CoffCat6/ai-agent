# src/ui/panels/ 架构地图

## 职责

本目录只含 `ContextPanel.qml`——主窗口右侧栏（上下文面板）的唯一实现，是**容器型面板**：自身不做业务逻辑，只负责 ① 顶部固定展示会话待办（`TodoListPanel`）；② 下方用「审阅 / Git / 知识库」三个等宽子标签 + `StackLayout` 切换视图；③ 承接**知识库文档的导入 / 移除 / 重新嵌入**的全部 UI 入口（知识库的单一 UI 入口；`MemoryPage` 只管 episodic 长期记忆，不碰文档库）。面板无标题栏、无折叠按钮——折叠由 `main.qml` 驱动。

## 设计

- 骨架：`Rectangle(root, color=Theme.bgSecondary, clip:true)` → 左侧 1px 分隔线 → `ColumnLayout(margins 20, spacing 12, opacity=root.contentFade)` → `TodoListPanel` → 1px 横线（`visible: todoPanel.hasItems`）→ 标签行 → `StackLayout(currentIndex: activeSection)`。三分区：审阅 = `FileChangesPanel` + 空态 Text；Git = `GitPanel`；知识库 = 头部行 / 重试按钮 / 空态 Text / `ScrollView+ListView`。
- `contentFade = AgentVM.isPanelVisible ? 1 : 0`，折叠时内容淡出（`Behavior on opacity` 150ms）。`property int activeSection`（0=Git、1=知识库、2=文件）是**面板局部状态**，不持久化、不进 VM；初值 1（知识库），`Component.onCompleted` 里仅当已有 `activeProjectRoot` 才置 0（Git）——首次启动是本地会话，停在被隐藏的 Git 分区会让右侧栏显示 Git 空态。切会话不重置，仅当 Git 标签被隐藏时回落。写成普通属性而非绑定：回落逻辑会对它赋值，赋值会打断绑定。
- 三个标签用 `Layout.fillWidth` + `implicitHeight: 26` + `buttonColor:"transparent"`，高亮靠 `highlighted` + `textColor`（选中 bgPrimary / 未选 textSecondary），保证面板收到 220px 下限时三个标题仍可读；具体计数放在分区标题（知识库为「知识库 · N」）。
- `signal documentDetailsRequested(string name)` 是唯一对外信号 → `main.qml` 接 `knowledgeDocDialog.openFor(name)`。
- **宽度/折叠/分割条全在 `main.qml`**：`Layout.preferredWidth: AgentVM.isPanelVisible ? AgentVM.panelWidth : 0`、`Layout.minimumWidth: 0`、`visible: isPanelVisible || width > 0.5`；`Behavior on Layout.preferredWidth` 220ms，拖拽中 `enabled: !rightSplitter.dragActive` 以跟手；面板**左侧**的 `SplitBar` 拖拽 → `AgentVM.panelWidth = clamp(220, panelWidth - delta, min(640, 窗口剩余宽))`，双击复位 300；折叠靠右侧浮动拉手 `AgentVM.togglePanel()`（`Ctrl+B` 同入口）。

## 数据与控制流

### 知识库导入全链路（文件选择 → PdfTextExtractor → RagService → 持久化）

1. `FileDialog{ fileMode: OpenFiles }`（过滤器 `*.txt *.md *.json *.csv *.log *.pdf`）→ `onAccepted: AgentVM.importDocuments(selectedFiles)`。VM 侧：单文件走 `importDocument()`（保留分类错误提示），多文件走 `startBatchQueue()` → `importNextBatchFile()` 逐个派发（GUI 线程串行推进）。
2. **worker 读文件**：批量路径 `readAndIngestFile(path, rag)`；`.pdf` → `extractPdfText()`（`viewmodels/PdfTextExtractor`，`Qt6::Pdf` 的 `QPdfDocument`），失败按 `OpenFailed/Encrypted/Corrupted/NoText` 分类成中文消息；其余后缀按 UTF-8 读；空内容 → failed。单文件路径同样分流：`.pdf` 走 `startPdfImport()`（`QFutureWatcher` + `QtConcurrent::run`），其余同步读后走 `importExtractedText()` → `startIngestion()`。
3. **去重与命名**：`knowledgeContentHash()`（SHA-256）→ `rag->containsContentHash()` 命中记 duplicate；`rag->uniqueDocumentName()` 生成 `a (2).txt`。**入库**：`rag->ingestDocument(name, content, pageTexts)`；返回码 `>0`=段数、`0`=空/无法分块、`-1`=超单文档上限、`-3`=超总量预算；`usesPseudoEmbedding()` 为真记 degraded（伪嵌入）。结果回 GUI 线程发 `noticeOccurred("已导入 %1（%2 段）")` 或 `errorOccurred`。
4. **持久化**：`RagService::ingestDocument` 发 `documentPersisted`；`main.cpp` 以 `&app` 作 context 做**排队连接**（信号可能来自 worker）→ `SqliteMemory::upsertKnowledgeDoc` + `replaceKnowledgeChunks` → `knowledge_docs` / `knowledge_chunks` 表。**列表刷新**：`RagService::changed` → `AgentViewModel::refreshKnowledge()` → `knowledgeChanged` → QML 的 `AgentVM.knowledgeDocuments` 重填（字段 `name/chunks/chars/pseudo`）。
5. **UI 反馈**：`knowledgeBusy` 转圈（`AppIcon "refresh"` + `RotationAnimation` 无限 1200ms）；进度文案三态：批量 `批量导入 %1/%2：%3` → `正在取消…` → `已嵌入 %1/%2` → `导入中…`；取消 `AgentVM.cancelBatchImport()`（**不再开始后续文件**，在飞文件跑完）；失败项 `AgentVM.retryFailedBatchImports()`（以文件系统当前内容为准）。

### 各分区数据源

| 分区 | 绑定 | 说明 |
|---|---|---|
| 待办 | `AgentVM.todoList` | 由 `todo_write` 驱动；空时组件自折叠到 0 高度并隐藏分隔线 |
| 审阅 | `AgentVM.fileCheckpoints` + `activeProjectRoot` | `FileChangesPanel` 收到 `listMaxHeight: 0`（撑满分区高度）；空态文案「本次会话暂无可审阅的文件改动…」 |
| 文件 | `AgentVM.activeProjectRoot` | `ProjectFilesPanel{ active: activeSection === 2 }`；只读浏览，过滤规则在 `ProjectFileIndex::listDir`（与 `scan()` **有意不同**：浏览器要看见 README/LICENSE 等非白名单文件，只排除 ignore 目录与敏感文件） |
| Git | `AgentVM.activeProjectRoot` | `GitPanel{ active: activeSection === 0 }`；GitPanel 写 `GitVM.projectRoot`，仅当 `active && projectRoot` 非空才刷新。`repoKnown / identifying / hasProject` 三态区分「无项目」「识别中」「已知」，无项目时只显示 `gitNoProjectHint` 且不调用 git |
| 知识库 | `AgentVM.knowledgeDocuments` | 行点击 → `documentDetailsRequested(name)`；`pseudo===true` 显示「降级」徽标（带 tooltip）+「重新嵌入」按钮 `reembedDocument(name)`；删除 `removeDocument(name)` |

### 错误态

面板内**无内联错误文案**：导入失败/重复/超预算都经 `AgentVM.errorOccurred` / `noticeOccurred` 由 `main.qml` 的全局 Toast 呈现；面板自身只表达「空」（空态 Text）与「忙」（转圈 + 进度 + 失败重试按钮）两种状态。

## 集成点

- **AgentVM**：`isPanelVisible`、`activeProjectRoot`、`activeProjectId`（仅用其 changed 信号）、`todoList`、`fileCheckpoints`、`knowledgeDocuments`、`knowledgeBusy`、`knowledgeEmbedDone/Total`、`batchImportRunning/Done/Total/Current/FailedCount`、`batchImportCancelling`；invokable：`importDocuments` / `cancelBatchImport` / `retryFailedBatchImports` / `removeDocument` / `reembedDocument`。
- **GitVM**：不直接引用；`GitPanel` 内部设置 `GitVM.projectRoot` 并调用 `refreshRepoInfo` / `refreshStatus` / `requestRepoLog` / `stageFiles` / `commitStaged` 等。
- **RagService**：不直接引用，经 `AgentVM.setRagService()` 注入；`changed` 驱动列表刷新，`documentPersisted` 由 `main.cpp` 落 SQLite。
- **main.qml**：`onDocumentDetailsRequested: knowledgeDocDialog.openFor(name)`；宽度/折叠/分割条/拉手。**PdfTextExtractor**：只经 `AgentViewModel` 使用，面板不感知 PDF。

## 关键文件

- `src/ui/panels/ContextPanel.qml`（本目录唯一文件）
- `src/ui/components/TodoListPanel.qml`、`FileChangesPanel.qml`、`ChangeTree.qml`、`GitPanel.qml`
- `src/ui/components/AppButton.qml`、`IconButton.qml`、`AppIcon.qml`、`SplitBar.qml`、`src/ui/main.qml`
- `src/viewmodels/AgentViewModel.h/.cpp`、`PdfTextExtractor.h/.cpp`、`GitViewModel.h`
- `src/core/rag/RagService.h`、`src/core/memory/SqliteMemory.cpp`
- `tests/qml/ReviewTabHarness.qml`（审阅分区按本面板宿主形态复刻的离屏夹具）

## 约束与风险

- **不要在 QML 侧解析文件路径**：`FileDialog.selectedFiles` 需 C++ 用 `QUrl::toLocalFile()` 归一。
- **批量导入是串行的**：一文件一池任务，GUI 线程链式推进；取消只是不再派发后续文件，在飞文件仍会跑完（单文件嵌入 120s 超时），文案必须写明「等待当前文件完成」。
- **同一时刻只允许一个知识库写操作**：`m_knowledgeBusy || m_batchRunning` 时 VM 拒绝并报错。
- **降级（伪嵌入）必须显式提示**：嵌入服务离线时整篇退化为 128 维哈希向量，模型侧无感知，只能靠 `pseudo` 徽标提示「可稍后重新嵌入」。
- **切到本地对话必须离开项目专用标签**：`onActiveProjectIdChanged` 里 `activeProjectRoot` 为空且 `activeSection` 为 0(Git) 或 2(文件) 时置 1，否则会停在不可见分区。
- **无项目不得识别仓库**：「识别中」只在 `hasProject && !repoKnown` 成立；无项目时 GitPanel 不得调用 `GitVM.refreshRepoInfo/refreshStatus`（用例 `localChatDoesNotIdentifyRepository` 用 `everRequestedGit` 把守）。
- **宽度下限 220px**：知识库头部（标题 + 隐私说明 + 进度 + 按钮）靠 `ElideRight` + tooltip 兜底，新增头部控件必须给 `elide` 或固定宽度。
- **隐私提示不可删**：文案「文档内容会随提问发送给当前模型服务」与「仅本地模式」设置项联动（审计 P1-7 结论）。
- **知识库 UI 的单一入口就是本文件**：设置中心没有第二个知识库页，不要在 `MemoryPage` 另起一套导入/删除入口。
- **`GitPanel` 直接使用 `GitVM` 单例**：`projectRoot` 变更会清空 Git 状态并作废在飞请求，不要同时实例化两个 GitPanel（否则会互相抢同一个单例状态）。
