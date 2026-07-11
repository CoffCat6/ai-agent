# NexusAI MVP 闭环工程实施计划

**文档状态**：实施基线  
**目标版本**：0.2.x  
**更新时间**：2026-07-11  
**适用范围**：把当前可运行 UI 原型推进到真实可用的 Agent MVP。

---

## 1. 目标摘要

当前项目已经具备 Qt/QML 桌面外壳、主题系统、三栏布局、Trace 时间线、输入控制台、工具确认弹窗、OpenAI/Ollama 服务雏形、工具注册表和 SQLite 记忆骨架。

MVP 的核心目标不是继续扩展 RAG、长期记忆或多 Agent，而是先完成一条真实闭环：

```text
QML 输入
  -> AgentViewModel
  -> AIAgent
  -> 自定义模型提供商
  -> 工具调用解析
  -> ToolRegistry 执行工具
  -> 工具结果回灌模型
  -> Trace UI 实时展示
  -> 最终回复
```

MVP 完成后，用户应能：

1. 在 QML 设置面板配置自定义模型提供商。
2. 使用 OpenAI-compatible provider 完成真实对话。
3. 使用 OpenAI-compatible provider 完成基础工具调用闭环。
4. 使用 Anthropic provider 完成 Claude Messages API 风格普通对话。
5. 在 Trace UI 中看到真实思考、工具调用、确认、工具结果和最终回复。

---

## 2. 当前状态基线

### 2.1 已完成

- 应用可构建，当前 `cmake --build build -j4` 基线通过。
- `main.cpp` 已注册 `Theme`、`ThemeManager`、`AgentVM` 到 QML。
- QML 主界面已有 Sidebar、TopBar、AgentChatPage、ContextPanel、InputConsole、ToolConfirmDialog。
- `AgentViewModel` 暴露了 QML 所需的状态、Trace、输入、确认、取消等属性和动作。
- `OpenAIService` 和 `OllamaService` 已有 `chatComplete()` 雏形。
- `ToolRegistry` 已注册 `read_file` 和 `write_file`。
- `SqliteMemory` 已有 messages、episodic、FTS5 表结构和基础读写。

### 2.2 主要缺口

- `AgentViewModel` 当前仍使用 `QTimer` 模拟流程，没有接入真实 `AIAgent`。
- `AIAgent` 收到 tool call 后只是标记成功，没有实际执行工具。
- 工具结果没有以 `role=tool` 或等价协议消息回灌给模型。
- 模型 provider 配置缺失，没有 QML 设置入口。
- OpenAI-compatible 与 Anthropic 的协议差异尚未抽象。
- 测试目录为空，缺少 Agent 主循环、工具、响应解析和配置持久化测试。

---

## 3. MVP 范围与非目标

### 3.1 MVP 范围

MVP 只做真实 Agent 闭环所需的最小能力：

- 自定义模型提供商配置。
- OpenAI-compatible 普通对话和工具调用。
- Anthropic 普通对话。
- `read_file` 自动执行。
- `write_file` 用户确认后执行。
- Trace UI 显示真实运行状态。
- 基础错误处理、取消和最大迭代保护。
- 最小单元测试与手工验收清单。

### 3.2 非目标

以下能力明确推迟到 MVP 之后：

- RAG 知识库管理 UI。
- 长期记忆自动总结与召回。
- 多模型混合路由。
- Claude 工具调用。
- 并发工具执行。
- Agent 审计日志持久化。
- SSE/流式 token 级渲染。
- 插件市场或外部工具扩展系统。

---

## 4. 阶段 1：模型配置与 Provider 抽象

### 4.1 目标

提供一个用户可操作的 QML 设置入口，让用户配置自定义模型服务，并让后端可以按 provider 类型选择不同 wire format。

### 4.2 Provider 类型

新增稳定枚举：

```cpp
enum class ProviderType {
    OpenAICompatible,
    Anthropic
};
```

### 4.3 配置结构

新增配置结构，字段最小化：

```cpp
struct AIProviderConfig {
    ProviderType providerType;
    QString displayName;
    QString baseUrl;
    QString apiKey;
    QString model;
};
```

字段含义：

| 字段 | 说明 |
|------|------|
| `providerType` | 协议类型，决定请求和响应解析方式 |
| `displayName` | UI 展示名 |
| `baseUrl` | API 基础地址 |
| `apiKey` | 鉴权密钥 |
| `model` | 模型名称 |

### 4.4 持久化策略

MVP 使用 `QSettings` 持久化，不引入数据库迁移。

建议 key：

```text
ai/providerType
ai/displayName
ai/baseUrl
ai/apiKey
ai/model
```

默认值：

```text
providerType = OpenAICompatible
displayName  = Custom OpenAI-Compatible
baseUrl      = http://localhost:11434/v1
apiKey       = ""
model        = ""
```

### 4.5 QML 设置面板

新增设置入口，可放在右侧 `ContextPanel` 或独立弹窗中。

必须包含：

- Provider 类型选择：`OpenAI Compatible` / `Anthropic`
- Display Name 输入框
- Base URL 输入框
- API Key 输入框，默认密码显示
- Model 输入框
- 保存按钮
- 测试连接按钮

