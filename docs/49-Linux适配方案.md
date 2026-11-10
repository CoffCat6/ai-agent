# 49 - Linux 适配方案

| | |
|---|---|
| **状态** | 阶段 0/1/2 代码已实施（D1–D4 全落地）；CI 仍在 Bring-up：此工作流此前从未成功运行，已修 aqt 参数/编码/磁盘目录名（CLI 令牌≠安装目录名）三连，runner 上个别测试静默失败待诊断（已加直接重跑诊断步） |
| **范围** | Linux 桌面（X11 / Wayland；GCC/Clang，Qt ≥ 6.5）。macOS 明确不做（§8） |
| **前置** | 全量只读分析：`linux/master@7790fabb`，约 282 个源文件逐文件核查 `Q_OS_WIN` 门控、进程/路径/编码/环境变量、构建与 CI |

## 1. 结论先行

代码层的可移植性设计远好于预期：Windows 专属代码几乎全部有 `Q_OS_WIN` 门控且带
POSIX `#else` 分支（沙箱、shell 执行、MCP stdio、路径大小写敏感策略全部就绪）。
真正的缺口集中在三类：

| 类 | 内容 | 规模 |
|---|---|---|
| **编译阻塞** | `ProjectFileViewModel.cpp` 一处无门控的注册表代码（§2.1） | 全仓仅此 1 处 |
| **管线空白** | install/部署/测试运行时/CI 全部只有 Windows 路径（§2.3） | 工作量主体 |
| **运行时缺口** | 凭据加密、开机自启、进程树清理等功能降级 + 桌面集成待验证（§2.2/§5） | 分阶段补齐 |

阶段 0 的验收标准：**Linux CI 全绿 + Windows CI/本地构建零回归**。此后 Linux 有了持续验证基线，后续阶段不再有"改一处坏 Windows"的盲区。

## 2. 现状盘点

### 2.1 编译阻塞（唯一硬失败点）

「shell 文件关联损坏时的紧急 open-with 回退」功能，从 `readRegString` 到
`candidateEditors` 约 150 行注册表代码完全无门控：

| 位置 | 内容 |
|---|---|
| `ProjectFileViewModel.cpp:17` | `#include <windows.h>` 无门控 |
| 同文件 :41–147 | `readRegString` / `readShellOpenCommand` / `cleanChildEnvironment` / `candidateEditors`（`HKEY`、`RegOpenKeyExW`、`ExpandEnvironmentStringsW`） |
| 同文件 :356 | `openWithKnownEditor()` 无条件调用 `candidateEditors()` |

修法（阶段 0）：`#ifdef Q_OS_WIN` 包裹 include 与四个静态函数；`openWithKnownEditor`
的探测循环同样门控，非 Windows 返回空串（UI 侧按"回退失败"处理）。Linux 对应能力
（`xdg-mime query default`）列阶段 2，编译期先不引入。

### 2.2 编译通过但功能缺失 / 降级

| 优先级 | 项目 | 位置 | 现状 → 目标 |
|---|---|---|---|
| 高（安全） | API Key 加密 | SettingsService.cpp:297–353 | Windows 走 DPAPI（`dpapi:v1:` 前缀，加密失败绝不落盘）；Linux `#else` **明文**写入 QSettings → QtKeychain（§3 D1） |
| 高（功能） | 开机自启动 | SettingsService.cpp:2260–2330、main.cpp:1013–1037 | 非 Windows 恒 `false`，失败弹窗文案是"无法更新 **Windows** 开机自启动设置" → XDG autostart（§3 D2） |
| 中 | shell_exec 进程树清理 | ToolRegistry.cpp:608/625/646 | Windows `taskkill /T /F`；Linux 只有 `proc.kill()`，孙进程残留孤儿 → 进程组（§3 D3） |
| 中 | QSaveFile 权限丢失 | ToolRegistry.cpp:279/431、ApplyProposalTool.cpp:219、SkillEditorService.cpp:574、McpConfig.cpp:68、McpConfigTransfer.cpp:151、AIAgent.cpp:1368、AgentViewModel.cpp:2088 | 暂存 → rename 替换后新文件拿 umask 默认权限，**原文件执行位丢失** → commit 前记录权限、commit 后恢复（§3 D4） |
| 中低 | 字体探测别名 | FontProbe.cpp:31–48 | Linux 返回 `"Sans Serif"`/`"Monospace"`，fontconfig 别名 `hasFamily()` 可能 false → `QFont().defaultFamily()`（§3 D6） |
| 中低 | 图形后端 | main.cpp:1044–1052 | Windows 钉 D3D11；Linux 未设（Qt 自动选）→ 不钉死，仅把渲染后端诊断日志扩展到非 Windows（§3 D5） |
| 低 | git 中文文件名 | GitService.cpp:198–280、ToolRegistry.cpp:923–956 | `LC_ALL=C` + 默认 `core.quotepath` → 中文路径显示八进制转义 → 统一 `-c core.quotepath=false`（§3 D7） |
| 低 | 既有 bug（平台无关） | ProjectFileViewModel.cpp:314/338、GitViewModel.cpp:693 | 对 `systemEnvironment()` 返回的**临时副本**调 `remove("ELECTRON_RUN_AS_NODE")` 是无效 no-op → `qunsetenv` 修复 |
| 低 | 提示词措辞 | ToolRegistry.cpp:519–526、AIAgent.cpp:196 | 工具描述写死"nested quotes break on Windows"，Linux 实际走 `/bin/sh -c` |
| 低 | 沙箱大小写 | Sandbox.h:520 | `isWithin()` 无条件 CaseInsensitive（全项目其它路径比较均按平台分支），Linux 上方向是"过拒"，安全无害，顺手对齐 |

