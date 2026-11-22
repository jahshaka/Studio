# BUILT_FROM — WHAT build-linux WAS BUILT FROM (GATE-COST-2 #8; the BATCH-GATE-1 delta read's stale-build hole).
#
# The run log keyed every record on the worktree's HEAD, never on what the binaries were built from: a tree
# moved to a new commit and gated WITHOUT a rebuild counted a stale binary as the tip. The configure-time
# GIT_COMMIT_HASH (jah_provenance.h) cannot say it either — it is read when CMake runs, not when ninja builds,
# so it goes stale on every rebuild without a reconfigure. So EVERY BUILD writes <build>/BUILT_FROM — the
# studio and irisgl HEADs and whether either tree was dirty — as its LAST step: the stamp target depends on
# every build target of the project, so a build that fails or is stopped never refreshes it, and a
# `--target <one>` build by hand does not either (the stamp then reads stale, which only ever costs a no-op build).
# The lead tools' BY-REACH build (a `--target` pass of the targets its rows need) stamps through jah_built_reach, and the
# stamp's `targets=` line says which targets it claims (below).
# scripts/gate_runlog.py copies it into each record (`tip.built`); ci_gate_check.py treats a record whose
# built studio/irisgl sha is not the tip's — or was built dirty, untracked files included, or carries no
# stamp at all — as studio_dirty (never the tip's run); gate_runlog.stale_build refuses a record built dirty, stale, from
# another commit or by a pass that did not build its row's targets. NOTHING rebuilds before a gate by itself: the lead tools
# build before they run rows (util.build, util.build_rows), and a claim built after the stamp is dropped (below).
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
    set(head "studio=${studio}\nirisgl=${irisgl}\ndirty=${dirty}\nfork_dirty=${fdirty}\n")
    # THE TARGETS THIS STAMP CLAIMS (ENGINE-HEADERS-1 stage 0, BUILD BY REACH; CODING_RULES § 4 "WHICH BUILD"): `targets=` names
    # what is current at this state - `all` (every default target: the jah_built_from step, after every one of them) and/or
    # the CMake targets a `--target` pass built (jah_built_reach: the lead tools' util.build runs it as its OWN ninja call, only
    # after the `--target <T>` call exited 0, with JAH_BUILT_REACH=<T, comma-separated>). A target named here was built WITH
    # EVERYTHING IT LINKS AND DEPENDS ON at this state: a `--target T` ninja call brings T's whole input closure up to date
    # before T, by construction. THE STATE is studio, irisgl, dirty, fork_dirty and content= (one hash over every file's bytes,
    # the uncommitted ones included: cmake/built_from.py): a stamp at an EQUAL state and not STALE ADDS its targets to the
    # set on disk (two --code calls at one tip: A then B = A + B); at any other state - a commit, an edit, a library's source
    # changed between two by-reach calls - the set STARTS EMPTY, so a binary built from older bytes is never claimed. A FAILED
    # build leaves no stamp (util.build removes it). tests/hygiene/built_from_heals.py cases 5-8; the readers:
    # scripts/row_targets.py (a row's targets), gate_runlog.py (a record whose row's targets are not here is never its tip's),
    # tests/support/on_demand.sh (an on-demand binary runs only when named here).
    if(REACH)
        set(now "$ENV{JAH_BUILT_REACH}")
        if(now STREQUAL "")
            message(FATAL_ERROR "BUILT_FROM: jah_built_reach needs JAH_BUILT_REACH=<target>[,<target>...] - the targets the "
                                "pass before it built (the lead tools' util.build sets it); never built by hand")
        endif()
        string(REPLACE "," ";" now "${now}")
    else()
        set(now "all")
    endif()
    set(old "")
    if(EXISTS "${OUT}")
        file(READ "${OUT}" old)
    endif()
    # _jah_kv(<text> <key> <out>): the value of `<key>=` on its own line of <text> ("" when absent)
    function(_jah_kv text key out)
        if("\n${text}" MATCHES "\n${key}=([^\n]*)")
            set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        else()
            set(${out} "" PARENT_SCOPE)
        endif()
    endfunction()
    _jah_kv("${extra}" content new_content)
    _jah_kv("${extra}" stale new_stale)
    _jah_kv("${old}" content old_content)
    _jah_kv("${old}" stale old_stale)
    _jah_kv("${old}" targets old_targets)
    string(FIND "${old}" "${head}" at)
    if(at EQUAL 0 AND old_content STREQUAL new_content AND NOT new_content STREQUAL "?" AND old_stale STREQUAL "0"
       AND new_stale STREQUAL "0" AND NOT old_targets STREQUAL "")
        string(REPLACE "," ";" old_targets "${old_targets}")
        # A CLAIM NEVER OUTLIVES A BUILD THE STAMP DID NOT SEE (the final read's L2): a target built after the old stamp - a
        # `--target` build by hand, at a state since reverted to this one - is built from bytes this state does not hold, so its
        # old claim is dropped. Its artefact is the build's own map (MAP: $<TARGET_FILE> of each executable and library, a
        # custom target's JAH_ARTEFACT); the stamp is TOUCHED by every by-reach pass, so a target the tools just built is older
        # than it (built_from_heals case 11).
        # `all` is checked the same way through its lines `all=<file>` (every default target's artefact): a default target
        # built by hand after the stamp voids the old `all` (the targets THIS pass built are its own claim: never checked)
        set(art "")
        if(DEFINED MAP AND EXISTS "${MAP}")
            file(STRINGS "${MAP}" art)
        endif()
        set(drop "")
        set(allfiles "")   # a whole pass (not REACH) built every default target itself: those claims are its own
        if(NOT REACH)
            foreach(line IN LISTS art)
                if(line MATCHES "^all=(.+)$")
                    list(APPEND allfiles "${CMAKE_MATCH_1}")
                endif()
            endforeach()
        endif()
        foreach(line IN LISTS art)
            if(line MATCHES "^([^=]+)=(.+)$")   # (CMAKE_MATCH_n is read in the NEXT command: an if's arguments expand first)
                set(n "${CMAKE_MATCH_1}")
                set(f "${CMAKE_MATCH_2}")
                if(n IN_LIST old_targets AND NOT n IN_LIST drop AND NOT f IN_LIST allfiles AND NOT (n STREQUAL "all" AND NOT REACH)
                   AND EXISTS "${f}" AND "${f}" IS_NEWER_THAN "${OUT}")
                    list(FIND now "${n}" mine)
                    get_filename_component(fn "${f}" NAME)
                    if(n STREQUAL "all" OR mine EQUAL -1)
                        list(APPEND drop "${n}")
                        message(STATUS "BUILT_FROM: ${n} (${fn}) was built after this stamp (by hand, at another state?) - no longer claimed")
                    endif()
                endif()
            endif()
        endforeach()
        foreach(t IN LISTS old_targets)
            if(NOT t IN_LIST drop)
                list(APPEND now ${t})
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES now)
    list(SORT now)
    list(JOIN now "," now)
    set(text "${head}${extra}targets=${now}\n")
    if(NOT old STREQUAL text)
        file(WRITE "${OUT}" "${text}")
    elseif(REACH)
        file(TOUCH "${OUT}")   # the targets this pass built are older than the stamp that claims them (above)
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
            -DPY=${Python3_EXECUTABLE} -DOGRE_PREFIX=${OGRE_NEXT_PREFIX} -DMAP=${CMAKE_BINARY_DIR}/built_from_artefacts.txt
            -P ${CMAKE_CURRENT_LIST_DIR}/BuiltFrom.cmake
    COMMENT "BUILT_FROM: the studio and irisgl commits this build was made from"
    VERBATIM)
