# Token 消耗统计与计费细则方案

> **文档编号**: 16
> **状态**: 待实施
> **创建日期**: 2026-09-28
> **依赖文档**: docs/15（模型上下文长度按模型配置方案）
> **涉及模块**: AIAgent / TokenEstimator / IMemory / ChatSessionController / UsageStatsPage

---

## 1. 背景与现状分析

### 1.1 问题陈述

当前项目的 Token 统计存在以下缺口：

| 缺口 | 现状 | 影响 |
|------|------|------|
| **无分类统计** | `audit_log.tokens` 存储一个总数，不区分输入/输出/缓存 | 无法分析各模块消耗占比，无法精确成本核算 |
| **不区分输入/输出** | `m_lastTokensUsed` 取自 `response.tokensUsed` 或估算，是 prompt+completion 合计 | 输出 token 价格通常高于输入，合计无法准确计费 |
| **无缓存命中** | 不记录 cache hit/miss | 无法评估 prompt 缓存效果，无法计算缓存折扣成本 |
| **无来源归属** | 不区分 system prompt / 工具调用 / 对话 / MCP / 技能 | 无法定位消耗热点，无法针对性优化 |
| **llm_request 写 0** | 请求开始时 token 数未知，`llm_request` 行 tokens=0 | 输入 token 在请求时未记录，仅 task_complete 有总数 |
| **无成本核算** | 无模型定价表，无成本累计 | 用户无法感知费用消耗 |
| **无配额控制** | 无日/月配额限制 | 无法控制支出上限 |

### 1.2 当前数据流

```
AIAgent::onLLMResponse
  ├─ m_lastTokensUsed = response.tokensUsed > 0
  │                      ? response.tokensUsed          // API 真实值（合计）
  │                      : TokenEstimator::estimateTokens(m_messages)  // 估算
  └─ task_complete 时 logAudit(..., m_lastTokensUsed, model)

SqliteMemory::tokenStats
  └─ SELECT SUM(tokens) FROM audit_log WHERE event='task_complete'
     → {totalCalls, totalTokens, avgLatencyMs, byModel, byDay}

UsageStatsPage.qml
  └─ 展示 totalTokens（一个总数，无分类）
```

### 1.3 API 可用信息

主流 LLM API 在响应中返回 `usage` 对象，包含分类 token 计数：

```jsonc
// OpenAI / Anthropic 通用结构
{
  "usage": {
    "prompt_tokens": 1200,           // 输入 token（含 system + 历史 + tools schema）
    "completion_tokens": 350,        // 输出 token
    "total_tokens": 1550,            // 合计
    "prompt_tokens_details": {       // OpenAI 特有
      "cached_tokens": 800           // 缓存命中的输入 token
    },
    "cache_creation_input_tokens": 200,  // Anthropic 特有：缓存写入
    "cache_read_input_tokens": 800        // Anthropic 特有：缓存读取
  }
}
```

**关键发现**：API 已返回分类 token 数据，但当前代码仅取 `tokensUsed`（合计），丢弃了分类信息。

---

## 2. Token 消耗分类定义

### 2.1 二维分类体系

Token 消耗按 **方向维度** × **来源维度** 二维分类：

#### 方向维度（3 类）

| 方向 | 标识 | 说明 | 计费差异 |
|------|------|------|---------|
| **输入** | `input` | 发送给 LLM 的 prompt token（system + 历史 + tools schema + 当前用户消息） | 基础输入单价 |
| **输出** | `output` | LLM 生成的 completion token | 输出单价（通常 3-5x 输入） |
| **缓存命中** | `cache_hit` | 命中 prompt 缓存的输入 token（OpenAI cached_tokens / Anthropic cache_read） | 缓存单价（通常 0.1-0.5x 输入） |

#### 来源维度（7 类）

| 来源 | 标识 | 范围 | 采集时机 |
|------|------|------|---------|
| **系统提示词** | `system_prompt` | System Prompt 文本的 token | 构建请求时 |
| **对话消息** | `conversation` | 用户消息 + 助手回复（历史消息 + 当前轮） | 构建请求时 / 收到响应时 |
| **工具调用** | `tool_call` | 工具 schema（tools 定义）+ tool_calls 请求 + tool 返回结果 | 构建请求时 / 工具执行后 |
| **MCP** | `mcp` | MCP 协议交互内容（server → tool list / tool call → result） | MCP 交互时 |
| **技能** | `skill` | 技能调用过程中的 prompt 消耗（技能编排 + 技能内部 LLM 调用） | 技能执行时 |
| **图片附件** | `image` | 视觉图片的 token（按模型不同计价） | 构建请求时 |
| **其他** | `other` | 上述未覆盖的来源（如格式化开销、分隔符等） | 请求完成时补差 |

### 2.2 分类矩阵

