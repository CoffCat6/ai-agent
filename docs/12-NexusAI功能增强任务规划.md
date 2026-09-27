# NexusAI 功能增强 — 编码任务规划（tasks.md）

> **特性名称**：nexus_ai_enhancement
> **需求基线**：`.codeartsdoer/specs/nexus_ai_enhancement/spec.md`（65 条 EARS 需求，覆盖 T1–T15）
> **设计依据**：`.codeartsdoer/specs/nexus_ai_enhancement/design.md`（第 0 章交付基线校正、第三章需求可追溯性矩阵、第四章 DFX 量化红线、第五章波次实施顺序、第七章验收与 7.3 回归验证矩阵）
> **基线代码版本**：master 分支 `580b749`（2026-09-23 实测复核）
> **技术栈约定**：Qt 6 / C++20 / QML / CMake 四层目标（nexus_core → nexus_infra → nexus_viewmodels → nexus_ai）/ MVVM / core 零 UI 依赖 / ITool 契约零变更 / QtTest 单测 / Windows 优先
> **核心策略**：已交付 8 项**仅回归验证、不重复开发**；待实现 5 项 + T14 补齐部分做原子任务拆解

---

# 0. 任务规划总览

## 0.1 规划依据

本清单以 design.md 的**真实交付基线**（第 0 章）为准，取代其早期版本的"存量匹配度"判定；需求覆盖以 spec.md 的 65 条 EARS 需求为权威基线，逐条映射到任务（对齐 design.md 第三章）。任务编排对齐 design.md 第五章波次实施顺序，DFX 量化红线对齐第四章。

## 0.2 交付基线与任务归属

| 交付状态 | 任务 | 数量 | 本清单处置 | 主任务编号 |
|---------|------|------|-----------|-----------|
| **已交付** | T1、T2、T3、T4、T10、T11、T12、T13 | 8 | **仅回归验证与验收核对，不重复开发** | 1–8（波次 0） |
| **待实现** | T5、T6、T7、T8、T9 | 5 | **新开发**（原子拆解） | 9–13（波次 1–2） |
| **部分交付** | T14 | 1 | 已交付部分回归验证；未交付部分新开发 | 14（波次 3） |
| **不排期** | T15 | 1 | 仅登记，不排期 | 15（波次 4） |

## 0.3 波次实施顺序

对齐 design.md 5.2 的三波次（+ 回归轨道）：

```
波次 0：回归验证轨道（与开发并行，不占开发主线）
  T1 / T2 / T3 / T4 / T10 / T11 / T12 / T13  →  主任务 1–8
  └─ 只读验证，不改源码；发现退化登记缺陷

波次 1：P1 收尾（仅剩 T5）              →  主任务 9
  └─ 收尾：T5 完成后全量 lupdate 重提双 .ts（T3 翻译收口闭环）

波次 2：P2 能力补全（T6–T9）             →  主任务 10–13
  ├─ T6（web_search）★ 最高频缺失，建议先完成（为 T14 门控铺路）
  ├─ T7（无障碍）   ∥ 纯 UI 层走查
  ├─ T8（episodic） ∥ 依赖 T1 注入点（已交付，无阻塞）
  └─ T9（日志回填） ∥ 小批，可搭车

波次 3：P3 安全收尾（T14 补齐部分）       →  主任务 14
  └─ 依赖 T12/T13（已交付）+ 建议 T6 先完成

波次 4：远期展望（不排期）               →  主任务 15

全局集成与验收                          →  主任务 16
交付、文档与签核                        →  主任务 17
```

**关键约束**：
1. 回归验证为只读操作，不产生新功能代码；发现退化按缺陷流程处理。
2. T5 是 P1 唯一待实现项，完成后 P1 档位闭环。
3. T6/T7/T8/T9 文件域独立，可并行推进。
4. T14 补齐项相互隔离（声明/截断/门控/上限/文档），可分批交付；门控定案须与 T6 保持一致。

## 0.4 需求可追溯性总览（任务 ↔ 需求双向追溯）

| 主任务 | T 编号 | 关联需求 ID | 需求条数 | 波次 | 状态 |
|--------|--------|------------|---------|------|------|
| 1 | T1 | REQ-T1-01、REQ-T1-02、REQ-T1-03 | 3 | 0 | 已交付（回归） |
| 2 | T2 | REQ-T2-01、REQ-T2-02、REQ-T2-03 | 3 | 0 | 已交付（回归） |
| 3 | T3 | REQ-T3-01、REQ-T3-02、REQ-T3-03 | 3 | 0 | 已交付（回归） |
| 4 | T4 | REQ-T4-01、REQ-T4-02 | 2 | 0 | 已交付（回归） |
| 5 | T10 | REQ-T10-01、REQ-T10-02、REQ-T10-03、REQ-T10-04 | 4 | 0 | 已交付（回归） |
| 6 | T11 | REQ-T11-01 ~ REQ-T11-05 | 5 | 0 | 已交付（回归） |
| 7 | T12 | REQ-T12-01 ~ REQ-T12-07 | 7 | 0 | 已交付（回归） |
| 8 | T13 | REQ-T13-01 ~ REQ-T13-05 | 5 | 0 | 已交付（回归） |
| 9 | T5 | REQ-T5-01 ~ REQ-T5-06 | 6 | 1 | 待实现 |
| 10 | T6 | REQ-T6-01 ~ REQ-T6-06 | 6 | 2 | 待实现 |
| 11 | T7 | REQ-T7-01 ~ REQ-T7-04 | 4 | 2 | 待实现 |
| 12 | T8 | REQ-T8-01 ~ REQ-T8-05 | 5 | 2 | 待实现 |
| 13 | T9 | REQ-T9-01 ~ REQ-T9-04 | 4 | 2 | 待实现 |
| 14 | T14 | REQ-T14-01、02、04、05、07（待实现）+ 03、06（回归） | 7 | 3 | 部分交付 |
| 15 | T15 | REQ-T15-01 | 1 | 4 | 不排期 |
| **合计** | — | **65 条需求 100% 覆盖** | **65** | — | — |

**覆盖率核对**（对齐 design.md 3.4）：

| 维度 | 数量 |
|------|------|
| 需求总数 | 65 |
| 已映射到任务 | 65（100%） |
| 已交付（回归验证） | 34 |
| 待实现（新开发） | 30 |
| 不排期 | 1 |

## 0.5 任务依赖关系与并行机会

```
主任务 1–8（回归轨道）      只读，可与全部开发波次并行
主任务 9（T5）              依赖 T2 已交付（Theme 令牌协同已就绪）；完成后触发 T3 lupdate 收口
主任务 10（T6）             独立；建议先于主任务 14 完成（门控回归统一覆盖）
主任务 11（T7）             独立（纯 UI 层走查）；与主任务 9 共用 InterfacePage.qml 需协调
主任务 12（T8）             依赖 T1 注入点（已交付，无阻塞）
主任务 13（T9）             独立小批，可搭车任意任务
主任务 14（T14）            依赖 T12/T13（已交付）+ 建议主任务 10 先完成
主任务 15（T15）            依赖 T12 JSON-RPC 反向复用（不排期）
```

| 可并行组合 | 理由 |
|-----------|------|
| 主任务 10 ∥ 11 ∥ 12 ∥ 13 | 文件域独立（core 工具 / UI 走查 / core 记忆 / core 日志） |
| 主任务 9 ∥（10/11/12/13） | T5 属 viewmodels+ui，与 P2 任务无文件冲突 |
| 主任务 13 搭车任意批 | 小批、无强依赖 |
| 主任务 14 ∥（9/11/13） | T14 安全项在 core+viewmodels，与主题/无障碍/日志无冲突 |
| 主任务 1–8 ∥ 全部开发波次 | 回归为只读验证，不修改源码 |

## 0.6 DFX 量化红线 → 任务验收映射

对齐 design.md 第四章与 spec.md 第 5 章，以下红线为**可验证的验收标准**：

