# cmake/ — 构建校验脚本与构建拓扑

## 职责

- 存放根 `CMakeLists.txt` 引用的脚本：`CheckIconResources.cmake`（图标资源别名回归门禁）、`QmllintGate.cmake`（CTest 里的 qmllint 门禁）、`QmllintGuard.ps1`（qmllint 的内存/超时护栏）。
- 本身不定义目标；四个目标与资源/翻译/部署规则都在根 `CMakeLists.txt`。本文件只记录 `cmake/` 目录与整体构建拓扑；`src/` 的整体索引见 [src/codemap.md](../src/codemap.md)。

## 设计

### 脚本分工

| 文件 | 触发方式 | 关键输入 | 失败语义 |
| --- | --- | --- | --- |
| `CheckIconResources.cmake` | 自定义目标 `nexus_icon_qrc_check ALL`，依赖 `nexus_ai` | `QRC_FILE`（`<build>/.qt/rcc/app_icons.qrc`）、`EXPECTED_ALIASES` | 缺任一 `alias="nexus-<size>.png"` → `FATAL_ERROR`；`.qrc` 尚未生成时软跳过 |
| `QmllintGate.cmake` | CTest `qmllint_gate`（`cmake -P`） | `QMLLINT`、`RSP_DIR`（`<build>/.rcc/qmllint`）、`POWERSHELL`、`GUARD_SCRIPT` | 缺可执行/响应文件即失败；任一目标退出码非 0 或输出含 `Error: ` → `FATAL_ERROR` |
| `QmllintGuard.ps1` | 由 `QmllintGate.cmake` 在可用时调用 | `-Exe`、`-Rsp`、`-LimitMb`(1024)、`-TimeoutSec`(120) | 缺输入 exit 3；超内存 exit 4；超时 exit 5；否则透传 qmllint 退出码 |

- `QmllintGate.cmake` 顺序检查 `nexus_ai`、`nexus_viewmodels` 两个目标的响应文件；响应文件里没有 `.qml`/`.js` 时跳过（qmllint 对纯选项响应文件会返回 -1）。warning 级只打印，不阻断。
- 护栏用 `System.Diagnostics.Process` 异步排空 stdout/stderr，100ms 轮询 `WorkingSet64` 与耗时；触发上限即 Kill 并给出可诊断输出（qmllint 6.9 在病态 QML 上可吃数十 GB）。

### 根 CMakeLists 拓扑

```
nexus_core (STATIC, no Gui)
  └─ nexus_infra (STATIC, +Network)
       └─ nexus_viewmodels (STATIC + QML module NexusAI.ViewModels, +Quick/Qml/Pdf/crypt32)
            └─ nexus_ai (exe, +QuickControls2/Widgets, QML module NexusAI)
```

- **约束**：`nexus_core` 只链 `Qt6::Core/Sql/Concurrent/Network`，禁止 Gui；`Qt6::Pdf` 传递依赖 Gui，所以 `PdfTextExtractor` 放 `viewmodels`。`nexus_ai` 额外链 `nexus_viewmodelsplugin` 完成 QML 类型注册。
- **QML 模块**：`nexus_viewmodels` 用 `qt_add_qml_module(URI NexusAI.ViewModels OUTPUT_DIRECTORY <build>/NexusAI/ViewModels)`，`SOURCES` 列出单例头；`QML_IMPORT_PATH=<build>` 让 qmlcachegen/qmllint 解析。`nexus_ai` 的 `NexusAI` 模块用 `QML_FILES` 注册 `main.qml`、components、pages、panels、charts 与两个 `.js`。
- **Windows 资源**：`app.rc.in` 经 `configure_file` 生成到构建目录，图标用相对路径喂 windres；`APP_VERSION` 来自 `project(VERSION 0.2.0)`。
- **图标**：`qt_add_resources(... PREFIX "/icons" BASE "assets/icons")` 提供 `:/icons/nexus-{16,24,32,48,64,128,256}.png`，再由 `CheckIconResources.cmake` 锁定别名。
- **翻译**：`qt_add_translations(nexus_ai TS_FILES en/zh_CN SOURCE_TARGETS 四目标)` 生成并嵌入 `:/i18n`。
- **部署**：`install(TARGETS nexus_ai RUNTIME DESTINATION bin)` + `qt_generate_deploy_qml_app_script`；`scripts/deploy.ps1` 包装 build → install → 依赖/插件/`qt.conf` 校验 → 可选 zip。
- **测试**：`option(BUILD_TESTS "Build unit tests" OFF)`；开启后 `enable_testing()`、找 `Qt6::Test`、`add_subdirectory(tests)`。