每个 Token 计量是 (方向, 来源) 的组合，共 3 × 7 = 21 个计量单元：

```
                    input    output    cache_hit
system_prompt       ✓        —         ✓
conversation        ✓        ✓         ✓
tool_call           ✓        ✓         ✓
mcp                 ✓        ✓         —
skill               ✓        ✓         —
image               ✓        —         —
other               ✓        ✓         —
```

> **说明**：`—` 表示该组合理论上不存在（如 system_prompt 不产生 output；image 不产生 output/cache）。实际采集中仅记录非零单元。

### 2.3 来源归属规则

对于 API 返回的 `prompt_tokens`（输入合计），需按来源拆分。拆分规则：

| 来源 | 拆分方式 | 依据 |
|------|---------|------|
| `system_prompt` | `TokenEstimator::estimateTokens(systemPrompt)` | 系统提示词文本可直接估算 |
| `tool_call` (schema) | `TokenEstimator::estimateToolsSchema(toolsArray)` | 工具 schema 可直接估算 |
| `tool_call` (历史结果) | 历史消息中 role=tool 的 token 之和 | 历史工具返回结果 |
| `conversation` (历史) | 历史消息中 role=user/assistant 的 token 之和 | 历史对话消息 |
| `conversation` (当前轮) | 当前用户消息的 token | 当前轮输入 |
| `image` | `images.size() * perImageTokens(model)` | 图片按模型计价 |
| `mcp` | MCP 交互内容的 token | MCP 协议消息 |
| `skill` | 技能 prompt 的 token | 技能编排消息 |
| `other` | `prompt_tokens - 上述之和`（补差） | 格式化开销等 |

**优先使用 API 真实值**：当 API 返回 `prompt_tokens` 时，各来源的估算值按比例缩放至真实值，确保总和一致：

```
scaleFactor = prompt_tokens / sum(estimatedSources)
scaledSource[i] = estimatedSource[i] * scaleFactor
```

当 API 不返回分类数据（如 Ollama 本地模型）时，直接使用估算值。

---

## 3. 计量规则

### 3.1 单次调用记录

每次 LLM 请求完成时，记录一条 `token_usage` 明细：

```
TokenUsageRecord {
  // 标识
  requestId:    quint64      // 请求 ID（关联 audit_log）
  sessionId:    QString      // 会话 ID
  model:        QString      // 模型标识
  timestamp:    qint64       // 时间戳（毫秒）

  // 方向维度合计（来自 API 或估算）
  inputTokens:       int     // 输入 token 合计
  outputTokens:      int     // 输出 token 合计
  cacheHitTokens:    int     // 缓存命中 token 合计
  totalTokens:       int     // 总计 = input + output（cache_hit 已含在 input 中）

  // 来源维度拆分（input 方向）
  inputSystemPrompt:  int    // 输入·系统提示词
  inputConversation:  int    // 输入·对话消息
  inputToolCall:      int    // 输入·工具调用（schema + 历史结果）
  inputMcp:           int    // 输入·MCP
  inputSkill:         int    // 输入·技能
  inputImage:         int    // 输入·图片
  inputOther:         int    // 输入·其他

  // 来源维度拆分（output 方向）
  outputConversation: int   // 输出·对话消息（助手回复）
  outputToolCall:     int   // 输出·工具调用（LLM 生成的 tool_calls）
  outputMcp:          int   // 输出·MCP
  outputSkill:        int   // 输出·技能
  outputOther:        int   // 输出·其他

  // 缓存命中拆分（cache_hit 方向）
  cacheHitSystemPrompt: int // 缓存·系统提示词
  cacheHitConversation: int // 缓存·对话消息
  cacheHitToolCall:     int // 缓存·工具调用

  // 计费
  costCents:     qint64     // 折算费用（分），见 §6
  latencyMs:     qint64     // 延迟（毫秒）
}
```

### 3.2 累计消耗聚合

按不同维度聚合：

```
TokenStatsAggregate {
  // 时间范围
  rangeStart: qint64
  rangeEnd:   qint64

  // 方向维度累计
  totalInputTokens:     qint64
  totalOutputTokens:    qint64
  totalCacheHitTokens:  qint64
  totalTokens:          qint64   // = input + output

  // 来源维度累计（input 方向）
  totalInputSystemPrompt: qint64
  totalInputConversation: qint64
  totalInputToolCall:     qint64
  totalInputMcp:          qint64
  totalInputSkill:       qint64
  totalInputImage:       qint64
  totalInputOther:       qint64

  // 来源维度累计（output 方向）
  totalOutputConversation: qint64
  totalOutputToolCall:     qint64
  totalOutputMcp:          qint64
  totalOutputSkill:       qint64
  totalOutputOther:       qint64

  // 缓存命中累计
  totalCacheHitSystemPrompt: qint64
  totalCacheHitConversation: qint64
  totalCacheHitToolCall:     qint64

  // 调用统计
  totalCalls:       int
  avgLatencyMs:     int
  cacheHitRate:     double   // cacheHit / input

  // 费用累计
  totalCostCents:   qint64

  // 分组聚合
  byModel:  [{model, ...sameFields, calls}]
  byDay:    [{day, ...sameFields, calls}]
  bySource: [{source, inputTokens, outputTokens, cacheHitTokens, costCents}]
}
```