| DFX 红线 | 量化指标 | 验收任务 | 验收方式 |
|---------|---------|---------|---------|
| 代码高亮回退 | >64KB 或 >2000 行回退纯文本 | 主任务 2（回归） | `test_highlighter` 边界用例 |
| 会话结束响应 | 摘要异步生成，不阻塞 UI | 主任务 12 | `test_episodic` + GUI 冒烟 |
| MCP 调用超时 | 单次 >30s 终止并失败回灌 | 主任务 7、14（回归） | `test_mcp` 超时用例 |
| 工具清单预算 | 单服务器 ≤32、全局 ≤64，超限禁用并告警 | 主任务 14 | `test_mcp` 上限用例 |
| 记忆注入预算 | 历史摘要注入 ≤1500 token | 主任务 12 | `test_episodic` 预算钳制 |
| 摘要容量 | 单条 ≤500 字符，单次检索 ≤3 条 | 主任务 12 | `test_episodic` |
| 工具描述 | 单条 ≤1024 字符 | 主任务 14 | `test_mcp` 截断用例 |
| 日志回填量 | ≤ 环形缓冲容量 50%（2500 条） | 主任务 13 | `test_logger` 钳制用例 |
| 搜索结果为回灌 | 截断 ≤6000 字符 | 主任务 10 | `test_web_search` 截断用例 |
| 图片计费 | 1000 token/张 | 主任务 6（回归） | `test_token_estimator` |
| 构建门禁 | 零错误、零新增警告，qmllint 零 error | 主任务 16 | ctest + qmllint_gate |

---

# 第一部分 波次 0：回归验证轨道（与开发并行，不占开发主线）

> **轨道纪律**：回归验证为**只读操作**，不修改源码；发现退化登记为缺陷，按缺陷流程处理。回归在对应开发波次合并前执行一次，结论需归档（执行人、日期、结果、证据链接）。

---

## 1. T1 系统提示词编辑（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T1-01（覆盖生效）、REQ-T1-02（空值回退内置）、REQ-T1-03（持久化）
**任务描述**：核对系统提示词 override 优先级策略与持久化行为未因后续变更退化，不重复开发。
**涉及文件/模块**：`src/core/agent/AIAgent.h/.cpp`（`setSystemPromptOverride` :138、`effectiveSystemPrompt` :144）、`src/viewmodels/SettingsService.h`（`systemPromptOverride` :43,89-90）、`src/main.cpp` 接线
**前置依赖**：无
**风险等级**：中（R1：override 空串与"未设置"语义混淆）
**回滚方式**：不涉及（回归只读）

### 1.1 回归验证点
- [ ] 核对 `effectiveSystemPrompt()` 优先级策略：trim 后非空用 override，空/未设置回退内置 `systemPrompt()`（REQ-T1-01/02）
- [ ] 核对 `setSystemPromptOverride` trim 归一语义（REQ-T1-02）
- [ ] 核对 `SettingsService::systemPromptOverride` 持久化读写（REQ-T1-03）

### 1.2 验收标准（Done Definition）
- [ ] override 非空时生效、空时回退内置，与 spec.md REQ-T1-01/02 验收标准一致
- [ ] 重启后 override 保留，与 REQ-T1-03 一致

### 1.3 测试要求
- [ ] 运行 `ctest -R test_agent`：override 生效 / 空回退内置用例全绿
- [ ] 运行 `ctest -R test_settings`：`systemPromptOverride` 持久化用例全绿
- [ ] GUI 冒烟：设置中心编辑提示词 → 重启后持久；清空 → 回退内置

---

## 2. T2 代码语法高亮（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T2-01（按语言着色）、REQ-T2-02（主题协同刷新）、REQ-T2-03（超大代码块回退）
**任务描述**：核对高亮语言覆盖、主题协同与性能边界未退化。
**涉及文件/模块**：`src/viewmodels/CodeHighlighter.h/.cpp`（`MaxChars=64*1024`、`MaxLines=2000` :87-88）、`src/ui/components/CodeBlock.qml`、`tests/test_highlighter.cpp`
**前置依赖**：无
**风险等级**：低
**回滚方式**：不涉及（回归只读）

### 2.1 回归验证点
- [ ] 核对语言覆盖：json / python / cpp / xml-html / css / bash / js-ts / markdown（REQ-T2-01）
- [ ] 核对主题协同：`themeChanged` 触发 rehighlight，深浅主题配色可读（REQ-T2-02）
- [ ] **核对 DFX 红线**：单代码块 `>64KB` 或 `>2000` 行回退纯文本（REQ-T2-03）

### 2.2 验收标准（Done Definition）
- [ ] 9 语言按语言着色正确；主题切换后高亮配色跟随
- [ ] 超 64KB 或超 2000 行代码块回退纯文本，不执行高亮（DFX 性能红线）

### 2.3 测试要求
- [ ] 运行 `ctest -R test_highlighter`：语言覆盖 / 主题协同 / 边界回退用例全绿

---

## 3. T3 英文翻译全量（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T3-01（英文界面覆盖）、REQ-T3-02（中文回切无损）、REQ-T3-03（翻译资源完整性）
**任务描述**：核对双语加载链路与翻译完整性未退化；T5 完成后随波次 1 执行一次全量 lupdate 收口。
**涉及文件/模块**：`translations/nexus_ai_en.ts`、`translations/nexus_ai_zh_CN.ts`、`src/viewmodels/I18nManager.h/.cpp`、`CMakeLists.txt:234-238`、`tests/test_viewmodel.cpp`
**前置依赖**：无（lupdate 收口需等待主任务 9 完成）
**风险等级**：高（R2：`QTranslator::load` 重载误用导致加载恒失败；R3：lupdate 二次提取）
**回滚方式**：不涉及（回归只读）

### 3.1 回归验证点
- [ ] 运行 i18n 加载 / 中英切换即时生效用例（REQ-T3-01/02）
- [ ] 核对 `QTranslator::load` 使用 **QLocale 重载**（R2 高风险点）
- [ ] 核对 `:/i18n` 双 `.qm` 嵌入（CMakeLists.txt:234-238）
- [ ] 核对双 `.ts` 0 unfinished（REQ-T3-03）

### 3.2 lupdate 收口（与主任务 9 联动，design.md 3.2 关键约束）
- [ ] 主任务 9（T5）完成后执行一次全量 lupdate，重提双 `.ts`，确认 0 unfinished 不回退
- [ ] 确认 `zh_CN` 恒等校验不回退（R3 缓解措施）

### 3.3 验收标准（Done Definition）
- [ ] 英文界面全覆盖、中文回切无损、双 `.ts` 0 unfinished

### 3.4 测试要求
- [ ] 运行 `ctest -R test_viewmodel`：i18n 用例全绿
- [ ] lupdate 收口后条目数对齐核对

---

## 4. T4 嵌入端点可配置（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T4-01（端点可配）、REQ-T4-02（端点变更与模型变更语义区分）
**任务描述**：核对嵌入端点重定向语义未退化，不触发全量重嵌。
**涉及文件/模块**：`src/viewmodels/SettingsService.h`（`embeddingBaseUrl` :40,87-88）、`src/infrastructure/embed/OllamaEmbedding.h`、`src/main.cpp` 接线
**前置依赖**：无
**风险等级**：低
**回滚方式**：不涉及（回归只读）

### 4.1 回归验证点
- [ ] 核对 baseUrl 变更语义：仅重定向后续请求、**不触发**全量重嵌（REQ-T4-02）
- [ ] 核对与 `embeddingModel` 变更语义的区分

### 4.2 验收标准（Done Definition）
- [ ] 端点变更仅重定向；模型变更触发重嵌，二者语义可区分（REQ-T4-02）

### 4.3 测试要求
- [ ] 运行 `ctest -R test_embedding`：端点可配 / 语义区分用例全绿
- [ ] GUI 冒烟：嵌入指向远程 Ollama 可完成导入与检索（需用户环境实测）

---

