# 37 - Skill 能力与输入框命令补全方案

> 目标：输入框支持 `/` 命令补全（修复现状缺陷）与 `$` 引用 Skill（新增能力）。
> 基线：HEAD `56a635d`，全量 57 条目全绿。
> 决策默认（用户已确认“开始进行”）：项目/用户目录 + Markdown frontmatter；系统提示注入；项目 Skill 默认启用但标注来源；`$name` 单语法；`/` 补全 Enter 不发送。

---

## 1. 现状与根因

| 项 | 事实 | 证据 |
|---|---|---|
| `/` 无补全 | 输入框只有 `@` 文件补全状态机 | `InputConsole.qml:26-115, 400-520` |
| `/` 仅发送时展开 | `expandSlashCommand` 在 submit 调用 | `InputConsole.qml:213` |
| 默认命令已存在 | review / explain / translate / commit | `SettingsService.cpp:660-681` |
| 无 Skill 基础设施 | 仅用量统计有 `skill` 分类，无发现/解析/注入 | 全仓 grep |

结论：`/` 不是失效，而是**未实现输入即补全**；Skill 为全新能力。

---

## 2. Skill 模型

### 2.1 目录

| 来源 | 路径 | 优先级 |
|---|---|---|
| 项目 | `<projectRoot>/.nexusai/skills/<id>/SKILL.md` | 高（覆盖用户同名） |
| 用户 | `QStandardPaths::AppConfigLocation/skills/<id>/SKILL.md` | 中 |
| 内置 | `:/skills/<id>/SKILL.md` | 低（本期不做） |

### 2.2 格式

```markdown
---
name: code-review
description: 审查 diff 并给出分级问题
version: 1
allowed-tools: [read_file, search_code, git_diff]
max-bytes: 65536
---
（正文：注入系统提示的行为指令/知识）
```

- 宽松解析：frontmatter 可选；缺失时 name=目录名、description=首个非空行。
- 单文件上限 64KB（超出截断并标记），总数上限 200。
- `allowed-tools` 本期只解析、不生效（工具仍走现有权限）。
- 不执行 scripts/resources，仅注入正文。

### 2.3 发现与安全

- 启动扫描 + 项目切换重扫 + 手动刷新；缓存。
- 路径 canonical 校验；仅允许上述两个根目录内。
- 项目 Skill 来自仓库，UI 标注“项目”；设置页提示只使用可信项目。
- 去重：项目覆盖用户；重复 id 只保留高优先级。

---

## 3. 交互设计

### 3.1 `/` 命令补全

| 项 | 规则 |
|---|---|
| 触发 | 光标前 token 以 `/` 开头，且 `/` 在行首或空白后；token 内无空格；排除 `/etc/hosts`、`http://` |
| 候选 | `Settings.slashCommands`（name + note） |
| 键盘 | ↑/↓ 移动；Tab/Enter 只补全为 `/name `，不发送；Esc 关闭 |
| 面板 | 复用 `@` 的非聚焦 Popup 样式；空列表提示去设置添加 |
| 发送 | 仍走 `expandSlashCommand`，模板语义不变 |

### 3.2 `$` Skill 引用

| 项 | 规则 |
|---|---|
| 触发 | 光标前 token 以 `$` 开头，位于行首/空白后；候选来自 SkillRegistry |
| 候选 | name + description + 来源徽标（项目/用户） |
| 选中 | 插入 `$name `；输入框上方 chip 显示已引用（可移除；最多 3；去重） |
| 发送 | 保留 `$name` 文本；解析为 skill 列表传给 `startTaskWithSkills` |
| 注入 | 每轮系统提示追加 `<skills><skill name source>正文</skill></skills>`；计入 token 预算；超限截断 |
| 记录 | 消息/trace metadata 记录 skills；审计 `skill_refs` |

### 3.3 三态互斥

`@` / `/` / `$` 同一时刻只允许一个候选面板；Esc 关闭；Tab/Enter 只作用于当前面板。

---

## 4. 分层落点

