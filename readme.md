# NexusAI

本地优先的桌面 AI 智能体应用：ReAct 循环驱动的大模型对话 + 工具调用 + 知识库检索增强（RAG），基于 Qt 6 / QML，Windows 优先。

## 功能特性

- **ReAct 智能体管线**：思考 → 工具调用 → 结果回灌的完整循环，带步数预算、防重复调用守卫、流式输出与随时中止
- **多模型多配置**：OpenAI 兼容 / Anthropic / Ollama 等协议，多 profile 管理；聊天区按标签选择模型与思考强度，执行中的任务保持配置快照；设置中的活动配置作为新会话默认
- **工具沙箱**：三级安全策略（直接执行 / 用户确认 / 永禁用），读取默认限制在用户目录（可设置中开启全盘读取），路径黑白名单 + 全量审计日志
- **知识库 RAG**：中文友好分块、Ollama 向量嵌入（可配置模型，离线自动降级本地伪嵌入）、内容去重与同名共存、向量持久化（重启秒级恢复）、本地知识库管理与检索；管理界面在独立对话框（侧栏左下工具条入口），表格式列表展示段数/字符/嵌入状态
- **引用溯源徽标**：知识库命中项带 `[doc:文档名#chunkId]` 标记，回答中的 `[文档名]` 记号渲染为可点击徽标 chip，点开即复用知识库文档对话框按名定位，回答来源可验证
- **PDF 导入**：知识库可直接导入 `.pdf`（Qt6Pdf 逐页提取文本，去重/分块/嵌入链路与文本文件完全复用），加密、损坏、无文本层（扫描件）、读取失败给出分类错误提示
- **会话持久化**：SQLite 保存消息、思考轨迹、附件、审计与知识库；重开应用恢复时间线。会话支持置顶、归档与恢复，历史搜索可选择包含归档记录
- **数据库备份与数据保留**：启动自动备份（VACUUM INTO 一致性快照，保留最近 N 份）与随时手动备份，备份文件可预览内容概况与完整性，一键恢复（自动回滚保护）；会话删除支持 10 秒内撤销（软删除 30 天后硬回收）；审计日志与 Token 用量按天保留（可配置，0 为永久）
- **消息级操作**：末条助手回复一键重新生成、末条用户消息内联编辑重发并保留附件（上下文与持久化消息、轨迹同步回退，审计链保留）；输入框 ↑/↓ 回溯本标签输入历史（内存态、相邻去重）；拖拽本地图片/文本文件到输入区直接成附件（任意启动方式可用：从终端/管理员启动时自动放行 UIPI 的 OLE 拖放握手消息）
- **项目级会话**：文件夹可导入为「项目」，项目会话与本地对话在侧栏分树共存；右侧栏的 Git 与文件浏览仅在项目会话中出现，本地对话时面板保持空白；归属按标签持有（不存在全局"当前项目"），顶栏徽标显示当前会话的归属，新建会话默认落到普通对话，项目会话由项目行的「+」创建
- **项目文件浏览**：右侧栏「文件」分区以目录树只读浏览当前项目（仅项目会话），目录按需展开、支持名称过滤与折叠全部；点击文件在面板内预览前 512 KB（超大/二进制/已删除各有明确提示），双击或用按钮交给系统默认程序打开。忽略目录与敏感文件（`.env`、`id_rsa` 等）在数据层排除；预览带语法高亮（按扩展名判定，未识别扩展名保持纯文本而不猜测），支持自动换行开关、可拖动的高度调节与行号显示
- **可观测性**：内置实时日志面板（分类/级别/关键词筛选），审计链凭 request_id 串起 LLM 请求、工具执行与任务结果，使用与网络事件独立留存（usage_log，30 天/10000 行保留）
- **稳定性**：网络瞬时失败自动重试一次、任务超时自动封口收敛、上下文预算计入工具调用与工具定义、知识库总量预算（默认 4096 段）
- **桌面集成**：系统托盘（后台任务完成气泡通知，主窗不在前台才打扰、可一键关闭通知）、关闭可最小化到托盘（托盘不可用时自动退回正常退出，不会把窗口"藏丢"）、单实例锁（重复启动静默退出，杜绝双写 SQLite/配置）、开机自启（用户级注册表，设置页开关）、快捷键速查表（`Ctrl+/`）
- **日志面板**：侧栏入口打开实时日志视图，级别/分类/关键词三维筛选，一键复制与打开日志目录；重启后回填上次会话历史日志（标注"历史"）
- **联网搜索**：`web_search` 工具（默认 Tavily）让模型检索实时信息，结果截断 6000 字符回灌；密钥经 DPAPI 加密；调用前需确认；`localOnlyMode` 下门控禁用
- **SearXNG 搜索后端**：联网搜索新增自托管 SearXNG 后端（`GET /search?format=json`，无需 API Key），与 Tavily / DuckDuckGo 同走 `WebSearchBackend` 扩展点，工具管理页可切换后端并配置实例地址
- **长期记忆**：情景记忆在会话结束时异步生成摘要，新会话按相关性检索注入（≤1500 token），默认关闭
- **主题跟随系统**：主题模式三档（跟随系统 / 深色 / 浅色），`auto` 实时跟随 Windows 深浅色，显式选择优先
- **无障碍**：自定义 QML 控件补全 `Accessible.role/name/description`，纯图标控件具备可读名称，纯键盘可完成核心流程
- **MCP 协议接入**：支持 MCP（Model Context Protocol）服务器动态注册工具，带安全收紧策略（详见下文 MCP 安全章节）

