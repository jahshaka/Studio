# THE BOOT DECLARATION AND THE CLASS SLOTS (TEST-NEEDS-1; TESTING-SIMPLE). Included by tests/CMakeLists.txt.
#
# EVERY ROW THAT STARTS THE APP DECLARES WHAT IT BOOTS (owner 2026-10-09: "a test suite passes
# variables for what it needs; if you don't need Photon, run Low"): `TIER <low|medium|high|epic>`
# — the World Mode the process puts every scene it binds on (JAHSHAKA_TEST_TIER) — and
# `NEEDS <photon bloom ssao smaa planar…>|NONE` — the switchable features it keeps
# (JAHSHAKA_TEST_NEEDS); the app switches every one not named OFF (worldmodes::applyTestTier).
# NONE is explicit: the row keeps none of them ("this row reads no switchable feature").
# `TIER document` (and no NEEDS) is the one declaration with no test tier: the process honours
# each scene's OWN tier, like the product — for a claim ABOUT the document's World state. There is no
# default: a jah_gpu_row CLASS app|vr, a jah_gpu_exclusive_test of those classes and every engine-up
# jah_add_pool without both words is a configure error, and source.row_declares_needs refuses what
# ctest would run without them.

set(JAH_TEST_TIERS low medium high epic document)
set(JAH_TEST_NEEDS_WORDS photon bloom ssao smaa planar)

function(jah_test_boot_check _who _tier)
    set(_needs ${ARGN})
    if(NOT _tier)
        message(FATAL_ERROR "${_who}: TIER <low|medium|high|epic> is required — every row that starts "
                            "the app declares the World Mode it boots (tests/support/test_boot.cmake)")
    endif()
    if(NOT _tier IN_LIST JAH_TEST_TIERS)
        message(FATAL_ERROR "${_who}: TIER '${_tier}' is not one of ${JAH_TEST_TIERS}")
    endif()
    if(_tier STREQUAL "document")
        if(_needs)
            message(FATAL_ERROR "${_who}: TIER document takes no NEEDS — the document decides every "
                                "feature (got '${_needs}')")
        endif()
        return()
    endif()
    if(NOT _needs)
        message(FATAL_ERROR "${_who}: NEEDS <${JAH_TEST_NEEDS_WORDS}…>|NONE is required — every row "
                            "that starts the app declares what it keeps; NONE = it reads no picture")
    endif()
    if("NONE" IN_LIST _needs)
        list(LENGTH _needs _n)
        if(NOT _n EQUAL 1)
            message(FATAL_ERROR "${_who}: NEEDS NONE stands alone (got '${_needs}')")
        endif()
        return()
    endif()
    foreach(_w IN LISTS _needs)
        if(NOT _w IN_LIST JAH_TEST_NEEDS_WORDS)
            message(FATAL_ERROR "${_who}: NEEDS '${_w}' is not a switchable feature (${JAH_TEST_NEEDS_WORDS}, or NONE)")
        endif()
    endforeach()
    set(_dup ${_needs})
    list(REMOVE_DUPLICATES _dup)
    if(NOT _dup STREQUAL _needs)
        message(FATAL_ERROR "${_who}: NEEDS names a feature twice ('${_needs}')")
    endif()
endfunction()

# jah_test_boot_env(<out-var> <tier> <needs-list>) — the ENVIRONMENT_MODIFICATION entries a
# declaration puts on its row (`set:` replaces whatever the environment carried).
function(jah_test_boot_env _out _tier)
    set(_needs ${ARGN})
    if(_tier STREQUAL "document")
        # no test tier: `unset:` clears whatever the environment carried
        set(${_out} "JAHSHAKA_TEST_TIER=unset:" "JAHSHAKA_TEST_NEEDS=unset:" PARENT_SCOPE)
        return()
    endif()
    if("NONE" IN_LIST _needs)
        set(_v none)
    else()
        list(JOIN _needs " " _v)
    endif()
    set(${_out} "JAHSHAKA_TEST_TIER=set:${_tier}" "JAHSHAKA_TEST_NEEDS=set:${_v}" PARENT_SCOPE)
endfunction()

# THE WIDTH PER CLASS — the card's slots a row of the class takes (tests/cmake/vram_rows.cmake: 8 slots): EVERY GPU ROW
# TAKES ALL EIGHT, so GPU rows run ONE AT A TIME (urgent #44, PHOTON-7 stage 7, 2026-10-24); `none` 0 (a row that boots
# no Vulkan still overlaps them). WHY: a row's 60 s cap is ITS OWN cost, and on one card a neighbour inflates it - a spec
# call ran 24 GPU rows beside gi.card_lamp_shadow (11.18 s alone) and timed it out at 60 s, gi.opaque_bindings read 28 s
# against 5.34 alone; eight fixtures at once had made a row's wall time a function of whatever else the call held.
set(JAH_SLOTS_engine 8)
set(JAH_SLOTS_selftest 8)
set(JAH_SLOTS_app 8)
set(JAH_SLOTS_vr 8)
set(JAH_SLOTS_none 0)

# jah_card_slots(<class> <out-var> [TIER <tier> NEEDS <list>|NONE]) — the class's slots; an app row's TIER + NEEDS are
# checked here (a row that starts the app declares what it boots).
function(jah_card_slots _class _out)
    cmake_parse_arguments(V "" "TIER" "NEEDS" ${ARGN})
    if(NOT DEFINED JAH_SLOTS_${_class})
        message(FATAL_ERROR "jah_card_slots: unknown class '${_class}' (app|selftest|engine|vr|none)")
    endif()
    if(_class STREQUAL "app")
        jah_test_boot_check("jah_card_slots(app)" "${V_TIER}" ${V_NEEDS})
    endif()
    set(${_out} ${JAH_SLOTS_${_class}} PARENT_SCOPE)
endfunction()