### 3.3 占比计算

各来源在总上下文中的占比：

```
占比(source, direction) = tokens(source, direction) / totalTokens(direction) × 100%

示例：
  输入占比(system_prompt) = inputSystemPrompt / totalInputTokens × 100%
  输出占比(conversation)  = outputConversation / totalOutputTokens × 100%
  缓存命中率              = totalCacheHitTokens / totalInputTokens × 100%
```

**上下文窗口占用率**（实时，非累计）：

```
占用率 = usedTokens / contextLimit × 100%

其中 usedTokens = TokenEstimator::estimateTokens(m_messages)
     contextLimit = resolveContextLimit()  // docs/15 按模型配置
```

---

## 4. 数据模型设计

### 4.1 新增表：token_usage

```sql
CREATE TABLE IF NOT EXISTS token_usage (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    request_id      INTEGER NOT NULL,       -- 关联 audit_log.request_id
    session_id      TEXT NOT NULL,
    model           TEXT NOT NULL,
    timestamp       INTEGER NOT NULL,       -- 毫秒

    -- 方向维度合计
    input_tokens        INTEGER NOT NULL DEFAULT 0,
    output_tokens       INTEGER NOT NULL DEFAULT 0,
    cache_hit_tokens    INTEGER NOT NULL DEFAULT 0,

    -- 来源拆分·输入
    input_system_prompt INTEGER NOT NULL DEFAULT 0,
    input_conversation  INTEGER NOT NULL DEFAULT 0,
    input_tool_call     INTEGER NOT NULL DEFAULT 0,
    input_mcp           INTEGER NOT NULL DEFAULT 0,
    input_skill         INTEGER NOT NULL DEFAULT 0,
    input_image         INTEGER NOT NULL DEFAULT 0,
    input_other         INTEGER NOT NULL DEFAULT 0,

    -- 来源拆分·输出
    output_conversation INTEGER NOT NULL DEFAULT 0,
    output_tool_call    INTEGER NOT NULL DEFAULT 0,
    output_mcp          INTEGER NOT NULL DEFAULT 0,
    output_skill        INTEGER NOT NULL DEFAULT 0,
    output_other        INTEGER NOT NULL DEFAULT 0,

    -- 来源拆分·缓存命中
    cache_hit_system_prompt INTEGER NOT NULL DEFAULT 0,
    cache_hit_conversation  INTEGER NOT NULL DEFAULT 0,
    cache_hit_tool_call     INTEGER NOT NULL DEFAULT 0,

    -- 计费与延迟
    cost_cents       INTEGER NOT NULL DEFAULT 0,
    latency_ms       INTEGER NOT NULL DEFAULT -1,

    workspace_id     TEXT NOT NULL DEFAULT 'local'
);

CREATE INDEX IF NOT EXISTS idx_token_usage_ts ON token_usage(timestamp);
CREATE INDEX IF NOT EXISTS idx_token_usage_model ON token_usage(model);
CREATE INDEX IF NOT EXISTS idx_token_usage_session ON token_usage(session_id);
```

### 4.2 新增表：model_pricing（模型定价表）

```sql
CREATE TABLE IF NOT EXISTS model_pricing (
    model               TEXT PRIMARY KEY,     -- 模型标识（如 "gpt-4o"）
    input_price_cents   INTEGER NOT NULL,     -- 每 1M token 输入价格（分）
    output_price_cents  INTEGER NOT NULL,     -- 每 1M token 输出价格（分）
    cache_hit_price_cents INTEGER NOT NULL DEFAULT 0,  -- 每 1M token 缓存命中价格（分）
    effective_date      INTEGER NOT NULL,     -- 生效日期（毫秒）
    notes               TEXT                  -- 备注
);
```

**预置定价数据**（2026 年公开价格，单位：分/1M token）：

| 模型 | 输入 | 输出 | 缓存命中 |
|------|------|------|---------|
| gpt-4o | 250 | 1000 | 125 |
| gpt-4o-mini | 15 | 60 | 7.5 → 8 |
| gpt-4-turbo | 1000 | 3000 | 500 |
| o3-mini | 110 | 440 | 55 |
| claude-sonnet-4-5 | 300 | 1500 | 30 |
| claude-3-5-haiku | 80 | 400 | 8 |
| claude-3-5-sonnet | 300 | 1500 | 30 |
| claude-3-opus | 1500 | 7500 | 150 |
| qwen2.5:7b (local) | 0 | 0 | 0 |
| llama3.1:8b (local) | 0 | 0 | 0 |
| deepseek-r1:7b (local) | 0 | 0 | 0 |