| 层 | 文件 | 内容 |
|---|---|---|
| core | `src/core/skills/Skill.h`、`SkillRegistry.h/.cpp` | 发现/解析/搜索/去重/上限 |
| core | `src/core/agent/AIAgent.h/.cpp` | 接收 active skills；系统提示注入；metadata/审计；预算 |
| viewmodels | `AgentViewModel.h/.cpp` | `skillMentions` / `resolveSkills` / `startTaskWithSkills` |
| ui | `InputConsole.qml` | `/`、`$` 补全状态机；chip；三态互斥 |
| ui | `SkillsPage.qml` | 用户/项目技能列表、来源、刷新、打开目录、启用开关 |
| 设置 | `SettingsSchema.cpp`、`CategoryNav` | `skills/enabled`、`skills/projectEnabled`、`skills/maxPerMessage` |
| 构建 | `CMakeLists.txt`、`tests/CMakeLists.txt` | 新源、新 QML、新测试 |

---

## 5. 任务批次

| 波次 | 任务 | 依赖 | 验收 |
|---|---|---|---|
| W1 | `/` 命令补全 + 回归 | 无 | 输入 `/re` 出现 review；↑/↓/Tab/Esc；`/etc/hosts` 不触发；Enter 不发送 |
| W2 | `SkillRegistry` + 解析/发现/搜索 + 单测 | 无 | frontmatter/缺失/超限/去重/项目覆盖/路径越界；`test_skills` 全绿 |
| W3 | `$` 补全 + chip + 注入 + metadata + SkillsPage | W1/W2 | 端到端：引用 skill → 发送 → 请求系统提示含 `<skill>`；metadata 记录；UI 可移除 |
| W4 | 审查 + 定向/全量 + 真机冒烟 | W1-W3 | ctest 全绿；qmllint Error 0；无越界 |

---

## 6. 明确不做（本期）

- 不执行 Skill 的 scripts/resources。
- 不让 `allowed-tools` 改变本轮工具集。
- 不做内置 Skill、Skill 市场、远程安装。
- 不做 `$name(args)` 参数语法（参数留在正文）。
- 不做 Skill 编辑器（仅列表/打开目录/刷新）。

---

## 7. 安全与边界

| 风险 | 处理 |
|---|---|
| 恶意项目 SKILL.md 注入 | 仅注入文本；UI 标注来源；设置页提示；不执行脚本 |
| 路径越界 | canonical 校验，仅两个根目录 |
| 超大文件/数量 | 64KB/200 个上限，截断提示 |
| Token 预算 | Skill 正文计入上下文预算；单 skill/总量超限截断 |
| 命令注入 | `/` 仅文本模板；`$` 仅文本注入；不拼接 shell |
| 现有契约 | 保留 `startTask` 三参；新增 `startTaskWithSkills` 四参，避免破坏 QML/测试 |

---

## 8. 进度

| 波次 | 状态 | 提交 | 验证 |
|---|---|---|---|
| W1 | 已完成 | 待提交 | chat 43/43；`/` 候选/键位/路径排除/空候选不发送 |
| W2 | 已完成 | 待提交 | test_skills 13/13；发现/解析/上限/canonical |
| W3 | 已完成 | 待提交 | W3-1 注入 + W3-2 UI；agent 139、viewmodel 70、skills_qml 5、chat 43 |
| W4 | 已完成 | 待提交 | 全量 58/58 + panel 1/1；qmllint Error 0；check_sources 69/69 |

### W4 收口记录（2026-11-04）

- 修复 W4 审查 P1/P2：空候选 Enter 只关闭不发送；`/`/`$` 补全替换整个 token（中间补全不再留下后缀）；chip 按解析到的 token 删除（id≠frontmatter name 可移除）；`resolveSkills` 按 skill id 去重（大小写/别名不重复注入）；`skills/enabled`/`projectEnabled` 接线（此前为死键）。
- 修复 qmllint 6.9 病理内存：`InputConsole.removeSkill` 改为纯字符扫描，移除「同函数正则字面量 + 循环」组合；`qmllint_gate` 恢复通过（Error 0）。
- 修复 `test_settings::schemaCoversBypassWriters`：审计键列表补 `skills/enabled`、`skills/projectEnabled`、`skills/maxPerMessage`。
- 验证口径：`test_chat_workflows` 曾因构建中断残留 0 字节 exe 假失败，删除重链后正常；全量用 `-j2`（`-j4` 下 `test_agent` 因资源竞争 flaky），58/58 + panel 1/1。
- 遗留： SkillsPage 未提供启用开关与“打开目录”（docs §4 的设计项，暂以说明文案/隐藏键替代）；项目 skill 可覆盖用户同名 skill（按设计，但没有 UI 开关可关闭项目来源）；`test_viewmodel` 的 skill 用例未隔离真实 AppConfig 用户目录（本机若存在同名/大量用户 skill 可能影响计数）；panel 测试仍可能拉起 MarkText。

