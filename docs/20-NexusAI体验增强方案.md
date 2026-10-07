# 20 - NexusAI 体验增强方案（消息操作 / 桌面集成 / RAG 溯源 / 数据安全）

> **文档编号**：docs/20
> **状态**：工作流 A 第 1 批已实施（A1 重新生成 + A2 编辑重发；链路为
> SqliteMemory::deleteMessagesFrom / lastUserMessageId → AIAgent::regenerateLast →
> ChatSessionController::regenerate → AgentViewModel.regenerate / editAndResend，
> QML 侧 AssistantMessage 重新生成按钮 + UserMessageBubble 内联编辑；
> test_memory/test_agent/test_viewmodel 新增用例全绿）。
> **A3 输入历史已实施**（ChatSessionController 输入环 + historyPrev/historyNext，
> InputConsole ↑/↓ 回溯；test_viewmodel 新增用例全绿）。A4 拖拽附件、D9 快捷键速查待实施。
> **创建日期**：2026-10-06
> **依赖文档**：docs/14（L1–L6 遗留修复）、docs/17（沙箱与设置体系）、docs/18（会话归属模型）、docs/19（对话界面视觉完善）
> **涉及模块**：AgentViewModel / ChatSessionController / AIAgent / SqliteMemory / main.cpp / AgentChatPage / AssistantMessage / InputConsole / Sidebar / SettingsSchema
> **执行纪律**：全文沿用 docs/7 §7 与 docs/9 §7 的全部纪律（锚点实测、每批验收清单、SettingsGuard 键登记、SQLite 迁移纪律、i18n 收口、构建零警告基线、测试先行）

---

## 0. 背景与定位

功能波次 0–4 已交付：ReAct 管线、工具沙箱、RAG、MCP、用量统计、会话归属、
气泡对话流（docs/19 V1–V6）均落地，ctest 20 套件全绿。近期提交（2026-10 初）
集中在对话视口视觉与滚动体验，UI 打磨进入收尾阶段。

本方案对下一阶段的**用户体验增强**做现状排查与任务规划，按投入产出比分为
四个工作流：

| 工作流 | 主题 | 一句话概括 |
|--------|------|-----------|
| **A 消息操作** | 重新生成 / 编辑重发 / 输入历史 | 补齐对话类产品的基本盘，纯 ViewModel+QML 改动 |
| **B 桌面集成** | 系统托盘 / 单实例锁 / 开机自启 / 全局快捷键 | "桌面 AI 助手"的标志性体验，成本极低 |
| **C RAG 可信度** | 引用溯源 / PDF 导入 / 搜索后端扩展点 | 让"知识库回答"可验证、知识库格式更通用 |
| **D 数据安全** | 配置导出导入 / DB 自动备份 / 会话置顶归档 | 用户换机与数据防丢失的真实痛点 |

**非范围**：设置中心设置搜索与 Accessible 补齐（docs/17 B6 可裁剪项，另行裁剪）、
AutoEdit 权限策略变更、MCP 能力扩展（resources/prompts）。

---

## 1. 现状诊断（附证据）

> 以下结论均以 master 源码为准（2026-10-06 排查）。**难度列**：S（纯 UI/绑定）/
> M（跨层新增方法与信号）/ L（结构性重构）。

### 1.1 消息操作

| # | 现状 | 证据（`文件:行号`） | 影响 | 难度 |
|---|------|--------------------|------|------|
| D1 | **无重新生成**：`ChatSessionController` 无 regenerate 方法，`AssistantMessage` 无操作栏 | `ChatSessionController.h:70-74`（仅 startTask/approve/stop）、`AssistantMessage.qml`（grep regenerate 无命中） | 回答不满意时只能重新手打问题 | M |
| D2 | **无编辑重发**：用户消息发出后不可修改重发 | `AgentViewModel.h:305-306`（startTask 只增不改）、`UserMessageBubble.qml` 无编辑入口 | 打错字/补充上下文只能重发一条 | M |
| D3 | **无输入历史**：`InputConsole` 无 ↑ 键回溯，草稿仅按标签单值 | `InputConsole.qml`（grep history/up 无命中）、`ChatSessionController.h:47-49`（draftText 单值） | 重复提问需重新输入 | S |
| D4 | **无会话分支**：无法从任意消息节点分叉新会话 | `TraceModel` 节点无 fork 入口；侧栏仅 newChat/delete/rename（`Sidebar.qml` grep fork 无命中） | 多方案对比需手动复制粘贴 | L |
| D5 | 附件芯片已修（docs/19 D16）但**拖拽创建附件缺失** | `InputConsole.qml:383-399` 仅 FileDialog 手动选择 | 桌面文件需 3 步才能附上 | S |