## 目录结构

```
src/
├── core/            # 纯 C++ 核心库（零 UI 依赖）：agent / memory / rag / tools / utils
├── infrastructure/  # 外部服务适配：OpenAI / Anthropic / Ollama 服务与嵌入客户端
├── viewmodels/      # MVVM 层：AgentViewModel / SettingsService / ThemeManager
├── ui/              # QML 视图（main.qml + components / panels / pages）
└── main.cpp         # 组装依赖链并暴露给 QML
tests/               # QtTest 单测（49 个套件）
docs/                # 设计文档与技术路线
```

## 构建与运行

要求：CMake ≥ 3.24、C++20 编译器、Qt ≥ 6.5（Core/Quick/Sql/Network/Concurrent/QuickControls2/LinguistTools）。

```bash
# Windows (MinGW) 示例：Qt 工具链需在 PATH 中
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/nexus_ai.exe
```

未配置模型时，空白页提供“配置模型”入口。填写、测试并启用配置后即可发送；未被接受的发送会保留草稿和附件。

## 部署与 CI

```powershell
# 一键产出自包含部署目录（构建 + cmake --install + Qt 依赖收集 + 完整性检查）
powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1 -OutDir deploy

# 追加产出 NexusAI-<version>-win64.zip
powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1 -OutDir deploy -Zip
```

产物布局：`deploy/bin/nexus_ai.exe`（与 Qt DLL、qt.conf 同目录，双击即启动），`qml/`、`plugins/`、`translations/` 为运行时资源。exe 内嵌 VERSIONINFO（版本号单一来源为 CMakeLists 的 `project(VERSION)`）。

CI（`.github/workflows/ci.yml`）：GitHub Actions `windows-latest` 单 job——aqtinstall 安装 Qt 6.9.0 MinGW 工具链 → Ninja 构建（BUILD_TESTS=ON）→ 全量 ctest（含 qmllint 门禁）→ 调 deploy.ps1 并上传部署产物 artifact。当前仓库尚无远端：**推送到 GitHub 后工作流即生效**；「非开发机可启动」验收需在第二台 Windows 机器解压 zip 直接运行。

## 测试

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

覆盖范围：沙箱策略、Token 估算、三家 LLM 响应解析、SQLite 记忆、RAG、ReAct 全流程、嵌入分页与进度、ViewModel、设置服务、日志（环形缓冲/Activity 分类/历史回填）、Provider 请求生命周期与并发隔离、Web 搜索、Episodic 记忆、消息重新生成/编辑重发（FORK-701）、附件类型分派与拖放边界（A4）、偏好键账本与开机自启（B）、设置项编辑生命周期（Loader 销毁不丢值，含可复用字段行/销毁掉真实文本两条守卫）、schema v3/v4 迁移与保守 128000 默认、Git 面板与文件改动审阅、知识库导入、代码图表渲染、设置页工具分组与折叠、MCP 与代理链路，共 49 套件约 270 用例，另含 `qmllint_gate` 静态门禁。双语 .ts 已补齐全部译文（0 unfinished）。

## 配置与数据位置

- 模型配置：QSettings（Windows 注册表 `HKCU\Software\NexusAI\NexusAI`），API Key 在 Windows 上经 DPAPI 按用户加密存储
- 应用数据：`%APPDATA%\NexusAI\NexusAI\nexusai_memory.db`（消息 / 轨迹 / 审计 / 知识库）
- 运行日志：`%LOCALAPPDATA%/NexusAI/NexusAI/logs/`，按天轮转保留 7 天；使用/网络事件另存 usage_log（同一数据库，保留 30 天/10000 行）

### 设置导出与恢复

设置中心的「设置与备份」可导出 JSON 并预览后恢复。该文件仅用于同一设置 schema 版本的迁移；不包含 SQLite 会话/知识库、日志、已知 API Key 或 MCP 环境变量/请求头。MCP 服务器恢复时默认禁用；安全权限、工具开关及开机自启不会被恢复，需用户自行确认。模型密钥需要重新填写。自定义提示词、MCP 命令参数和 URL 等自由文本可能含有用户自行填写的敏感信息，分享文件前请检查。恢复成功后需要重启应用才能完整生效；这不是数据库的完整备份。