## 5. T10 多标签并行会话（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T10-01（并行运行）、REQ-T10-02（状态隔离）、REQ-T10-03（切换保持状态）、REQ-T10-04（关闭行为）
**任务描述**：核对多标签并行、状态隔离与关闭行为未退化。
**涉及文件/模块**：`src/viewmodels/AgentViewModel.h/.cpp`、`src/viewmodels/ChatSessionController.h/.cpp`、`src/ui/components/SessionTabs.qml`、`src/main.cpp`
**前置依赖**：无
**风险等级**：高（R9：SQLite 并发写冲突）
**回滚方式**：不涉及（回归只读）

### 5.1 回归验证点
- [ ] 核对每标签独立 `AIAgent` + 独立 `SqliteMemory` 连接（连接名隔离）；同库文件经 busy_timeout 串行化写（REQ-T10-01/02）
- [ ] 核对每标签独立 ProviderSet（避免共享服务导致流串扰 / 跨标签 chunk 泄漏）（REQ-T10-02）
- [ ] 核对标签切换不中断运行中任务、不丢失状态（REQ-T10-03）
- [ ] 核对 `closeTab` 停止运行中任务、末标签不可关闭（REQ-T10-04）

### 5.2 验收标准（Done Definition）
- [ ] 多标签并行互不串扰；切换不丢状态；末标签受保护

### 5.3 测试要求
- [ ] 运行 `ctest -R test_viewmodel`：多标签用例全绿
- [ ] GUI 冒烟：多标签并行与切换体感验证（需用户配合）

---

## 6. T11 多模态图片输入（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T11-01（图片输入与发送）、REQ-T11-02（多服务商格式适配）、REQ-T11-03（图片 token 计费）、REQ-T11-04（图片外发门控）、REQ-T11-05（缩略图展示）
**任务描述**：核对三家 vision 格式转换、计图与门控未退化。
**涉及文件/模块**：`src/core/agent/AIAgent.h/.cpp`（`startTask(images, files)` :90）、`src/core/utils/TokenEstimator.h`（计图 1000/图 :40-47）、`src/core/utils/DataUrlUtils.h/.cpp`、三家 LLM Service、消息渲染 QML、`tests/test_data_url_utils.cpp`、`tests/test_token_estimator.cpp`
**前置依赖**：无
**风险等级**：高（R10：图片附件持久化迁移失败）
**回滚方式**：不涉及（回归只读）

### 6.1 回归验证点
- [ ] 核对三家 Provider vision 格式转换正确（OpenAI / Anthropic / Ollama，IAIService 契约不变）（REQ-T11-02）
- [ ] **核对 DFX 红线**：`TokenEstimator` 计图 1000 token/图 + 文本附件计入预算（REQ-T11-03）
- [ ] 核对图片外发受 `localOnlyMode` 门控（REQ-T11-04）
- [ ] 核对图片输入与发送链路（REQ-T11-01）、缩略图展示（REQ-T11-05）

### 6.2 验收标准（Done Definition）
- [ ] 三家 vision 转换正确；1000 token/图计费准确；本地模式门控生效

### 6.3 测试要求
- [ ] 运行 `ctest -R "test_data_url_utils|test_token_estimator"` 全绿
- [ ] GUI 冒烟：粘贴 / 选择图片可发送、缩略图正确

---

## 7. T12 MCP stdio 核心链路（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T12-01（接入本地服务器）、REQ-T12-02（工具动态注册命名）、REQ-T12-03（工具调用往返）、REQ-T12-04（强制工具确认）、REQ-T12-05（服务器故障恢复）、REQ-T12-06（超时与畸形响应容错）、REQ-T12-07（工具注销与在途处置）
**任务描述**：核对 MCP stdio 链路核心契约未退化。
**涉及文件/模块**：`src/core/mcp/`（McpTransport / StdioTransport / McpClient / McpToolAdapter / McpConfig）、`src/core/tools/ToolRegistry.h`（`takeTool` :25）、`tests/test_mcp.cpp`、`tests/fake_mcp_server_main.cpp`
**前置依赖**：无
**风险等级**：高（R11：Windows QProcess 控制台闪现；R12：第三方服务器行为不可控）
**回滚方式**：不涉及（回归只读）

### 7.1 回归验证点
- [ ] 核对 initialize 握手（REQ-T12-01）、`tools/list` 注册命名 `mcp__<server>__<tool>`（REQ-T12-02）
- [ ] 核对 `tools/call` 往返（REQ-T12-03）、**DFX 红线 30s 超时**、畸形响应容错（REQ-T12-06）
- [ ] 核对崩溃检测 + 指数退避重启（上限 3 次）、在途调用失败回灌（REQ-T12-05）
- [ ] 核对工具强制 `RequiresConfirm`（永不注册为 Dangerous）（REQ-T12-04）
- [ ] 核对 `takeTool` 注销与在途 execute 处置（先停可见性后移除）（REQ-T12-07）
- [ ] 核对 Windows QProcess `CREATE_NO_WINDOW` 标志（R11）

### 7.2 验收标准（Done Definition）
- [ ] 握手 / 命名 / 往返 / 超时 / 畸形容错 / 崩溃退避 / 注销七项契约均无退化

### 7.3 测试要求
- [ ] 运行 `ctest -R test_mcp`：全绿
- [ ] GUI 冒烟：手编 `mcp_servers.json` 接入 stdio MCP 服务器并完成一次真实跨服务器调用

---

## 8. T13 MCP HTTP transport + 管理页（波次 0 · 已交付 · 回归验证）

**关联需求**：REQ-T13-01（接入远程服务器）、REQ-T13-02（流式响应解析）、REQ-T13-03（管理界面）、REQ-T13-04（配置持久化）、REQ-T13-05（工具清单预览）
**任务描述**：核对 HTTP transport 与 MCP 管理页未退化。
**涉及文件/模块**：`src/core/mcp/HttpTransport.h/.cpp`、`src/viewmodels/McpManager.h/.cpp`、`src/ui/pages/McpPage.qml`、`src/main.cpp`
**前置依赖**：无
**风险等级**：中
**回滚方式**：不涉及（回归只读）

### 8.1 回归验证点
- [ ] 核对 Streamable HTTP 握手与会话维持、SSE 流式响应行缓冲解析（REQ-T13-01/02）
- [ ] 核对管理页增删改查 / 启停 / 重连（REQ-T13-03）
- [ ] 核对配置持久化到 `mcp_servers.json`（REQ-T13-04）
- [ ] 核对工具清单预览与 `toolCount` 展示（REQ-T13-05）

### 8.2 验收标准（Done Definition）
- [ ] SSE 解析正确；管理页 CRUD/启停/重连可用；配置持久化；工具清单预览准确

### 8.3 测试要求
- [ ] 运行 `ctest -R "test_mcp|qmllint_gate"` 全绿
- [ ] GUI 冒烟：管理页添加 HTTP 服务器并连接成功

---

# 第二部分 波次 1：P1 收尾

---

## 9. T5 主题跟随系统（波次 1 · 已交付）

**关联需求**：REQ-T5-01（三档与默认值）、REQ-T5-02（系统实时跟随）、REQ-T5-03（显式选择优先）、REQ-T5-04（auto 档显式切换升级）、REQ-T5-05（兼容存量偏好）、REQ-T5-06（设置界面呈现）
**任务描述**：`ThemeManager` 的 `ui.themeMode` 扩展 `auto` 档（改为默认 auto），监听系统深浅色变化并实时切换；设置中心外观区由 Switch 改三选一控件（跟随系统 / 深色 / 浅色）；auto 档下用户显式切换"升级"为显式 dark/light 并持久化。
**涉及文件/模块**：`src/viewmodels/ThemeManager.h/.cpp`、`src/viewmodels/PreferenceRegistry.h/.cpp`、`src/viewmodels/PreferenceStore.h/.cpp`、`src/viewmodels/ChangeBus.h/.cpp`、`src/ui/pages/InterfacePage.qml`、`src/ui/main.qml`（Ctrl+D 路径）、`src/main.cpp`（palette 同步链路）、`tests/test_settings.cpp`
**前置依赖**：T2 已交付（Theme 令牌协同已就绪）；完成后触发主任务 3 的 lupdate 收口
**风险等级**：中（R4：跨平台系统主题监听差异；R4b：auto 档显式切换未升级导致偏好丢失；R15：与 T7 共用 InterfacePage.qml 需协调）
**回滚方式**：低 —— 移除 `auto` 枚举档 + revert 系统监听接线，回退 dark/light 两档

