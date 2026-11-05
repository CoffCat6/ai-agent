# 41 - Skill 工具限制回滚决策（S1 反向）

> 决策（2026-11-05，用户）：**Skill 不应限制工具调用**。技能只提供指令/知识；工具是否可用由对话中的权限模式、工具确认、全局禁用与 localOnly 决定。
> 影响：S1（`allowed-tools` 生效）与 S5+ 的工具候选/未知工具校验被废止。

---

## 1. 结论

| 项 | 处理 |
|---|---|
| 技能白名单运行期生效 | **删除**（schema 过滤、派发拒绝、确认路径拒绝、系统提示允许清单全部移除） |
| 编辑器 allowed-tools 字段/工具候选/未知工具二次确认 | **删除** |
| `allowed-tools` frontmatter | **保留解析但忽略**（旧文件不破坏；编辑保存时不再写该键） |
| `allowedToolsSpecified` 等字段 | 作为 legacy 元数据保留，运行期不读；后续可择机清理 |
| 全局工具策略 | **不变**：Dangerous/禁用/localOnly/PlanOnly/四档权限、对话确认 |
| 未知 `$skill` 提示 | **保留**（与工具权限无关） |
| `$skill` 正文注入、SkillRegistry、编辑器、开关/目录、搜索筛选 | **不变** |

---

## 2. 为什么不是“纯黑名单”或其他替代

- 用户决策是**取消技能级工具约束**，不是换成黑名单；再引入 `denied-tools` 仍是同一层多余限制。
- 对话级权限已能表达“是否允许写/执行/联网”；技能文件不应成为第二套权限系统。
- 保留 `allowed-tools` 解析只为兼容旧文件；运行期与 UI 都不再使用。

---

## 3. 代码改动

### 3.1 Runtime（core/agent）
- 删除 `AIAgent::toolAllowedByActiveSkills / activeSkillAllowedTools / activeSkillPromptTools / activeSkillsRestrictTools`。
- `processIteration` / `countIterationTools` 的 schema 循环恢复为只按 `isToolUsable` + `toolAllowedForModel`。
- `processNextToolCall` / `approveToolCall` 删除技能拒绝分支与 `tool_not_allowed_by_skill` 审计。
- `activeSkillsBlock()` 只输出 `<skills><skill>正文</skill></skills>`；删除“本轮允许工具”段。
- 保留任务开始/清理 active skills、metadata `skills` 记录（仍用于 UI/审计可追溯）。

### 3.2 UI
- `SkillEditorDialog.qml`：删除 allowed-tools 输入、`skillToolPicker`、`skillUnknownToolsHint`、未知工具确认状态、`parseTools/preview` 中 allowed-tools 行；保存 draft 不再传 `allowedTools/allowedToolsSpecified`。
- 保留：id/name/description/body、预览、覆盖确认（`code === "exists"`）、删除确认。
- `InputConsole` 的未知 `$skill` 提示保持不变。

### 3.3 数据/VM（最小）
- `SkillRegistry` / `SkillEditorService` 的 allowed-tools 解析与序列化保留为 legacy；编辑器不再写入。
- `AgentViewModel::resolveSkills/skillToMap/startTaskWithSkills` 可继续携带 legacy 字段，但 `AIAgent` 忽略；不做大规模字段删除，避免旧文件/测试连锁。
- 编辑旧技能保存后：`allowed-tools` 键消失（迁移为“不限制”）。

### 3.4 测试
- `test_agent`：删除/改写白名单生效用例；新增“即使 frontmatter 写了 allowed-tools，schema 仍包含全部可用工具、派发不被技能拒绝、提示不含 allowed-tools”的回归。
- `test_skills_qml`：删除工具候选/未知工具二次确认用例；既有编辑用例去掉 allowed-tools 断言（属功能删除，非弱化）。
- `test_skills_editor`：legacy 解析/序列化测试保留（不涉及运行期权限）。
- `test_skills`：allowed-tools 解析测试保留为 legacy 元数据。

---

## 4. 安全说明

- 项目 Skill 正文仍可能诱导模型调用工具；最终由 **对话权限模式 + 工具确认 + Dangerous 策略 + 全局禁用 + localOnly** 拦截。
- 用户选择 `AutoEdit/FullAccess` 时本就放宽；技能不再额外缩小或扩大权限。
- 旧技能文件中的 `allowed-tools` 不再生效；首次编辑保存会移除该键。

---

## 5. 文档
- `docs/38`、`docs/40` 顶部标注“已废弃（见 docs/41）”。
- `docs/37 §6` “allowed-tools 不生效”的历史描述恢复为当前事实。
- `docs/27`/codemap 如提及技能工具白名单，改为“legacy 元数据，不生效”。

---

## 6. 任务

| 波次 | 状态 | 提交 | 验证 |
|---|---|---|---|
| W8-A runtime 回滚 | 已完成 | 见下条提交 | test_agent 140/140 |
| W8-B UI 回滚 | 已完成 | 见下条提交 | test_skills_qml 22/22 |
| W8-C 审查/验证 | 已完成 | 见下条提交 | 全量 59/59 + panel 1/1；qmllint 0 |

### W8 收口记录（2026-11-05）

- 运行期：删除 `toolAllowedByActiveSkills/activeSkillAllowedTools/activeSkillPromptTools/activeSkillsRestrictTools`、schema 两处过滤、`processNextToolCall`/`approveToolCall` 技能拒绝、`tool_not_allowed_by_skill` 审计、系统提示“本轮允许工具”段；保留 `<skills><skill>` 正文注入、8KB/32KB 截断、metadata、任务生命周期清理。
- UI：`SkillEditorDialog` 移除 allowed-tools 输入、工具候选 `skillToolPicker`、未知工具二次确认/警告、preview allowed-tools 行、draft 的 `allowedTools/allowedToolsSpecified`；保留 id/name/description/body、覆盖/删除确认、`code=exists`。
- 数据：`Skill`/Registry/Editor 的 allowed-tools 解析仅作 legacy 兼容，运行期不读；编辑旧技能保存后该键消失。
- 测试：`test_agent` 删除 8 个白名单生效用例，新增 `allowedToolsFrontmatterDoesNotRestrictTools`（写了 allowed-tools 仍下发全部可用工具、提示无白名单段）；`test_skills_qml` 删除 3 个工具 UI 用例，普通创建/编辑恢复单次保存。
- 权限不变：Dangerous/禁用/localOnly/PlanOnly/四档权限、工具确认继续生效。
- 验证：test_agent 140、test_skills_qml 22、test_chat_workflows 44、test_skills_editor 24；全量 59/59（128.18s，`-j2`）+ panel 1/1（160.76s）；`qmllint_gate` Error 0；`check_sources` OK。
- 遗留：旧技能文件中的 `allowed-tools` 保留到下次编辑；legacy 字段/解析未删除（降低 churn）；项目技能正文仍可能诱导工具调用，依赖对话权限与确认兜底。