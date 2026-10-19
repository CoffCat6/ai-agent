# Unit tests for the NexusAI core & infrastructure layers.
# Enabled with -DBUILD_TESTS=ON; run via ctest.

enable_testing()

add_executable(test_chat_workflows test_chat_workflows.cpp)
target_link_libraries(test_chat_workflows PRIVATE
    nexus_core nexus_infra nexus_viewmodels nexus_viewmodelsplugin
    Qt6::Test Qt6::Qml Qt6::Quick)
target_compile_definitions(test_chat_workflows PRIVATE NEXUSAI_TESTING NEXUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
file(GLOB_RECURSE NEXUS_WORKFLOW_QML CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/*.qml")
qt_add_resources(test_chat_workflows "workflow_ui"
    PREFIX "/qt/qml/NexusAI" BASE "${CMAKE_SOURCE_DIR}" FILES ${NEXUS_WORKFLOW_QML}
    # docs/17 B6: SettingsCenterDialog 的搜索索引是 .js —— 上面的 glob 只
    # 匹配 *.qml，漏掉会让 main.qml 在测试里加载失败（设置中心不可用）。
    "${CMAKE_SOURCE_DIR}/src/ui/components/settings-search.js")
add_test(NAME test_chat_workflows COMMAND test_chat_workflows -platform offscreen)
set_tests_properties(test_chat_workflows PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms")

function(nexus_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE nexus_core nexus_infra Qt6::Test)
    target_compile_definitions(${name} PRIVATE NEXUSAI_TESTING)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nexus_add_test(test_sandbox test_sandbox.cpp)
nexus_add_test(test_logger test_logger.cpp)
nexus_add_test(test_token_estimator test_token_estimator.cpp)
nexus_add_test(test_llm_parsers test_llm_parsers.cpp)
# 协议注册表：描述符完整性与 profile 归一化回退
nexus_add_test(test_protocol_registry test_protocol_registry.cpp)
# OpenAI Responses 协议的报文转换与非流式解析
nexus_add_test(test_openai_responses test_openai_responses.cpp)
nexus_add_test(test_memory test_memory.cpp)
nexus_add_test(test_tools test_tools.cpp)
# docs/20 §5.2 (C2): PDF text extraction (PdfTextExtractor) lives in
# nexus_viewmodels, not nexus_core - Qt6::Pdf pulls Qt6::Gui along and core
# must stay UI-free (docs/13 §4.1). test_rag therefore links the viewmodels
# library; tests/resources/sample.pdf lives in the source tree.
add_executable(test_rag test_rag.cpp)
target_link_libraries(test_rag PRIVATE
    nexus_core nexus_infra nexus_viewmodels Qt6::Test Qt6::Qml)
target_compile_definitions(test_rag PRIVATE
    NEXUS_TEST_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
add_test(NAME test_rag COMMAND test_rag)

# 任务 #12（审查 M2）：VM 层字段契约用例构造 AgentViewModel，它 lives in
# nexus_viewmodels（nexus_add_test 只链 core+infra，见上 test_viewmodel 同因
# 单列）。用例本身仍不依赖 QML/Quick，链接集与 test_viewmodel 一致。
add_executable(test_agent test_agent.cpp)
target_link_libraries(test_agent PRIVATE
    nexus_core nexus_infra nexus_viewmodels Qt6::Test)
target_compile_definitions(test_agent PRIVATE NEXUSAI_TESTING)
add_test(NAME test_agent COMMAND test_agent)

# H1: proxy factory modes, real routing through a local HTTP proxy, readable
# failure for a dead proxy, and the SettingsService probe behind the UI button.
add_executable(test_network_proxy test_network_proxy.cpp)
target_link_libraries(test_network_proxy PRIVATE
    nexus_core nexus_infra nexus_viewmodels Qt6::Test)
target_compile_definitions(test_network_proxy PRIVATE NEXUSAI_TESTING)
add_test(NAME test_network_proxy COMMAND test_network_proxy)

# Provider lifecycle tests drive real HTTP through a scripted loopback server
add_executable(test_provider_lifecycle test_provider_lifecycle.cpp)
target_link_libraries(test_provider_lifecycle PRIVATE
    nexus_core nexus_infra Qt6::Test Qt6::Network)
add_test(NAME test_provider_lifecycle COMMAND test_provider_lifecycle)

# OllamaEmbedding batching/progress (FEAT-803), against a scripted local server
add_executable(test_embedding test_embedding.cpp)
target_link_libraries(test_embedding PRIVATE
    nexus_core nexus_infra Qt6::Test Qt6::Network)
add_test(NAME test_embedding COMMAND test_embedding)

# TEST-905: qmllint gate — error-level findings fail the test run
# (warning-level legacy noise is tolerated and printed).
add_test(NAME qmllint_gate
    COMMAND ${CMAKE_COMMAND}
        -DQMLLINT=${QT6_INSTALL_FULL_BINS}/qmllint.exe
        -DRSP_DIR=${CMAKE_BINARY_DIR}/.rcc/qmllint
        -P ${CMAKE_SOURCE_DIR}/cmake/QmllintGate.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Exercise transcript scrolling with real QML geometry and pointer/wheel input.
add_test(NAME test_transcript_scroll
    COMMAND $<TARGET_FILE:Qt6::qmltestrunner>
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml
        -platform offscreen)

# Needs the viewmodels library (AgentViewModel, SettingsService) in addition to core & infra
add_executable(test_viewmodel test_viewmodel.cpp)
target_link_libraries(test_viewmodel PRIVATE nexus_core nexus_infra nexus_viewmodels Qt6::Test)
# Batch 24 G9: the i18n tests load ":/i18n/nexus_ai_*.qm" — embed the app's
# generated .qm files into this test binary too (the app's own resource
# bundle lives in the nexus_ai target, unreachable from here). The lrelease
# dependency guarantees the .qm files exist before rcc runs.
add_dependencies(test_viewmodel NexusAI_lrelease)
# QT_RESOURCE_ALIAS must name the file under the PREFIX; absolute build-tree
# paths are rejected by qt_add_resources otherwise.
set_source_files_properties(${CMAKE_BINARY_DIR}/nexus_ai_zh_CN.qm PROPERTIES
    GENERATED TRUE
    QT_RESOURCE_ALIAS "nexus_ai_zh_CN.qm")
set_source_files_properties(${CMAKE_BINARY_DIR}/nexus_ai_en.qm PROPERTIES
    GENERATED TRUE
    QT_RESOURCE_ALIAS "nexus_ai_en.qm")
qt_add_resources(test_viewmodel "i18n_test"
    PREFIX "/i18n"
    FILES
        ${CMAKE_BINARY_DIR}/nexus_ai_zh_CN.qm
        ${CMAKE_BINARY_DIR}/nexus_ai_en.qm
)
add_test(NAME test_viewmodel COMMAND test_viewmodel)

add_executable(test_settings test_settings.cpp)
# Qt6::Quick/Qt6::Qml: the combo-echo regression loads QtQuick.Controls items
target_link_libraries(test_settings PRIVATE nexus_core nexus_infra nexus_viewmodels Qt6::Test Qt6::Qml Qt6::Quick)
add_test(NAME test_settings COMMAND test_settings)

add_executable(test_settings_backup test_settings_backup.cpp)
target_link_libraries(test_settings_backup PRIVATE
    nexus_core nexus_infra nexus_viewmodels Qt6::Test Qt6::Qml)
add_test(NAME test_settings_backup COMMAND test_settings_backup)

# Batch 23 F11: syntax highlighting — needs QQuickTextDocument (QtQuick)
add_executable(test_highlighter test_highlighter.cpp)
target_link_libraries(test_highlighter PRIVATE
    nexus_core nexus_infra nexus_viewmodels Qt6::Test Qt6::Quick Qt6::Qml)
add_test(NAME test_highlighter COMMAND test_highlighter)

# docs/20 §5.1 (CITE-801): CitationParser 的 QML 通道回归。用真实
# QQmlApplicationEngine 加载 import NexusAI.ViewModels 的夹具 —— 类型没注册
# 成功时 AssistantMessage 的引用 chip 会静默不渲染，没有任何报错，故必须
# 有一条真跑 QML 的用例把守。
add_executable(test_citation_qml test_citation_qml.cpp)
target_link_libraries(test_citation_qml PRIVATE
    nexus_core nexus_infra nexus_viewmodels nexus_viewmodelsplugin
    Qt6::Test Qt6::Qml Qt6::Quick)
target_compile_definitions(test_citation_qml PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME test_citation_qml COMMAND test_citation_qml)
# offscreen：CI 无显示器，而本用例要建真实 QQuickWindow。与 test_droparea /
# test_settings_edit_lifecycle 同一套 env（构建树只部署 qwindows.dll，需指向
# SDK 的 platform 插件目录）。
set_tests_properties(test_citation_qml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms")

# Sampling-param normalizer (header-only, no service dependencies)
nexus_add_test(test_sampling_params test_sampling_params.cpp)

# Batch 30 (F6): multimodal data-URL parsing (pure string helpers)
nexus_add_test(test_data_url_utils test_data_url_utils.cpp)
nexus_add_test(test_attachment_classifier test_attachment_classifier.cpp)

# G2: project file index + '@' mention parsing. The popup and the
# submit-time resolver share this implementation, so ordering/rejection
# rules are locked here rather than in QML.
nexus_add_test(test_project_files test_project_files.cpp)

# docs/20 §3.4（A4）：拖放边界回归。QML 的 url 对象没有 toLocalFile()，
# 在 QML 里转路径会抛 TypeError 让拖拽静默失效；本用例用真实拖放事件走
# 一遍 delivery agent，锁住"urls 原样交给 C++ 的 QList<QUrl>"这一契约。
# 需要 Qt Quick 与 offscreen 平台（无真实显示也能投递合成拖放事件）。
add_executable(test_droparea test_droparea.cpp)
target_link_libraries(test_droparea PRIVATE Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick)
# QML fixtures (DropAreaHarness.qml / UrlApiProbe.qml) are loaded by path.
target_compile_definitions(test_droparea PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
# Use offscreen like test_transcript_scroll so the suite stays headless. The
# build tree only deploys qwindows.dll, so point Qt at the SDK's plugin dir
# (QT6_INSTALL_PREFIX is the Qt root; QT6_INSTALL_PLUGINS is relative to it).
# test_droparea itself forces contentItem geometry, which offscreen never sizes.
add_test(NAME test_droparea
    COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms"
        $<TARGET_FILE:test_droparea> -platform offscreen)

# docs/20 §4.1：设置项"改完没保存"回归。分类页挂在 Loader 上，切换分类会销毁
# 页面，而销毁不触发 editingFinished —— 通用页三个文本框的编辑因此静默丢失。
# 本用例驱动同一生命周期，断言防抖 + Component.onDestruction 能把值送达 sink，
# 并反向锁定"editingFinished 单独存在时确实会丢"（防止断言失去意义）。
add_executable(test_settings_edit_lifecycle test_settings_edit_lifecycle.cpp)
target_link_libraries(test_settings_edit_lifecycle PRIVATE
    Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(test_settings_edit_lifecycle PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME test_settings_edit_lifecycle
    COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms"
        $<TARGET_FILE:test_settings_edit_lifecycle> -platform offscreen)

# Batch 31a (F9): fake MCP stdio server + client tests. The fake server is
# a separate executable the transport spawns as a child process.
add_executable(fake_mcp_server fake_mcp_server_main.cpp)
target_link_libraries(fake_mcp_server PRIVATE Qt6::Core)
add_executable(test_mcp test_mcp.cpp)
target_link_libraries(test_mcp PRIVATE nexus_core nexus_infra Qt6::Test)
add_dependencies(test_mcp fake_mcp_server)
add_test(NAME test_mcp COMMAND test_mcp)

# T6 (F3): web search tool. Uses a fake backend for deterministic results.
add_executable(test_web_search test_web_search.cpp)
target_link_libraries(test_web_search PRIVATE nexus_core nexus_infra Qt6::Test)
add_test(NAME test_web_search COMMAND test_web_search)

# T8 (F4): episodic memory service.
add_executable(test_episodic test_episodic.cpp)
target_link_libraries(test_episodic PRIVATE nexus_core nexus_infra Qt6::Test)
add_test(NAME test_episodic COMMAND test_episodic)
# 任务 20：统计页三图表的渲染级回归。用户真机反馈 LineTrendChart 图表
# 区域完全空白（数据链路已实证正常：d30 有 19 天数据、总计 1,676,757），
# 而既有测试无一真正加载过这三个 QML 组件——qmllint 是静态检查、
# test_memory 是 C++ 数据层。本用例用 QQmlApplicationEngine 加载
# build/NexusAI/ 文件系统副本里的真实组件（图表 qrc 只在 nexus_ai.exe
# 里，测试进程访问不到；qmldir 的 prefer 指向该 qrc 且只做优先、不做
# 失败回退，由测试内 URL 拦截器重写回文件系统副本，见 cpp 注释），
# 再对 grabImage 结果做像素断言：
#   1. 每图表品牌主色系像素（与 Theme.brandPrimary RGB 距离 < 0.15）
#      > 50 —— trend 失败即复现用户反馈的空白 bug；
#   2. 图表有可见内容（非透明像素 > 500）；
#   3. 尺寸 > 100（布局未塌陷）；
#   4. trend 数据派生链：points=19、hasData、axisMax 有限且覆盖峰值。
# offscreen 平台（headless 稳定，Canvas 已实证能完整渲染）；构建树只
# 部署 qwindows.dll，需指向 SDK 的 platform 插件目录（同 test_droparea）。
add_executable(test_charts_qml test_charts_qml.cpp)
target_link_libraries(test_charts_qml PRIVATE
    nexus_core nexus_infra nexus_viewmodels nexus_viewmodelsplugin
    Qt6::Test Qt6::Qml Qt6::Quick)
# NEXUS_TEST_QML_DIR: ChartsHarness.qml 所在目录；
# NEXUS_BUILD_DIR: build/NexusAI/qmldir（模块 NexusAI 的文件系统副本）
# 所在目录，测试引擎 addImportPath 用。
target_compile_definitions(test_charts_qml PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    NEXUS_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME test_charts_qml
    COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms"
        $<TARGET_FILE:test_charts_qml> -platform offscreen)

# F1: TodoListPanel 渲染/状态回归。与 test_charts_qml 共用「build/NexusAI
# 文件系统副本 + qrc 回退拦截器 + offscreen」方案，锁住空态不占位、三态
# 映射、折叠/展开和像素级非空白。
add_executable(test_todo_qml test_todo_qml.cpp)
target_link_libraries(test_todo_qml PRIVATE
    nexus_core nexus_infra nexus_viewmodels nexus_viewmodelsplugin
    Qt6::Test Qt6::Qml Qt6::Quick)
target_compile_definitions(test_todo_qml PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    NEXUS_BUILD_DIR="${CMAKE_BINARY_DIR}")
# 保证 build/NexusAI/ 下的 QML 文件系统副本（含新增 TodoListPanel.qml）
# 在测试运行前已同步。
add_dependencies(test_todo_qml nexus_ai)
add_test(NAME test_todo_qml
    COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms"
        $<TARGET_FILE:test_todo_qml> -platform offscreen)

# G2: the '@' reference status row is the only feedback telling users whether
# a mention will actually be attached. Render it for real (same filesystem
# copy + offscreen setup as test_todo_qml) so "logic wired, row invisible"
# cannot regress silently.
add_executable(test_reference_row_qml test_reference_row_qml.cpp)
target_link_libraries(test_reference_row_qml PRIVATE
    nexus_core nexus_infra nexus_viewmodels nexus_viewmodelsplugin
    Qt6::Test Qt6::Qml Qt6::Quick)
target_compile_definitions(test_reference_row_qml PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    NEXUS_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_dependencies(test_reference_row_qml nexus_ai)
add_test(NAME test_reference_row_qml
    COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms"
        $<TARGET_FILE:test_reference_row_qml> -platform offscreen)

# G4: FileChangesPanel 渲染/状态回归（空态不占位、行状态映射、折叠、像素级
# 非空白）。与 test_todo_qml 同一套文件系统副本 + offscreen 方案。
add_executable(test_file_changes_qml test_file_changes_qml.cpp)
target_link_libraries(test_file_changes_qml PRIVATE
    nexus_core nexus_infra nexus_viewmodels nexus_viewmodelsplugin
    Qt6::Test Qt6::Qml Qt6::Quick)
target_compile_definitions(test_file_changes_qml PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    NEXUS_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_dependencies(test_file_changes_qml nexus_ai)
add_test(NAME test_file_changes_qml
    COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms"
        $<TARGET_FILE:test_file_changes_qml> -platform offscreen)

# G2: '@' completion keyboard/mouse interaction. Real QTest key and click
# events against the real InputConsole; guards Enter/Tab/Up-Down/click
# acceptance, which the parser and rendering tests cannot see.
add_executable(test_mention_interaction test_mention_interaction.cpp)
target_link_libraries(test_mention_interaction PRIVATE
    nexus_core nexus_infra nexus_viewmodels nexus_viewmodelsplugin
    Qt6::Test Qt6::Qml Qt6::Quick)
target_compile_definitions(test_mention_interaction PRIVATE
    NEXUS_TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    NEXUS_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_dependencies(test_mention_interaction nexus_ai)
add_test(NAME test_mention_interaction
    COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms"
        $<TARGET_FILE:test_mention_interaction> -platform offscreen)

