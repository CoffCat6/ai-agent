# 39 - 技能列表搜索与来源筛选方案（S4）

> 目标：SkillsPage 支持按关键字搜索、按来源（全部/项目/用户）筛选，并给出结果计数与可区分的空态。
> 基线：HEAD `835b8e6`，全量 59/59 + panel 1/1。
> 范围：仅 SkillsPage + 对应 QML 测试；不改 SkillRegistry/VM/AIAgent。

---

## 1. 现状

| 项 | 事实 |
|---|---|
| 列表 | `page.skills` 快照，按 `source` 分成 project/user 两组 |
| 刷新 | `refresh()` 调 `AgentVM.reloadSkills()` + `AgentVM.listSkills("", 200)` |
| 搜索 | 无输入框、无过滤 |
| 来源筛选 | 仅固定分组展示，不能只看项目/用户 |
| 计数 | 工具条只显示“共 N · 项目 X · 用户 Y” |
| 空态 | `skillsEmptyHint` 仅基于 `page.skills.length===0` |

---

## 2. 设计

### 2.1 数据流

- `refresh()`：`reloadSkills()` → `allSkills = listSkills("", 200)` → `applySearch()`（保留当前 search/filter）。
- `applySearch()`：本地过滤 `allSkills` → `skills`；不再触发 C++ 重扫。
- 搜索关键字：`name` / `description` / `id` 子串，大小写不敏感。
- 来源筛选：`all` / `project` / `user`，只控制分组可见性与计数，不重新查询。

### 2.2 页面属性

```qml
property var allSkills: []
property var skills: []          // 搜索后
property string searchText: ""
property string sourceFilter: "all"
readonly property var projectSkills: skills.filter(s => s.source === "project")
readonly property var userSkills: skills.filter(s => s.source !== "project")
readonly property int resultCount:
    sourceFilter === "all" ? skills.length
    : sourceFilter === "project" ? projectSkills.length
    : userSkills.length
```

### 2.3 UI

| 控件 | objectName | 行为 |
|---|---|---|
| 搜索框 | `skillsSearchField` | 150ms 防抖 → `applySearch()`；清空恢复全部 |
| 来源筛选 | `skillsSourceFilter` | 全部/项目/用户；切换只改 `sourceFilter` |
| 结果计数 | `skillsResultCount` | “显示 X / 共 N · 项目 A · 用户 B” |
| 空态 | `skillsEmptyHint` | `allSkills.length===0` → “尚未发现技能”；有原始技能但过滤为 0 → “没有匹配的技能” |
| 分组 | `skillsProjectSection` / `skillsUserSection` | 按 `sourceFilter` 隐藏；badge 用搜索后的分组数量 |

- 搜索框放在工具条下方独立 RowLayout；与新建/刷新/打开目录同页。
- 刷新按钮保留当前 `searchText` 与 `sourceFilter`。
- 不做使用统计/排序（不在 S4 范围）。

### 2.4 交互细节

- `searchText` 变化：立即更新 `skills`（不必等防抖）＋防抖仅用于 UI 抖动；实现可选择“防抖后过滤”，测试需等待。
- `sourceFilter` 变化：仅绑定可见性/计数，不重置搜索。
- 空态文案：
  - `allSkills.length === 0` → “尚未发现技能”
  - 否则 `resultCount === 0` → “没有匹配的技能，请调整关键字或来源”
- 刷新后若当前搜索无匹配，保持关键字，不静默清空。

---

## 3. 测试

| 用例 | 断言 |
|---|---|
| 默认列表 | 2 行、resultCount=2、空态隐藏 |
| 搜索 name | `searchText="review"` → 1 行、resultCount=1 |
| 搜索 description | `searchText="评审"`（描述命中）→ 1 行 |
| 无匹配 | `searchText="zzz"` → 0 行、空态可见且文案为“没有匹配” |
| 来源筛选 | `sourceFilter="user"` → 项目分组隐藏、用户分组可见、resultCount=1 |
| 清空恢复 | 清空搜索 + all → 2 行、空态隐藏 |
| 刷新保留 | 设置 search/filter 后 refresh → 条件不变 |

---

## 4. 不做

- 不使用统计/排序控件。
- 不改 `SkillRegistry::search` 或 `AgentViewModel::listSkills` 签名。
- 不新增 QML 文件，不改 CMake。

---

## 5. 进度

| 项 | 状态 | 提交 | 验证 |
|---|---|---|---|
| 实现 | 已完成 | 见下条提交 | test_skills_qml 8.87s 全绿 |
| 审查 | 已完成 | - | 1 项 P2（来源空态误报）已修 |
| 验证 | 已完成 | - | 全量 59/59 + panel 1/1；qmllint 0 |

### S4 收口记录（2026-11-04）

- 数据流：`allSkills` 由 `refresh()` 一次性拉全量（`listSkills("", 200)`）；`skills = allSkills.filter(matchesSearch)`；搜索匹配 `name/description/id`、大小写不敏感；`searchText/sourceFilter` 为唯一事实源，刷新保留两者。
- UI：新增搜索框（150ms 防抖，清空立即恢复）、来源筛选 ComboBox（全部/项目/用户）、结果计数 `显示 X / 共 N · 项目 A · 用户 B`；分组可见性随筛选变化；刷新按钮保留当前条件。
- 审查修复（P2）：搜索把某来源全部过滤掉时，分组空提示仍显示“当前项目没有技能/还没有用户技能”，属误导。新增 `allProjectCount/allUserCount`（未过滤全量计数）；分组提示只在“该来源确实没有技能”时显示，否则显示“本组没有匹配的技能”或交由全局 `skillsEmptyHint`。
- 测试：新增搜索 name/description/id、无匹配空态、防抖与清空、来源切换、刷新保留、跨来源不误报、项目/用户/全部筛选可见性；既有列表/编辑器/开关/目录用例未改。
- 验证：全量 59/59（139.02s，`-j2`）+ panel 1/1（17.41s）；`qmllint_gate` Error 0；`check_sources` OK。
- 遗留（P3）：ComboBox `onActivated` 用户点击路径未自动化；连续输入“只应用一次”未断言；name/id 字段隔离度弱（fixture 的 name 与 id 相同）；`Layout.minimumWidth: 180` 为硬编码；筛选时计数仍显示被隐藏来源的数量。