### 1.2 桌面集成

| # | 现状 | 证据 | 影响 | 难度 |
|---|------|------|------|------|
| D6 | **无系统托盘**：grep QSystemTrayIcon 全仓无命中 | `src/main.cpp`（仅 QQmlApplicationEngine） | 最小化即离开视野，无法后台接收长任务完成通知 | S |
| D7 | **无单实例锁**：重复启动会产生双实例、双写 DB | grep QLockFile 无命中；`main.cpp:68-69` 单进程直接启动 | SQLite 并发写风险 + 配置页错乱 | S |
| D8 | **无开机自启**：无注册表 Run 键写入 | SettingsService 键清单无对应项 | "桌面助手"定位缺少常驻入口 | S |
| D9 | **快捷键仅 5 个且无速查入口** | `main.qml:312-338`（Ctrl+, / L / B / [ / D）；设置中心无快捷键页 | 新用户无法发现功能 | S |
| D10 | **无任务完成通知**：长任务后台完成后无任何提示 | `ChatSessionController.cpp` stateChanged 无托盘/声音联动 | 切走窗口后只能守着 | S |

### 1.3 RAG 可信度

| # | 现状 | 证据 | 影响 | 难度 |
|---|------|------|------|------|
| D11 | **检索结果无溯源 UI**：`search_knowledge_base` 结果回灌为纯文本，回答不标注出处 | `KnowledgeSearchTool.h`（结果含 chunk 但无引用格式约定）、`AssistantMessage.qml` 无引用区 | 用户无法验证"依据哪份文档"，不敢信任 RAG 回答 | M |
| D12 | **仅纯文本导入**：`importDocument` 用 `QIODevice::Text` 直读，PDF/HTML/docx 一律乱码入库 | `AgentViewModel.cpp:340-357`（readAll + fromUtf8，无格式分支）；InputDialog nameFilters 也只列文本 | 知识库实用性大打折扣 | M |
| D13 | **嵌入硬绑 Ollama**：离线时降级伪嵌入（不可检索），无 API 嵌入选项 | `OllamaEmbedding.cpp` 唯一实现；docs/14 L4 同构问题（搜索后端仅 Tavily） | 无 Ollama 的用户 RAG 完全不可用 | L |
| D14 | 搜索后端扩展点已预留未启用 | `docs/14 §3.4` 决策 D2："工厂已预留扩展点，后续按需新增子类（候选：Brave / SearXNG / DuckDuckGo）" | 无 Tavily 密钥则联网搜索不可用 | M |

### 1.4 数据安全与管理

| # | 现状 | 证据 | 影响 | 难度 |
|---|------|------|------|------|
| D15 | **配置不可导出/导入**：docs/14 §3.2 明确 B 档（仅登记键清单）"若后续需要配置备份再升级到 A" | `SettingsSchema.h:13` 注释 "no import/export"；SettingsSchema 已有键登记基础设施 | 换机/重装需手工重配模型与密钥 | M |
| D16 | **DB 无自动备份**：`nexusai_memory.db` 单文件，损坏即丢全部历史 | `%APPDATA%\NexusAI\NexusAI\` 无备份产物；`SqliteMemory.cpp` 无 backup 调用 | SQLite 单文件损坏风险无兜底 | S |
| D17 | **会话无置顶/归档**：侧栏仅 updated_at 排序 + 删除 + 重命名 | `SqliteMemory.cpp:247-253`（sessions 表无 pinned 列）、`listSessions:1064` ORDER BY updated_at | 会话多了以后搜索是唯一组织手段 | M |

---

## 2. 目标与原则

1. **基本盘优先**：先补消息操作（A）这类每天都会碰到的缺口，再做锦上添花。
2. **零破坏**：不改变 docs/18 的会话归属模型、docs/19 的组件拆分与令牌层、
   MEM-701 resume 契约（工具证据不回注 LLM 上下文）。
3. **令牌先行**：新增 UI 全部走 `ThemeProvider` 令牌 + `qsTr()` 双语收口 +
   `Accessible.*`，无魔法数字、无硬编码英文。
4. **键登记纪律**：所有新增设置键同步登记进 `SettingsSchema`（docs/17 B3 账本），
   测试断言账本完整性。
5. **SQLite 迁移纪律**：sessions 表加列一律走 `execDdl(ALTER TABLE ...)`
   幂等迁移（与 `project_id:408` 同构）。
6. **隐私边界**：任何新功能不改变沙箱策略、DPAPI 密钥加密与 localOnlyMode 门控。

---

## 3. 工作流 A：消息操作（第一批实施）

### 3.1 重新生成（修 D1，量级 M）

**设计**：

- `ChatSessionController` 新增 `Q_INVOKABLE void regenerate()`：
  1. `isBusy()` 时拒绝（沿用 stop 前置守卫模式）；
  2. **数据契约（FORK-701）**：找到本会话**最后一条 assistant 消息**在持久层的
     位置，删除它及其之后的 trace 节点展示层，但**不回滚更早历史**；
  3. `m_agent->regenerateLast()`（AIAgent 新增）：把 `m_messages` 尾部的
     assistant 消息移除后，从最后一条 user 消息重新发起请求——与
     `resumeSession` 的"干净历史"契约一致，工具证据仅存 trace_nodes。
  4. 模拟模式（无 agent）：清空 `m_finalResponse` 重播 startSimulation。
- 信号：复用 `responseReady` / `traceChanged` / `stateChanged`，不新增信号。
- **持久化**：`SqliteMemory` 新增 `deleteMessagesFrom(sessionId, fromMessageId)`，
  按 message_id ≥ 阈值删除（audit_log 不动——重新生成是正常行为，保留审计）。
- **UI**：`AssistantMessage.qml` 末条消息显示"重新生成"IconButton（图标 `refresh`），
  仅 `index === trace.count - 1` 且状态为 Idle 时可点。

**测试**：`test_agent` 新增——regenerate 后 LLM 请求不含旧 assistant 消息、
trace 节点重建、busy 时拒绝；`test_memory` 新增——deleteMessagesFrom 边界
（空会话/仅 assistant/中途消息）。

### 3.2 编辑重发（修 D2，量级 M）

**设计**：

- `AgentViewModel` 新增 `Q_INVOKABLE void editAndResend(const QString& newText)`：
  等价于"对最后一条 user 消息执行 regenerate + 替换内容"，即：
  1. 删除持久层最后一条 user 消息 + 之后全部消息（复用 3.1 的
     `deleteMessagesFrom`，阈值取最后一条 user 的 message_id）；
  2. 以 `newText` 调 `startTask(newText)`（沿用现有附件参数语义，图片/文件
     附件不保留——与 resume 契约一致）。
- **UI**：`UserMessageBubble.qml` hover 显示"编辑"IconButton → 气泡切换为
  TextArea 内联编辑（草稿进入 `EditBuffer` 同款校验路径，空文本禁发）→
  确认后调 VM。仅非 busy 状态可编辑。

**测试**：`test_viewmodel` 扩展——editAndResend 后持久层消息序列正确、
LLM 请求含新文本不含旧对白。

### 3.3 输入历史（修 D3，量级 S）

**设计**：

- `ChatSessionController` 新增输入历史环（内存态，进程内保留最近 50 条，不持久化——
  避免新增 schema 键与隐私顾虑）：
  - `startTask` 成功入队时 push 非空文本，去重相邻重复；
  - `Q_INVOKABLE QString historyPrev()` / `historyNext()`：在 InputConsole
    焦点于空输入框或"原始未编辑态"时响应 ↑/↓。
- **UI**：`InputConsole.qml` Keys.onUpPressed 分派（Shift+↑ 仍走多行光标移动）。
- 编辑态即退出历史浏览（与主流终端一致）。

**测试**：`test_viewmodel` 新增环行为（入队去重、越界夹取、编辑退出）。

### 3.4 拖拽创建附件（修 D5，量级 S）

**设计**：`InputConsole.qml` 根节点接 `DropArea`：

- `urls` 全为图片扩展名 → 逐个 `AgentVM.loadImageDataUrl(localFile)` 压缩入列；
- 含文本扩展名 → `AgentVM.loadTextFile` 生成 `{name, content}` 附件；
- 混合拖拽按扩展名逐个分派；不支持的扩展名 toast 提示（复用 `AppToast`）。

**测试**：QMl 层冒烟 + `test_data_url_utils` 补扩展名分派纯函数用例
（分派逻辑抽为可测纯函数 `classifyDropUrls()`）。

### 3.5 本工作流不做：会话分支（D4）

会话分支需要 trace 树模型 + 持久层 parent_id + 侧栏分树 UI 三层重构（量级 L），
且与 docs/18 分树心智模型有交互设计冲突（分支挂在项目树还是会话列表）。
**决策：暂缓**，登记为候选，待 A1–A4 验证后再评估。

---

## 4. 工作流 B：桌面集成（第二批实施）

### 4.1 系统托盘 + 任务完成通知（修 D6/D10，量级 S）

**设计**：

- `main.cpp` 构造 `QSystemTrayIcon`（图标复用应用图标资源）：
  - 菜单：显示/隐藏主窗、新建会话、退出（退出走 `engine.quit()` 前先停全部
    busy tab——沿用"关 tab 停任务"封口语义）；
  - **通知**：`ChatSessionController::stateChanged` 汇聚到 VM 后，主窗不可见且
    任务从 Busy→Idle 时 `showMessage`（标题带会话名，复用 sessionTitle）。
  - 关闭行为：新增偏好键 `prefs/v1/window.closeToTray`（默认 false = 直接退出）。
- **键登记**：`window.closeToTray` 与 `window.startMinimized` 登记进
  SettingsSchema（encrypted=false），InterfacePage 增加 Switch。

**测试**：冒烟为主（QSystemTrayIcon 无 headless 断言价值）；键登记进
test_settings 账本完整性用例。

### 4.2 单实例锁（修 D7，量级 S）

**设计**：`main.cpp` 启动最前（QApplication 构造后）：

```cpp
QLockFile lock(QDir::temp().absoluteFilePath("NexusAI.lock"));
if (!lock.tryLock(100)) return 0;   // 已有实例在跑，静默退出
```

- 二期可选：第二个实例通过本地 socket 唤起已有实例（`QLocalServer`），本期
  仅静默退出（量级 S 不膨胀）。

**测试**：`test_*` 新增最小用例——锁文件竞争时 tryLock 失败路径。

### 4.3 开机自启（修 D8，量级 S）

**设计**：`SettingsService` 新增 `Q_INVOKABLE bool autoStart()` /
`setAutoStart(bool)`，实现为注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
`NexusAI` 键（值 = exe 全路径，`QCoreApplication::applicationFilePath()`）。

- UI：GeneralPage「应用行为」区 Switch（实时保存，M1 语义）。
- 键登记：`general.autoStart` 进 schema。**非 Windows 平台直接返回 false**
  （Windows 优先策略，预留扩展点）。

**测试**：`test_settings` mock 路径校验（读写 Run 键封装为可注入接口）。

### 4.4 快捷键速查表（修 D9，量级 S）

**设计**：`main.qml` 新增 Shortcut（`?`，即 Ctrl+/ 的常见布局）打开
`ShortcutsDialog.qml`：静态两列列表（现有 5 个 + 本批新增），文案走 qsTr 双语。
设置中心 InterfacePage 加入口按钮。

**测试**：qmllint + 冒烟。

---

## 5. 工作流 C：RAG 可信度（第三批实施）

### 5.1 引用溯源（修 D11，量级 M）

**设计**（数据契约 **CITE-801**）：

- `KnowledgeSearchTool` 结果格式扩展：命中项带 `[doc:name#chunkId]` 标记前缀，
  截断逻辑不变（6000 字符预算内）。
- **回答侧约定**：系统提示词追加一句（仅在知识库非空时注入，避免 token 浪费）：
  "引用知识库内容时，在句子末尾以 [文档名] 标注来源"。
- **UI 渲染**：`AssistantMessage` 散文拆分（docs/19 V5 已有交替结构）识别
  `[文档名]` 记号 → 渲染为小徽标 chip，点击打开 `KnowledgeDocDialog` 并
  滚动到对应文档（现有按名定位能力复用）。
- **不解析进数据库**：引用是纯展示层约定，trace_nodes/audit 不动。

**测试**：`test_rag` 补结果标记格式用例；`test_highlighter` 补 `[doc]` 记号
解析用例（解析函数放 `CodeHighlighter` 同级的纯函数）。

### 5.2 PDF 导入（修 D12，量级 M）

**设计**：

- `importDocument` 按后缀分派：`.pdf` → `QPdfDocument`（QtPdf 模块，Qt 6.4+
  即有，项目 6.5+ 满足）逐页 `getAllText` 拼接；`.txt/.md/...` 维持现状；
  其余扩展名维持现有错误提示。
- CMake：`find_package(Qt6 COMPONENTS Pdf)` + `target_link_libraries`，
  CI aqtinstall 参数补 Pdf 模块。
- **去重/命名/嵌入链路完全复用**（content_hash → uniqueDocumentName →
  startIngestion），PDF 仅是"内容提取"这一步的替换。
- FileDialog nameFilters 补 `*.pdf`。

**测试**：`test_rag` 补 PDF 提取用例（测试资源放 `tests/resources/sample.pdf`）。

### 5.3 搜索后端扩展点启用：SearXNG（修 D14，量级 M）

**设计**（延续 docs/14 §3.4 决策 D2 的预留扩展点，本次启用第一个第二后端）：

- 新增 `SearxBackend : WebSearchBackend`：GET `<<baseUrl>>/search?q=...&format=json`，
  结果映射 `{title,url,content}` 截断规则与 Tavily 一致；
- `createWebSearchBackend` 注册 `searxng`，`baseUrl` 为必填设置键
  （`webSearch/searxBaseUrl`，进 schema）；
- 无需 API key——自托管 SearXNG 与"本地优先"定位契合；DuckDuckGo/Brave 仍
  留扩展点暂不实现。
- ToolsPage 联网搜索区 provider ComboBox 增加选项，选 searxng 时显示 baseUrl 字段。

**测试**：`test_web_search` 补 SearXNG 响应解析与 URL 拼接用例（本地 mock JSON）。

### 5.4 本工作流暂缓：嵌入后端可插拔（D13）

Ollama Embedding 接口化需要抽象 `IEmbeddingBackend` + 分页/进度/降级三层重接
（OllamaEmbedding.cpp 与 RagService 耦合较深），且 API 嵌入引入密钥管理面。
**决策：暂缓**，登记候选。离线降级伪嵌入现状已有明确日志与文档说明，可用性
缺口集中在"无 Ollama"人群，由 5.3（SearXNG 无密钥）先行缓解联网侧痛点。

---

## 6. 工作流 D：数据安全与管理（第四批实施）

### 6.1 配置导出/导入（修 D15，量级 M）

**设计**（docs/14 §3.2 B 档升级到 A 档）：

- `SettingsSchema` 补齐字段：全部登记键增加 `default` / `type` / `scope` /
  `encrypted` / `backupable`（docs/17 B3 账本基础上补元数据）。
- `SettingsSchema::exportAll(path)`：遍历 backupable 键 → JSON
  `{schemaVersion, exportedAt, values:{...}}`；**encrypted 键不导出明文**，
  导出占位 `"<encrypted>"`，导入时保留现状（留空=不修改语义一致）。
- `SettingsSchema::importAll(path)`：逐键类型校验 → 合法写穿（EditBuffer
  同款校验）→ 返回 {imported, skipped} 报告；版本高于当前 schema 拒绝并提示。
- **M5（mcp_servers.json）不在本账本**（docs/17 B3 已注明理由），导出报告
  中提示用户另行备份该文件。
- UI：GeneralPage「应用行为」区「导出配置 / 导入配置」按钮 + FileDialog。

**测试**：`test_settings` 新增导出/导入环回用例（含 encrypted 跳过、非法 JSON
拒绝、版本不匹配拒绝）。

### 6.2 DB 自动备份（修 D16，量级 S）

**设计**：

- `main.cpp` 启动时（SqliteMemory 构造后、业务前）：若距今超过 24h 未备份，
  `QFile::copy(db, db + ".bak")`（覆盖上一份），失败仅 qWarning 不阻断启动。
- 备份目录：`%APPDATA%\NexusAI\NexusAI\backups\`，保留最近 3 份
  （`nexusai_memory-<date>.db` + 清理旧文件）。
- 说明：不做 SQLite Online Backup API（单用户桌面场景文件拷贝足够，
  启动时无并发写窗口）。
- GeneralPage「诊断」区加"立即备份"按钮 + 上次备份时间展示（键
  `diagnostics.lastBackupAt`，内存态从文件时间戳读，不新增 schema 键）。

**测试**：`test_memory` 补备份轮转用例（临时目录注入，保留 3 份断言）。

### 6.3 会话置顶与归档（修 D17，量级 M）

**设计**：

- **迁移**：sessions 表 `ALTER TABLE sessions ADD COLUMN pinned INTEGER DEFAULT 0`
  与 `ADD COLUMN archived INTEGER DEFAULT 0`（`execDdl` 幂等，同构 `:408`）。
- `SqliteMemory`：`setSessionPinned(id, bool)` / `setSessionArchived(id, bool)`；
  `listSessions` 排序改 `ORDER BY pinned DESC, updated_at DESC`；默认列表
  过滤 archived（新增 `listSessions(includeArchived)`）。
- UI：Sidebar 会话行右键/长按菜单加"置顶 / 归档"；侧栏底部"已归档"折叠区
  （复用项目分树的折叠交互先例）；置顶行显示图钉徽标（token：brandPrimary）。
- 归档会话不参与跨会话搜索默认结果（搜索框内显式勾选"含已归档"）。

**测试**：`test_memory` 补 pinned 排序、archived 过滤、迁移幂等（二次启动）
用例。

---

## 7. 分期实施计划

| 批 | 内容 | 主要文件 | 测试 | 量级 |
|----|------|----------|------|------|
| **1** | A1 重新生成 + A2 编辑重发 | ChatSessionController、AIAgent、SqliteMemory、AgentViewModel、AssistantMessage、UserMessageBubble | test_agent / test_memory / test_viewmodel 扩展 | M |
| **2** | A3 输入历史 + A4 拖拽附件 + D9 快捷键速查 | InputConsole、ChatSessionController、main.qml、ShortcutsDialog（新） | test_viewmodel 扩展 + 冒烟 | S |
| **3** | B1 托盘/通知 + B2 单实例 + B3 开机自启 | main.cpp、SettingsService、SettingsSchema、GeneralPage、InterfacePage | test_settings + 冒烟 | S |
| **4** | C1 引用溯源 + C2 PDF 导入 | KnowledgeSearchTool、AIAgent(系统提示)、AssistantMessage、importDocument、CMakeLists、CI | test_rag / test_highlighter 扩展 | M |
| **5** | D1 配置导出导入 + D2 DB 备份 | SettingsSchema、SettingsService、GeneralPage、main.cpp | test_settings / test_memory 扩展 | M |
| **6** | C3 SearXNG + D3 会话置顶归档 | WebSearchBackend（新子类）、SqliteMemory、Sidebar、ToolsPage | test_web_search / test_memory 扩展 | M |

每批完成后：ctest 全绿 + qmllint 门禁通过 + 双语 .ts 收口 0 unfinished +
构建零警告；更新本文档状态行与根 readme.md 功能特性列表。

---

## 8. 验收标准（对照工作流）

1. **A**：最后一条回复可一键重新生成且 LLM 请求不含旧 assistant 消息；
   最后一条提问可编辑重发；↑ 键可回溯输入历史；拖拽 txt/png 入输入区直接
   成附件。
2. **B**：关窗到托盘可配置；双开静默退出；开机自启开关写入注册表 Run 键；
   `?` 打开快捷键速查。
3. **C**：知识库回答带 `[文档名]` 徽标可点击跳转；PDF 文件可导入并正常分块
   嵌入；provider 选 searxng + baseUrl 后联网搜索可用且无需密钥。
4. **D**：配置导出 JSON 不含明文密钥、导入可环回；启动自动备份保留 3 份；
   会话可置顶/归档且排序与搜索行为符合 §6.3。
5. **回归**：docs/18 会话归属、MEM-701 resume 契约、docs/19 视觉令牌、
   沙箱三级策略全部不回归（既有 20 套件全绿）。

---

## 9. 候选池（本轮不做，登记备查）

| 候选 | 来源 | 未做理由 |
|------|------|---------|
| 会话分支（fork） | §1.1 D4 | 量级 L，与 docs/18 分树模型有交互冲突，待 A 批验证后评估 |
| 嵌入后端可插拔（IEmbeddingBackend） | §1.3 D13 | 重构面大；SearXNG 先缓解联网痛点 |
| 自动更新检查（GitHub Releases） | 初版分析 | 需网络拉取 + 版本比较策略，价值依赖发布节奏 |
| DuckDuckGo / Brave 搜索后端 | docs/14 预留 | SearXNG 落地后按需再加 |
| docs/17 B6 设置搜索 | docs/17 | 可裁剪项，独立裁剪决策 |
| Global 唤起快捷键（Alt+Space 全局热键） | §1.2 | 需原生 API（RegisterHotKey），托盘先行观察需求 |
