# api.contract — the scripting surface's CONTRACT GATE (AI_SURFACE_PROGRAM_SPEC §2.0).
#
# The registry is the single source for help(), the MCP schemas and
# docs/SCRIPTING.md, and the docs page is generated — but nothing asserted the
# committed file had actually been regenerated. This suite closes that: it runs
# `--dump-api-docs` in the real binary (forced headless by the CLI itself) under
# a scratch HOME and byte-compares the result against the file in the repo. The
# failure message carries the exact regenerate command, because the first
# failure will always be somebody who added a verb and forgot.
#
# It is deliberately NOT part of tests/scripting: every lane that adds a verb
# touches that directory, and the gate that catches them should not move with
# them.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/api-contract-home)
file(MAKE_DIRECTORY ${_home}/run)
# THE SCRATCH HOME IS WIPED BEFORE EVERY RUN (2026-10-20): it persists across runs of one build dir, and a JahLibrary.db
# left by an older build carries an older schema — the app's reset keeps the storages, wants their sidecar directory, an
# older AssetStore has none, and the library is REFUSED (exit 4): "--dump-api-docs exited 4" on main's build-linux, green
# in a fresh ship tree. A row owns its own state (the same fault as shadercache.app, 67cefee33).
jah_fresh_home_fixture(api.contract.fresh_home api_contract_home "${_home}" WARM LABELS "scripting")
jah_ship_row(api.contract.fresh_home)   # a ship row's setup runs in the ship set too
add_test(NAME api.contract
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/api_contract.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_SOURCE_DIR}/docs/SCRIPTING.md"
         WORKING_DIRECTORY ${_home}/run)
jah_ship_row(api.contract)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(api.contract PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED api_contract_home
    TIMEOUT 60
    LABELS "scripting")
# `--dump-api-docs` boots the NULL render system (SPECS/SCENEGRAPH_SPEC.md §3b):
# generating the docs needs the registry and a document, never a pixel. Run with
# DISPLAY stripped so that stays true.
jah_no_display(api.contract)