---

## 9. W5 体验补齐（2026-11-04 启动）

### W5-1 SkillsPage 开关与目录入口
- `SettingsService::setValueForSetting` 增加 `skills/enabled`、`skills/projectEnabled`、`skills/maxPerMessage`（clamp 1-5），走 `persistValue`（发 `settingSaved`）。
- `AgentViewModel` 增加 3 个 getter/setter + `openSkillsDirectory(source)`（project/user）+ 测试缝 `openUrlHook`。
- `SkillsPage` 增加“技能设置”Section（两个 Switch + 每轮上限 1-5）与“打开用户/项目技能目录”按钮。
- 测试：`test_skills_qml` 覆盖开关写读、列表/解析联动、目录路径。

### W5-2a SkillEditorService（core）
- 新 `core/skills/SkillEditorService.h/.cpp`：`validateId`、`createOrUpdate`、`remove`、`load`；`QSaveFile` + `PathWriteGuard` + canonical containment；单文件 64KB 上限；保存旧文件 `.bak`。
- 新增 `test_skills_editor` 单测；CMake 注册。

### W5-2b 编辑器 UI
- `SkillEditorDialog.qml`：id/name/description/allowed-tools/body、来源选择（项目/用户）、预览、覆盖确认。
- `SkillsPage` 增加“新建/编辑/删除”入口；`AgentViewModel` 暴露 draft/save/remove。
- QML harness + C++ 驱动测试。

### 约束
- 用户技能目录由应用管理；项目技能写入仅限 canonical 项目根内。
- 不执行脚本/资源；删除需确认；保存原子写 + 旧文件 `.bak`。
- QML 调用的 C++ 成员必须 `Q_INVOKABLE` 且不得依赖默认参数。


### W5 交付记录（2026-11-04）

- W5-1：`SettingsService::setValueForSetting` 支持 `skills/enabled`、`skills/projectEnabled`、`skills/maxPerMessage`（clamp 1-5，走 `persistValue`）；`AgentViewModel` 增加 3 组 getter/setter、`openSkillsDirectory(user|project)` 与测试缝 `openUrlHook`；`SkillsPage` 增加技能设置 Section（两个 Switch + 上限 SpinBox）与打开用户/项目目录按钮。
- W5-2a：新增 `core/skills/SkillEditorService`：id 校验（`../`、分隔符、`..` 段）、canonical containment、`QSaveFile` 原子写、覆盖前 `SKILL.md.bak`、64KB（UTF-8）限制、删除只清 `SKILL.md` 与空目录；`test_skills_editor` 覆盖往返/备份/注入/越界/删除。
- W5-2b：`SkillEditorDialog`（来源/id/name/description/allowed-tools/body、只读预览、覆盖二次确认）；`SkillsPage` 新增“新建/编辑/删除”，删除走二次确认；`AgentViewModel` 暴露 `skillDraft/saveSkill/removeSkillById/validateSkillId/skillRootForSource`。
- 审查修复：`SkillEditorDialog.projectAvailable` 改由 `SkillsPage.hasActiveProject` 绑定（原 C++ 方法调用无法被 QML 依赖追踪，项目切换后不刷新）；`remove` 拒绝根内 symlink（原先按 canonical 目标删除会误删被指向技能）；无活动项目时 `openEdit` 不再静默回落到用户来源。
- 验证：`test_skills 13`、`test_skills_editor 16`、`test_skills_qml 14`、`test_chat_workflows 43`、`test_settings 83`；全量 59/59（114.40s，`-j2`）+ panel 1/1（13.20s）；`qmllint_gate` Error 0；`check_sources` OK。
- 遗留：覆盖确认用“已存在”中文文案匹配（建议改结构化 code）；id 允许尾点，Windows 可能别名（P3）；Windows 保留设备名未拒绝（P3）；QML 测试未覆盖“无项目时项目开关/目录按钮 disabled”与 setter 失败回滚；panel 仍可能拉起 MarkText；`-j2` 下 `test_agent` 偶发资源竞争 flaky（单独约 33s 通过）。