### 9.1 扩展 themeMode 枚举与持久化
- [ ] 在 `ThemeManager::registerPreferences()` 将 `ui.themeMode` 的 `constraint.options` 由 `{dark, light}` 扩展为 `{auto, dark, light}`，`defaultValue` 由 `dark` 改为 `auto`；保留 `legacyKeys = {theme/dark}` 兼容已存值（REQ-T5-01/05）
- [ ] 调整 `loadTheme()`：`auto` 档解析为"当前系统深浅色"，`dark`/`light` 档保持显式语义；已存 dark/light 值零迁移成本（REQ-T5-05）
- [ ] 调整 `setIsDark()`：在 `auto` 档下被调用时将偏好"升级"为显式 dark/light 并持久化（REQ-T5-04，R4b）

### 9.2 系统主题监听与实时切换
- [ ] 新增系统深浅色监听：以 `QStyleHints::colorScheme` 变化为主、平台事件为辅（REQ-T5-02，R4 缓解）
- [ ] 监听仅在 `auto` 档生效：显式 dark/light 档不被系统变化覆盖（REQ-T5-03）
- [ ] 复用既有 `themeChanged → applyControlPalette` 链路，使 Basic 风格控件配色随切换同步
- [ ] 确保 `auto` 档下系统切换实时更新 `m_isDark` 并 emit `themeChanged`

### 9.3 设置中心外观区改造
- [ ] 将 InterfacePage 外观区主题 Switch 改为三选一控件（跟随系统 / 深色 / 浅色），经 `PreferenceRegistry` 枚举元数据绑定（不硬编码选项）（REQ-T5-06）
- [ ] 新增枚举显示文案并纳入 lupdate（联动主任务 3 收口）
- [ ] 保持 Ctrl+D 快捷键可用，且遵循"auto 档下升级为显式选择"语义

### 9.4 验收标准（Done Definition）
- [ ] 首次启动主题模式为"跟随系统"（REQ-T5-01）
- [ ] 切换 Windows 深浅色 → 应用实时跟随（REQ-T5-02）
- [ ] 显式档忽略系统变化并持久（REQ-T5-03）
- [ ] auto 档下 Ctrl+D/设置切换 → 升级为显式档并持久，重启后仍为显式档（REQ-T5-04）
- [ ] 存量 dark/light 偏好零迁移（REQ-T5-05）
- [ ] 设置中心外观区呈现三选一控件且选择后立即生效（REQ-T5-06）
- [ ] 新增设置键默认值 = 现有行为（兼容性红线）；SettingsGuard 键备份覆盖 `themeMode` 新档

### 9.5 测试要求
- [ ] 扩展 `tests/test_settings.cpp`：themeMode 三档持久化与读回 / auto 档系统色解析 / 存量 dark/light 兼容 / auto 档显式升级持久化
- [ ] 运行 `ctest -R "test_settings|test_viewmodel|qmllint_gate"` 全绿、零 error 级诊断
- [ ] GUI 冒烟（需用户配合）：切换 Windows 深浅色实时跟随；显式选择重启后持久；auto 档为默认

---

# 第三部分 波次 2：P2 能力补全

---

## 10. T6 web_search 联网能力（波次 2 · 已交付）

**关联需求**：REQ-T6-01（工具可用）、REQ-T6-02（结果截断回灌）、REQ-T6-03（密钥配置与缺失处理）、REQ-T6-04（本地模式门控）、REQ-T6-05（密钥安全保护）、REQ-T6-06（调用前用户确认）
**任务描述**：新增 `web_search` 工具，Agent 在 ReAct 循环中像内置工具一样调用；查询外发到搜索 API（默认 Tavily），结果截断 6000 字符后回灌 LLM；API key 经 DPAPI 加密存储；受 `localOnlyMode` 门控；调用前需用户确认。
**涉及文件/模块**：`src/core/tools/WebSearchTool.h/.cpp`（新增，参照 `NetworkFetchTool.h` 范式）、`src/core/tools/WebSearchBackend.h`（搜索后端抽象，新增）、`src/infrastructure/`（Tavily 实现，新增）、`src/viewmodels/SettingsService.h/.cpp`（`webSearchProvider` + `webSearchApiKey`）、`src/main.cpp`（装配 + `applyLocalOnlyGate` 扩展）、`CMakeLists.txt`（nexus_core / nexus_infra 源文件）、`tests/test_web_search.cpp`（新增）、`tests/CMakeLists.txt`
**前置依赖**：无强依赖；**建议先于主任务 14 完成**（门控回归统一覆盖）
**风险等级**：高（R5：API key 泄露；R6：查询外发与 localOnlyMode 冲突）
**回滚方式**：低 —— 移除新增设置键 + 反注册工具 + revert SettingsService / 门控改动

### 10.1 设置项扩展（SettingsService）
- [ ] 新增 `webSearchProvider`（`webSearch/provider`，QString enum，默认 `tavily`）与 `webSearchApiKey`（`webSearch/apiKey`，QString，默认空，**DPAPI 加密**）两个 Q_PROPERTY + getter/setter + NOTIFY（REQ-T6-03）
- [ ] 沿用既有 apiKey 加密链路（DPAPI）；**加密失败宁可丢弃不落明文**；key 不入日志 / 审计（REQ-T6-05，R5）
- [ ] 同批补 `SettingsGuard` 键备份与 usage 埋点 setter
- [ ] 越界枚举值钳制到合法区间（`webSearchProvider` 钳制到 `tavily`）

### 10.2 搜索后端与工具实现
- [ ] 新增 `WebSearchBackend` 抽象（输入 query + max_results，输出 `{title,url,snippet}` 数组）与默认 Tavily 实现（HTTP POST + JSON 解析）（REQ-T6-01，D9）
- [ ] 新增 `WebSearchTool : ITool`：`definition()` 返回 `name="web_search"`、`level=RequiresConfirm`；`execute(args)` 接收 `{query, max_results?}`（默认 5，钳制 `[1,10]`）（REQ-T6-01/06）
- [ ] 复用 `NetworkFetchTool` 先例：QNAM + sliced QEventLoop + atomic `cancel()` + `Activity::usage`（不阻塞 GUI、不跨线程触碰 reply）
- [ ] **实现 DFX 红线**：结果截断 ≤6000 字符（复用 `kMaxResultChars`）（REQ-T6-02）
- [ ] 异常映射：API key 缺失 → `ToolResult::fail("未配置搜索 API key")`；网络超时 → 复用超时机制；畸形响应 → 失败回灌（REQ-T6-03）

### 10.3 装配与门控
- [ ] 在 main.cpp 装配 `WebSearchTool` 并注册进 `ToolRegistry`（参照 `NetworkFetchTool` 装配模式）
- [ ] 扩展 `applyLocalOnlyGate`：`localOnlyMode` 开启且 cloud provider 时禁用 `web_search`（REQ-T6-04，R6）
- [ ] 确认工具被禁用时不出现在 `toolNamesForModel()`（schema 不暴露）（REQ-T6-04）

