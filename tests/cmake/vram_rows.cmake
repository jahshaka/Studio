# THE ADMISSION OUTSIDE THE ROW'S CLOCK (TESTING-CLEANUP-2B item 2; the STAGE-CLOSE-1 read 2 finding F1).
#
# Every row that boots Vulkan is registered through the VRAM budget (scripts/gpu-admit.sh / gpu-exclusive.sh /
# run_pool.py --vram-tokens; tests/CMakeLists.txt). Its admission waits for free tokens, and the wait used to be
# INSIDE ctest's clock — so every such row's TIMEOUT was widened by the wait's whole bound (+900 s, added at the
# end of the row's directory by a deferred helper). That widening hid more than the wait: a row that RAN 1,008 or
# 1,159 s against a declared 900 (the shadow churn twins) passed, and every row of the gate could run 900 s past
# its own budget unseen. It is deleted. The declared TIMEOUT is the row's whole bound.
#
# THE WAIT LEAVES THE CLOCK IN TWO HALVES:
#   1. INSIDE ONE GATE (one ctest process — and the gate slot, GATE-COST-1, lets exactly one gate run on the
#      card): ctest itself schedules the rows by their tokens. Every registered row carries RESOURCE_GROUPS
#      `vram:<k>` (k = its tokens; a timing row's `all` = every token), and the build's resource spec
#      (vram-resources.json, written here: ONE `vram` resource with the box's token count) is named by
#      CTEST_RESOURCE_SPEC_FILE in this directory's CTestTestfile — so plain `ctest -jN`, the gates and a hand
#      run alike, never STARTS a row whose tokens its own running rows hold. ctest's clock starts at the
#      start, so a gate's rows never wait for each other inside their TIMEOUT.
#   2. ACROSS PROCESSES (a builder's hand-run app holding tokens beside the gate): the flock admission stays
#      (vram_tokens.py; nothing inside ctest can see another process). Inside a gate it does NOT wait
#      (vram_tokens.row_wait_bound): short, the row exits 75 at once — NOADMIT, re-queued at the gate's end (P5),
#      MISSING to the judge if still short — so no second of another process's hold is charged to the row's
#      TIMEOUT. Outside a gate a hand run waits as before; a TIMEOUT that ends such a wait is recorded NOADMIT
#      (gate_runlog.unadmitted_wait), never a TIMEOUT of its code.
# gate.tokens_by_needs proves the declared TIMEOUT is the effective one and that the spec serialises rows by
# their tokens (a toy project through this file).

# THE BOX'S TOKEN COUNT: vram_tokens.py's default (one place: read from it, so the two cannot drift).
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../scripts/vram_tokens.py" _jah_vt_src)
string(REGEX MATCH "JAH_VRAM_TOKENS\", \"([0-9]+)\"" _jah_vt_m "${_jah_vt_src}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "vram_rows.cmake: cannot read the default token count from scripts/vram_tokens.py")
endif()
set(JAH_VRAM_TOKEN_COUNT ${CMAKE_MATCH_1})
# THE SPEC IS SIZED AS THE RUNTIME COUNTS (TESTING-CLEANUP-2B fix round 4, item 5): vram_tokens.token_count() honours
# JAH_VRAM_TOKENS, so the spec does too — a box or a test universe configured with fewer tokens never has ctest
# schedule more than the admission can grant (its rows would NOADMIT each other inside a gate). Configure and run
# with the same JAH_VRAM_TOKENS.
if(DEFINED ENV{JAH_VRAM_TOKENS} AND "$ENV{JAH_VRAM_TOKENS}" MATCHES "^[1-9][0-9]*$")
    set(JAH_VRAM_TOKEN_COUNT $ENV{JAH_VRAM_TOKENS})
endif()
unset(_jah_vt_src)
unset(_jah_vt_m)

# jah_vram_resource_spec() — write <binary dir>/vram-resources.json and name it for this directory's ctest
# (CTEST_RESOURCE_SPEC_FILE is read from the CTestTestfile of the directory that sets it; ctest run from the
# build's top picks it up through subdirs()). Call it once, from the directory whose rows it covers.
macro(jah_vram_resource_spec)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vram-resources.json"
         "{\"version\": {\"major\": 1, \"minor\": 0}, \"local\": [{\"vram\": [{\"id\": \"card\", \"slots\": ${JAH_VRAM_TOKEN_COUNT}}]}]}\n")
    set(CTEST_RESOURCE_SPEC_FILE "${CMAKE_CURRENT_BINARY_DIR}/vram-resources.json")
endmacro()

# _jah_vram_register(<row> <class> <k|all>) — a registered GPU row: listed for the closure lint (JAH_GPU_ROWS)
# and scheduled by its tokens. Its TIMEOUT is never touched here: what the site declares is the bound.
function(_jah_vram_register _row _class _k)
    set_property(GLOBAL APPEND PROPERTY JAH_GPU_ROWS "${_row}|${_class}|${_k}")
    set(_n ${_k})
    if(_k STREQUAL "all")
        set(_n ${JAH_VRAM_TOKEN_COUNT})
    endif()
    if(_n GREATER JAH_VRAM_TOKEN_COUNT)
        set(_n ${JAH_VRAM_TOKEN_COUNT})       # the admission takes all of them for a larger ask, so does ctest
    endif()
    set_property(TEST ${_row} PROPERTY RESOURCE_GROUPS "vram:${_n}")
endfunction()