> 本地模型（Ollama）定价为 0，但仍有 token 统计（用于上下文管理）。

### 4.3 audit_log 兼容

`audit_log.tokens` 保持不变（向后兼容），`token_usage` 表是新增的明细表。`tokenStats()` 查询改为从 `token_usage` 聚合，`audit_log` 仅保留事件审计功能。

### 4.4 IMemory 接口扩展

```cpp
// 新增：记录单次 token 使用明细
virtual void logTokenUsage(const QVariantMap& record) {
    Q_UNUSED(record);
}

// 新增：聚合查询（扩展 tokenStats）
virtual QVariantMap tokenStatsDetailed(int recentDays = 30) {
    Q_UNUSED(recentDays);
    return {};
}

// 新增：按来源聚合
virtual QVariantList tokenStatsBySource(int recentDays = 30) {
    Q_UNUSED(recentDays);
    return {};
}

// 新增：成本核算
virtual QVariantMap costStats(int recentDays = 30) {
    Q_UNUSED(recentDays);
    return {};
}
```

---

## 5. 采集机制设计

### 5.1 LLMResponse 结构扩展

```cpp
struct LLMResponse {
    // ... 现有字段 ...

    // 分类 token 计数（来自 API usage 对象）
    int promptTokens{0};           // 输入 token 合计
    int completionTokens{0};       // 输出 token 合计
    int cachedTokens{0};           // 缓存命中 token（OpenAI）
    int cacheReadTokens{0};        // 缓存读取（Anthropic）
    int cacheCreationTokens{0};    // 缓存写入（Anthropic）
};
```

### 5.2 采集埋点

#### 5.2.1 请求构建时（输入 token 来源拆分）

在 `AIAgent::processIteration` 构建请求消息后、发送前：

```cpp
TokenUsageRecord rec;
rec.requestId = m_activeRequestId;
rec.sessionId = m_sessionId;
rec.model = currentModel();

// 来源拆分·输入（估算）
rec.inputSystemPrompt = TokenEstimator::estimateTokens(m_systemPrompt);
rec.inputConversation  = sumTokensByRole(m_messages, {"user", "assistant"});
rec.inputToolCall      = estimateReservedTokensForNextRequest()  // tools schema
                       + sumTokensByRole(m_messages, {"tool"});
rec.inputMcp           = sumMcpInteractionTokens();
rec.inputSkill         = sumSkillPromptTokens();
rec.inputImage         = countImages(m_messages) * perImageTokens(currentModel());
rec.inputOther         = 0;  // 补差在响应后计算
```

#### 5.2.2 响应接收时（输出 token + API 真实值校准）

在 `AIAgent::onLLMResponse`：

```cpp
// 方向维度合计（优先用 API 真实值）
rec.inputTokens  = response.promptTokens > 0
                   ? response.promptTokens
                   : TokenEstimator::estimateTokens(m_messages);
rec.outputTokens = response.completionTokens > 0
                   ? response.completionTokens
                   : TokenEstimator::estimateTokens(response.content);
rec.cacheHitTokens = response.cachedTokens + response.cacheReadTokens;

// 来源拆分·输出
rec.outputConversation = TokenEstimator::estimateTokens(response.content);
rec.outputToolCall     = sumToolCallTokens(response.toolCalls);
rec.outputMcp          = 0;  // MCP 响应在 MCP 层单独统计
rec.outputSkill        = 0;
rec.outputOther        = rec.outputTokens
                       - rec.outputConversation
                       - rec.outputToolCall;

// API 真实值校准：按比例缩放输入来源拆分
if (response.promptTokens > 0) {
    const int estimatedInput = rec.inputSystemPrompt + rec.inputConversation
                             + rec.inputToolCall + rec.inputMcp
                             + rec.inputSkill + rec.inputImage;
    if (estimatedInput > 0) {
        const double scale = static_cast<double>(response.promptTokens) / estimatedInput;
        rec.inputSystemPrompt = static_cast<int>(rec.inputSystemPrompt * scale);
        rec.inputConversation = static_cast<int>(rec.inputConversation * scale);
        rec.inputToolCall     = static_cast<int>(rec.inputToolCall * scale);
        rec.inputMcp          = static_cast<int>(rec.inputMcp * scale);
        rec.inputSkill        = static_cast<int>(rec.inputSkill * scale);
        rec.inputImage        = static_cast<int>(rec.inputImage * scale);
    }
    rec.inputOther = response.promptTokens
                   - rec.inputSystemPrompt - rec.inputConversation
                   - rec.inputToolCall - rec.inputMcp
                   - rec.inputSkill - rec.inputImage;
}

// 缓存命中拆分（按输入来源比例分摊）
if (rec.cacheHitTokens > 0 && rec.inputTokens > 0) {
    const double cacheRatio = static_cast<double>(rec.cacheHitTokens) / rec.inputTokens;
    rec.cacheHitSystemPrompt = static_cast<int>(rec.inputSystemPrompt * cacheRatio);
    rec.cacheHitConversation = static_cast<int>(rec.inputConversation * cacheRatio);
    rec.cacheHitToolCall     = static_cast<int>(rec.inputToolCall * cacheRatio);
}

// 计费
rec.costCents = calculateCost(rec.model, rec);

// 持久化
if (m_memory) {
    m_memory->logTokenUsage(rec.toVariantMap());
}
```