## MCP 安全注意事项

NexusAI 对 MCP 服务器接入采取多层安全防护策略：

### 不可信数据声明

系统提示词内置 MCP 不可信数据声明：MCP 工具返回的内容视为**不可信数据**，模型不得将其作为指令执行。此声明在用户未自定义 override 时自动生效（走内置 `systemPrompt()`）。

### 工具描述截断

MCP 工具的 `description` 字段截断至 **≤ 1024 字符**（`McpToolAdapter::MaxDescriptionChars`），防止超长描述导致的提示注入与 token 膨胀。

### 本地模式门控

`localOnlyMode` 开启时，**全部 `mcp__*` 工具被禁用**且不暴露给模型。此策略在 `applyLocalOnlyGate()` 中实现，采用保守默认（D1 方案）：本地模式下所有 MCP 工具一律禁用，预留按服务器 `local: bool` 字段细分的扩展点。

### 工具数量上限

施加工具数上限防止 schema token 膨胀：

- 单服务器：**≤ 32 个工具**
- 全局：**≤ 64 个工具**

超出上限的工具被禁用并告警（`qCWarning`），且不进入 `toolNamesForModel()`（不暴露 schema 给模型）。上限校验仅约束 MCP 动态工具，不影响内建工具。

### 审计与超时

- **审计来源标记**：工具执行审计记录的 `detail` 字段包含 `mcp_server`，标识工具来源服务器
- **请求超时**：MCP 请求超时 30s（`McpClient::RequestTimeoutMs`）
- **强制确认**：MCP 工具默认 `RequiresConfirm`，高危操作需用户允许或拒绝
- **崩溃退避**：服务器崩溃后按指数退避策略重连

## 更多文档

架构与实施细节见 `docs/`（1-开发文档、2-技术路线、3-界面开发、4-MVP 实施计划）。

**完整文档体系**（面向使用与二次开发）见 [`docs/read_files/`](docs/read_files/)：

- [0-项目导读](docs/read_files/0-项目导读.md) —— 代码库阅读路线与端到端数据流
- [1-设计文档](docs/read_files/1-设计文档.md) —— 架构、ReAct 循环机制、沙箱与 RAG 设计
- [2-部署与个人使用指南](docs/read_files/2-部署与个人使用指南.md) —— 构建、配置、备份与故障排查
- [3-接口与扩展开发指南](docs/read_files/3-接口与扩展开发指南.md) —— 新增工具 / Provider / 界面
- [4-用户手册](docs/read_files/4-用户手册.md) —— 面向使用者的操作说明
- [6-日志开发](docs/6-日志开发.md) —— 日志信息可视化（已落地：环形缓冲/LogModel/LogPanel/分类事件流）；[5-完善计划](docs/5-完善计划.md) —— 架构审计与 Batch 0-12 执行实录
- [11-功能增强实现方案](docs/11-NexusAI功能增强实现方案.md) / [12-任务规划与实施记录](docs/12-NexusAI功能增强任务规划.md) / [13-需求规格](docs/13-NexusAI功能增强需求规格.md) —— 功能增强 T1–T15 的需求、设计与实施记录（§18–§20）

本批核心流程修复、验收与后续范围见 [21-核心流程修复与体验完善](docs/21-核心流程修复与体验完善.md)。

正确性缺陷修复方案（MCP 工具生命周期 use-after-free / per-tab SQLite 连接泄漏 / 搜索与嵌入配置热切换数据竞争 / 沙箱写路径联接穿透）见 [22-P0正确性缺陷修复方案](docs/22-P0正确性缺陷修复方案.md)。

数据层完善方案（索引与 WAL / 审计与用量保留策略 / write_file 与 loadMessages 修复 / 会话删除撤销 / 数据库备份与恢复预览）见 [23-P1数据层完善方案](docs/23-P1数据层完善方案.md)。

数据库恢复实施与边界加固批次（恢复编排与失败回滚 / MCP 传输缓冲上限 / 摘要请求配对 / 退出期 worker 守护 / 敏感环境变量剔除等十项）见 [24-P1b恢复与P2边界加固方案](docs/24-P1b恢复与P2边界加固方案.md)。

体验审计与分批优化方案（A 正确性 / B 性能 / C 体验与 i18n / D 无障碍 / E 清理五批，含用量统计页去冗余与请求明细表格化）见 [25-体验审计与分批优化方案](docs/25-体验审计与分批优化方案.md)。

下一阶段功能完善目标与计划（F 接通已有能力 / G 编码场景 / H 平台与集成 / I 安全与长尾四批，含优先级、依赖顺序与验收口径）见 [26-下一阶段功能完善目标与计划](docs/26-下一阶段功能完善目标与计划.md)。
