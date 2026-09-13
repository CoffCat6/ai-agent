cmake_minimum_required(VERSION 3.24)
project(NexusAI VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt 6 ──────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Qml Quick Sql Network Concurrent QuickControls2 LinguistTools
)

qt_standard_project_setup(REQUIRES 6.5)

# qmlcachegen / qmllint: resolve the NexusAI.ViewModels module types. The
# module's <module>.qmltypes is generated into the build root, so the build
# dir acts as the extra import path for QML ahead-of-time compilation.
set(QML_IMPORT_PATH ${CMAKE_BINARY_DIR} CACHE STRING "QML import paths" FORCE)

# Warnings on all targets (no -Werror: third-party/generated code must
# still build with newer toolchains)
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Set Qt policies for QML module
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# ── Core Library (no Qt UI dependency) ────────────────────
add_library(nexus_core STATIC
    src/core/agent/AgentState.h
    src/core/agent/LLMTypes.h
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
    src/core/utils/Logger.h
    src/core/utils/Logger.cpp
    src/core/utils/Activity.h
    src/core/utils/Activity.cpp
)
target_include_directories(nexus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nexus_core PUBLIC Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Network)  # Network: Batch 20 http_fetch

# ── Infrastructure Library ────────────────────────────────
add_library(nexus_infra STATIC
    src/infrastructure/llm/IAIService.h
    src/infrastructure/llm/LLMServiceBase.h
    src/infrastructure/llm/LLMServiceBase.cpp
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

# ── ViewModels Library (C++ QML module with UI singletons) ──
# The five singleton classes are registered into the NexusAI.ViewModels
# module (QML_SINGLETON + create() factories); main.cpp grabs them via
# instance() first to inject dependencies before the engine loads the UI.
# TraceModel stays a plain library class: QML reaches it through the
# AgentVM.traceModel property, not by type name.
add_library(nexus_viewmodels STATIC
    src/viewmodels/TraceModel.h
    src/viewmodels/TraceModel.cpp
    src/viewmodels/LogModel.h
    src/viewmodels/LogModel.cpp
    src/viewmodels/ChatSessionController.h
    src/viewmodels/ChatSessionController.cpp
)
qt_add_qml_module(nexus_viewmodels
    URI NexusAI.ViewModels
    VERSION 1.0
    SOURCES
        src/viewmodels/ThemeProvider.h
        src/viewmodels/ThemeManager.h
        src/viewmodels/ThemeManager.cpp
        src/viewmodels/CodeHighlighter.h
        src/viewmodels/CodeHighlighter.cpp
        src/viewmodels/SettingsService.h
        src/viewmodels/SettingsService.cpp
        src/viewmodels/ConnectionTester.h
        src/viewmodels/ConnectionTester.cpp
        src/viewmodels/AgentViewModel.h
        src/viewmodels/AgentViewModel.cpp
        src/viewmodels/PreferenceRegistry.h
        src/viewmodels/PreferenceRegistry.cpp
        src/viewmodels/ChangeBus.h
        src/viewmodels/ChangeBus.cpp
        src/viewmodels/PreferenceStore.h
        src/viewmodels/PreferenceStore.cpp
        src/viewmodels/FontProbe.h
        src/viewmodels/FontProbe.cpp
        src/viewmodels/LocaleProbe.h
        src/viewmodels/LocaleProbe.cpp
        src/viewmodels/I18nManager.h
        src/viewmodels/I18nManager.cpp
        src/viewmodels/EditBuffer.h
        src/viewmodels/EditBuffer.cpp
)
target_include_directories(nexus_viewmodels PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # qmltyperegistration includes the module SOURCES by bare file name
    # (<AgentViewModel.h>), so the class directory must be on the path
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodels
)
target_link_libraries(nexus_viewmodels PUBLIC nexus_core nexus_infra Qt6::Quick Qt6::Qml)
if(WIN32)
    # DPAPI (CryptProtectData) for encrypted API key storage
    target_link_libraries(nexus_viewmodels PUBLIC crypt32)
endif()

# ── Main Executable ───────────────────────────────────────
qt_add_executable(nexus_ai
    src/main.cpp
)

# Windows VERSIONINFO resource: single source of truth is project(VERSION)
if(WIN32)
    configure_file(src/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(nexus_ai PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    target_compile_definitions(nexus_ai PRIVATE APP_VERSION="${PROJECT_VERSION}")
endif()

# QML module with singleton Theme
qt_add_qml_module(nexus_ai
    URI NexusAI
    VERSION 1.0
    QML_FILES
        src/ui/main.qml
        src/ui/components/AppButton.qml
        src/ui/components/AppIcon.qml
        src/ui/components/AppToast.qml
        src/ui/components/CodeBlock.qml
        src/ui/components/CopyButton.qml
        src/ui/components/DialogBackground.qml
        src/ui/components/IconButton.qml
        src/ui/components/SettingsCenterDialog.qml
        src/ui/components/CategoryNav.qml
        src/ui/components/PreferenceControl.qml
        src/ui/components/AuditDialog.qml
    src/ui/components/LogPanel.qml
        src/ui/components/KnowledgeDocDialog.qml
        src/ui/components/AgentTraceNode.qml
        src/ui/components/InputConsole.qml
        src/ui/components/ModelSelector.qml
        src/ui/components/Sidebar.qml
        src/ui/components/SessionTabs.qml
        src/ui/components/SplitBar.qml
        src/ui/components/TopBar.qml
        src/ui/components/ToolConfirmDialog.qml
        src/ui/panels/ContextPanel.qml
        src/ui/pages/AgentChatPage.qml
        src/ui/pages/GeneralPage.qml
        src/ui/pages/InterfacePage.qml
        src/ui/pages/ModelConfigPage.qml
)

target_link_libraries(nexus_ai PRIVATE
    nexus_core
    nexus_infra
    nexus_viewmodels
    nexus_viewmodelsplugin   # QML module registration for NexusAI.ViewModels
    Qt6::Quick
    Qt6::QuickControls2
)

# ── Windows deploy ────────────────────────────────────────
if(WIN32)
    set_target_properties(nexus_ai PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ── Install / deployment (Batch 21, G7) ───────────────────
# `cmake --install build --prefix <dir>` produces a self-contained folder:
# the exe plus Qt runtime dependencies resolved by the generated deploy
# script (windeployqt equivalent). scripts/deploy.ps1 wraps this.
if(WIN32)
    # exe must live in bin/ next to the Qt runtime: the generated deploy
    # script places DLLs and bin/qt.conf (Prefix=..) under the install
    # prefix's bin directory, and Windows DLL search starts at the exe dir.
    install(TARGETS nexus_ai RUNTIME DESTINATION bin)
    qt_generate_deploy_qml_app_script(
        TARGET nexus_ai
        OUTPUT_SCRIPT nexus_ai_deploy_script
    )
    install(SCRIPT ${nexus_ai_deploy_script})
endif()

# ── Translations (Batch 8 FEAT-804) ───────────────────────
# Infrastructure only: .ts is generated/merged by lupdate over the C++ and
# QML sources; the .qm is embedded under :/i18n and loaded at startup when
# it matches the system locale. Untranslated strings fall back to source.
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(nexus_ai
    TS_FILES ${CMAKE_SOURCE_DIR}/translations/nexus_ai_zh_CN.ts
             ${CMAKE_SOURCE_DIR}/translations/nexus_ai_en.ts
    SOURCE_TARGETS nexus_core nexus_infra nexus_viewmodels nexus_ai
)

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