#### 5.2.3 工具执行时（tool_call 来源补充）

在 `ToolRegistry::execute` 完成后，工具返回结果的 token 计入 `inputToolCall`（因为工具结果作为下一轮请求的输入）：

```cpp
// 工具返回结果的 token 已在历史消息中（role=tool），
// 下一轮 processIteration 的 sumTokensByRole({"tool"}) 会自动统计。
// 此处无需额外埋点，但可记录工具调用延迟到 audit_log。
```

#### 5.2.4 MCP 交互时

在 MCP 消息收发处埋点：

```cpp
// MCP 请求 token
mcpInputTokens += TokenEstimator::estimateTokens(mcpRequestJson);
// MCP 响应 token
mcpOutputTokens += TokenEstimator::estimateTokens(mcpResponseJson);
// 累加到当前请求的 rec.inputMcp / rec.outputMcp
```

#### 5.2.5 技能调用时

在技能执行处埋点：

```cpp
// 技能 prompt token
skillInputTokens += TokenEstimator::estimateTokens(skillPrompt);
// 技能内部 LLM 调用的 token（如果有）
skillOutputTokens += response.completionTokens;
// 累加到当前请求的 rec.inputSkill / rec.outputSkill
```

### 5.3 估算精度说明

| 来源 | 估算方式 | 精度 | 校准 |
|------|---------|------|------|
| system_prompt | TokenEstimator 字符级 | 中 | API 真实值按比例缩放 |
| conversation | TokenEstimator 字符级 | 中 | 同上 |
| tool_call (schema) | TokenEstimator JSON 序列化 | 偏高 | 同上 |
| tool_call (结果) | TokenEstimator 字符级 | 中 | 同上 |
| mcp | TokenEstimator 字符级 | 中 | 无 API 校准（MCP 不经 LLM） |
| skill | TokenEstimator 字符级 | 中 | 同上 |
| image | 固定 perImageTokens | 低 | 无校准 |
| other | 补差 | N/A | 由 API 真实值决定 |

> **关键**：当 API 返回 `prompt_tokens` 时，所有输入来源估算值按比例缩放至真实值，确保 `sum(sources) == prompt_tokens`。当 API 不返回时（如 Ollama），使用纯估算值。

---

## 6. 成本核算

### 6.1 计费公式

```
costCents = ceil(
    inputTokens       × inputPriceCents       / 1_000_000
  + outputTokens      × outputPriceCents      / 1_000_000
  + cacheHitTokens    × cacheHitPriceCents    / 1_000_000
)
```

其中 `inputTokens` 不含 `cacheHitTokens`（缓存命中部分单独按缓存价格计费）：

```
billableInputTokens = inputTokens - cacheHitTokens
```

修正公式：

```
costCents = ceil(
    (inputTokens - cacheHitTokens) × inputPriceCents     / 1_000_000
  +  cacheHitTokens                × cacheHitPriceCents  / 1_000_000
  +  outputTokens                  × outputPriceCents    / 1_000_000
)
```

### 6.2 定价表管理

- **预置定价**：`model_pricing` 表在首次启动时插入公开价格（见 §4.2）
- **用户自定义**：用户可在设置中覆盖任意模型的定价
- **生效日期**：支持历史定价（价格变更后旧记录仍按旧价计算）
- **本地模型**：Ollama 模型定价为 0，但仍统计 token
- **未知模型**：使用同 provider 的默认定价或 0

### 6.3 成本聚合

```
CostStats {
  totalCostCents:      qint64     // 总费用
  inputCostCents:      qint64     // 输入费用
  outputCostCents:     qint64     // 输出费用
  cacheHitCostCents:   qint64     // 缓存费用
  byModel: [{model, costCents, calls, tokens}]
  byDay:   [{day, costCents, calls, tokens}]
  projectedMonthlyCents: qint64   // 预估月费用（按当前日均推算）
}
```

---

## 7. 配额控制

### 7.1 配额维度