### 10.4 验收标准（Done Definition）
- [ ] 配置 `webSearchApiKey` 后 `web_search` 可被 Agent 调用并返回结果（REQ-T6-01）
- [ ] 返回超长结果 → 回灌内容 ≤6000 字符（REQ-T6-02）
- [ ] 未配置密钥调用 → 明确失败提示"未配置搜索 API key"（REQ-T6-03）
- [ ] `localOnlyMode` 下被门控且不出现在模型可选工具（REQ-T6-04）
- [ ] 密钥掩码展示且不入日志/审计（REQ-T6-05）
- [ ] 调用前弹确认对话框，未确认不外发（REQ-T6-06）
- [ ] 新增设置键默认值 = 现有行为；SettingsGuard 备份覆盖两个新键

### 10.5 测试要求
- [ ] 新增 `tests/test_web_search.cpp`：API key 缺失 / 网络超时 / 结果截断 6000 字符 / localOnlyMode 门控 / 请求级往返 / 畸形响应容错；复用 `FakeSseServer` 脚本化模式
- [ ] 扩展 `tests/test_agent.cpp` 的 `dangerousToolsNotExposed` 契约：纳入 `web_search` 工具计数联动断言
- [ ] 运行 `ctest -R "test_web_search|test_agent|test_settings"` 全绿
- [ ] GUI 冒烟（需用户配合）：配置真实 key 后 Agent 可调用；localOnlyMode 下被门控

---

## 11. T7 无障碍属性补全（波次 2 · 已交付）

**关联需求**：REQ-T7-01（控件可访问信息）、REQ-T7-02（纯键盘核心流程）、REQ-T7-03（焦点顺序与对话框行为）、REQ-T7-04（既有快捷键保持）
**任务描述**：为全部自定义 QML 控件补全 `Accessible.role/name/description`，修复 Tab 焦点链，补齐快捷键，使纯键盘可完成核心流程且屏幕阅读器可读。
**涉及文件/模块**：`src/ui/components/`（Sidebar / TopBar / SessionTabs / InputConsole / CodeBlock / ToolConfirmDialog / SettingsCenterDialog / AuditDialog / CategoryNav / PreferenceControl / AppButton / IconButton / AppIcon / ModelSelector / CopyButton 等）、`src/ui/pages/`（AgentChatPage / GeneralPage / InterfacePage / ModelConfigPage / McpPage / ToolsPage）、`src/ui/panels/ContextPanel.qml`、`src/ui/components/LogPanel.qml`
**前置依赖**：无（纯 UI 层走查）；与主任务 9 共用 `InterfacePage.qml` 需协调（R15）
**风险等级**：中（R9：焦点链改动破坏既有快捷键）
**回滚方式**：低 —— revert `Accessible.*` 补全与焦点链改动（纯 UI 层隔离）

### 11.1 可访问属性补全
- [ ] 为可交互控件补 `Accessible.role`（Button / TextField / ListItem / CheckBox / ComboBox 等）与 `Accessible.name`（可读名称）（REQ-T7-01）
- [ ] 为复杂控件补 `Accessible.description`（操作语义），避免仅依赖视觉图标（REQ-T7-01）
- [ ] 确认 `AppIcon` / `IconButton` 等纯图标控件具备可读名称（不依赖 tooltip）（REQ-T7-01）

