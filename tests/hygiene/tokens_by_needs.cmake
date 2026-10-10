# gate.tokens_by_needs — THE TOKEN LOOKUP RETURNS 2/2/2/3 BY DECLARATION (TEST-NEEDS-1; the brief's
# 1/1/2/3 corrected by the measurement in vram_tokens.cmake: a Low/Medium process peaks over 1 token
# and the card adds per process). A toy:
# tests/support/vram_tokens.cmake included ALONE (the file the gate's rows are registered
# through), the lookup asked for the four headline declarations and the edges, and the
# validation proved to REFUSE a row without its words (each refusal a child `cmake -P` that
# must fail, since a FATAL_ERROR ends the process that raised it).
#   cmake -DSRC=<tests dir> -P tokens_by_needs.cmake
# RED ON BASE (a5ab3a057): tests/support/vram_tokens.cmake does not exist — the include fails.
if(NOT SRC)
    message(FATAL_ERROR "tokens_by_needs: -DSRC=<tests dir> is required")
endif()
if(MODE STREQUAL "refuse")
    # a child: one declaration that must be a configure error
    include(${SRC}/support/vram_tokens.cmake)
    string(REPLACE "," ";" _args "${ARGS}")
    jah_vram_tokens(app _k ${_args})
    message(STATUS "tokens_by_needs child: '${ARGS}' was ACCEPTED (${_k} tokens)")
    return()
endif()
include(${SRC}/support/vram_tokens.cmake)
set(_fails 0)
macro(expect _class _want)
    jah_vram_tokens(${_class} _got ${ARGN})
    if(_got EQUAL ${_want})
        message(STATUS "ok: ${_class} ${ARGN} -> ${_got} token(s)")
    else()
        message(STATUS "FAIL: ${_class} ${ARGN} -> ${_got} token(s), want ${_want}")
        math(EXPR _fails "${_fails} + 1")
    endif()
