# 48 - MCP Resources 接入方案

| | |
|---|---|
| **状态** | 已实施（2026-11-09）：test_mcp 37/37、test_viewmodel 全量通过 |
| **范围** | docs/26 H2 第二批：只接 `resources/list` + `resources/read`；templates/subscribe/sampling 不做（§5） |
| **前置** | docs/46（prompts 第一批）——协议层模式、fake server 桩、聚合/门控口径全部沿用 |

## 1. 结论先行

- MCP 服务器暴露的远程数据（数据库表、文档页、API 数据）此前无法进入对话。
  本批在协议层拉取资源清单（`resources/list`）、按 uri 读取内容（`resources/read`），
  经输入框 `+` 附件菜单的选择器进入消息——**资源语义是"数据"而非"指令模板"**，
  所以走附件管线而不是像 prompts 那样 fetch-and-insert 进草稿。
- 入口形态决策：`+` 附件菜单（用户选定）优于 `@` 引用扩展——后者需要改 token
  规则（`://` 区分本地/远程）、chip 校验、发送管线异步预取，改动面大且本地
  （同步读盘）与远程（异步 read）两种时序混在同一心智里。

## 2. 数据流

```
initialize result.capabilities.resources（McpClient::runHandshake 解析）
  → resources/list（nextCursor 分页循环，上限 kMaxResources=32/服务器）
  → McpClient::m_resources（resourcesChanged；notifications/resources/list_changed 重拉）
  → McpManager::listResources()（跨服务器聚合，description ≤1024）
  → InputConsole 的 McpResourceDialog（搜索过滤 + 服务器徽标 + 空态）
选中 → resourceActivated 信号 → InputConsole::requestMcpResource 登记 pending
  → resources/read（contents[].text 拼接；blob 记占位符；空内容按失败）
  → resourceRead 回程 → appendAttachments([{kind:"file", name, content, origin:"mcp"}])
  → 附件 chip 地球图标 → 发送组装/上下文预算全走现有附件管线（零改动）
```

## 3. 门控与上限

- **localOnlyMode**：`+` 菜单项隐藏（与 mcp__* 工具 / prompts 门控同层同向）；
  已取回的附件不受后续开关影响。
- **per-server ≤32**：`McpClient::kMaxResources`，分页拉满即停。
- **description ≤1024**：`McpManager::listResources` 聚合时截断（与 tools/prompts
  同一收口）。
- **512KB 拒绝**：读取结果 UTF-8 字节数 > 512KB 直接拒绝（`McpManager::readResource`
  内判定）——直接构造附件绕过了 C++ 读取侧的 `MaxTextAttachmentBytes`，这里补同一
  道闸；拒绝优于截断（截半的 JSON/表格对模型是误导）。
- 不可信数据声明：系统提示词的 MCP 条目语义覆盖 resources 内容（作为附件进上下文，
  与用户手动上传文件同待遇）。

## 4. 边界与错误处理

- 资源清单拉取失败不连坐连接（诊断进日志，卡片资源计 0）——同 prompts。
- `resourceRead` 回程以 pending uri 匹配，只接受本实例发起的请求（旧回程/竞态丢弃）。
- 失败给输入框瞬态提示（4s 自清，与 prompts 取回失败同一条通路）。
- 断开连接清空并发 resourcesChanged（卡片计数归零）。
- chip 的 `origin:"mcp"` 字段仅用于图标区分（地球 vs 导出箭头），发送组装只取
  kind/name/content，不下发、不影响管线；随草稿持久化透传。

## 5. 明确不做

- `resources/templates/list`（URI 模板填参——如 `postgres://{schema}/{table}`）：
  需要类似 prompts 参数表的交互，视服务器生态再评估
- `resources/subscribe` + `notifications/resources/updated`：订阅推送需先定
  生命周期（谁订阅、何时退订、变更后已入附件是否失效）
- sampling：反向请求，需另定安全边界（docs/26 原文）
- 资源一键入知识库（本地 RAG 覆盖远程数据）

## 6. 测试基建

- fake server：`MCP_FAKE_RESOURCES="uri|name|mime;..."`（`|` 分字段——uri 自带
  `:` `/`）、`MCP_FAKE_RESOURCE_BYTES` 控制回包大小（触发上限拒绝测试）；
  capabilities 按需声明。
- test_mcp：资源发现（uri/name/mimeType 缺省）、read 往返、per-server 上限、
  无能力不发请求。
- test_viewmodel：`mcpManagerAggregatesResources`——聚合归属、resourceCount、
  readResource 回程、超 512KB 拒绝、未知服务器失败、移除后清空。
