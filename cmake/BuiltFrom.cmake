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
# built studio/irisgl sha is not the tip's — or was built dirty, untracked files included, or carries no
# stamp at all — as studio_dirty (never the tip's run). Every gate first runs a no-op `cmake --build`
# (gate_runlog.prebuild): a stale binary or a `--target` build's leftovers are rebuilt and the stamp is fresh.
#
# THE CONTENT (URGENT #187): ninja rebuilds by mtime, so a source restored with an OLD mtime (tar xf, cp -p) rebuilt nothing
# and this always-run step stamped the tree as it is now - a stamp naming content the binary was never built from.
# cmake/built_from.py keeps the tree's per-file content map beside the stamp; a file whose content moved while its mtime did
# not pass the last stamp is touched, ONE loud line says "this pass is STALE - build again", and the stamp carries
# stale=<n> for this pass (content=, stale_fork=, stale_files= too); the next pass rebuilds it. util.build passes again on
# that line; a FORK file in it needs build-ogre.sh first (spec.sh test refuses, exit 2, with the command).
#
# Two modes: included from the top CMakeLists (defines the target), and run as a script (-P) by the target.
if(CMAKE_SCRIPT_MODE_FILE)
    # _jah_head(<dir> <out sha> <out dirty>)
    function(_jah_head dir out_sha out_dirty)
        execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${dir}" OUTPUT_VARIABLE sha
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
        # untracked files count (F4c): an untracked new Hlms piece or source is in the build and in no commit
        execute_process(COMMAND git status --porcelain --untracked-files=normal --ignore-submodules=dirty
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
    # A VENDORED SUBMODULE'S APPLIED PATCH STACK IS NEVER DIRT (assimp-patches, bullet-patches: applied at
    # configure, ` m thirdparty/assimp` by construction): --ignore-submodules=dirty hides a submodule's modified
    # content and still shows a MOVED gitlink — an uncommitted pin change of assimp is dirt like any other
    # (TOOLING-2: the old `:(exclude)thirdparty/assimp` was redundant under the flag and hid exactly that).
    _jah_head("${SRC}/irisgl" irisgl idirty)
    # THE FORK is read apart (C3): irisgl's --ignore-submodules=dirty hides its working tree, and a record's fork is
    # only the pin — so a fork edited in place, untracked files included, stamps fork_dirty=1 and is never served.
    _jah_head("${SRC}/irisgl/thirdparty/ogre-next" fork fdirty)
    if(sdirty OR idirty)
        set(dirty 1)
    else()
        set(dirty 0)
    endif()
    # THE CONTENT AND WHAT NINJA COULD NOT SEE (URGENT #187; cmake/built_from.py says how): content=, stale=, stale_fork=,
    # stale_files= - a source restored with an old mtime is touched here and this pass is stamped STALE (build again)
    get_filename_component(_build "${OUT}" DIRECTORY)
    set(extra "content=?\nstale=?\nstale_fork=0\nstale_files=\n")
    if(PY)
        execute_process(COMMAND "${PY}" -I "${CMAKE_CURRENT_LIST_DIR}/built_from.py" "${SRC}" "${_build}" "${OGRE_PREFIX}"
                        OUTPUT_VARIABLE extra RESULT_VARIABLE prc)
        if(NOT prc EQUAL 0)
            set(extra "content=?\nstale=?\nstale_fork=0\nstale_files=\n")
        endif()
    endif()
    set(text "studio=${studio}\nirisgl=${irisgl}\ndirty=${dirty}\nfork_dirty=${fdirty}\n${extra}")
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
            -DPY=${Python3_EXECUTABLE} -DOGRE_PREFIX=${OGRE_NEXT_PREFIX}
            -P ${CMAKE_CURRENT_LIST_DIR}/BuiltFrom.cmake
    COMMENT "BUILT_FROM: the studio and irisgl commits this build was made from"
    VERBATIM)
_jah_collect_targets("${CMAKE_SOURCE_DIR}" _jah_all_targets)
list(REMOVE_ITEM _jah_all_targets jah_built_from)
if(_jah_all_targets)
    add_dependencies(jah_built_from ${_jah_all_targets})
endif()
