# Unit tests for the NexusAI core & infrastructure layers.
# Enabled with -DBUILD_TESTS=ON; run via ctest.

enable_testing()

function(nexus_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE nexus_core nexus_infra Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nexus_add_test(test_sandbox test_sandbox.cpp)
nexus_add_test(test_token_estimator test_token_estimator.cpp)
nexus_add_test(test_llm_parsers test_llm_parsers.cpp)
nexus_add_test(test_memory test_memory.cpp)
nexus_add_test(test_rag test_rag.cpp)
nexus_add_test(test_agent test_agent.cpp)

# Needs the viewmodels library (AgentViewModel, SettingsService) in addition to core & infra
add_executable(test_viewmodel test_viewmodel.cpp)
target_link_libraries(test_viewmodel PRIVATE nexus_core nexus_infra nexus_viewmodels Qt6::Test)
add_test(NAME test_viewmodel COMMAND test_viewmodel)

add_executable(test_settings test_settings.cpp)
target_link_libraries(test_settings PRIVATE nexus_core nexus_infra nexus_viewmodels Qt6::Test)
add_test(NAME test_settings COMMAND test_settings)