保存后行为：

- 更新 `QSettings`。
- 重建或重新配置当前 `IAIService`。
- UI 给出成功或错误状态。

测试连接行为：

- OpenAI-compatible：发起最小 `chatComplete()` 请求，不带 tools。
- Anthropic：发起最小 Messages API 请求。
- 成功返回模型响应即通过。

---

## 5. 阶段 2：真实 Agent 主链路

### 5.1 目标

移除 `AgentViewModel` 的模拟执行路径，让 QML 操作真实 `AIAgent`。

### 5.2 ViewModel 职责

`AgentViewModel` 作为 QML 和 core 的桥接层，职责限定为：

- 暴露 UI 属性：状态、状态文本、Trace、最终回复、Token 用量。
- 接收 QML 动作：`startTask()`、`approveToolCall()`、`cancel()`。
- 转发 `AIAgent` 信号并转换成 QML 友好的 `QVariantList`。
- 不直接模拟 LLM、工具调用或业务流程。

### 5.3 AIAgent 依赖注入

应用启动时应创建并注入：

```text
AIProviderConfig
IAIService implementation
ToolRegistry
SqliteMemory
AIAgent
AgentViewModel
```

`AgentViewModel` 持有或引用 `AIAgent`，不在 QML 中直接操作 `AIAgent`。

### 5.4 Agent 状态流

MVP 状态流固定为：

```text
Idle
 -> Thinking
 -> ExecutingTool
 -> AwaitingToolConfirm
 -> Thinking
 -> Summarizing
 -> Completed
```

异常状态：

```text
Error
Cancelled
```

### 5.5 最大迭代保护

默认最大迭代次数为 10。

达到上限时：

- 停止继续调用模型。
- Trace 追加错误或终止节点。
- 状态变为 `Completed` 或 `Error`，MVP 推荐 `Error`。
- 最终回复提示：`Max iterations reached before final answer.`

---

## 6. 阶段 3：OpenAI-compatible 工具调用闭环

### 6.1 目标

OpenAI-compatible provider 支持 tools/function calling，模型可调用本地工具，工具执行结果会回灌给模型继续推理。

Anthropic provider 在 MVP 阶段只支持普通对话，不暴露 tools。

### 6.2 工具暴露规则

OpenAI-compatible 请求中暴露：

- `read_file`
- `write_file`

不暴露：

- dangerous 工具
- 未注册工具
- Anthropic provider 的工具 schema

### 6.3 工具执行规则

`read_file`：

- 安全等级：Safe
- 自动执行
- 失败时将错误作为 tool result 回灌模型

`write_file`：

- 安全等级：RequiresConfirm
- 进入 `AwaitingToolConfirm`
- QML 弹出确认框
- 用户允许后执行
- 用户拒绝后回灌拒绝结果，让模型收敛回复

### 6.4 OpenAI-compatible 消息回灌

模型返回 tool call 后，消息历史追加 assistant tool call 消息，再追加 tool result 消息。

最小语义：

```json
{
  "role": "tool",
  "tool_call_id": "call_xxx",
  "content": "tool output or error"
}
```

若工具执行失败，`content` 写入可读错误：

```text
Tool failed: <error>
```

若用户拒绝：

```text
Tool call rejected by user.
```

### 6.5 Trace 节点

真实 Agent 运行时至少产生这些节点：

| type | 场景 |
|------|------|
| `user_message` | 用户输入 |
| `thinking` | 调用模型中 |
| `tool_call` | 模型请求工具 |
| `tool_result` | 工具执行完成 |
| `confirmation` | 等待用户确认 |
| `summary` | 最终回复 |
| `error` | 失败或上限终止 |

每个节点最少包含：

```text
id
type
title
status
timestamp
args
result
```

---

## 7. 阶段 4：Anthropic 普通对话

### 7.1 目标

支持 Claude Messages API 风格普通对话，满足自定义 Claude provider 的基础聊天能力。

### 7.2 请求转换

Anthropic 实现需要将内部消息转换为 Messages API 风格。

MVP 只处理：

- `user`
- `assistant`
- system prompt 如后续需要，可映射到顶层 `system`

不处理：

- tool use
- tool result
- multimodal content
- streaming

### 7.3 响应解析

解析普通文本响应，输出统一 `LLMResponse`：

```cpp
LLMResponse {
    content;
    toolCalls = {};
    tokensUsed;
    success;
    error;
}
```

如果 Anthropic provider 收到工具调用需求：

- 不暴露 tools 给模型。
- Agent 视为普通对话。
- UI 不显示工具可用状态。

---

## 8. 错误处理与取消

### 8.1 网络错误

网络失败时：

- `IAIService` 返回失败响应或发出统一错误信号。
- `AIAgent` 进入 `Error`。
- Trace 追加 `error` 节点。
- UI 保留用户输入和已有 Trace。

### 8.2 配置错误

配置缺失时：

- 缺少 `baseUrl`：阻止保存或阻止测试连接。
- 缺少 `model`：阻止测试连接。
- 缺少 `apiKey`：允许保存；请求时按 provider 需求决定是否失败。

