# Unit tests for the NexusAI core & infrastructure layers.
# Enabled with -DBUILD_TESTS=ON; run via ctest.

enable_testing()

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
nexus_add_test(test_rag test_rag.cpp)
nexus_add_test(test_agent test_agent.cpp)

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

# Batch 23 F11: syntax highlighting — needs QQuickTextDocument (QtQuick)
add_executable(test_highlighter test_highlighter.cpp)
target_link_libraries(test_highlighter PRIVATE
    nexus_core nexus_infra nexus_viewmodels Qt6::Test Qt6::Quick Qt6::Qml)
add_test(NAME test_highlighter COMMAND test_highlighter)

# Sampling-param normalizer (header-only, no service dependencies)
nexus_add_test(test_sampling_params test_sampling_params.cpp)

# Batch 30 (F6): multimodal data-URL parsing (pure string helpers)
nexus_add_test(test_data_url_utils test_data_url_utils.cpp)

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
