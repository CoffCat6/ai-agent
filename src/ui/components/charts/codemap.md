# src/ui/components/charts/ 架构地图

## 职责

本目录是**用量统计页的三张图表组件**，全部用 QML `Canvas` 2D 上下文**手绘**，不引入 Qt Charts（根 `CMakeLists.txt` 的 `find_package` 保持「核心组件 + Pdf」，新增图表依赖会破坏这条约束）：`DonutChart.qml`（环形图 + 图例，模型 Token 占比）、`LineTrendChart.qml`（折线 + 渐变面积，按天 Token 走势）、`BarCompareChart.qml`（横向柱状图，按模型对比 Token/调用次数）。三者都是**纯展示 + 交互**的哑组件：数据全部由调用方注入，组件不查库、不翻页、不认识「模型 / Token」等业务概念（只有 `unitLabel` / `centerLabel` / `mode` 三个文案开关）。

## 设计

### 统一契约与交互

`required property var series`：数组，每项 `{ label: string, value: number }`——组件只认这两个字段，缺 `label` 退化为空串、缺/坏 `value` 退化为 0。派生链：`slices`/`points`/`bars`（结构一致 `{label,value}`）→ `totalValue`/`maxValue` → `hasData`。交互状态：`hoverIndex`（鼠标）+ `keyIndex`（键盘）→ `highlightIndex = hoverIndex >= 0 ? hoverIndex : keyIndex`（悬停优先于键盘选中）；方向键在「整体概览（index = -1）」与各数据项间移动，越界钳制在首/末项。

可达性：`focusPolicy: Qt.StrongFocus` + `Keys.onLeftPressed/onRightPressed`；焦点环 `Rectangle{ border.color: Theme.brandPrimary }`；`Accessible.role: Accessible.Chart`，`Accessible.description` 绑定 `accessSummary`（由真实聚合数据生成，不伪造）。主题：只读 `Theme.*`；`paletteColors = [brandPrimary, statusSuccess, statusWarning, statusDanger]`，`seriesColor(i)` 用 `((i % n) + n) % n` 循环回绕；`onPaletteColorsChanged: canvas.requestPaint()` 保证明暗主题切换后重绘。

**安全**：模型名来自 LLM 网关响应（远端可控）。DonutChart 的模型名 `Text` 显式 `textFormat: Text.PlainText`；DonutChart 与 BarCompareChart 的 tooltip 拼串前用 `plainLabel()` 把 `<` 打断为 `\<`（`QQuickToolTip` 无 `textFormat`，否则 `<img src=...>` 会被当富文本渲染成信标）。BarCompareChart / LineTrendChart 的模型名与日期走 Canvas `ctx.fillText`（本身即纯文本）。

### 空值与除零

| 组件 | 空值处理 | 除零 / 非法值处理 |
|---|---|---|
| DonutChart | `value <= 0` 的项**直接过滤**，不参与占比；`slices` 空 → `hasData=false` → 「该范围内暂无数据」占位（不伪造数据） | `totalValue <= 0` 时 `percentText()` 返回 `"0.0%"`；`sectorGeometry()` 的 `outerR` 用 `Math.max(0, …)` 钳制（Canvas 负半径行为未定义） |
| LineTrendChart | 空序列 → 空态；**全 0 序列是真实数据**，画 0 基线平线 | `value` 非有限数记 0；`niceStep() <= 0` 记 1；`axisStep`/`axisMax` 非有限或非正回退（`axisMax` → `max(1, maxValue)`） |
| BarCompareChart | 空序列 → 空态；**value = 0 是真实数据**，画 0 长度柱 + 柱尾「0」 | `value` 非有限记 0；`axisStep`/`axisMax` 同上回退；`plotW/plotH` 用 `Math.max(1, …)` 防零除；`barH` 下限 3px |

### 绘制入口与尺寸自适应

每个组件内一个 `Canvas{ anchors.fill: parent }`，`onWidthChanged`/`onHeightChanged` → `requestPaint()`。显式重绘触发点：三者共有 `onSlices/Points/BarsChanged` + `onHighlightIndexChanged` + `onPaletteColorsChanged`；LineTrend 另有 `onAxisMaxChanged`，BarCompare 另有 `onModeChanged` + `onPadLeftChanged`；**LineTrend 与 BarCompare 另有 `onVisibleChanged: if (visible) canvas.requestPaint()`**——组件位于 `UsageStatsPage` 的 `Flickable` 内，初始可能在视口外，Canvas 在不可见期间会跳过 paint。

