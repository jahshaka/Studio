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
# TESTING-CLEANUP-2B item 2: TIMEOUT 450 = 6 x the run log's quiet median 74.0 s (44 quiet PASS runs since 2026-10-01, worst 232.0 s);
# declared 300 was below the policy, honest only with the +900 s widening (now deleted).
# STAGE-CLOSE (TESTING-SPEED-B2; SPECS/TESTING_SPEED_ROWS.md MOVE): the ship and the stage close only.
set_tests_properties(scene.reopen_fidelity PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting;stage-close"
    TIMEOUT 450)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, a stage close) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST scene.reopen_fidelity)
    jah_fresh_home_fixture(scene.reopen_fidelity.fresh_home sceneReopenFidelityFreshHome "${CMAKE_CURRENT_BINARY_DIR}/reopen-home" WARM LABELS scripting stage-close)
    set_property(TEST scene.reopen_fidelity APPEND PROPERTY FIXTURES_REQUIRED sceneReopenFidelityFreshHome)
endif()

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST scene.reopen_fidelity)
    set_property(TEST scene.reopen_fidelity PROPERTY JAHSHAKA_TEST_FRAMES 2774)
endif()