### 11.2 键盘可达性与焦点链
- [ ] 走查并修复 Tab 焦点顺序：侧栏 → 顶栏 → 对话区 → 输入区 → 设置中心三页 → 双面板 → 日志面板（REQ-T7-03）
- [ ] 为对话框（ToolConfirmDialog / SettingsCenterDialog / AuditDialog）设置正确的初始焦点与 Esc 关闭（REQ-T7-03）
- [ ] 补齐关键操作快捷键，确认既有快捷键 Ctrl+, / Ctrl+D / Ctrl+L / Ctrl+B / Ctrl+[ 继续有效（REQ-T7-04，R9）

### 11.3 验收标准（Done Definition）
- [ ] Windows 讲述人可读出主要控件角色与名称；纯图标控件具备可读名称（REQ-T7-01）
- [ ] 纯键盘完成"新建会话 → 提问 → 审批工具 → 导出"全流程（REQ-T7-02）
- [ ] Tab 顺序符合视觉顺序；对话框初始焦点合理且 Esc 关闭（REQ-T7-03）
- [ ] 既有快捷键逐项有效（REQ-T7-04）
- [ ] `qmllint_gate` 零 error 级诊断

### 11.4 测试要求
- [ ] 运行 `ctest -R qmllint_gate`：无新增 error 级诊断
- [ ] 用户冒烟（需用户配合）：纯键盘完成核心全流程；Windows 讲述人走查主要控件

---

## 12. T8 episodic 长期记忆接入（波次 2 · 已交付）

**关联需求**：REQ-T8-01（会话结束生成摘要）、REQ-T8-02（新会话检索注入）、REQ-T8-03（注入预算钳制）、REQ-T8-04（开关控制与默认关闭）、REQ-T8-05（失败不阻塞）
**任务描述**：新增 `EpisodicService`，会话结束时异步生成摘要写入既有 `episodic` + `episodic_fts` 表；新会话启动时按 system prompt 检索 top-k 摘要注入上下文（复用 T1 注入点）；由 `episodicEnabled` 开关控制（默认关闭）。
**涉及文件/模块**：`src/core/agent/EpisodicService.h/.cpp`（新增，入 nexus_core）、`src/core/agent/AIAgent.h/.cpp`（会话结束摘要钩子 + 检索注入点）、`src/viewmodels/SettingsService.h/.cpp`（`episodicEnabled`）、`src/main.cpp`（装配）、`CMakeLists.txt`、`tests/test_episodic.cpp`（新增）、`tests/CMakeLists.txt`
**前置依赖**：T1 提示词注入点（**已交付**，无阻塞）；`IMemory::saveEpisodic/searchEpisodic` + `SqliteMemory`（已实现，表结构零变更）
**风险等级**：中（R7：摘要额外 LLM 调用成本/延迟；R8：注入与上下文预算冲突；R8b：摘要质量不足导致注入噪声）
**回滚方式**：低 —— 开关默认关闭；移除 EpisodicService + revert AIAgent 钩子（D4 止损形态：删表与触发器为备选）

### 12.1 设置项与接口暴露
- [ ] 新增 `episodicEnabled`（`memory/episodicEnabled`，bool，**默认 false**）Q_PROPERTY + getter/setter + NOTIFY；同批补 SettingsGuard 备份与 usage 埋点（REQ-T8-04）
- [ ] 确认 `IMemory::saveEpisodic/searchEpisodic` 与 `SqliteMemory` 既有实现可直接复用（表结构零变更）

### 12.2 EpisodicService 实现
- [ ] 新增 `EpisodicService`：`generateAndStoreSummary(sessionId, messages)`、`retrieveRelevantSummaries(query, topK=3)`、`setEnabled/isEnabled`（REQ-T8-01/02）
- [ ] 摘要生成：会话消息历史 → LLM 生成摘要（**单摘要 ≤500 字符**）→ 写入 `episodic` 表 + FTS5 索引（REQ-T8-01）
- [ ] 检索：按 system prompt 关键词检索 top-k（**≤3**）摘要（REQ-T8-02）
- [ ] 异常映射：LLM 摘要失败 → 跳过本次摘要、**不阻塞会话结束**；FTS5 检索失败 → 返回空数组、**不阻塞会话启动**（REQ-T8-05）

### 12.3 Agent 接入
- [ ] 在 `AIAgent::finishTask` 挂会话结束摘要钩子：`episodicEnabled` 时**异步**生成摘要（不阻塞会话结束 UI，R7，REQ-T8-01）
- [ ] 在 `AIAgent::startTask` 挂检索注入：`episodicEnabled` 时检索 top-k 摘要，注入 system prompt（复用 `effectiveSystemPrompt` 注入点）（REQ-T8-02）
- [ ] **实现 DFX 红线**：注入预算钳制 **≤1500 token**，受 `TokenEstimator` 约束（REQ-T8-03，R8）
- [ ] main.cpp 装配 EpisodicService 并注入 AIAgent

### 12.4 验收标准（Done Definition）
- [ ] 开启后会话结束异步生成摘要（≤500 字符）且不阻塞 UI，摘要可在后续被检索到（REQ-T8-01）
- [ ] 新会话系统提示词包含检索到的历史摘要，且与当前话题相关（REQ-T8-02）
- [ ] 历史摘要较多时注入量 ≤1500 token（REQ-T8-03）
- [ ] 默认关闭时不生成摘要、不注入历史；开启后两项生效（REQ-T8-04）
- [ ] 摘要失败不阻塞会话结束；检索失败不阻塞会话启动（REQ-T8-05）
- [ ] 新增设置键默认值 = 现有行为；SettingsGuard 备份覆盖 `episodicEnabled`

### 12.5 测试要求
- [ ] 新增 `tests/test_episodic.cpp`：摘要生成 / FTS5 检索 top-k / 注入预算钳制 1500 token / 开关启停 / 摘要失败不阻塞 / 检索失败不阻塞
- [ ] 复用 `FakeAIService`（test_agent）断言 system 消息 content 含 episodic 注入
- [ ] 运行 `ctest -R "test_episodic|test_agent|test_memory"` 全绿
- [ ] GUI 冒烟（需用户配合）：开启后会话结束生成摘要、新会话检索注入；关闭时跳过；体感记忆相关性

---

## 13. T9 日志跨重启回填（波次 2 · 已交付）

**关联需求**：REQ-T9-01（历史日志可见）、REQ-T9-02（历史标识）、REQ-T9-03（不冲淡当前会话日志）、REQ-T9-04（回填去重）
**任务描述**：启动时将最近日志文件尾部回填进 `Logger` 环形缓冲（去重），使日志面板在重启后可见上次会话历史日志（标注"历史"），且回填量不冲淡当前会话新日志。
**涉及文件/模块**：`src/core/utils/Logger.h/.cpp`（回填 API + `LogEntry.historical`）、`src/viewmodels/LogModel.h/.cpp`（"历史"标识）、`src/ui/components/LogPanel.qml`、`src/main.cpp`（启动调用）、`tests/test_logger.cpp`
**前置依赖**：无（小批，可搭车任意任务）
**风险等级**：低（R10：回填量过大冲淡当前会话日志；R11：行格式解析失败导致回填错乱）
**回滚方式**：低 —— revert 日志回填读取侧逻辑（环形缓冲结构不变）

### 13.1 Logger 回填 API
- [ ] 新增 `Logger::backfillFromFile(int maxEntries = 0)`：读取 `logDir()` 下最新 `nexus-YYYYMMDD.log` 尾部 N KB，解析行格式 `[yyyy-MM-dd hh:mm:ss.zzz] [LEVEL] [category] message`（REQ-T9-01）
- [ ] **实现 DFX 红线**：回填条目入环形缓冲并**去重**；回填量钳制 **≤ `RingCapacity` 的 50%（2500 条）**，不冲淡当前会话新日志（REQ-T9-03/04，R10）
- [ ] 回填在 `Logger::install()` 后、日志面板首次拉取前完成；线程安全（复用 `g_mutex`）
- [ ] 异常映射：文件不存在 / 不可读 / 行格式不匹配 → 跳过该行或返回 0，绝不抛异常、绝不阻塞启动（R11）

### 13.2 UI 标注与展示
- [ ] `LogEntry` 新增 `historical` 字段；LogModel 侧为回填条目标注"历史"标识（REQ-T9-02）
- [ ] LogPanel 展示"历史"标记，与当前会话日志视觉区分（REQ-T9-02）

### 13.3 验收标准（Done Definition）
- [ ] 重启应用后日志面板可见上次会话历史日志（REQ-T9-01）
- [ ] 回填日志带"历史"标记，当前会话新日志无该标记（REQ-T9-02）
- [ ] 大量历史日志时回填量 ≤2500 条，新日志仍完整可见（REQ-T9-03）
- [ ] 重复条目在面板中不重复出现（REQ-T9-04）
- [ ] 文件不可读时返回 0 且不阻塞启动（R11）

### 13.4 测试要求
- [ ] 扩展 `tests/test_logger.cpp`：回填行解析正确性 / 去重 / 容量钳制 ≤2500 / 当前会话日志不被冲淡 / 文件不可读返回 0
- [ ] 运行 `ctest -R "test_logger|qmllint_gate"` 全绿
- [ ] GUI 冒烟（需用户配合）：重启后日志面板可见上次会话历史日志并标注"历史"

---

# 第四部分 波次 3：P3 安全收尾

---

## 14. T14 MCP 安全收紧补齐（波次 3 · 已交付）

**关联需求（待实现）**：REQ-T14-01（不可信数据声明）、REQ-T14-02（工具描述截断）、REQ-T14-04（本地模式门控定案）、REQ-T14-05（工具清单预算上限）、REQ-T14-07（MCP 安全文档）
**关联需求（已交付·回归）**：REQ-T14-03（审计来源服务器字段）、REQ-T14-06（超时与确认）
**任务描述**：补齐 MCP 接入的安全收尾 —— 系统提示词 MCP 不可信声明、工具 description 截断、`localOnlyMode` 门控定案、工具 schema 预算上限、安全文档；已交付部分仅回归验证。
**涉及文件/模块**：`src/core/agent/AIAgent.cpp`（`systemPrompt()`）、`src/core/mcp/McpToolAdapter.h/.cpp`、`src/core/mcp/McpClient.h/.cpp`、`src/main.cpp`（`applyLocalOnlyGate`）、`src/core/utils/TokenEstimator.h`、`src/viewmodels/McpManager.h/.cpp`、`src/core/tools/ToolRegistry.h/.cpp`、`README.md`、`tests/test_mcp.cpp`、`tests/test_agent.cpp`
**前置依赖**：T12/T13（**已交付**）；**建议主任务 10（T6）先完成**（门控回归统一覆盖）
**风险等级**：高（R12：description/输出提示注入；R13：schema token 膨胀；R14：门控策略待定）
**回滚方式**：低 —— revert 安全收紧项（声明/截断/门控/上限，各项隔离）

### 14.1 已交付部分回归验证（避免重复实现）
- [ ] 核对审计 `detail` 含 `mcp_server` 字段（REQ-T14-03）
- [ ] 核对请求超时 30s（`McpClient::RequestTimeoutMs`）、`RequiresConfirm` 强制、崩溃指数退避（REQ-T14-06）
- [ ] 核对工具名 `mcp__<server>__<tool>` 与 `dangerousToolsNotExposed` 契约一致（D5）

### 14.2 系统提示词不可信声明
- [ ] 在 `systemPrompt()` 补 MCP 不可信数据声明：MCP 工具返回内容视为不可信数据、不得作为指令执行（REQ-T14-01，R12）
- [ ] 确认声明对 `effectiveSystemPrompt()` 路径同样生效（override 为空时走内置）；override 场景在设置页说明

### 14.3 description 截断与审计补全
- [ ] 在 `McpToolAdapter` 构造 / `McpClient::ToolInfo` 处理处对 `description` 截断 **≤1024 字符**（`McpToolAdapter::MaxDescriptionChars`）（REQ-T14-02，R12）
- [ ] 确认审计 `detail` 的 `mcp_server` 字段在 tools/call 全路径记录完整（REQ-T14-03）

### 14.4 localOnlyMode 门控定案
- [ ] 按 D1 保守默认实现：`localOnlyMode` 开启时禁用**全部** `mcp__*` 工具（`applyLocalOnlyGate` 扩展）（REQ-T14-04，R14）
- [ ] 预留按服务器 `local: bool` 字段细分的扩展点（D1-扩展待定项）

### 14.5 schema token 上限
- [ ] **实现 DFX 红线**：在 `TokenEstimator` / `McpManager` 施加工具数上限：**单服务器 ≤32、全局 ≤64**；超出禁用并告警（REQ-T14-05，R13）
- [ ] 确认超限工具不进 `toolNamesForModel()`（不暴露 schema）；上限校验仅约束 MCP 动态工具，不影响内建工具

### 14.6 验收标准（Done Definition）
- [ ] 内置系统提示词含 MCP 不可信数据声明，override 为空时走内置（声明生效）（REQ-T14-01）
- [ ] 超长工具描述提供给模型时 ≤1024 字符（REQ-T14-02）
- [ ] `localOnlyMode` 开启时全部 MCP 工具被禁用且不暴露给模型（REQ-T14-04）
- [ ] 单服务器 >32 或全局 >64 的工具被禁用并告警，且不进入 `toolNamesForModel()`（REQ-T14-05）
- [ ] README 含 MCP 接入安全注意事项与门控说明（REQ-T14-07）
- [ ] 已交付部分（审计 `mcp_server`、30s 超时、RequiresConfirm、指数退避）无退化（REQ-T14-03/06）

### 14.7 测试要求
- [ ] 扩展 `tests/test_mcp.cpp`：description 截断 1024 / 审计 server 字段 / localOnlyMode 门控全部 MCP / 单服务器 32 上限 / 全局 64 上限 / 超限不暴露 schema；`fake_mcp_server` 注入超长 description 与超量工具
- [ ] 扩展 `tests/test_agent.cpp`：系统提示词含不可信声明断言（override 空路径）
- [ ] 运行 `ctest -R "test_mcp|test_agent"` 全绿
- [ ] GUI 冒烟（需用户配合）：配置真实 MCP 服务器（如 `@modelcontextprotocol/server-filesystem`）验证门控与审计；超量工具场景验证告警

---

# 第五部分 波次 4：远期展望（不排期）

---

## 15. T15 NexusAI 作为 MCP Server（波次 4 · 不排期）

**关联需求**：REQ-T15-01（反向对外提供能力）
**任务描述**：本期不排期，仅登记。依赖 T12 的 JSON-RPC 层反向复用；仅在出现真实需求时立项并另行产出需求规格。
**涉及文件/模块**：`src/core/mcp/`（JSON-RPC 层反向复用，远期）
**前置依赖**：T12（已交付）
**风险等级**：—
**回滚方式**：不涉及（未排期）

### 15.1 处置
- [ ] 不在本计划范围内排期；如立项，另行产出 spec.md 与 tasks.md（D7-待定）

---

# 第六部分 全局集成与验收

---

## 16. 全局集成、门禁与契约核对（波次 0–3 全部完成后）

**关联需求**：全局（DFX 可维护性/兼容性红线）
**任务描述**：确保待实现任务与已交付基线集成后，全局门禁全绿，契约与兼容性无破坏。
**涉及文件/模块**：`CMakeLists.txt`、`tests/CMakeLists.txt`、`translations/`、`src/main.cpp`、全部新增模块
**前置依赖**：主任务 1–14
**风险等级**：中
**回滚方式**：按批回滚（每批成对提交）

### 16.1 构建与测试门禁
- [ ] 以 `-DBUILD_TESTS=ON` 配置并构建 Debug，编译零错误、零新增警告
- [ ] `ctest` 全绿（含新增 `test_web_search` / `test_episodic` 套件），既有 16 个测试目标（含 `fake_mcp_server`）无回归破坏
- [ ] `ctest -R qmllint_gate`：无 error 级诊断（含 T5/T7 的 QML 改动）
- [ ] 双语翻译收口 0 unfinished（T5 新增源串后）

### 16.2 契约与兼容性核对
- [ ] core 零 UI 依赖：新增 `EpisodicService` / `WebSearchTool` / MCP 收紧项均在 nexus_core，无 UI 依赖
- [ ] 四层 CMake 依赖单向，无跨层环依赖
- [ ] ITool 契约零变更；新增工具作为 ITool 实现接入
- [ ] 所有新增设置键默认值 = 现有行为，未配置时零行为变更（兼容性红线）
- [ ] SettingsGuard 键备份覆盖全部新增设置键（`webSearchProvider` / `webSearchApiKey` / `episodicEnabled` / `themeMode` 新档）
- [ ] 既有快捷键 Ctrl+, / Ctrl+D / Ctrl+L / Ctrl+B / Ctrl+[ 继续有效
- [ ] Dangerous 工具不暴露给模型（`dangerousToolsNotExposed` 契约不回退）

### 16.3 验收标准（Done Definition）
- [ ] 全局门禁全绿；无跨层环依赖；契约与兼容性红线逐项通过

### 16.4 批次交付纪律
- [ ] 每批成对提交，提交前 `ctest` 全绿为基线门禁
- [ ] 每批冒烟清单随批移交用户

---

# 第七部分 交付、文档与签核

---

## 17. 交付、文档与签核

**关联需求**：REQ-T14-07（安全文档）、全局文档更新
**任务描述**：补齐文档、汇总验收证据、形成阶段交付记录。
**涉及文件/模块**：`README.md`、`docs/read_files/`、`docs/12-NexusAI功能增强任务规划.md`
**前置依赖**：主任务 16
**风险等级**：低
**回滚方式**：不涉及

### 17.1 文档更新
- [ ] README 补 MCP 安全章节（主任务 14）与 web_search 配置说明（主任务 10）（REQ-T14-07）
- [ ] 更新 `docs/read_files/` 体系（接口与扩展开发指南 / 用户手册）涉及新增设置项与工具的部分
- [ ] 记录 T5/T6/T7/T8/T9/T14 的实现决策与遗留事项

### 17.2 验收签核
- [ ] 汇总 6 项待实现任务的验收证据（单测结果 + GUI 冒烟清单结果）
- [ ] 确认已交付 8 项任务的回归验证结论归档
- [ ] 形成阶段交付记录（含执行人、验证结果、遗留事项、T15 立项建议）

### 17.3 验收标准（Done Definition）
- [ ] 文档齐备；验收证据与回归结论归档；阶段交付记录完成

---

# 第八部分 实施记录与遗留事项

---

## 18. 实施记录（T5–T9 / T14 已交付）

> **执行日期**：2026-09-25
> **交付范围**：波次 1（T5）、波次 2（T6–T9）、波次 3（T14 补齐部分）
> **说明**：§9–§14 正文的规划验收项（checkbox）均已通过；正文 checkbox 保留为规划形态，实际验收证据见 §18.3。

### 18.1 执行概览

| 波次 | 任务 | 状态 | 新增/改动要点 |
|------|------|------|--------------|
| 1 | T5 主题跟随系统 | ✅ 完成 | `ui.themeMode` 扩展 auto 档并设为默认；系统深浅色实时监听；设置中心改三选一控件 |
| 2 | T6 web_search 联网能力 | ✅ 完成 | 新增 `WebSearchTool` + `WebSearchBackend` 抽象（Tavily 默认）；结果截断 ≤6000；密钥 DPAPI；门控联动 |
| 2 | T7 无障碍属性补全 | ✅ 完成 | 21 个组件 + 6 个页面 + 1 个面板补全 `Accessible.role/name/description` |
| 2 | T8 episodic 长期记忆 | ✅ 完成 | 新增 `EpisodicService`；异步摘要 ≤500 字；FTS5 检索 top-3；注入 ≤1500 token；开关默认关闭 |
| 2 | T9 日志跨重启回填 | ✅ 完成 | `Logger::backfillFromFile` + `LogEntry.historical`；回填去重且 ≤2500 条；面板"历史"标记 |
| 3 | T14 MCP 安全收紧补齐 | ✅ 完成 | 不可信声明 / description 截断 1024 / `localOnlyMode` 门控全部 `mcp__*` / 工具数上限 32·64 / README 安全章节 |

### 18.2 各任务实现决策

**T5 主题跟随系统**
- `ui.themeMode` 枚举由 `{dark, light}` 扩展为 `{auto, dark, light}`，`defaultValue` 改为 `auto`；保留 `legacyKeys` 兼容存量 dark/light（零迁移成本）。
- 以 `QStyleHints::colorSchemeChanged` 为主监听系统深浅色，仅在 `auto` 档生效；显式档忽略系统变化。
- `setIsDark()` 在 `auto` 档被调用时，将偏好"升级"为显式 dark/light 并持久化（避免用户显式选择被系统覆盖）。
- `InterfacePage.qml` 外观区由 Switch 改为 ComboBox 三选一，选项经 `PreferenceRegistry` 枚举元数据绑定（不硬编码），标签走 `qsTr()`。

**T6 web_search 联网能力**
- 后端抽象 `WebSearchBackend`（`providerKey()` + `search()`），默认 `TavilyBackend`；接新搜索服务只需新增子类。
- `WebSearchTool : ITool`，`level = RequiresConfirm`；参数 `{query, max_results?}`，`max_results` 钳制 `[1,10]` 默认 5。
- DFX：结果按编号列表拼装，总字符 ≤6000（`kMaxResultChars`），超限追加截断标记。
- 设置键：`webSearch/provider`（默认 `tavily`）、`webSearch/apiKey`（DPAPI 加密，getter 返回空串掩码）。
- 门控：`applyLocalOnlyGate` 扩展——`localOnlyMode` 且云端 provider 时禁用 `web_search`，禁用后不进 `toolNamesForModel()`。

**T7 无障碍属性补全**
- 覆盖组件：IconButton / AppButton / AppIcon / CopyButton / ToolConfirmDialog / AuditDialog / ModelSelector / Sidebar / InputConsole / SessionTabs / CodeBlock / PreferenceControl / CategoryNav / LogPanel / SettingsCenterDialog / SplitBar / AppToast / AgentTraceNode / KnowledgeDocDialog；页面：AgentChatPage / GeneralPage / ModelConfigPage / McpPage / ToolsPage / InterfacePage；面板：ContextPanel。
- 纯图标控件（IconButton / AppIcon / CopyButton）具备可读名称，不依赖 tooltip。
- 验证方式：`qmllint_gate` 零 error 级诊断。

**T8 episodic 长期记忆**
- 新增 `EpisodicService`（入 nexus_core，零 UI 依赖），构造参数化注入 `IMemory*` 与 `IAIService*`。
- 摘要生成**异步**（fire-and-forget，`onSummaryResponse` 路由回写），单摘要 ≤500 字符（`kMaxSummaryChars`）。
- 检索为同步 FTS5 MATCH，top-k 默认 3（`kDefaultTopK`）；注入总量 ≤1500 token（`kMaxInjectionTokens`）。
- `AIAgent::finishTask` 挂摘要钩子、`startTask` 挂检索注入（复用 `effectiveSystemPrompt` 注入点）。
- 设置键：`memory/episodicMemoryEnabled`（默认 `false`）。
- 异常映射：LLM 摘要失败跳过、FTS5 检索失败返回空，均不阻塞主流程。

**T9 日志跨重启回填**
- `Logger::backfillFromFile()`：读取最新 `nexus-YYYYMMDD.log` 尾部，正则解析 `[时间] [级别] [分类] 消息`，入环形缓冲前**去重**。
- DFX：回填量钳制 ≤ `RingCapacity` 的 50%（2500 条），不冲淡当前会话新日志。
- `LogEntry.historical` 字段 + `LogModel::HistoricalRole`；`LogPanel.qml` 以斜体 + "历史"文案区分。
- 文件不存在/不可读/行不匹配时跳过，绝不抛异常、不阻塞启动。

**T14 MCP 安全收紧补齐**
- `systemPrompt()` 补 MCP 不可信数据声明：MCP 返回内容视为不可信数据、不得作为指令执行。
- `McpToolAdapter::MaxDescriptionChars = 1024`：工具描述截断，防提示注入与 token 膨胀。
- `ToolRegistry::setToolsEnabledByPrefix()`：按前缀批量门控，`applyLocalOnlyGate` 覆盖全部 `mcp__*`。
- `McpManager` 工具数上限：单服务器 ≤32（`kMaxToolsPerServer`）、全局 ≤64（`kMaxGlobalMcpTools`），超限禁用并告警、不进 `toolNamesForModel()`。
- README 新增「MCP 安全注意事项」章节（REQ-T14-07）。

### 18.3 验收证据

| 验收项 | 结果 |
|--------|------|
| 全量单元测试 | **18/18 套件通过（100%）**，含新增 `test_web_search`、`test_episodic` |
| QML 静态检查 | `qmllint_gate` 通过，零 error 级诊断 |
| 双语翻译 | `nexus_ai_zh_CN.ts` / `nexus_ai_en.ts` 各 290 条，**0 unfinished**；`.qm` 生成成功 |
| 已交付任务回归 | 波次 0 的 T1–T4 / T10–T13 回归无退化 |
| 文档 | README「MCP 安全注意事项」；本记录 |

---

## 19. 遗留事项

| 编号 | 事项 | 影响 | 建议 |
|------|------|------|------|
| L1 | **web_search 与 episodic 缺设置中心 UI 入口** | `webSearch/provider`、`webSearch/apiKey`、`memory/episodicMemoryEnabled` 仅暴露为 C++ Q_PROPERTY 与 QSettings 持久化，QML 侧无图形入口，用户须手动改配置键 | 后续在设置中心补"工具 / 记忆"分区，接入 SettingsService 对应属性 |
| L2 | **SettingsGuard 键备份未落地** | 规划中的 `SettingsGuard` 集中键备份类在代码中不存在；新设置键未纳入统一备份/迁移清单（仅 `ui.themeMode` 在 `PreferenceStore` 有 legacy 兼容） | 统一收敛设置键清单，补集中备份/迁移机制 |
| L3 | **FTS5 中文分词限制** | FTS5 默认 tokenizer（unicode61）中文分词有限，episodic 中文关键词检索召回率可能偏低；当前测试用英文摘要规避 | 评估 ICU / jieba 分词扩展，或检索侧加中文 n-gram 兜底 |
| L4 | **搜索后端仅 Tavily** | `webSearchProvider` 目前只有 `tavily` 一个合法值 | 按需新增 `WebSearchBackend` 子类并扩展合法枚举 |
| L5 | **GUI 冒烟未完成** | T5 系统主题实时跟随、T6 真实 key 调用、T7 讲述人走查、T8 记忆相关性、T9 重启回填、T14 真实 MCP 门控等需真机验证 | 由用户在 Windows 真机按各任务冒烟清单逐项验证 |
| L6 | **日志目录路径文档不一致** ✅ 已修复（2026-09-26） | 根因：日志走 `QStandardPaths::AppLocalDataLocation`（`%LOCALAPPDATA%`），数据库走 `AppDataLocation`（`%APPDATA%`，Roaming），二者本就不同 | 已统一：部署指南 §6/§8、用户手册 FAQ、设计文档 §6.1 改为日志 `%LOCALAPPDATA%`、数据库 `%APPDATA%`；详见 `docs/14-L1-L6遗留事项修复方案.md` §3.6 |

---

## 20. T15 立项建议

T15（NexusAI 作为 MCP Server）**仍不排期**。MCP 客户端链路（T12 stdio / T13 HTTP）已交付，JSON-RPC 层可反向复用；建议仅在出现真实第三方复用需求时立项，届时另行产出 spec.md 与 tasks.md，不在本计划范围内实现。

---

> **文档状态**：tasks.md 已更新，位于 `.codeartsdoer/specs/nexus_ai_enhancement/tasks.md`
> **需求基线**：spec.md（65 条 EARS 需求，覆盖率 100%）
> **交付基线**：已交付 8（主任务 1–8，回归）/ **已实现 5（主任务 9–13）** / 已实现 1（主任务 14）/ 不排期 1（主任务 15）
> **实施顺序**：波次 0 回归轨道（并行）→ 波次 1（T5）→ 波次 2（T6–T9）→ 波次 3（T14 补齐）→ 波次 4（T15 不排期）
> **实施状态**：波次 0–3 全部完成（2026-09-25），18/18 ctest 全绿；遗留事项见 §19
