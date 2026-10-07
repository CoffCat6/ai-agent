# CheckIconResources.cmake — docs/20 §4.1 regression guard.
#
# The app loads its window/tray icons from the Qt resource system as
# ":/icons/nexus-<size>.png". qt_add_resources keeps the FILES path in the
# alias unless BASE is set, so dropping BASE silently turns every lookup into
# a null QIcon: the tray appears empty and "close to tray" looks broken with
# no error anywhere. This asserts the generated .qrc really carries the
# expected aliases so the mistake fails the build instead of shipping.
#
# Called from CMakeLists.txt with:
#   QRC_FILE         generated .qrc path
#   EXPECTED_ALIASES list of 'alias="nexus-<size>.png"' strings

if(NOT QRC_FILE)
    message(FATAL_ERROR "CheckIconResources requires QRC_FILE")
endif()

# The .qrc is produced during the build; on a clean configure it may not
# exist yet (the custom target runs as part of ALL and is ordered after the
# resource generation). Treat "not generated yet" as a soft skip — the check
# still fires on every subsequent build once the file exists.
if(NOT EXISTS "${QRC_FILE}")
    message(STATUS "icon resource check: ${QRC_FILE} not generated yet; skipping")
    return()
endif()

file(READ "${QRC_FILE}" qrc_content)

set(missing "")
foreach(alias IN LISTS EXPECTED_ALIASES)
    string(FIND "${qrc_content}" "${alias}" pos)
    if(pos EQUAL -1)
        list(APPEND missing "${alias}")
    endif()
endforeach()

if(NOT missing STREQUAL "")
    message(FATAL_ERROR
        "icon resource aliases missing from ${QRC_FILE}: ${missing}\n"
        "The runtime expects :/icons/nexus-<size>.png. qt_add_resources needs "
        "BASE \"assets/icons\" so the source directory is stripped from the alias; "
        "without it QIcon resolves to null and the tray shows no icon.")
endif()

message(STATUS "icon resource check: all :/icons/ aliases present")