DonutChart `implicitWidth 360 / implicitHeight 220`：左 `chartArea` 用 `Layout.preferredWidth: height` 保持正方形，半径 `min(w,h)/2 - 8`，内半径 = 外半径 × 0.62。图例宽度钉在 `[120, 340]`，高度 `≤ root.height`，超出时 `Flickable` 垂直滚动；行内 = 色块 10px + 模型名（`Layout.minimumWidth: 0` 允许收缩触发 `ElideRight`）+ 百分比固定 44px 右对齐——保证任何模型名长度下百分比始终可见。LineTrendChart `400×220`：内边距 `padLeft 48 / padRight 16 / padTop 14 / padBottom 26`（左留 y 轴刻度、底留日期）；x 轴标签按可用宽度抽样（`maxLabels = max(2, floor(plotW/56))`，`stride = ceil(n/maxLabels)`），末点标签在间距 ≥30px 时补画，避免重叠。BarCompareChart `400×220`：`padLeft` 动态 = `max(56, min(width*0.42, 最长模型名实测宽+16))`——用隐藏 `Text{ id: labelMeasure }` 的 `implicitWidth` 实测自然宽；`padRight 54`（柱尾数值）、`padTop 8 / padBottom 24`；行高 `rowH = plotH/n`，柱高 `max(3, min(rowH*0.62, 22))`。

### 横向 vs 纵向柱图（关键决策）

模型名是长 ID（`deepseek-v4-flash-0731`、`gemini-3.6-flash-high`），纵向柱图的 x 轴标签只能分到「绘图区宽 / 模型数」，~560px 内容区下 7 个模型每槽仅 ~70px，名字必被截成 `deepseek-v4-f…`。改横向后模型名独占左侧一列（按最长名实测），名字完整可读，柱长表示数值。

### 命中测试、高亮、动画与高 DPI

- Donut `hitSector(x,y)`：按到环心距离过滤（`innerR` 与 `outerR+6` 之间），`atan2(dy,dx) + PI/2` 把 3 点方向基准换算成 12 点起顺时针（与绘制一致），累加 sweep 定位扇区。LineTrend `indexAtX(x)`：横坐标反算最近点（钳制 `[0,n-1]`）；鼠标在 `padLeft-8` / `width-padRight+8` 之外视为未命中。BarCompare `indexAtY(y)`：`floor((y-padTop)*n/plotH)` 得行号。
- Tooltip 三者均 `delay: 150`，格式「标签: 数值 单位」。高亮表现：Donut 扇区半径外扩 4px；LineTrend 竖虚线参考线（`setLineDash([3,3])`）+ 放大高亮点；BarCompare 整行底色（`Theme.surfaceHover`）+ 柱体 `brandPrimary` 描边。
- **无动画**：三图都没有 `NumberAnimation`/`Behavior`，重绘是即时的 `requestPaint()`，悬停高亮直接跳变。**高 DPI**：依赖 Qt `Canvas` 自身的设备像素比处理；组件内没有手写 `devicePixelRatio` 缩放，线宽/字号都是逻辑像素。

## 数据与控制流

### 注入契约（调用方负责）

```qml
DonutChart      { series: [{label, value}] }        // value = token 数
LineTrendChart  { series: [{label, value}] }        // label = "MM-dd"，value = 当日 token
BarCompareChart { series: [{label, value}], mode: "tokens" | "calls" }
```

### 与 UsageStatsPage 的转换关系

`UsageStatsPage` 是唯一调用方（三图各占一行卡片，卡片高 236 / 212 / `barCardHeight`）。转换链路：① `refresh()` 拉 `AgentVM.tokenStatsDetailed(30, from, to)` → `page.stats`；② `page.modelSeries`（`byModel` → 图表口径中间层）：`label = m.model`（空则 `qsTr("(未知)")`）、`tokens = inputTokens + outputTokens`、`calls = m.calls`；③ `page.donutSeries` = `modelSeries` 取 `tokens`，`page.barSeries` 按 `page.barMode` 取 `calls` 或 `tokens`（口径切换由页面上「Token / 调用次数」两个 `AppButton` 驱动，组件只通过 `mode` 换单位文案、不改数值）；④ `page.dayTrendSeries`：`byDay` 从 DB 按 **day DESC** 返回 → `.slice().reverse()` 转升序 → `label = String(d.day).substring(5)`（"yyyy-MM-dd" 取 "MM-dd"）、`value = inputTokens + outputTokens`；⑤ 组件侧再降一级到 `{label,value}`，`DonutChart` 的 `centerLabel` 由页面注入为 `qsTr("总 Token")`（组件默认「总量」），`BarCompareChart.mode` 与 `page.barMode` 同步。

DB 口径（`SqliteMemory::tokenStatsDetailed`）：`byModel` 行含 `model/calls/inputTokens/outputTokens/cacheHitTokens/costCents/avgLatencyMs`；`byDay` 行含 `day/calls/inputTokens/outputTokens/cacheHitTokens/costCents`，`day` 来自 `date(timestamp/1000,'unixepoch','localtime')`。`byDay` 行上限：范围模式 366 行、`recentDays` 回退 31 行。

### 渲染级回归

