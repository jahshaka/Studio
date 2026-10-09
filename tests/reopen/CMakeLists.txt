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
jah_gpu_row(scene.reopen_fidelity CLASS app TIER document
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/reopen_fidelity.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scene.reopen_fidelity PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, the nightly) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST scene.reopen_fidelity)
    jah_fresh_home_fixture(scene.reopen_fidelity.fresh_home sceneReopenFidelityFreshHome "${CMAKE_CURRENT_BINARY_DIR}/reopen-home" WARM LABELS "scripting")
    set_property(TEST scene.reopen_fidelity APPEND PROPERTY FIXTURES_REQUIRED sceneReopenFidelityFreshHome)
endif()
