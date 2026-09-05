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
add_test(NAME api.contract
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/api_contract.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_SOURCE_DIR}/docs/SCRIPTING.md"
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(api.contract PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    TIMEOUT 120
    LABELS "scripting")
# `--dump-api-docs` boots the NULL render system (SPECS/SCENEGRAPH_SPEC.md §3b):
# generating the docs needs the registry and a document, never a pixel. Run with
# DISPLAY stripped so that stays true.
jah_no_display(api.contract)