### 8.3 取消

用户点击取消：

- 停止当前网络请求。
- 清理 pending confirmation。
- 状态变为 `Cancelled` 或回到 `Idle`，MVP 推荐暴露 `Cancelled`。
- Trace 保留并追加取消节点，不清空。

---

## 9. 测试计划

### 9.1 单元测试

新增最小测试覆盖：

1. Provider config
   - 保存后可读取。
   - provider type 正确恢复。
   - 默认值可用。

2. OpenAI-compatible parser
   - 普通文本响应。
   - tool_calls 响应。
   - error 响应。
   - malformed JSON 不崩溃。

3. Anthropic parser
   - 普通文本响应。
   - error 响应。
   - 空 content 不崩溃。

4. ToolRegistry / Sandbox
   - `read_file` 成功。
   - `read_file` 文件不存在。
   - `write_file` 被 sandbox 拦截。
   - `write_file` 合法路径成功。

5. AIAgent
   - 无工具调用直接完成。
   - `read_file` 自动执行并回灌。
   - `write_file` 进入确认状态。
   - 用户允许后继续。
   - 用户拒绝后继续收敛。
   - 超过最大迭代停止。
   - cancel 中止请求。

### 9.2 手工验收

验收前先构建：

```powershell
cmake --build build -j4
```

手工场景：

1. 打开应用，进入设置面板，配置 OpenAI-compatible provider。
2. 点击测试连接，返回成功。
3. 发送普通消息，Trace 展示 thinking 和 summary。
4. 发送读取文件任务，Trace 展示 tool_call 和 tool_result。
5. 发送写文件任务，弹出确认框。
6. 点击拒绝，Agent 给出无法继续或替代回复。
7. 再次发送写文件任务，点击允许，文件写入成功并最终回复。
8. 切换到 Anthropic provider，普通对话成功。
9. Anthropic provider 下不显示或不启用工具调用能力。

---

## 10. 实施顺序

建议严格按以下顺序实施，避免 UI、Agent、provider 同时变化导致难以定位问题。

### Step 1：配置模型

- 新增 `AIProviderConfig` 和 `ProviderType`。
- 使用 `QSettings` 保存和读取。
- QML 增加设置面板。
- 增加测试连接动作。

验收标准：

- 配置可保存、重启后可恢复。
- OpenAI-compatible test connection 可运行。

### Step 2：接入真实 Agent

- `AgentViewModel` 移除模拟 timer 路径。
- `AgentViewModel` 桥接 `AIAgent` 信号。
- `main.cpp` 创建真实依赖并注入。

验收标准：

- 普通对话不再走模拟文本。
- Trace 来自真实 Agent 事件。

### Step 3：工具执行闭环

- `AIAgent` 执行 `ToolRegistry::find()`。
- Safe 工具自动执行。
- RequiresConfirm 工具等待 UI 确认。
- 工具结果回灌后继续下一轮模型调用。

验收标准：

- `read_file` 自动跑通。
- `write_file` 必须确认。
- 工具失败不会崩溃，模型能收到错误。

### Step 4：Anthropic 普通对话

- 新增 Anthropic provider 实现或在统一 service 内分支实现。
- 支持普通 Messages API 请求和响应解析。
- 不暴露 tools。

验收标准：

- Claude 风格 provider 可普通对话。
- 工具相关 UI 不误导用户。

### Step 5：测试补齐

- 启用 `BUILD_TESTS` 的最小测试集。
- 添加 fake LLM service 驱动 Agent 测试。
- CI 或本地脚本至少运行构建和单元测试。

验收标准：

- 核心闭环测试可重复运行。
- Agent 主循环关键分支有覆盖。

---

## 11. 风险与约束

### 11.1 协议差异

OpenAI-compatible 与 Anthropic 的消息格式、鉴权 header、响应结构不同。实现时不要把 Anthropic 强行塞进 OpenAI parser。

### 11.2 工具安全

工具调用能力必须默认保守：

- 未注册工具拒绝。
- dangerous 工具不暴露。
- 写操作必须确认。
- sandbox 校验失败时返回错误，不尝试绕过。

### 11.3 UI 状态一致性

取消、错误、确认等待期间，QML 按钮状态必须和 Agent 状态一致。

禁止在 QML 中重新模拟 Agent 流程。

### 11.4 测试优先级

如果时间有限，优先测试 Agent 主循环和响应解析。UI 自动化测试可以放到 MVP 后。

---

## 12. 完成定义

MVP 闭环完成必须同时满足：

- 应用可构建。
- 设置面板可配置自定义 provider。
- OpenAI-compatible 普通对话成功。
- OpenAI-compatible 工具调用闭环成功。
- Anthropic 普通对话成功。
- Trace UI 显示真实流程。
- 写工具必须确认。
- 取消和错误不会清空已有 Trace。
- 最小单元测试覆盖核心 Agent 分支。

完成后再进入下一阶段：

1. RAG 文档索引与检索。
2. 记忆压缩与召回。
3. Claude 工具调用。
4. 流式响应。
5. 审计日志和长期 Trace 持久化。