| 维度 | 标识 | 说明 | 触发动作 |
|------|------|------|---------|
| **日 Token 配额** | `dailyTokenQuota` | 每日 token 消耗上限 | 超额时警告/拒绝新请求 |
| **日费用配额** | `dailyCostQuotaCents` | 每日费用上限 | 同上 |
| **月 Token 配额** | `monthlyTokenQuota` | 每月 token 消耗上限 | 同上 |
| **月费用配额** | `monthlyCostQuotaCents` | 每月费用上限 | 同上 |
| **单次请求上限** | `perRequestTokenCap` | 单次请求 token 上限 | 构建请求前检查 |

### 7.2 配额检查流程

```
AIAgent::startTask
  └─ checkQuota()
       ├─ 查询当日/当月已消耗 token/cost
       ├─ if (dailyUsed + estimated > dailyQuota)
       │    └─ emit quotaWarning(quotaType, used, quota)
       │       └─ if (strictMode) return false  // 拒绝请求
       └─ return true  // 允许请求
```

### 7.3 配额配置

```cpp
// SettingsService 新增
Q_PROPERTY(int dailyTokenQuota DEFAULT 0)       // 0 = 不限制
Q_PROPERTY(int dailyCostQuotaCents DEFAULT 0)   // 0 = 不限制
Q_PROPERTY(int monthlyTokenQuota DEFAULT 0)
Q_PROPERTY(int monthlyCostQuotaCents DEFAULT 0)
Q_PROPERTY(bool strictQuotaMode DEFAULT false)  // true = 超额拒绝, false = 仅警告
```

### 7.4 SettingsSchema 登记

新增设置键须在 `SettingsSchema` 登记（项目规范）：
- `dailyTokenQuota`, `dailyCostQuotaCents`, `monthlyTokenQuota`, `monthlyCostQuotaCents`, `strictQuotaMode`

---

## 8. UI 展示设计

### 8.1 UsageStatsPage 扩展

#### 8.1.1 概览卡片（新增分类）

```
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  任务次数   │  累计 Token  │  累计费用    │  平均延迟   │
│    128      │  1.2M        │  ¥12.50     │  2.3 s      │
└─────────────┴─────────────┴─────────────┴─────────────┘
```

#### 8.1.2 方向维度卡片（新增）

```
┌──────────────────────────────────────────┐
│  Token 构成                               │
│                                          │
│  输入:  850K (68%)  ████████████░░░░░░  │
│  输出:  350K (28%)  ██████░░░░░░░░░░░░  │
│  缓存:  50K  (4%)   █░░░░░░░░░░░░░░░░░  │
│                                          │
│  缓存命中率: 5.9%                         │
└──────────────────────────────────────────┘
```

#### 8.1.3 来源维度卡片（新增）

```
┌──────────────────────────────────────────┐
│  按来源拆分                               │
│                                          │
│  来源          输入      输出     占比    │
│  ──────────────────────────────────────  │
│  对话消息      420K     320K     59%    │
│  系统提示词    180K       0      14%    │
│  工具调用      150K      30K     14%    │
│  MCP            80K       0       6%    │
│  图片           20K       0       2%    │
│  技能            0       0        0%    │
│  其他            0       0        0%    │
└──────────────────────────────────────────┘
```

#### 8.1.4 费用明细（新增）

```
┌──────────────────────────────────────────┐
│  费用明细                                 │
│                                          │
│  模型          调用   Token    费用       │
│  ──────────────────────────────────────  │
│  gpt-4o        45    580K    ¥8.20      │
│  claude-sonnet 32    340K    ¥3.50      │
│  gpt-4o-mini   51    280K    ¥0.80      │
│  ──────────────────────────────────────  │
│  合计                 1.2M    ¥12.50     │
│                                          │
│  预估月费用: ¥375 (按当前日均推算)        │
└──────────────────────────────────────────┘
```

#### 8.1.5 配额状态（新增）

```
┌──────────────────────────────────────────┐
│  配额状态                                 │
│                                          │
│  日 Token:  45K / 100K     ██████░░░░   │
│  日费用:    ¥1.20 / ¥5.00  ██████░░░░   │
│  月 Token:  1.2M / 3M      ████████░░   │
│  月费用:    ¥12.50 / ¥100  ████░░░░░░   │
└──────────────────────────────────────────┘
```

### 8.2 实时上下文占用（ChatPanel 扩展）

在聊天面板底部状态栏显示实时上下文占用：

```
[████████████░░░░░░░░] 45K / 128K (35%)  |  gpt-4o  |  ¥0.02/轮
```

- 进度条：`usedTokens / contextLimit`
- 模型名：当前活跃模型
- 单轮费用：最近一次请求的 `costCents`

---

## 9. 统计粒度

### 9.1 粒度层次