endmacro()
# the four headline declarations (measured: low/NONE 2, medium+photon 2, high+photon 2, epic+photon 3)
expect(app 2 TIER low NEEDS NONE)
expect(app 2 TIER medium NEEDS photon)
expect(app 2 TIER high NEEDS photon)
expect(app 3 TIER epic NEEDS photon)
# Photon decides, the chain words do not; NONE is the tier's chain without Photon
expect(app 3 TIER epic NEEDS photon bloom ssao smaa planar)
expect(app 2 TIER low NEEDS photon)
expect(app 2 TIER epic NEEDS bloom planar)
expect(app 2 TIER medium NEEDS NONE)
# the document's own tier: no test tier, no NEEDS, the product's Epic
expect(app 3 TIER document)
# the other classes take no declaration
expect(vr 3)
expect(engine 1)
expect(selftest 2)
# the refusals: no default to fall back on
foreach(_bad "TIER,low" "NEEDS,photon" "" "TIER,ultra,NEEDS,NONE" "TIER,low,NEEDS,shadows"
             "TIER,low,NEEDS,NONE,photon" "TIER,high,NEEDS,photon,photon" "TIER,document,NEEDS,photon")
    execute_process(COMMAND ${CMAKE_COMMAND} -DSRC=${SRC} -DMODE=refuse "-DARGS=${_bad}"
                            -P ${CMAKE_CURRENT_LIST_FILE}
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
    if(_rc EQUAL 0)
        message(STATUS "FAIL: app '${_bad}' was accepted — a row without a valid declaration must be refused")
        math(EXPR _fails "${_fails} + 1")
    else()
        string(REGEX MATCH "(TIER|NEEDS)[^\n]*" _why "${_err}")
        message(STATUS "ok: app '${_bad}' refused (${_why})")
    endif()
endforeach()
# THE DECLARED TIMEOUT IS THE ROW'S WHOLE BOUND (TESTING-CLEANUP-2B item 2; tests/cmake/vram_rows.cmake).
# RED ON BASE (b0a3b1f3c): every one of the build's 423 registered GPU rows had an effective ctest TIMEOUT of its declared
# value + 900 s (the deleted _jah_vram_widen_timeout: min 960 = 60 + 900), and vram_rows.cmake did not exist.
# A toy project registered through vram_rows.cmake ALONE, listed and run by ctest: the TIMEOUT ctest will
# enforce is the one the site declared (77, not 977), the row carries its tokens as RESOURCE_GROUPS, `all` is
# every token, and two rows that need 6 of the 11 tokens each never RUN together under -j3 (ctest schedules
# them on the build's resource spec — the admission's wait outside the row's clock). Counted, never timed:
# each toy row marks itself running and records any sibling it finds running.
if(BIN)
    set(_toy "${BIN}/tokens_by_needs_toy")
    file(REMOVE_RECURSE "${_toy}")
    file(MAKE_DIRECTORY "${_toy}/src" "${_toy}/marks")
    file(WRITE "${_toy}/src/claim.sh" [=[
m="$1"; me="$2"
for o in "$m"/running.*; do [ -e "$o" ] && echo "$me beside ${o##*/running.}" >> "$m/overlaps"; done
touch "$m/running.$me"; sleep 1
for o in "$m"/running.*; do [ "$o" != "$m/running.$me" ] && [ -e "$o" ] && echo "$me beside ${o##*/running.}" >> "$m/overlaps"; done
rm -f "$m/running.$me"
]=])
    file(WRITE "${_toy}/src/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)
project(toy NONE)
enable_testing()
include(\"${SRC}/cmake/vram_rows.cmake\")
jah_vram_resource_spec()
add_test(NAME toy.declared COMMAND \${CMAKE_COMMAND} -E true)
set_tests_properties(toy.declared PROPERTIES TIMEOUT 77)
_jah_vram_register(toy.declared engine 1)
add_test(NAME toy.undeclared COMMAND \${CMAKE_COMMAND} -E true)
_jah_vram_register(toy.undeclared app 3)
add_test(NAME toy.timing COMMAND \${CMAKE_COMMAND} -E true)
set_tests_properties(toy.timing PROPERTIES TIMEOUT 210)
_jah_vram_register(toy.timing app all)
foreach(_r a b)
    add_test(NAME toy.six_\${_r} COMMAND sh \"${_toy}/src/claim.sh\" \"${_toy}/marks\" \${_r})
    set_tests_properties(toy.six_\${_r} PROPERTIES TIMEOUT 60)
    _jah_vram_register(toy.six_\${_r} app 6)
endforeach()
")
    execute_process(COMMAND ${CMAKE_COMMAND} -S "${_toy}/src" -B "${_toy}/build" RESULT_VARIABLE _rc
                    OUTPUT_QUIET ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(STATUS "FAIL: the toy project through vram_rows.cmake does not configure: ${_err}")
        math(EXPR _fails "${_fails} + 1")
    else()
        execute_process(COMMAND ${CMAKE_CTEST_COMMAND} --show-only=json-v1 WORKING_DIRECTORY "${_toy}/build"
                        OUTPUT_VARIABLE _json RESULT_VARIABLE _rc)
        string(JSON _n LENGTH "${_json}" tests)
        math(EXPR _last "${_n} - 1")
        set(_seen "")
        foreach(_i RANGE ${_last})
            string(JSON _name GET "${_json}" tests ${_i} name)
            set(_t "")
            set(_g "")
            string(JSON _np ERROR_VARIABLE _e LENGTH "${_json}" tests ${_i} properties)
            if(NOT _e AND _np GREATER 0)
                math(EXPR _pl "${_np} - 1")
                foreach(_k RANGE ${_pl})
                    string(JSON _pn GET "${_json}" tests ${_i} properties ${_k} name)
                    if(_pn STREQUAL "TIMEOUT")
                        string(JSON _t GET "${_json}" tests ${_i} properties ${_k} value)
                        string(REGEX REPLACE "\\.0+$" "" _t "${_t}")     # the listing prints 77.0
                    elseif(_pn STREQUAL "RESOURCE_GROUPS")
                        string(JSON _g GET "${_json}" tests ${_i} properties ${_k} value 0 requirements 0 slots)
                    endif()
                endforeach()
            endif()
            list(APPEND _seen "${_name}=${_t}/${_g}")
        endforeach()
        foreach(_want "toy.declared=77/1" "toy.undeclared=/3" "toy.timing=210/11" "toy.six_a=60/6" "toy.six_b=60/6")
            if("${_want}" IN_LIST _seen)
                message(STATUS "ok: ${_want} (the declared TIMEOUT / the row's tokens as ctest will enforce them)")
            else()
                message(STATUS "FAIL: want ${_want} (TIMEOUT/tokens), the listing says ${_seen}")
                math(EXPR _fails "${_fails} + 1")
            endif()
        endforeach()
        execute_process(COMMAND ${CMAKE_CTEST_COMMAND} -j3 -R "^toy\\.six_" WORKING_DIRECTORY "${_toy}/build"
                        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
        if(EXISTS "${_toy}/marks/overlaps")
            file(READ "${_toy}/marks/overlaps" _ov)
            string(STRIP "${_ov}" _ov)
            message(STATUS "FAIL: two rows needing 6 of the 11 tokens ran together under -j3 (${_ov})")
            math(EXPR _fails "${_fails} + 1")
        elseif(NOT _rc EQUAL 0)
            message(STATUS "FAIL: the toy rows did not run (${_rc}): ${_out}")
            math(EXPR _fails "${_fails} + 1")
        else()
            message(STATUS "ok: two rows needing 6 of the 11 tokens never ran together under -j3 (the resource spec)")
        endif()
    endif()
    file(REMOVE_RECURSE "${_toy}")
endif()
if(_fails)
    message(FATAL_ERROR "tokens_by_needs: ${_fails} FAIL")
endif()
message(STATUS "tokens_by_needs: all ok")
