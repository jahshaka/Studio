# gate.tokens_by_needs — THE TOKEN LOOKUP RETURNS 1/1/2/3 BY DECLARATION (TEST-NEEDS-1). A toy:
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
# the four headline declarations (the brief's table: low/NONE 1, medium+photon 1, high+photon 2,
# epic+photon 3)
expect(app 1 TIER low NEEDS NONE)
expect(app 1 TIER medium NEEDS photon)
expect(app 2 TIER high NEEDS photon)
expect(app 3 TIER epic NEEDS photon)
# Photon decides, the chain words do not; NONE is the tier's chain without Photon
expect(app 3 TIER epic NEEDS photon bloom ssao smaa planar)
expect(app 1 TIER low NEEDS photon)
expect(app 2 TIER epic NEEDS bloom planar)
expect(app 1 TIER medium NEEDS NONE)
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
if(_fails)
    message(FATAL_ERROR "tokens_by_needs: ${_fails} FAIL")
endif()
message(STATUS "tokens_by_needs: all ok")
