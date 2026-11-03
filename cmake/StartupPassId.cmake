# Generates JAH_STARTUP_PASS_CODE_ID (jah_startup_pass_id.h) at BUILD time - THE GLOBAL STARTUP PASS's CODE
# (TEST-STREAMLINE-1 point 3, 2026-11-03).
#
# WHY. The app's global shader pass (src/app/shaderbuildgate.cpp) re-runs over a warm cache when its key moves, so that a
# change to what it compiles - the born World, a template world, a preset, the gizmos, the engine's own property code - is
# compiled behind the splash and never LIVE (URGENT #104). Its key named the Studio COMMIT (GIT_COMMIT_HASH), so EVERY
# Studio commit re-ran it (and re-made both startup seeds of the test rows, ~280-291 s each, #125/#139) although ~10 of
# 1,032 Studio commits since 10-20 touched what it compiles. Now the key names WHAT DECIDES ITS BYTES: this id - the
# content of the pass's own sources (shaderbuildgate.cpp and the src/ and irisgl/ sources of every header it includes,
# read at configure from its #include lines: a new include is in the list at the next configure, and shaderbuildgate.cpp
# is a configure dependency) and the engine's FULL build id (an engine edit that sets a new property changes what the
# pass compiles: the narrowed hlms key alone would leave the pass skipped) - plus, at runtime, the staged media by
# content and the engine cache's fingerprint (src/app/startuppasskey.h).
#
# Run with: cmake -DJAH_PASS_FILES=<|-list> -DJAH_ENGINE_ID_HEADER=<engine's generated id header> -DJAH_PASS_OUT=<header> -P <this>
# Contents only, never a path (#154: a key serves every tree); written only when the value changes (copy_if_different).
string(REPLACE "|" ";" _files "${JAH_PASS_FILES}")
set(_blob "")
foreach(_f IN LISTS _files)
    if(EXISTS "${_f}")
        file(SHA256 "${_f}" _h)
    else()
        set(_h "missing")
    endif()
    string(APPEND _blob "${_h};")
endforeach()
set(_engine "none")
if(EXISTS "${JAH_ENGINE_ID_HEADER}")
    file(STRINGS "${JAH_ENGINE_ID_HEADER}" _l REGEX "^#define JAHSHAKA_ENGINE_BUILD_ID ")
    if(_l)
        set(_engine "${_l}")
    endif()
endif()
if(_engine STREQUAL "none")
    message(FATAL_ERROR "StartupPassId.cmake: no JAHSHAKA_ENGINE_BUILD_ID in ${JAH_ENGINE_ID_HEADER}")
endif()
string(APPEND _blob "engine ${_engine}")
string(SHA256 _id "${_blob}")
set(_tmp "${JAH_PASS_OUT}.tmp")
file(WRITE "${_tmp}"
"// GENERATED - do not edit, do not commit. See cmake/StartupPassId.cmake.\n\
#pragma once\n\
#define JAH_STARTUP_PASS_CODE_ID \"${_id}\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_tmp}" "${JAH_PASS_OUT}")
file(REMOVE "${_tmp}")
