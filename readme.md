# NexusAI

本地优先的桌面 AI 智能体应用：ReAct 循环驱动的大模型对话 + 工具调用 + 知识库检索增强（RAG），基于 Qt 6 / QML，Windows 优先。

## 功能特性

- **ReAct 智能体管线**：思考 → 工具调用 → 结果回灌的完整循环，带步数预算、防重复调用守卫、流式输出与随时中止
- **多模型多配置**：OpenAI 兼容 / Anthropic / Ollama 三种 Provider，多 profile 管理与主界面快速切换
- **工具沙箱**：三级安全策略（直接执行 / 用户确认 / 永禁用），读取默认限制在用户目录（可设置中开启全盘读取），路径黑白名单 + 全量审计日志
- **知识库 RAG**：中文友好分块、Ollama 向量嵌入（可配置模型，离线自动降级本地伪嵌入）、内容去重与同名共存、向量持久化（重启秒级恢复）、本地知识库管理与检索
- **会话持久化**：SQLite 保存消息、思考轨迹、审计与知识库；重开应用恢复完整时间线
- **消息级操作**：末条助手回复一键重新生成、末条用户消息内联编辑重发（持久层按主键阈值同步删除，审计链保留）；输入框 ↑/↓ 回溯本标签输入历史（内存态、相邻去重）
- **项目级会话**：文件夹可导入为「项目」，项目会话与本地对话在侧栏分树共存；归属按标签持有（不存在全局"当前项目"），顶栏徽标显示当前会话的归属，新建会话默认落到普通对话，项目会话由项目行的「+」创建
- **可观测性**：内置实时日志面板（分类/级别/关键词筛选），审计链凭 request_id 串起 LLM 请求、工具执行与任务结果，使用与网络事件独立留存（usage_log，30 天/10000 行保留）
- **稳定性**：网络瞬时失败自动重试一次、任务超时自动封口收敛、上下文预算计入工具调用与工具定义、知识库总量预算（默认 4096 段）
- **日志面板**：侧栏入口打开实时日志视图，级别/分类/关键词三维筛选，一键复制与打开日志目录；重启后回填上次会话历史日志（标注"历史"）
- **联网搜索**：`web_search` 工具（默认 Tavily）让模型检索实时信息，结果截断 6000 字符回灌；密钥经 DPAPI 加密；调用前需确认；`localOnlyMode` 下门控禁用
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
tests/               # QtTest 单测（18 个套件）
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

未配置后端时 UI 自带内置演示管线（模拟模式），可在设置中切换。

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

覆盖范围：沙箱策略、Token 估算、三家 LLM 响应解析、SQLite 记忆、RAG、ReAct 全流程、嵌入分页与进度、ViewModel、设置服务、日志（环形缓冲/Activity 分类/历史回填）、Provider 请求生命周期与并发隔离、Web 搜索、Episodic 记忆、消息重新生成/编辑重发（FORK-701），共 21 套件约 220 用例。

## 配置与数据位置

- 模型配置：QSettings（Windows 注册表 `HKCU\Software\NexusAI\NexusAI`），API Key 在 Windows 上经 DPAPI 按用户加密存储
- 应用数据：`%APPDATA%\NexusAI\NexusAI\nexusai_memory.db`（消息 / 轨迹 / 审计 / 知识库）
- 运行日志：`%LOCALAPPDATA%/NexusAI/NexusAI/logs/`，按天轮转保留 7 天；使用/网络事件另存 usage_log（同一数据库，保留 30 天/10000 行）

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