| 层次 | 粒度 | 用途 | 存储 |
|------|------|------|------|
| **L1 单次请求** | 每次LLM调用 | 精确审计/调试 | token_usage 表 |
| **L2 单会话** | 每个会话 | 会话级消耗分析 | 聚合查询 |
| **L3 按模型** | 每个模型 | 模型对比/选型 | 聚合查询 |
| **L4 按天** | 每天 | 趋势分析 | 聚合查询 |
| **L5 按来源** | 每个来源 | 消耗热点定位 | 聚合查询 |
| **L6 全局累计** | 全部 | 总消耗/总费用 | 聚合查询 |

### 9.2 查询接口

```cpp
// IMemory 扩展
virtual QVariantMap tokenStatsDetailed(int recentDays = 30);
// 返回 L3+L4+L5+L6 聚合

virtual QVariantList tokenUsageBySession(const QString& sessionId);
// 返回 L2 明细

virtual QVariantList tokenUsageRecords(int recentDays = 30, int limit = 1000);
// 返回 L1 明细

virtual QVariantMap costStats(int recentDays = 30);
// 返回费用聚合 + 预估月费用

virtual QVariantMap quotaStatus();
// 返回当前配额使用情况
```

---

## 10. 优化建议

### 10.1 消耗优化

基于来源拆分的优化方向：

| 热点来源 | 优化策略 | 预期收益 |
|---------|---------|---------|
| `system_prompt` 占比高 | 启用 prompt 缓存（OpenAI/Anthropic 原生支持） | 缓存命中后输入费用降至 10-50% |
| `tool_call` (schema) 占比高 | 精简工具 schema 描述；按需加载工具（非全量注入） | 减少 schema token 30-60% |
| `conversation` (历史) 占比高 | 更激进的历史压缩策略；摘要替代完整历史 | 减少历史 token 40-70% |
| `image` 占比高 | 降低图片分辨率；按需加载图片 | 减少 image token 50-80% |
| `mcp` 占比高 | 缓存 MCP 响应；精简 MCP 协议消息 | 减少 MCP token 20-40% |

### 10.2 成本优化

| 策略 | 说明 | 实现方式 |
|------|------|---------|
| **模型路由** | 简单任务用小模型，复杂任务用大模型 | 按任务复杂度自动选模型 |
| **缓存优先** | 重复 prompt 命中缓存 | 确保 prompt 前缀稳定（system prompt 在前） |
| **批处理** | 多个独立请求合并 | 减少 prompt 重复发送 |
| **本地优先** | 隐私/成本敏感任务用 Ollama | 定价 0，仅消耗本地算力 |
| **配额预警** | 接近配额时降级到小模型 | strictQuotaMode=false + 自动降级 |

### 10.3 TokenEstimator 精度改进

当前字符级估算对结构化文本（代码/JSON）低估约 40-50%。改进方向：

| 方案 | 成本 | 精度 | 建议 |
|------|------|------|------|
| **安全余量** | 极低 | 保守 | `effectiveBudget = contextLimit × 0.9` |
| **按 provider 调参** | 低 | 中 | OpenAI 4字符/token, 代码 2.5字符/token |
| **真实 tokenizer** | 高 | 高 | 引入 tiktoken（OpenAI）/ sentencepiece（Ollama） |

> 建议先实施"安全余量"方案（成本最低），配合本方案的 API 真实值校准，可显著提升精度。

---

## 11. 实施路线图

### Phase 1：数据采集与存储（核心）

| 改动 | 文件 | 工作量 |
|------|------|--------|
| LLMResponse 扩展分类 token 字段 | IAIService.h | 0.25d |
| token_usage 表 + model_pricing 表 | SqliteMemory.cpp | 0.5d |
| IMemory 接口扩展 | IMemory.h | 0.25d |
| logTokenUsage 实现 | SqliteMemory.cpp | 0.5d |
| AIAgent 采集埋点（请求构建 + 响应接收） | AIAgent.cpp | 1d |
| API 响应解析分类 token | IAIService 各实现 | 0.5d |

### Phase 2：聚合查询与成本核算

| 改动 | 文件 | 工作量 |
|------|------|--------|
| tokenStatsDetailed 实现 | SqliteMemory.cpp | 0.5d |
| tokenStatsBySource 实现 | SqliteMemory.cpp | 0.25d |
| costStats 实现 | SqliteMemory.cpp | 0.5d |
| calculateCost 函数 | 新增 PricingCalculator | 0.25d |
| 预置定价数据 | SqliteMemory.cpp | 0.25d |

### Phase 3：UI 展示

