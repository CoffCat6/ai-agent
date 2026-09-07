# QmllintGate.cmake — CTest gate for Batch 9 (TEST-905).
# Runs qmllint over the generated per-target response files and fails on
# ERROR-level findings. Warning-level findings (pre-existing layout /
# unqualified notes) are tolerated and printed, so the gate tracks real
# regressions without blocking on legacy noise.
if(NOT QMLLINT OR NOT RSP_DIR)
    message(FATAL_ERROR "QmllintGate requires QMLLINT and RSP_DIR")
endif()

set(failed_targets "")
foreach(target nexus_ai nexus_ui nexus_viewmodels)
    set(rsp "${RSP_DIR}/${target}.rsp")
    if(NOT EXISTS "${rsp}")
        message(FATAL_ERROR "qmllint gate: missing response file ${rsp}")
    endif()
    set(out "")
    set(rc "")
    execute_process(
        COMMAND "${QMLLINT}" "@${rsp}"
        OUTPUT_VARIABLE out
        ERROR_VARIABLE out
        RESULT_VARIABLE rc
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(FIND "${out}" "Error: " error_pos)
    if(NOT error_pos EQUAL -1)
        string(APPEND failed_targets " ${target}")
        message(STATUS "qmllint gate: ${target} has error-level findings:")
        message(STATUS "${out}")
    else()
        message(STATUS "qmllint gate: ${target} clean (warnings only: ${rc})")
    endif()
endforeach()

if(NOT failed_targets STREQUAL "")
    message(FATAL_ERROR "qmllint gate failed for:${failed_targets}")
endif()