### 2.3 构建 / 测试 / 部署 / CI

| 项目 | 位置 | 说明 |
|---|---|---|
| **install 规则全 WIN32** | CMakeLists.txt:566–576 | `install(TARGETS)` + `qt_generate_deploy_qml_app_script` 在 `if(WIN32)` 内；Linux 上 `cmake --install` 什么都不装 |
| **无部署脚本** | scripts/deploy.ps1 | Windows 一键打包（DLL 清单校验 / qt.conf 钉插件路径 / zip）；无 .sh 对应物 |
| **测试 DLL 解析靠 PATH** | tests/CMakeLists.txt:6–35 | shadow `add_test` 用 `path_list_prepend:PATH`；Linux 要 `LD_LIBRARY_PATH` + `lib/`（Qt 共享库不在 bin/） |
| **qmllint 守卫依赖 PowerShell** | cmake/QmllintGate.cmake:23–71 | 无 pwsh 时降级为裸 qmllint + 120s 超时（失去 1GB 内存上限）；ubuntu-latest **预装 pwsh**，走 guard 路径，首跑验证 |
| **CI 无 Linux job** | .github/workflows/ci.yml | 两个 job 全 `windows-latest`（MinGW debug/release） |
| **桌面集成资产缺失** | — | `.desktop`、hicolor 图标、`StartupWMClass`（Wayland 任务栏图标只认它）均不存在 |

Linux 依赖面：`Qt6::Pdf` 是额外模块（aqt `-m qtpdf`，gcc_64 架构可用）；运行时
`QDesktopServices` 依赖 `xdg-open`（xdg-utils）。

### 2.4 已就绪（无需改动，防重复排查）

