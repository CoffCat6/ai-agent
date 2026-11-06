# QmllintGate.cmake — CTest gate for Batch 9 (TEST-905) / docs/44 §5.5.
#
# 检查目标（响应文件由构建系统生成，本脚本只消费）：
#   nexus_ai / nexus_viewmodels — qt_add_qml_module 按 QML_FILES 生成；
#   nexus_style                 — 根 CMakeLists.txt 在 configure 阶段生成。
#                                 样式文件走 qt_add_resources，不在任何
#                                 QML_FILES 中，必须单独纳入门禁，否则
#                                 src/ui/style/NexusStyle/*.qml 无人检查。
#
# 判定语义：
#   Error 级诊断数必须为 0。Warning 允许，但会打印计数（现有基线噪声为
#   C++ 暴露 QColor 属性的 unresolved-type，属已知可容忍项）。
#
# qmllint 必须真正执行成功：缺可执行文件、崩溃、超时、内存失控都算门禁失败。
# QmllintGuard.ps1 给 qmllint 6.9 加内存上限（病态 QML 可吃掉数十 GB）。
if(NOT QMLLINT OR NOT RSP_DIR)
    message(FATAL_ERROR "QmllintGate requires QMLLINT and RSP_DIR")
endif()
if(NOT EXISTS "${QMLLINT}")
    message(FATAL_ERROR "qmllint gate: qmllint executable not found: ${QMLLINT}")
endif()

set(use_guard FALSE)
if(POWERSHELL AND GUARD_SCRIPT AND EXISTS "${POWERSHELL}" AND EXISTS "${GUARD_SCRIPT}")
    set(use_guard TRUE)
endif()

set(failed_targets "")
foreach(target nexus_ai nexus_viewmodels nexus_style)
    set(rsp "${RSP_DIR}/${target}.rsp")
    if(NOT EXISTS "${rsp}")
        # nexus_style.rsp 由根 CMakeLists.txt 在 configure 阶段写入；缺失通
        # 常意味着改了 CMakeLists 却还没重新 configure。
        message(FATAL_ERROR
            "qmllint gate: missing response file ${rsp}; re-run the CMake configure step")
    endif()
    file(STRINGS "${rsp}" rsp_lines)
    set(has_sources FALSE)
    foreach(line IN LISTS rsp_lines)
        if(line MATCHES "\\.(qml|js)$")
            set(has_sources TRUE)
            break()
        endif()
    endforeach()
    if(NOT has_sources)
        # qmllint exits -1 on an options-only response file.
        message(STATUS "qmllint gate: ${target} has no QML sources; skipped")
        continue()
    endif()
    set(out "")
    set(err "")
    set(rc "")
    if(use_guard)
        execute_process(
            COMMAND "${POWERSHELL}" -NoProfile -NonInteractive
                -ExecutionPolicy Bypass -File "${GUARD_SCRIPT}"
                -Exe "${QMLLINT}" -Rsp "${rsp}"
                -LimitMb 1024 -TimeoutSec 120
            OUTPUT_VARIABLE out
            ERROR_VARIABLE err
            RESULT_VARIABLE rc
        )
    else()
        execute_process(
            COMMAND "${QMLLINT}" "@${rsp}"
            OUTPUT_VARIABLE out
            ERROR_VARIABLE err
            RESULT_VARIABLE rc
            TIMEOUT 120
        )
    endif()
    string(APPEND out "${err}")

    # qmllint 文本诊断固定以 "Error:" / "Warning:" 前缀起行。按行首匹配，
    # 避免把 QML 源码文本里的同名字符串误判成诊断。
    string(REGEX MATCHALL "(^|\n)Error: " error_hits "${out}")
    list(LENGTH error_hits error_count)
    string(REGEX MATCHALL "(^|\n)Warning: " warning_hits "${out}")
    list(LENGTH warning_hits warning_count)

    if(NOT rc EQUAL 0 OR error_count GREATER 0)
        string(APPEND failed_targets " ${target}")
        message(STATUS
            "qmllint gate: ${target} failed (exit ${rc}, errors ${error_count}, warnings ${warning_count}):")
        message(STATUS "${out}")
    else()
        message(STATUS
            "qmllint gate: ${target} clean (errors 0, warnings ${warning_count})")
    endif()
endforeach()

if(NOT failed_targets STREQUAL "")
    message(FATAL_ERROR "qmllint gate failed for:${failed_targets}")
endif()