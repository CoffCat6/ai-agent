# NexusAI

本地优先的桌面 AI 智能体应用：ReAct 循环驱动的大模型对话 + 工具调用 + 知识库检索增强（RAG），基于 Qt 6 / QML，Windows 优先。

## 功能特性

- **ReAct 智能体管线**：思考 → 工具调用 → 结果回灌的完整循环，带步数预算、防重复调用守卫、流式输出与随时中止
- **多模型多配置**：OpenAI 兼容 / Anthropic / Ollama 三种 Provider，多 profile 管理与主界面快速切换
- **工具沙箱**：三级安全策略（直接执行 / 用户确认 / 永禁用），路径黑白名单 + 全量审计日志
- **知识库 RAG**：中文友好分块、Ollama 向量嵌入（离线自动降级本地伪嵌入）、本地知识库管理与检索
- **会话持久化**：SQLite 保存消息、思考轨迹、审计与知识库；重开应用恢复完整时间线

## 目录结构

```
src/
├── core/            # 纯 C++ 核心库（零 UI 依赖）：agent / memory / rag / tools / utils
├── infrastructure/  # 外部服务适配：OpenAI / Anthropic / Ollama 服务与嵌入客户端
├── viewmodels/      # MVVM 层：AgentViewModel / SettingsService / ThemeManager
├── ui/              # QML 视图（main.qml + components / panels / pages）
└── main.cpp         # 组装依赖链并暴露给 QML
tests/               # QtTest 单测（10 个套件）
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

## 测试

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

覆盖范围：沙箱策略、Token 估算、三家 LLM 响应解析、SQLite 记忆、RAG、ReAct 全流程、嵌入分页与进度、ViewModel、设置服务。

## 配置与数据位置

- 模型配置：QSettings（Windows 注册表 `HKCU\Software\NexusAI\NexusAI`），API Key 在 Windows 上经 DPAPI 按用户加密存储
- 应用数据：`%APPDATA%\NexusAI\NexusAI\nexusai_memory.db`（消息 / 轨迹 / 审计 / 知识库）
- 运行日志：`%APPDATA%\NexusAI\NexusAI\logs\`，按天轮转保留 7 天

## 更多文档

架构与实施细节见 `docs/`（1-开发文档、2-技术路线、3-界面开发、4-MVP 实施计划）。

**完整文档体系**（面向使用与二次开发）见 [`docs/read_files/`](docs/read_files/)：

- [0-项目导读](docs/read_files/0-项目导读.md) —— 代码库阅读路线与端到端数据流
- [1-设计文档](docs/read_files/1-设计文档.md) —— 架构、ReAct 循环机制、沙箱与 RAG 设计
- [2-部署与个人使用指南](docs/read_files/2-部署与个人使用指南.md) —— 构建、配置、备份与故障排查
- [3-接口与扩展开发指南](docs/read_files/3-接口与扩展开发指南.md) —— 新增工具 / Provider / 界面
- [4-用户手册](docs/read_files/4-用户手册.md) —— 面向使用者的操作说明
- [6-日志开发](docs/6-日志开发.md) —— 日志信息可视化开发计划（规划中）