| 改动 | 文件 | 工作量 |
|------|------|--------|
| UsageStatsPage 扩展（方向/来源/费用卡片） | UsageStatsPage.qml | 1d |
| ChatPanel 实时占用栏 | AgentChatPage.qml | 0.25d |
| ChatSessionController 信号转发 | ChatSessionController.h/.cpp | 0.25d |
| 翻译 | translations/*.ts | 0.25d |

### Phase 4：配额控制

| 改动 | 文件 | 工作量 |
|------|------|--------|
| 配额设置项 + SettingsSchema 登记 | SettingsService.h/.cpp, SettingsSchema | 0.5d |
| checkQuota 逻辑 | AIAgent.cpp | 0.5d |
| 配额状态 UI | UsageStatsPage.qml | 0.25d |
| 配额超限通知 | AgentChatPage.qml | 0.25d |

### Phase 5：测试与回归

| 改动 | 文件 | 工作量 |
|------|------|--------|
| token_usage 采集测试 | test_agent.cpp | 0.5d |
| 聚合查询测试 | test_settings.cpp | 0.5d |
| 成本核算测试 | test_settings.cpp | 0.25d |
| 配额控制测试 | test_agent.cpp | 0.25d |
| 全量回归 | ctest + qmllint | 0.25d |

**总工作量**：约 9.5 天

---

## 12. 接口变更摘要

### 12.1 IMemory 新增方法

```cpp
virtual void logTokenUsage(const QVariantMap& record);
virtual QVariantMap tokenStatsDetailed(int recentDays = 30);
virtual QVariantList tokenStatsBySource(int recentDays = 30);
virtual QVariantMap costStats(int recentDays = 30);
virtual QVariantMap quotaStatus();
```

### 12.2 LLMResponse 新增字段

```cpp
int promptTokens{0};
int completionTokens{0};
int cachedTokens{0};
int cacheReadTokens{0};
int cacheCreationTokens{0};
```

### 12.3 SettingsService 新增属性

```cpp
Q_PROPERTY(int dailyTokenQuota)
Q_PROPERTY(int dailyCostQuotaCents)
Q_PROPERTY(int monthlyTokenQuota)
Q_PROPERTY(int monthlyCostQuotaCents)
Q_PROPERTY(bool strictQuotaMode)
```

### 12.4 AIAgent 新增信号

```cpp
void quotaWarning(const QString& quotaType, qint64 used, qint64 quota);
void costChanged(qint64 costCents);
```

---

## 13. 验收标准

| 编号 | 验收项 | 验证方式 |
|------|--------|---------|
| AC-1 | token_usage 表正确创建 | SqliteMemory 初始化检查 |
| AC-2 | API 返回分类 token 时正确采集 | test_agent 用 FakeAIService 模拟 |
| AC-3 | API 不返回时用估算值 | test_agent Ollama 模拟 |
| AC-4 | 来源拆分总和 = 输入合计 | test_agent 断言 |
| AC-5 | API 真实值校准后总和一致 | test_agent 断言 |
| AC-6 | 成本核算正确 | test_settings 定价表测试 |
| AC-7 | 配额超限触发警告 | test_agent 模拟超额 |
| AC-8 | UsageStatsPage 展示分类统计 | 手动验证 |
| AC-9 | 现有 tokenStats 向后兼容 | ctest 回归 |
| AC-10 | qmllint_gate 通过 | Error 级为 0 |
| AC-11 | 翻译无 unfinished | lupdate 检查 |

---

## 14. 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| API 不返回分类 token（部分 Ollama 模型） | 高 | 中 | 降级为纯估算，注释标注 |
| 估算精度不足导致来源拆分偏差 | 中 | 中 | API 真实值按比例缩放校准 |
| 定价表过期 | 中 | 低 | 支持用户自定义覆盖 |
| token_usage 表膨胀 | 低 | 中 | 定期清理 + 按天聚合归档 |
| 配额检查增加请求延迟 | 低 | 低 | 配额查询走内存缓存（每 60s 刷新） |

---

## 附录 A：各 API usage 字段映射

| API | 输入 | 输出 | 缓存命中 |
|-----|------|------|---------|
| OpenAI | `usage.prompt_tokens` | `usage.completion_tokens` | `usage.prompt_tokens_details.cached_tokens` |
| Anthropic | `usage.input_tokens` | `usage.output_tokens` | `usage.cache_read_input_tokens` |
| Ollama | `prompt_eval_count` | `eval_count` | 不支持 |
| DeepSeek | `usage.prompt_tokens` | `usage.completion_tokens` | `usage.prompt_cache_hit_tokens` |

## 附录 B：perImageTokens 按模型计价

| 模型族 | 低分辨率 | 高分辨率 (1024×1024) | 说明 |
|--------|---------|---------------------|------|
| OpenAI GPT-4o | 85 | 765 (4 tiles × 170 + 85) | tile = 512×512 |
| Anthropic Claude | — | ~1600 (1.6 × √(1024×1024)) | 按像素计算 |
| Ollama | — | 1000 (固定) | 保守估计 |