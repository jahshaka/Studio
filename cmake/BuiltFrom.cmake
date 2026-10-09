# BUILT_FROM — WHAT build-linux WAS BUILT FROM (GATE-COST-2 #8; the BATCH-GATE-1 delta read's stale-build hole).
#
# The run log keyed every record on the worktree's HEAD, never on what the binaries were built from: a tree
# moved to a new commit and gated WITHOUT a rebuild counted a stale binary as the tip. The configure-time
# GIT_COMMIT_HASH (jah_provenance.h) cannot say it either — it is read when CMake runs, not when ninja builds,
# so it goes stale on every rebuild without a reconfigure. So EVERY BUILD writes <build>/BUILT_FROM — the
# studio and irisgl HEADs and whether either tree was dirty — as its LAST step: the stamp target depends on
# every build target of the project, so a build that fails or is stopped never refreshes it, and a
# `--target <one>` build does not either (the stamp then reads stale, which only ever costs a no-op build).
# scripts/gate_runlog.py copies it into each record (`tip.built`); ci_gate_check.py treats a record whose
# built studio/irisgl sha is not the tip's — or was built dirty — as studio_dirty (never the tip's run).
#
# Two modes: included from the top CMakeLists (defines the target), and run as a script (-P) by the target.
if(CMAKE_SCRIPT_MODE_FILE)
    function(_jah_head dir out_sha out_dirty)
        execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${dir}" OUTPUT_VARIABLE sha
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
        execute_process(COMMAND git status --porcelain --untracked-files=no --ignore-submodules=dirty
                        WORKING_DIRECTORY "${dir}" OUTPUT_VARIABLE st OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT rc EQUAL 0)
            set(sha "unknown")
        endif()
        set(${out_sha} "${sha}" PARENT_SCOPE)
        if(st)
            set(${out_dirty} 1 PARENT_SCOPE)
        else()
            set(${out_dirty} 0 PARENT_SCOPE)
        endif()
    endfunction()
    _jah_head("${SRC}" studio sdirty)
    _jah_head("${SRC}/irisgl" irisgl idirty)
    if(sdirty OR idirty)
        set(dirty 1)
    else()
        set(dirty 0)
    endif()
    set(text "studio=${studio}\nirisgl=${irisgl}\ndirty=${dirty}\n")
    set(old "")
    if(EXISTS "${OUT}")
        file(READ "${OUT}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${OUT}" "${text}")
    endif()
    return()
endif()

function(_jah_collect_targets dir out)
    get_property(ts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    set(all "")
    foreach(t IN LISTS ts)
        get_target_property(type ${t} TYPE)
        get_target_property(excluded ${t} EXCLUDE_FROM_ALL)
        if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT excluded)
            list(APPEND all ${t})
        endif()
    endforeach()
    foreach(s IN LISTS subs)
        _jah_collect_targets("${s}" sub)
        list(APPEND all ${sub})
    endforeach()
    set(${out} "${all}" PARENT_SCOPE)
endfunction()

add_custom_target(jah_built_from ALL
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/BUILT_FROM
            -P ${CMAKE_CURRENT_LIST_DIR}/BuiltFrom.cmake
    COMMENT "BUILT_FROM: the studio and irisgl commits this build was made from"
    VERBATIM)
_jah_collect_targets("${CMAKE_SOURCE_DIR}" _jah_all_targets)
list(REMOVE_ITEM _jah_all_targets jah_built_from)
if(_jah_all_targets)
    add_dependencies(jah_built_from ${_jah_all_targets})
endif()