- **沙箱完整可用**：Sandbox.h 有 POSIX `#else`（FHS 目录封锁 `/etc /usr /bin /sbin /boot /sys /proc`、`canonicalFilePath()` 穿 symlink、设备名拦截返回 false）；SandboxWin.cpp 整文件 `#ifdef` 内，Linux 编译为空 TU
- **shell_exec / MCP stdio**：`/bin/sh -c` 与直接 `setProgram/setArguments` 的 `#else` 已实现
- **路径大小写敏感**：GitService/GitViewModel/SkillRegistry/SkillEditorService/ProjectFileIndex/FileEditUtil 全部按平台正确分支
- **路径规范**：全仓统一 `QStandardPaths`（AppData/AppConfig/AppLocalData/Home），零 `C:\` 硬编码，`\\?\` 处理全在门控内
- **UI 层**：QML 零硬编码字体名（统一 `Theme.fontSans/fontMono`）；无 frameless/`setWindowFlags`（避开 Wayland 无边框窗口移植问题）；`QFileDialog` 零使用
- **单实例** `QLockFile`（QDir::temp）、托盘 `trayAvailable` 降级注入（main.cpp:1056–1061）、换行处理（无 CRLF 假设，`relocateHint` 内置 CRLF↔LF 双向探测）、编码回退（Linux local8Bit = UTF-8，无害）
- `QSysInfo::productType()` 自动让系统提示词显示 "linux"（AIAgent.cpp:167）

## 3. 关键设计决策

- **D1 API Key 加密**：QtKeychain（`Qt6Keychain::Qt6Keychain`）而非直连 libsecret——后者拖入 GLib/GObject 依赖栈，与纯 Qt 项目不搭；QtKeychain 统一封装 Secret Service（GNOME/桌面中性环境）与 KWallet（KDE）。**已实施**（2026-11-10）：`protectApiKey/unprotectApiKey` 增加「槽位」参数（profile 按 UUID、embedding/代理/legacy 各固定槽、webSearch 用各后端 settingKey）；QSettings 只落 `secret:v1:` 标记，密文在钥匙环；写入失败仍绝不回退明文。headless CI 无钥匙环服务，测试二进制（`NEXUSAI_TESTING`）经 `NEXUS_TEST_PLAINTEXT_KEYS=1` 环境变量显式切换到 `test-plain:v1:` 前缀格式（生产构建编译不到该路径，桌面测试运行也不触碰真实钥匙环）。**dpapi 密文不跨 OS 迁移**：Linux 首次使用需重录密钥。已知限制：删除 profile 不清理钥匙环残留条目（persist 是整表重写，无删除事件，后续补 DeletePasswordJob）。Ubuntu 依赖包：`qtkeychain-qt6-dev`。
- **D2 开机自启**：**已实施**——默认后端更名 `PlatformAutoStart`，Linux 分支写 `~/.config/autostart/nexusai.desktop`（QSaveFile 原子写、Exec 引号包裹 exe 全路径；禁用即删文件）；main.cpp 偏好↔状态协调块解出 Q_OS_WIN 门控；失败文案平台中性。
- **D3 进程树清理**：**已实施**——`setChildProcessModifier([]{ ::setpgid(0,0); })` 独立进程组（Qt 6.4+，exec 前回调无竞态）；超限/取消/超时三路径 `kill(-pgid)` SIGTERM + 2s 宽限 + SIGKILL 兜底。
- **D4 QSaveFile 权限保留**：**已实施**——新工具头 `core/utils/FilePermissions.h`（captureForReplace/restoreAfterReplace，Windows 门控为 no-op 保持既有行为），8 处 QSaveFile 调用点统一接入（write_file/edit_file/apply_proposal/skill 保存/mcp 配置两处/检查点恢复/会话与用量导出）。
- **D5 图形后端**：Linux 不钉死 RHI（Qt 自动选 OpenGL/Vulkan）；把 main.cpp:1080+ 的「后端 + 每屏 DPR」诊断日志移出门控，两平台统一输出。docs/47 的渲染层结论基于 D3D11 + DWM，**不可迁移**；其布局冻结策略可复用。
- **D6 字体探测**：Linux 分支改用 `QFont().defaultFamily()`（等宽用 `QFontDatabase` 探测 JetBrains Mono/DejaVu Sans Mono/Noto Sans Mono 链），避免 fontconfig 别名探测失败。
- **D7 git quotepath**：`GitService` 与 ToolRegistry 的 git 子工具统一追加 `-c core.quotepath=false`，中文路径不再转义。

## 4. 阶段计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| **0（本批）** | §2.1 编译门控；tests/CMakeLists 运行时解析按平台（`LD_LIBRARY_PATH`+`lib/`）；ci.yml 增 `linux-gcc` job（configure/build/ctest，暂不部署） | Linux CI 全绿；Windows CI/本地零回归 |
| **1 交付形态** | GNUInstallDirs install 规则（bin + `.desktop` + hicolor 图标 16–256 + `StartupWMClass=NexusAI`）；`scripts/deploy.sh`（**Qt deploy 脚本捆绑运行时 + appimagetool 纯封装 → AppImage**，deb 次选）；CI 增加 Linux artifact | `cmake --install` 系统布局正确；AppImage 通过脚本内完整性校验（Qt 库全部解析进 AppDir、QtQuick 模块在位） |
| **2 功能补齐** | §3 D1–D7 逐项落地 + §2.2 其余项（no-op bug、措辞、isWithin 对齐）；`openWithKnownEditor` 的 `xdg-mime` 实现顺带评估 | 各项带单测；密文格式有版本前缀、失败绝不落盘明文 |
| **3 真机验收** | §5 矩阵过一遍；readme 增 Linux 构建/数据位置章节 | 验收表全勾 |

## 5. 真机验证清单（阶段 3）

X11 + Wayland，GNOME（AppIndicator 扩展开/关）+ KDE 各一轮：

| 项 | 关注点 |
|---|---|
| 托盘 | StatusNotifierItem（GNOME 需扩展）；双击 = SNI `SecondaryActivate`；`trayAvailable=false` 时 close-to-tray 禁用 |
| 通知 | `tray->showMessage` → org.freedesktop.Notifications；无通知守护进程时不崩 |
| 几何/DPI | 物理像素持久化恢复正确；**Wayland 忽略窗口 x/y 属预期**（文档标注即可）；fractional scaling |
| 渲染 | 启动日志确认 RHI 后端（非 software）；缩放流畅度重新基准 |
| 桌面服务 | `QDesktopServices`（xdg-open）打开目录/文件/链接；剪贴板图片粘贴 |
| 拖拽 | 文件拖入 QML DropArea（Linux 原生支持，无需 UIPI 修复） |
| 沙箱 | 写允许层/拒绝层在 Linux 大小写敏感 FS 下行为抽查 |
| 数据位置 | `~/.config/NexusAI/`（QSettings ini）与 `~/.local/share/NexusAI/`（数据/日志/mcp_servers.json）落位正确 |

## 6. 数据位置对照（readme 更新素材）

| 数据 | Windows | Linux |
|---|---|---|
| QSettings | `HKCU\Software\NexusAI\NexusAI` | `~/.config/NexusAI/NexusAI.conf`（native 格式在非 Windows 为 ini） |
| API Key 密文 | DPAPI（`dpapi:v1:`，注册表内） | 阶段 2 后：Secret Service/KWallet（`secret:v1:`） |
| 用户数据/数据库 | `%APPDATA%\NexusAI`（AppDataLocation） | `~/.local/share/NexusAI` |
| 日志 | `%LOCALAPPDATA%\NexusAI\logs` | `~/.local/share/NexusAI/logs`（AppLocalDataLocation） |
| MCP 配置 | `%APPDATA%\NexusAI\mcp_servers.json` | `~/.local/share/NexusAI/mcp_servers.json` |

## 7. 风险与备忘

- **qmllint pwsh-on-Linux**：ubuntu-latest 预装 pwsh，`QmllintGuard.ps1` 的 .NET API（WorkingSet64/Stopwatch）在 Core 可用，`-ExecutionPolicy Bypass` 被接受；首跑若失败，兜底是给 `find_program` 加 `NO_DEFAULT_PATH` 或提供 shell 版守卫
- **aqt gcc_64 + qtpdf**：Qt 6.9.0 linux 桌面包带 `-m qtpdf` 可用（与 Windows 同源缓存策略）；若离线 apt 场景，发行版 qt6-pdf 包名待查
- **测试 GUI 依赖**：offscreen 平台仍需 libfontconfig/libglib 等运行时，CI 里显式 apt 安装最小集
- **AppImage 打包选型（阶段 1 实施定稿）**：放弃原推荐的 linuxdeploy-plugin-qt——它靠扫描 AppDir 内散落 .qml 文件判 QML 依赖，本项目 QML 全部 qrc 内嵌会扫空；改用 Qt 官方 deploy 脚本捆绑（与 Windows 侧同一机制，CI 已验证），**patchelf 是 rpath 改写硬依赖**，appimagetool 只做纯封装不做依赖分析，两层职责不重叠。appimagetool 用 continuous 版 + `--appimage-extract-and-run` 绕开 CI 无 libfuse2；aarch64 需 APPIMAGETOOL_URL 覆盖。首个 Linux CI 跑通前，deploy.sh 的捆绑/封装链路未经真机验证
- **ELECTRON_RUN_AS_NODE no-op**：平台无关既有 bug，与适配无关但顺路修复（改 `qunsetenv`，对本进程无影响——NexusAI 非 Electron 应用）

## 8. 明确不做

- **macOS**：无目标用户与验证环境；FontProbe 的 `#elif` 分支已存在，不构成阻塞
- **"在文件管理器中显示"**：docs/28 已决策（Linux 无统一方案，成本高于收益），不因适配翻案
- **UIPI 拖放修复的 Linux 对应**：`ChangeWindowMessageFilterEx` 是 Windows 完整性隔离专属，Linux 无此问题
- **单实例唤起已有实例**：main.cpp:255 注释预留的 QLocalServer 唤起属全平台二期功能，不并入本方案
