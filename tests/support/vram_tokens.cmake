# THE BOOT DECLARATION AND THE TOKEN LOOKUP (TEST-NEEDS-1; docs/TESTING_GATE.md §4b). Included by
# tests/CMakeLists.txt and, alone, by the toy suite gate.tokens_by_needs (cmake -P) — so the
# lookup the gate admits by is the lookup the toy proves.
#
# EVERY ROW THAT STARTS THE APP DECLARES WHAT IT BOOTS (owner 2026-10-09: "a test suite passes
# variables for what it needs; if you don't need Photon, run Low"): `TIER <low|medium|high|epic>`
# — the World Mode the process puts every scene it binds on (JAHSHAKA_TEST_TIER) — and
# `NEEDS <photon bloom ssao smaa planar…>|NONE` — the switchable features it keeps
# (JAHSHAKA_TEST_NEEDS); the app switches every one not named OFF (worldmodes::applyTestTier).
# NONE is explicit: the row keeps none of them ("this row reads no switchable feature").
# `TIER document` (and no NEEDS) is the one declaration with no test tier: the process honours
# each scene's OWN tier, like the product — for a claim ABOUT the document's World state (a
# sample opens at its authored tier with no deviations, a saved World row survives a reopen, the
# product's own boot), which a test tier would rewrite before the claim could read it
# (services/testtier.h: a test-tier process puts every scene it binds on its mode). It is a
# declaration, not a fallback: the row says so. There is no default: a jah_gpu_row CLASS app|vr,
# a jah_gpu_exclusive_test of those classes and every engine-up jah_add_pool without both words
# is a configure error, and source.row_declares_needs refuses what ctest would run without them.

set(JAH_TEST_TIERS low medium high epic document)
set(JAH_TEST_NEEDS_WORDS photon bloom ssao smaa planar)

# THE CLASSES (the audit's A2 table, measured 2026-09-26 with nvidia-smi per pid):
#   app      by the declaration (JAH_VRAM_TOKENS_BY_NEEDS below)
#   selftest 2  `--engine-selftest` (the app's own route at Epic; it binds no editor scene, so
#               it declares no TIER/NEEDS)
#   engine   1  a headless engine suite (its own tier, set in C++: ~0.6-1.2 GB)
#   vr       3  a VR / Monado row (app + stereo views + the runtime's compositor, ~2 GB)
#   none     0  only for a GPU-timing row (jah_gpu_exclusive_test) that runs with no display
set(JAH_VRAM_TOKENS_SELFTEST 2)
set(JAH_VRAM_TOKENS_ENGINE 1)
set(JAH_VRAM_TOKENS_VR 3)
# AN APP PROCESS'S TOKENS ARE ITS DECLARATION'S (1 token = 1,090 MiB at the measured peak). The key
# is `<tier>:<photon|none>` — Photon is the one switchable feature that moves the process by
# gigabytes (the voxel cascades, the field, the probes and cards); bloom/SSAO/SMAA/planar ride
# the chain's own targets.
#   low:*            2  a Low process peaks at 1,461-1,491 MiB (below): 1.37 tokens
#   medium:*         2  Photon Medium (four 64^3 cascades + the field) peaks at 1,587-1,655 MiB
#   high:photon      2  the hybrid at 128^3 near
#   epic:photon      3  the audit's 2-3.6 GB for the gather family at Epic (GATE-ADMIT-1's
#                       two-tier run); the old flat `app 2` was median 2,000 / p90 2,270 / max
#                       2,600 MiB against 2,180 — the thin margin this closes
#   medium/high/epic:none  2 / 2 / 2 — the chain without Photon (unmeasured at High/Epic: the
#                       shadow atlas and the ray reflections stay; kept at 2 until measured)
#   document         3  the document's own tier: a new scene is Epic with everything on, a
#                       sample opens at what it saved (Epic for four of the nine)
# MEASURED (TEST-NEEDS-1, 2026-10-09/10, nvidia-smi --query-compute-apps per pid, 0.5 s samples,
# peak per process, rows run at -j2 on a 1920x1080 Xvfb; spikes/test-needs-1/vram-runs*.json):
#   low NONE         median 1,461 / max 1,491 MiB  (23 processes: scripting.live/_off, mcp.e2e,
#                    ui.shot_aspect, perf.offscreen_isolation, the low pools, test_needs_boot)
#   medium none      1,587 MiB; medium photon 1,655 MiB (test_needs_boot.js by hand: no row
#                    declares medium)
#   high photon      1,777-2,089 MiB (gi.chain_converge_scenes, test_needs_boot high)
#   epic photon      median 2,292 / max 2,532 MiB (56 processes: gi.one_writer, the pcc and
#                    lattice rows, the lifecycle harnesses, the atom rows, all-five rows)
#   document         median 2,226 / max 2,610 MiB (pool.gi_verbs, pool.atom, cleanstart Showroom 2)
# ADDITIVITY, MEASURED AT STEADY STATE (2026-10-10, four processes booted at once on one Xvfb, each
# admitted through gpu-admit like a row, running a HOLD script — an empty project, frames; the
# card's memory.used minus the idle baseline at steady state, corrected by every other process's
# own change; spikes/test-needs-1/conc-*.json):
#   4 x low NONE       card +1,944 MiB against a per-pid sum of 1,888 (~472 per pid, max 567)
#   4 x medium photon  card +2,617 MiB against a per-pid sum of 2,592
# So the card adds per process at the hold's steady state. The ROW peaks (above, from
# vram-runs*.json, 23 Low processes) are larger — 1,461 / 1,491 MiB Low, 1,587 / 1,655 Medium —
# and were not measured concurrently; with the card additive per process, a Low or Medium row
# takes ceil(row peak / 1,090) = 2.
# Epic+photon / document peak at 2.3-2.6 GB: over 2 tokens (2,180), under 3 — hence 3.
set(JAH_VRAM_TOKENS_BY_NEEDS
    low:none=2 low:photon=2
    medium:none=2 medium:photon=2
    high:none=2 high:photon=2
    epic:none=2 epic:photon=3
    document=3)

