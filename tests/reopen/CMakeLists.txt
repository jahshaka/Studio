# scene.reopen_fidelity — the save/close/reopen round trip (2026-09-04).
#
# Drives the real binary with --script, like the scripting.e2e.* suites, and for
# the same reason: the defect it gates only exists when the writer, the reader,
# the asset catalog, the store and the renderer are all the live ones. HOME is
# redirected to a scratch dir so the run gets a fresh library and can never
# touch the user's. It needs a reachable display (it probes pixels) — the same
# contract every pixel e2e suite here has.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/reopen-home)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scene.reopen_fidelity
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/reopen_fidelity.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scene.reopen_fidelity PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 300)