## 数据与控制流

1. configure：`find_package(Qt6 ... Pdf)`、`qt_standard_project_setup(REQUIRES 6.5)`、设置 `QML_IMPORT_PATH`；`nexus_viewmodels` 的 `.qmltypes/qmldir` 生成到 `<build>/NexusAI/ViewModels`。
2. build：`rcc` 生成 `app_icons.qrc`，`lrelease` 生成 `.qm`；`nexus_icon_qrc_check` 在 `nexus_ai` 之后运行并读取 `.qrc` 断言别名。
3. test（`BUILD_TESTS=ON`）：`tests/CMakeLists.txt` 以 `-DQMLLINT=... -DRSP_DIR=<build>/.rcc/qmllint -DPOWERSHELL=... -DGUARD_SCRIPT=...` 调 `QmllintGate.cmake`；另有单测与 offscreen QML 渲染测试。
4. install/deploy：`cmake --install <build> --prefix <绝对目录>` 触发生成的部署脚本，产出 `bin/nexus_ai.exe`、Qt DLL、`plugins/`、`qml/`、`translations/`。

## 集成点

- 根 `CMakeLists.txt`：调用两个 `.cmake` 脚本、定义四目标与资源/翻译/部署；`tests/CMakeLists.txt` 是 `QmllintGate` 的唯一调用方（`NEXUS_QMLLINT`/`NEXUS_POWERSHELL` 用 `find_program` 定位）。
- `scripts/deploy.ps1`：依赖根 `CMakeLists.txt` 的 `install` 规则与 `project(VERSION)`；校验 `Qt6Core/Gui/Qml/Quick/Widgets/Pdf`、`bin/qt.conf`、`plugins/platforms/qwindows.dll`。
- `tests/CMakeLists.txt`：`test_viewmodel` 依赖 `NexusAI_lrelease` 并重新嵌入 `.qm`；QML 渲染测试依赖 `nexus_ai` 以同步 `build/NexusAI/` 文件系统副本。

## 关键文件

- `cmake/CheckIconResources.cmake`：`:/icons/` 别名构建期断言。
- `cmake/QmllintGate.cmake`：双目标 qmllint 门禁与结果聚合。
- `cmake/QmllintGuard.ps1`：内存上限/超时/输出排空。
- 只读关联：根 `CMakeLists.txt`、`scripts/deploy.ps1`、`tests/CMakeLists.txt`、`src/app.rc.in`。

## 约束与风险

- **`BASE "assets/icons"` 不可删**：缺省时资源别名保留源相对路径，`QIcon(":/icons/nexus-64.png")` 静默变空，托盘"没图标/关到托盘像坏了"；门禁正是为此存在。
- **门禁可能软跳过**：干净配置时 `.qrc` 尚未生成，`CheckIconResources` 只打印 STATUS；首次构建前不能据此判定通过。
- **qmllint 门禁默认不跑**：`BUILD_TESTS=OFF` 时 `qmllint_gate` 不存在；CI 必须显式开启。护栏仅在找到 PowerShell 且脚本存在时启用，否则退回 `execute_process(TIMEOUT 120)`（无内存上限）。
- **`OUTPUT_DIRECTORY` 是正确性要求**：静态 QML 模块的 `qmldir/qmltypes` 若落在构建根目录，AOT 代码会嵌入过期方法索引，恢复会话时调用 invokable 可能崩溃。
- **`QML_IMPORT_PATH` 必须指向构建目录**，否则 qmlcachegen/qmllint 解析不到 `NexusAI.ViewModels`。
- **无 `-Werror`**：`/W4` 或 `-Wall -Wextra` 只告警；新增代码不能依赖构建失败来发现警告。
- **部署目录必须绝对路径**：生成的部署脚本把绝对前缀写入 `bin/qt.conf`；`deploy.ps1` 会先转绝对路径，`-Clean` 只清 `bin/plugins/qml/translations`。
- **测试平台插件**：构建树只部署 `qwindows.dll`，offscreen 用例需 `QT_QPA_PLATFORM_PLUGIN_PATH` 指向 Qt SDK 的 platforms 目录。
