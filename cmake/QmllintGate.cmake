# QmllintGate.cmake — CTest gate for Batch 9 (TEST-905).
# Runs qmllint over the generated per-target response files and fails on
# ERROR-level findings. Warning-level findings (pre-existing layout /
# unqualified notes) are tolerated and printed, so the gate tracks real
# regressions without blocking on legacy noise.
#
# qmllint must actually run and exit cleanly: a missing executable, crash,
# timeout, or runaway memory counts as a gate failure. QmllintGuard.ps1 adds
# a memory cap because qmllint 6.9 can consume tens of GB on pathological QML.
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
foreach(target nexus_ai nexus_viewmodels)
    set(rsp "${RSP_DIR}/${target}.rsp")
    if(NOT EXISTS "${rsp}")
        message(FATAL_ERROR "qmllint gate: missing response file ${rsp}")
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
    string(FIND "${out}" "Error: " error_pos)
    if(NOT rc EQUAL 0)
        string(APPEND failed_targets " ${target}")
        message(STATUS "qmllint gate: ${target} failed (exit ${rc}):")
        message(STATUS "${out}")
    elseif(NOT error_pos EQUAL -1)
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