# THE BY-REACH STAMP (the comment at `targets=` above): no dependency, out of `all`; only util.build runs it, after its pass
add_custom_target(jah_built_reach
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/BUILT_FROM
            -DPY=${Python3_EXECUTABLE} -DOGRE_PREFIX=${OGRE_NEXT_PREFIX} -DREACH=1
            -DMAP=${CMAKE_BINARY_DIR}/built_from_artefacts.txt
            -P ${CMAKE_CURRENT_LIST_DIR}/BuiltFrom.cmake
    COMMENT "BUILT_FROM: the targets a by-reach pass built (JAH_BUILT_REACH)"
    VERBATIM)
# THE ARTEFACT MAP (the union's check above): `<target>=<its file>` for every executable and library, the out-of-`all` ones
# included, and every custom target that names its JAH_ARTEFACT
function(_jah_artefacts dir out)
    get_property(ts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    set(lines "")
    foreach(t IN LISTS ts)
        get_target_property(type ${t} TYPE)
        if(type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)$")
            set(f "$<TARGET_FILE:${t}>")
        elseif(type STREQUAL "UTILITY")
            get_target_property(f ${t} JAH_ARTEFACT)
        else()
            set(f "")
        endif()
        if(f)
            string(APPEND lines "${t}=${f}\n")
            get_target_property(excluded ${t} EXCLUDE_FROM_ALL)
            if(NOT excluded AND NOT t MATCHES "^jah_built_(from|reach)$")
                string(APPEND lines "all=${f}\n")   # a default target's file: what `all` claims
            endif()
        endif()
    endforeach()
    foreach(s IN LISTS subs)
        _jah_artefacts("${s}" sub)
        string(APPEND lines "${sub}")
    endforeach()
    set(${out} "${lines}" PARENT_SCOPE)
endfunction()
_jah_artefacts("${CMAKE_SOURCE_DIR}" _jah_artefact_lines)   # (included last: every target is defined - as _jah_collect_targets)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/built_from_artefacts.txt" CONTENT "${_jah_artefact_lines}")
_jah_collect_targets("${CMAKE_SOURCE_DIR}" _jah_all_targets)
list(REMOVE_ITEM _jah_all_targets jah_built_from jah_built_reach)
if(_jah_all_targets)
    add_dependencies(jah_built_from ${_jah_all_targets})
endif()
