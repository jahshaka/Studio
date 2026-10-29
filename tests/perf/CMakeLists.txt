# perf.pacing (archived: archive/tests/perf) — the frame-pacing interval policy (fps audit F1).
#
# DISPLAY-FREE and dependency-free: it links src/services/framepacing.cpp and
# Qt Core, nothing else. No engine, no widgets, no QScreen — the policy is a
# pure function and the suite exists so that it stays one. (The driver half,
# "the timer really re-times", needs a live engine and belongs to the app
# suites; what would break silently is this arithmetic.)
find_package(Qt6 REQUIRED COMPONENTS Core)

# THE APP ROWS OF THIS FAMILY ARE STAGE-CLOSE (TESTING-SPEED-B3, the ROWS table): the steady-state,
# drag and capture-cost measurements (timing rows: the stage close's quiet-box phase), the hide soak,
# the capture bundle and the offscreen isolation (minutes of app runs: the ship's) — with their fresh
# homes. perf.pacing (archived: archive/tests/perf) (a pure function, no engine) stays in the merge tier.
#
# perf.offscreen_isolation — a thumbnail/screenshot readback must not redraw
# (and present) the editor's on-screen view (fps audit F5). Engine up, its own
# HOME like every other scripted e2e; the script's header explains the counter
# it asserts on.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-offscreen_isolation)
file(MAKE_DIRECTORY ${_home}/run)
jah_gpu_row(perf.offscreen_isolation WARM_HOME "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-offscreen_isolation" SHIP CLASS app TIER low NEEDS NONE NEEDS_WHY "a readback must not redraw the editor view: a counter around a screenshot (no picture claim)"
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/offscreen_isolation.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(perf.offscreen_isolation PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "perf"
    TIMEOUT 60)

# perf.epic_steady_state (archived: archive/tests/perf) — the GROWTH-ONLY steady-state gate (fps audit F11).
#
# The regression net for the class that shipped on 2026-09-05: a frame that got
# more expensive, and object counts that grew, while every pixel and every
# document field stayed identical. The script's header is the reference for what
# it measures and why; what belongs HERE is why it is wired the way it is.
#
#   * A VIRGIN HOME, wiped by a setup fixture, exactly like samples.cleanstart.
#     The suite imports the Matcaps archive itself, so a HOME that survived the
#     previous ctest would make it a re-import — and, worse, would carry the
#     previous run's shader cache, which changes what sample 1 costs.
#   * ENGINE UP (no --headless): the whole subject is a frame being rendered.
#   * NOT a benchmark, and not labelled one. It asserts NOTHING absolute — every
#     comparison is one sample of a run against another sample of the SAME run —
#     so it is machine-independent, costs ~350 frames of one small scene, and
#     belongs in the ordinary gate rather than behind `ctest -LE benchmark`.
#   * THE GPU-TIMING LOCK (lane D6B-GATE-SHAPE; audit §4 R3). The equalities do not
#     need it (an object count does not care what else the box is doing), but the
#     cost ratio it prints and the one inequality it asserts do, and a number
#     nobody can trust is worse than no number. It used to be RUN_SERIAL, which
#     keeps only this ctest's own rows off it; the lock is box-wide.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})

# ---------------------------------------------------------------------------
# THE RENDER-LOOP MONITOR (RENDER_LOOP_MONITOR_SPEC.md §4.8, lane MON-P1b)
# ---------------------------------------------------------------------------
#
# Two suites, both DATA-INTEGRITY ONLY (owner D5: "the profiler shouldn't care
# about the targets, it should just collect data"). Nothing here asserts a
# performance number; the only inequality in either of them is a same-run ratio
# that catches a listener left attached.

# perf.capture_bundle (archived: archive/tests/perf) — the bundle is complete, parseable and internally
# consistent. Split in two on purpose: the app RECORDS (capture_bundle.js, which
# needs the engine and a project) and the harness READS what it wrote, because
# the scripting engine has no file access.
set(_cb_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-capture_bundle)
set(JAH_TEST_BUNDLE ${_cb_home}/bundle)
configure_file(capture_bundle.js.in ${CMAKE_CURRENT_BINARY_DIR}/capture_bundle.js @ONLY)
# Qt6::Network and the shared MCP harness (tests/support/mcpharness.h, included
# by path): phase 2 drives a REAL window close (app.quit over MCP) with a
# capture running, which is the path --script can never take.
find_package(Qt6 REQUIRED COMPONENTS Network)

# perf.capture_off_is_free (archived: archive/tests/perf) — off costs nothing, and the state machine's edges
# (refusals, the early stop, the return to exactly the state it started in).
# The engine is up: "no listener is attached to any workspace" needs workspaces.
set(_off_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-capture_off)
set(JAH_TEST_BUNDLE ${_off_home}/perf/bundle)
file(MAKE_DIRECTORY ${_off_home}/run)

# perf.capture_free_tick (archived: archive/tests/perf) — A CAPTURE IN FREE-RUNNING COSTS THE UI TICK NOTHING IT CAN FEEL (FRAME-MONITOR-DOOR-1): the
# owner's path (JAH_FREE_RUNNING=1, --script-live: Ctrl+F4's 60-frame capture at 200 cubes, the diagnosis' method) —
# every UI tick during the capture lasts at most one frame budget (<= 2 render-thread frames beside it), the capture
# ends within 120 ticks and 120 render-thread frames, the bundle holds exactly its 60 records, nothing was dropped, and
# the host's engine requests do not move across the capture's ticks (its start's two and its close's two only). Red on
# the base (the drain by request: ~192 frames per tick); a GUARD, in frames. Cannot boot under TSan (Qt's recycled
# pthread_t): its door's TSan form is the engine fixture frame.monitor_pop.
set(_cft_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-capture_free_tick)
file(MAKE_DIRECTORY ${_cft_home}/run)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, a stage close) — and every
# such home grew without bound (ui.column_law (archived: archive/tests/ui)'s held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST perf.offscreen_isolation)
    jah_fresh_home_fixture(perf.offscreen_isolation.fresh_home perfOffscreenIsolationFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-offscreen_isolation" WARM LABELS "perf")
    set_property(TEST perf.offscreen_isolation APPEND PROPERTY FIXTURES_REQUIRED perfOffscreenIsolationFreshHome)
endif()

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "perf.offscreen_isolation=487"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
