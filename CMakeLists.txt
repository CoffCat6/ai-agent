cmake_minimum_required(VERSION 3.24)
project(NexusAI VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt 6 ──────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Quick Sql Network Concurrent QuickControls2
)

qt_standard_project_setup(REQUIRES 6.5)

# Set Qt policies for QML module
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# ── Core Library (no Qt UI dependency) ────────────────────
add_library(nexus_core STATIC
    src/core/agent/AgentState.h
    src/core/agent/LLMTypes.h
    src/core/agent/Task.h
    src/core/agent/AIAgent.h
    src/core/agent/AIAgent.cpp
    src/core/memory/IMemory.h
    src/core/memory/SqliteMemory.h
    src/core/memory/SqliteMemory.cpp
    src/core/tools/ITool.h
    src/core/tools/ToolRegistry.h
    src/core/tools/ToolRegistry.cpp
    src/core/tools/Sandbox.h
    src/core/rag/VectorStore.h
    src/core/rag/DocumentRetriever.h
    src/core/rag/RagService.h
    src/core/rag/RagService.cpp
    src/core/rag/KnowledgeSearchTool.h
    src/core/utils/TokenEstimator.h
)
target_include_directories(nexus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nexus_core PUBLIC Qt6::Core Qt6::Sql Qt6::Concurrent)

# ── Infrastructure Library ────────────────────────────────
add_library(nexus_infra STATIC
    src/infrastructure/llm/IAIService.h
    src/infrastructure/llm/OpenAIService.h
    src/infrastructure/llm/OpenAIService.cpp
    src/infrastructure/llm/OllamaService.h
    src/infrastructure/llm/OllamaService.cpp
    src/infrastructure/llm/AnthropicService.h
    src/infrastructure/llm/AnthropicService.cpp
    src/infrastructure/embed/OllamaEmbedding.h
    src/infrastructure/embed/OllamaEmbedding.cpp
)
target_include_directories(nexus_infra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nexus_infra PUBLIC nexus_core Qt6::Network)

# ── ViewModels Library ────────────────────────────────────
add_library(nexus_viewmodels STATIC
    src/viewmodels/ThemeManager.h
    src/viewmodels/ThemeManager.cpp
    src/viewmodels/ThemeProvider.h
    src/viewmodels/SettingsService.h
    src/viewmodels/SettingsService.cpp
    src/viewmodels/ConnectionTester.h
    src/viewmodels/ConnectionTester.cpp
    src/viewmodels/AgentViewModel.h
    src/viewmodels/AgentViewModel.cpp
)
target_include_directories(nexus_viewmodels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nexus_viewmodels PUBLIC nexus_core nexus_infra Qt6::Quick)
if(WIN32)
    # DPAPI (CryptProtectData) for encrypted API key storage
    target_link_libraries(nexus_viewmodels PUBLIC crypt32)
endif()

# ── Main Executable ───────────────────────────────────────
qt_add_executable(nexus_ai
    src/main.cpp
)

# QML module with singleton Theme
qt_add_qml_module(nexus_ai
    URI NexusAI
    VERSION 1.0
    QML_FILES
        src/ui/main.qml
        src/ui/components/AppButton.qml
        src/ui/components/CodeBlock.qml
        src/ui/components/CopyButton.qml
        src/ui/components/SettingsDialog.qml
        src/ui/components/AuditDialog.qml
        src/ui/components/AgentTraceNode.qml
        src/ui/components/InputConsole.qml
        src/ui/components/Sidebar.qml
        src/ui/components/TopBar.qml
        src/ui/components/ToolConfirmDialog.qml
        src/ui/panels/ContextPanel.qml
        src/ui/pages/AgentChatPage.qml
    RESOURCES
        resources/icons/send.svg
)

target_link_libraries(nexus_ai PRIVATE
    nexus_core
    nexus_infra
    nexus_viewmodels
    Qt6::Quick
    Qt6::QuickControls2
)

# ── Windows deploy ────────────────────────────────────────
if(WIN32)
    set_target_properties(nexus_ai PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ── Tests (optional) ──────────────────────────────────────
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
        enable_testing()
        find_package(Qt6 REQUIRED COMPONENTS Test)
        add_subdirectory(tests)
    else()
        message(WARNING "BUILD_TESTS is ON but tests/CMakeLists.txt does not exist yet - skipping tests")
    endif()
endif()
