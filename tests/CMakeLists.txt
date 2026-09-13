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
add_test(NAME test_viewmodel COMMAND test_viewmodel)

add_executable(test_settings test_settings.cpp)
target_link_libraries(test_settings PRIVATE nexus_core nexus_infra nexus_viewmodels Qt6::Test)
add_test(NAME test_settings COMMAND test_settings)

# Batch 23 F11: syntax highlighting — needs QQuickTextDocument (QtQuick)
add_executable(test_highlighter test_highlighter.cpp)
target_link_libraries(test_highlighter PRIVATE
    nexus_core nexus_infra nexus_viewmodels Qt6::Test Qt6::Quick Qt6::Qml)
add_test(NAME test_highlighter COMMAND test_highlighter)

# Sampling-param normalizer (header-only, no service dependencies)
nexus_add_test(test_sampling_params test_sampling_params.cpp)
