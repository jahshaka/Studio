# THE ENGINE INSTALL MUST BE THE ENGINE CHECKOUT (D6-FORK-TOOLING, audit V2-D F11).
#
# irisgl/scripts/build-ogre.sh builds the ogre-next submodule into this tree's
# install and writes `<install>/BUILT_FROM`: the fork commit it built, `dirty` when
# the checkout had uncommitted edits, and `buildsettings <sha256>` of the installed
# OgreBuildSettings.h. Nothing else ties the install to the checkout: a pin bump
# (or a fork commit in a lane) moves the submodule, Studio stages the NEW media from
# it at build time, and links the OLD engine library — the C++ of one commit
# driving the shaders of another, silently (Ogre's ABI cookie hashes only the two
# threading macros; the same Ogre version and options pass it). So configure
# refuses, with the one line that fixes it.
#
# `jah_ogre_install_stamp_problem(<out-var> <install prefix> <ogre-next source>)`
# sets <out-var> to the refusal text, or to "" when the install is the checkout.
# Pure (no FATAL_ERROR, no directory properties) so the hygiene suite
# source.fork_stamp can drive it in script mode; cmake/IncludeOgre.cmake is the
# caller that fails configure. A dirty stamp is not a refusal (a lane builds its
# uncommitted fork edit before it commits it); the gate refuses that one
# (scripts/gate_runlog.py fork_pin_problem), because a gate must name a commit.
function(jah_ogre_install_stamp_problem out prefix src)
    set(${out} "" PARENT_SCOPE)
    execute_process(COMMAND git -C "${src}" rev-parse HEAD
                    OUTPUT_VARIABLE _head ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0 OR NOT _head)
        # Not a git checkout (an exported source tree): nothing to compare against.
        return()
    endif()
    get_filename_component(_irisgl "${src}/../.." ABSOLUTE)
    set(_remedy "Fix: ${_irisgl}/scripts/build-ogre.sh   (when the checkout is not the pin "
                "irisgl records, first: git -C ${_irisgl} submodule update --init thirdparty/ogre-next)")
    if(NOT EXISTS "${prefix}/BUILT_FROM")
        set(${out} "The Ogre-Next install at ${prefix} has no BUILT_FROM record, so nothing "
                   "says which fork commit it is (it predates the record). ${_remedy}" PARENT_SCOPE)
        return()
    endif()
    file(STRINGS "${prefix}/BUILT_FROM" _lines)
    list(GET _lines 0 _built)
    string(STRIP "${_built}" _built)
    if(NOT _built STREQUAL _head)
        string(SUBSTRING "${_built}" 0 9 _b9)
        string(SUBSTRING "${_head}" 0 9 _h9)
        set(${out} "STALE ENGINE: the Ogre-Next install at ${prefix} was built from fork commit "
                   "${_b9}, but the ogre-next checkout is at ${_h9}. Linking it would run the old "
                   "engine's C++ against the new commit's staged media. ${_remedy}" PARENT_SCOPE)
        return()
    endif()
    set(_recorded "")
    foreach(_l IN LISTS _lines)
        if(_l MATCHES "^buildsettings ([0-9a-f]+)$")
            set(_recorded "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(NOT _recorded)
        set(${out} "The Ogre-Next install at ${prefix} records no buildsettings hash (built by an "
                   "older build-ogre.sh). ${_remedy}" PARENT_SCOPE)
        return()
    endif()
    set(_bs "${prefix}/include/OGRE-Next/OgreBuildSettings.h")
    if(NOT EXISTS "${_bs}")
        set(${out} "The Ogre-Next install at ${prefix} has no OgreBuildSettings.h. ${_remedy}" PARENT_SCOPE)
        return()
    endif()
    file(SHA256 "${_bs}" _now)
    if(NOT _now STREQUAL _recorded)
        set(${out} "The Ogre-Next install at ${prefix} has an OgreBuildSettings.h that is not the "
                   "one build-ogre.sh installed (recorded ${_recorded}, found ${_now}): the "
                   "install was changed by something other than the script. ${_remedy}" PARENT_SCOPE)
        return()
    endif()
endfunction()

# The files whose change means "re-check": the stamp, and the submodule's HEAD as
# git stores it — the HEAD file (a detached checkout rewrites it), the branch ref it
# names (a commit on a branch moves only that), and logs/HEAD (every HEAD move
# appends to it, packed refs included). Sets <out-var> to the existing ones.
function(jah_ogre_install_stamp_inputs out prefix src)
    set(_files "${prefix}/BUILT_FROM")
    foreach(_p HEAD logs/HEAD)
        execute_process(COMMAND git -C "${src}" rev-parse --path-format=absolute --git-path ${_p}
                        OUTPUT_VARIABLE _f ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_f AND EXISTS "${_f}")
            list(APPEND _files "${_f}")
        endif()
    endforeach()
    execute_process(COMMAND git -C "${src}" symbolic-ref -q HEAD
                    OUTPUT_VARIABLE _ref ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_ref)
        execute_process(COMMAND git -C "${src}" rev-parse --path-format=absolute --git-path ${_ref}
                        OUTPUT_VARIABLE _f ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_f AND EXISTS "${_f}")
            list(APPEND _files "${_f}")
        endif()
    endif()
    set(${out} "${_files}" PARENT_SCOPE)
endfunction()

# Script mode: `cmake -DPREFIX=<install> -DSRC=<ogre-next> -P cmake/OgreInstallStamp.cmake`
# exits non-zero with the refusal text exactly when configure would (the hygiene
# suite's driver, and a one-line check for a human).
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    jah_ogre_install_stamp_problem(_problem "${PREFIX}" "${SRC}")
    if(_problem)
        message(FATAL_ERROR "${_problem}")
    endif()
    message(STATUS "Ogre-Next install ${PREFIX} is the checkout ${SRC}")
endif()