# jah_test_boot_check(<who> <tier> <needs-list>) — validates one declaration (a configure error
# names <who>): TIER one of JAH_TEST_TIERS; NEEDS a non-empty list of JAH_TEST_NEEDS_WORDS, or
# NONE alone.
function(jah_test_boot_check _who _tier)
    set(_needs ${ARGN})
    if(NOT _tier)
        message(FATAL_ERROR "${_who}: TIER <low|medium|high|epic> is required — every row that starts "
                            "the app declares the World Mode it boots (tests/support/vram_tokens.cmake)")
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

# jah_vram_tokens(<class> <out-var> [TIER <tier> NEEDS <list>|NONE]) — THE CLASS -> TOKENS LOOKUP.
# An app class REQUIRES the declaration (there is no default to fall back on); the other classes
# take none.
function(jah_vram_tokens _class _out)
    cmake_parse_arguments(V "" "TIER" "NEEDS" ${ARGN})
    if(_class STREQUAL "app")
        jah_test_boot_check("jah_vram_tokens(app)" "${V_TIER}" ${V_NEEDS})
        set(_key "${V_TIER}:none")
        if(V_TIER STREQUAL "document")
            set(_key "document")
        elseif("photon" IN_LIST V_NEEDS)
            set(_key "${V_TIER}:photon")
        endif()
        set(_k)
        foreach(_e IN LISTS JAH_VRAM_TOKENS_BY_NEEDS)
            if(_e MATCHES "^${_key}=([0-9]+)$")
                set(_k ${CMAKE_MATCH_1})
            endif()
        endforeach()
        if(NOT _k)
            message(FATAL_ERROR "jah_vram_tokens: no JAH_VRAM_TOKENS_BY_NEEDS entry for '${_key}'")
        endif()
    elseif(_class STREQUAL "selftest")
        set(_k ${JAH_VRAM_TOKENS_SELFTEST})
    elseif(_class STREQUAL "engine")
        set(_k ${JAH_VRAM_TOKENS_ENGINE})
    elseif(_class STREQUAL "vr")
        set(_k ${JAH_VRAM_TOKENS_VR})
    elseif(_class STREQUAL "none")
        set(_k 0)
    else()
        message(FATAL_ERROR "jah_vram_tokens: unknown class '${_class}' (app|selftest|engine|vr|none)")
    endif()
    set(${_out} ${_k} PARENT_SCOPE)
endfunction()
