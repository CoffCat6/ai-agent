# 38 - Skill allowed-tools 生效方案（S1）

> **已废弃（2026-11-05）**：技能不承担工具权限，S1 门控与工具候选已回滚，见 `docs/41-Skill工具限制回滚方案.md`。



> 目标：让 Skill frontmatter 的 `allowed-tools` 从“仅解析/展示”变为真正的工具白名单。
> 基线：HEAD `96aa231`，全量 59/59 + panel 1/1。
> 范围：主代理本轮；子代理角色白名单保持不变；不改变现有权限/PlanOnly/禁用工具语义，只做交集。

---

## 1. 现状与缺口

| 项 | 事实 | 证据 |
|---|---|---|
| `allowed-tools` 只解析 | `Skill.h` 注释“本期只解析，不改变工具权限” | `Skill.h:29` |
| 工具 schema 未过滤 | schema 只按 `isToolUsable` + `toolAllowedForModel` | `AIAgent.cpp:1654-1660, 1762-1766` |
| 派发未校验 | 只校验 unknown/disabled/PlanOnly | `AIAgent.cpp:2570-2600, 2707` |
| VM 丢失字段 | `startTaskWithSkills` 构造 `Skill` 时未复制 `allowedTools` | `AgentViewModel.cpp:1501-1518` |

---

## 2. 语义（本期拍板）

| 场景 | 行为 |
|---|---|
| 技能**未写** `allowed-tools` | 不限制（向后兼容） |
| 技能写了 `allowed-tools`（含空数组 `[]`） | 进入限制模式 |
| 多个技能 | 取所有“已声明”技能的**并集**；未声明技能的指令仍注入，但不扩大工具集 |
| 空集合 `[]` | 只允许元工具（当前仅 `todo_write`） |
| `todo_write` | **始终允许**（任务进度管理，不构成外部能力） |
| `spawn_agent` 在白名单 | 自动放行 `subagent_wait` / `subagent_status` / `subagent_cancel`（成组） |
| 工具名匹配 | trim、大小写不敏感、取 `(` 前名称（如 `shell_exec(git diff)` → `shell_exec`）；`mcp__*` 名称统一小写匹配（含 `mcp__` 前缀） |
| PlanOnly/禁用/localOnly | 与技能白名单取**交集**；拒绝原因按优先级给出 |
| 子代理 | 仍用角色自身白名单，不继承主代理技能限制 |
| 拒绝语义 | 补 `tool_call/tool` 配对错误，不中断历史；写审计 `tool_not_allowed_by_skill` |

---

## 3. 数据模型

- `Skill` 增加 `bool allowedToolsSpecified{false}`：解析到 `allowed-tools` 键即 true（即使值为空）。
- `SkillEditorService::Draft` 增加同名字段；`load` 保留、`save` 在 specified 时允许写 `allowed-tools: []`；编辑器 UI 不新增控件（空列表=“不限制”由 UI 表达，手工 `[]` 通过加载保留）。
- `AgentViewModel::resolveSkills` 输出 `allowedToolsSpecified`；`startTaskWithSkills` 复制 `allowedTools` 与 `allowedToolsSpecified`。

---

## 4. 生效点

| 阶段 | 实现 |
|---|---|
| schema 过滤 | `processIteration` / `countIterationTools` 增加 `toolAllowedByActiveSkills(def.name)` |
| 派发校验 | `processNextToolCall` 在 disabled/PlanOnly 之前/之后加技能拒绝分支，返回结构化错误并审计 |
| 系统提示 | `activeSkillsBlock()` 末尾列出“本轮允许工具”；限制模式下提示“未列出的工具不要调用” |
| 审计 | `tool_not_allowed_by_skill`（detail = 允许集合，省略正文） |

---

## 5. 测试

| 套件 | 用例 |
|---|---|
| `test_skills` | 缺省 → specified=false；`allowed-tools: []` → true+空；行内数组/逗号/引号 |
| `test_skills_editor` | load/save 保留 specified；空 specified 写 `[]` |
| `test_viewmodel` | `startTaskWithSkills` 复制 allowedTools + specified；resolve 输出字段 |
| `test_agent` | 有白名单 → schema 只含并集 + todo_write；未授权工具派发被拒且历史配对完整；空 `[]` → 仅 todo_write；无技能 → 工具集不变；spawn 放行控制工具；系统提示包含允许列表；审计存在 |

---

## 6. 不做

- 不做 `Bash(git diff)` 级别的“同工具按参数细分授权”；本期只按工具名。
- 不让子代理继承主代理白名单。
- 不在 SkillsPage 增加“空白名单”专用控件。
- 不回填历史任务的工具限制。

---

## 7. 进度

| 波次 | 状态 | 提交 | 验证 |
|---|---|---|---|
| W6-1 模型/解析/VM | 已完成 | 见下条提交 | 4/4 定向 |
| W6-2 AIAgent 生效 | 已完成 | 见下条提交 | test_agent 147/147 |
| W6-3 审查/验证 | 已完成 | 见下条提交 | 全量 59/59 + panel 1/1；qmllint 0 |

### W6 收口记录（2026-11-04）

- 数据层：`Skill.allowedToolsSpecified` 区分「缺省」与显式 `[]`；Registry/Editor 解析与序列化往返；`resolveSkills` 输出、`startTaskWithSkills` 复制白名单。
- 生效层：`AIAgent` 并集白名单 + `todo_write` 恒允许 + `spawn_agent` 单向成组；schema 两处过滤；`processNextToolCall` 与 `approveToolCall` 双闸拒绝；审计 `tool_not_allowed_by_skill`（detail 仅允许列表，不含正文）；系统提示输出与 schema 同源的可用工具。
- W6-3 修复：① 编辑器桥 `skillDraft/saveSkill` 透传 `allowedToolsSpecified`（否则 UI 保存会把 `[]` 静默降级为不限制，P1）；② 空正文 + 限制模式重置技能块，避免未闭合 `<skills>`（P2）；③ 提示改用 `activeSkillPromptTools`（声明集合 ∩ 注册 ∩ 可用 ∩ 模型策略），不再列出必被拒的工具（P2）。
- 验证：`test_agent 147`、`test_viewmodel 70`、`test_skills 14`、`test_skills_editor 17`；全量 59/59（134.21s，`-j2`）+ panel 1/1（23.06s）；`qmllint_gate` Error 0；`check_sources` OK。
- 遗留 P3：`dispatchToolExecution` 未复检技能闸（现有入口均已校验，属纵深防御）；unknown-tool 错误提示仍列全量工具名；`mcp__*` 匹配统一小写（文档措辞已对齐）；测试未覆盖 `approveToolCall` 防御分支、localOnly/禁用/PlanOnly 与白名单交集、子代理不继承。