`tests/test_charts_qml.cpp` + `tests/qml/ChartsHarness.qml`：offscreen 平台加载真实组件（图表只在 `nexus_ai.exe` 的 qrc 里，测试用 URL 拦截器改写回 `build/NexusAI/` 文件系统副本），再做像素断言：品牌主色系像素 > 50、非透明像素 > 500、尺寸 > 100，以及 trend 派生链（`points.size()==19`、`hasData`、`axisMax` 有限且 ≥ 峰值）。

## 集成点

- **Theme**（`NexusAI.ViewModels` 单例）：颜色/字体/间距令牌唯一来源；`onPaletteColorsChanged` 是明暗主题切换后的重绘钩子。
- **UsageStatsPage**（`src/ui/pages/UsageStatsPage.qml`）：唯一消费者，注入 series / mode / centerLabel。
- **构建**：根 `CMakeLists.txt` 的 `qt_add_qml_module(nexus_ai URI NexusAI)` 用 **QTP0004 NEW** 把 `charts/` 子目录文件**平铺注册**进 `NexusAI` 模块（不建子模块），故 QML 类型名就是 `DonutChart` / `LineTrendChart` / `BarCompareChart`，不带目录前缀。
- **import 依赖**：三个组件自身只 `import NexusAI.ViewModels`（拿 Theme）；`NexusAI` 模块内的其它类型由宿主模块提供——测试夹具需同时 `import NexusAI` 与 `import NexusAI.ViewModels`。
- **测试**：`tests/CMakeLists.txt` 的 `test_charts_qml`（offscreen + 像素断言），依赖 `nexus_ai` 先构建以同步 `build/NexusAI/` 文件系统副本。

## 关键文件

- `src/ui/components/charts/DonutChart.qml`（376 行）、`LineTrendChart.qml`（331 行）、`BarCompareChart.qml`（334 行）
- `src/ui/pages/UsageStatsPage.qml`（调用方：series 映射、`barMode`、卡片高度）；`src/ui/components/UsageDetailTable.qml`（同页请求明细表）
- `src/viewmodels/ThemeProvider.h`（Theme 令牌）
- `src/core/memory/SqliteMemory.cpp`（`tokenStatsDetailed` 的 byModel/byDay 口径）、`src/core/memory/IMemory.h`（聚合接口范围语义）
- `tests/qml/ChartsHarness.qml`、`tests/test_charts_qml.cpp`、`tests/CMakeLists.txt`

## 约束与风险

- **禁止引入 Qt Charts**：`find_package(Qt6 …)` 必须保持「核心组件 + Pdf」，图表一律 Canvas 自绘。
- **`charts/` 目录不能移动**：QTP0004 NEW 是平铺注册，移动子目录会改变 qrc 路径并让 `import NexusAI` 解析失败；新增文件还要加进根 `CMakeLists.txt` 的 `QML_FILES`。
- **`onPaint` 必须整体 try-catch**：`LineTrendChart` 与 `BarCompareChart` 已加，`console.warn` 打出 w/h/点数/axisMax 作真机定位线索；**`DonutChart` 目前没有这层保护**，新增绘制逻辑出错会静默变成空白图（`clearRect` 已执行）——已知不一致点。
- **Canvas 异常静默**：历史上真机反馈「折线图完全空白」，根因是 `onPaint` 内 JS 异常被吞。改任何 `onPaint` 逻辑必须保持 try-catch 且不引入 NaN 坐标。
- **非有限值必须防御**：`value`/`axisStep`/`axisMax` 任一为 NaN/Inf 会让整条折线坐标变 NaN、绘制被静默跳过（画面全白）。派生链已逐层 `Number.isFinite` 兜底，新增字段沿用。
- **空值语义三者不一致**：Donut 过滤掉 `value <= 0`（「全 0 模型」退化成空态）；折线/柱图把 0 当真实数据画。同一份数据在两张图上「有没有数据」可能不同，属刻意但需知晓。
- **DonutChart 未做 `onVisibleChanged` 补绘**（另两张图有）：在 `Flickable` 内初始不可见时存在不重绘风险。
- **tooltip 不能直接渲染模型名**：`QQuickToolTip` 无 `textFormat`，DonutChart / BarCompareChart 必须 `plainLabel()` 打断 `<`，DonutChart 的模型名 `Text` 还要用 `Text.PlainText`。
- **不要在组件里加动画**：零动画是既定约束（重绘即 `requestPaint()`），补间会与 Canvas 的同步重绘模型冲突。
- **数据契约不能私自扩张**：组件只读 `label`/`value`；`tokens`/`calls` 这类双口径字段必须在调用方（`modelSeries`）折叠好再注入，`mode` 只影响单位文案。
- **`byDay` 有行上限**：范围模式最多 366 行、`recentDays` 回退最多 31 行；跨多年范围会被截断，页面暂无降采样提示。
