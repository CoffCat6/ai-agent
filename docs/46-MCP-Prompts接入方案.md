# 46 - MCP Prompts 接入方案

| | |
|---|---|
| **状态** | 已实施（2026-11-09）：test_mcp 33/33、test_viewmodel 77/77 通过 |
| **范围** | docs/26 H2 第一批：只接 `prompts/list` + `prompts/get`；`resources/*` 明确不做（视实际服务器生态再定） |

## 1. 结论先行

- 服务器声明的 `capabilities.prompts` 此前在 initialize 握手时被整体丢弃
  （`McpClient.cpp` 的 `Q_UNUSED(result)`）——本批解析它并按需拉取提示词模板，
  在输入框 `/` 补全中混入动态条目，选中即取回文本插入草稿。
- 交互形态：**fetch-and-insert**。取回的文本替换 `/token` 落入输入框，用户
  审阅/编辑后自行发送——发送管线（expandSlashCommand / resolveSkills）零改动，
  MCP 内容永远经用户之手进入上下文，不存在静默注入路径。

## 2. 数据流

```
initialize result.capabilities.prompts（McpClient::runHandshake 解析）
  → prompts/list（nextCursor 分页循环，上限 kMaxPrompts=32/服务器）
  → McpClient::m_prompts（promptsChanged；notifications/prompts/list_changed 重拉）
  → McpManager::listPrompts()（跨服务器聚合，description ≤1024）
  → InputConsole::updateSlash()（Settings.slashCommands 之后 concat 动态条目）
选中 → applyMcpPrompt：记录 token 区间
  → 无 required 参数：直接 McpManager::requestPrompt
  → 有 required 参数：McpPromptDialog 填参（动态字段表单）→ requestPrompt
  → prompts/get（messages[].content.text 拼接，注意与 tools/call 的
    顶层 content[] 结构不同，不能复用 toolHandler 拼接路径）
  → promptResolved 信号 → 替换 token（已被编辑则退化为光标处插入）
```

## 3. 门控与上限

- **localOnlyMode**：`/` 补全不出现 MCP 条目（与 `mcp__*` 工具门控同方向，
  保守默认）。门控在 QML 侧读 `Settings.localOnlyMode`。
- **per-server ≤32**：`McpClient::kMaxPrompts`，分页拉满即停（与
  `kMaxToolsPerServer` 同量纲）。prompts 不进模型 schema，无全局上限必要。
- **description ≤1024**：`McpManager::listPrompts` 聚合时截断，与
  `McpToolAdapter::MaxDescriptionChars` 同一收口。
- **插入文本 ≤32KiB**：超长截断加"已截断"尾注（与技能注入 32KiB 上限同量纲）。
- 不可信数据声明：系统提示词已有的 MCP 内容声明语义覆盖 prompts 返回内容
  （进输入框由用户审阅，不直接入上下文）。

## 4. 边界与错误处理

- 握手后 prompts/list 失败**不连坐连接**：工具照常可用，卡片提示词计 0，
  诊断进日志。
- prompts/get 空结果按失败处理（"prompt returned no text content"），
  不静默插入空串。
- 取回失败在输入框上沿给瞬态提示（4s 自清），token 保留在草稿中可重试。
- 断开连接清空 prompts 并发 promptsChanged（卡片计数归零）。
- `listPrompts()` 对未知服务器的 `requestPrompt` 同步失败回程（ok=false）。

## 5. 测试基建

- fake_mcp_server 新增 `MCP_FAKE_PROMPTS`（`;` 分隔模板、`,` 分隔 required
  参数，如 `alpha;beta:lang,tone`），capabilities 按需声明 prompts；
  prompts/get 回显参数值供往返断言。
- test_mcp：发现/参数解析、promptsGet 往返、per-server 上限、无能力不发请求。
- test_viewmodel：`mcpManagerAggregatesPrompts`（聚合归属、卡片计数、
  promptResolved 回程、未知服务器失败回程、移除后清空）。
