# open.responsive — the force-quit fix, measured.
#
# Spawns the REAL binary (the import.shutdown pattern), imports the Matcaps
# sample, opens it through the THREADED path while a main-thread heartbeat
# probe records the worst gap between its own ticks, and asserts:
#   * the open completes and really loaded the world,
#   * the app answered requests while it was in flight,
#   * no UI-thread gap beyond the budget (500 ms, or twice what the app's own
#     frame loop costs on this box right now — see kControlFactor in the suite),
#   * the synchronous project.open still opens the same world unchanged,
#   * EVERY shipped sample, opened through the synchronous verb, parses no
#     model on the UI thread (app.openStats(); OPEN-ASSIMP-1),
#   * quitting with an open in flight terminates the process cleanly,
#   * AND (OPEN-COVER-2b) THE STREAMING ARM: with the loading cover off — the
#     default — a create blocks no frame over 300 ms and draws no cover, the
#     Showroom 2 open's worst frame is printed against the same 300 and
#     asserted at 600 until PCC-BUDGET-1 lands, the watchdog reports no stall
#     at its default 2,000 ms, app.openTimings() carries the CREATE's ledger,
#     and project.createAsync hands back a guid before the world is installed,
#   * AND (CREATE-GAP-1) THE FULL LIBRARY: a create over a 40-project desktop
#     stays inside the same budget, rebuilds no grid, leaves one tile per
#     project, and its ledger carries the close it began with — and after those
#     40+ creates in one process the Properties column is shown, mounted and owes
#     nothing (the create-loop SIGSEGV's column; ui.show_walk is its mechanism).
# EVERY RUN STARTS FROM A FIXED LIBRARY: the suite deletes the home's library
# database, its lock, the asset store and the project folders before it spawns
# the app (the shader cache stays) — the home used to grow ~14 projects a run.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(test_open_responsive test_open_responsive.cpp)
set_target_properties(test_open_responsive PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_open_responsive PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(test_open_responsive PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

# ONE ROW, AT THE STAGE CLOSE (TESTING-SPEED-B3): open.responsive.timing below is the suite — every
# count, structure and ledger assertion AND the millisecond bars armed (JAHSHAKA_TIMING_BARS=1), on a quiet
# box under the GPU-timing lock. Its push-tier twin `open.responsive` (the same binary in a second home,
# the bars only printed: 147 s a merge) is deleted; the merge tier's openasync guard is open.frames.
# open.responsive.timing — the counts and the millisecond bars (stage-close; `quiet-box`: never a scoped gate).
set(_open_timing_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-open-timing)
file(MAKE_DIRECTORY ${_open_timing_home}/run)
# TESTING-CLEANUP-2B item 7, re-based after read 4 (2026-10-10; the sample per tests/CMakeLists.txt's policy — the run
# log's SLOT-ERA quiet PASS runs, since 2026-10-09 18:00, taken as the gate runs the row): COLD, n=1 — the only
# cold PASS run (148.7 s, 14:08, its home's first run, load 7.1, 2 builds on the box); median 148.7 -> 6x = 900;
# slot-era worst 148.7 -> RUN_TIMEOUT 900 (was 1740; ctest's TIMEOUT is RUN_TIMEOUT + 30). Its two warm repeats in the
# same home (74.5 s with the tree dirty, 73.3 s) are not the sample; a run that went red on the ms bars (192.2 s) beside
# the builder's own CPU work was discarded. One cold sample: the next cold PASS runs re-price it.
jah_gpu_exclusive_test(NAME open.responsive.timing CLASS app TIER epic NEEDS photon bloom ssao smaa planar RUN_TIMEOUT 900 COMMAND test_open_responsive
         WORKING_DIRECTORY ${_open_timing_home}/run)
set_tests_properties(open.responsive.timing PROPERTIES
    ENVIRONMENT "HOME=${_open_timing_home};JAHSHAKA_DATA_ROOT=${_open_timing_home}/.local/share/Jahshaka;JAHSHAKA_TIMING_BARS=1"
    LABELS "openasync;measure")

# The race lane (scripts/sanitize.sh tsan): a threaded open racing a main-thread
# heartbeat is exactly what TSan is for. Note the 500 ms gap budget above is a
# WALL-CLOCK assertion — under TSan's 5-15x slowdown it can legitimately fail
# while proving no race; read the race reports, not the verdict.
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(open.responsive.timing)   # spawns the app: see jah_tsan_blocked

# open.frames — THE THREADED OPEN DRIVES ITS OWN FRAMES (lane OPEN-FRAMES-1).
#
# The deterministic half of the async-open crash fix: an open driven by a pump
# that renders NOTHING (no editor.frame anywhere in the script) must still have
# the renderer driven at every slice boundary, and must complete. It also pins
# the two contracts the same lane added around it: closeProject drains an
# install in flight, and project.openState() is the same predicate
# project.openAsync refuses on.
#
# Engine-up — it asserts real frames were drawn — so it needs the rig's display.
configure_file(e2e_open_frames.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_frames.js @ONLY)
set(_frames_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-frames)
file(MAKE_DIRECTORY ${_frames_home}/run)
jah_gpu_row(open.frames CLASS app TIER epic NEEDS photon
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_frames.js
         WORKING_DIRECTORY ${_frames_home}/run)
# budget 40 s: the measurement (TESTING-SPEED-B3, the lead's verdict 2026-10-12): 539 frames, ~23-25 s with the WARM
# home it runs in (a cold home's startup build is 40 s of its own: that is not this row's shape).
# TIMEOUT 300: three opens of the dragon sample plus two archive imports, cold
# (the home is not wiped between runs, so only the first is a cold bake).
set_tests_properties(open.frames PROPERTIES
    ENVIRONMENT "HOME=${_frames_home};JAHSHAKA_DATA_ROOT=${_frames_home}/.local/share/Jahshaka"
    TIMEOUT 300 LABELS "openasync")
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(open.frames)   # spawns the app: see jah_tsan_blocked

# open.crash_soak — the crash itself, N times, under glibc's heap checks.
#
# STAGE-CLOSE (labels `stage-close;quiet-box`, D6B-GATE-SHAPE — it was labelled `benchmark` when that
# label doubled as the stage-close marker): out of the MERGE and PUSH tiers (`gate-scope.py
# --merge-tier`) AND out of every scoped gate — twelve RUN_SERIAL engine processes read
# probabilistically would stop a lane's scoped gate for minutes and ask for re-runs.
#
# PROBABILISTIC ON THE BASE, deterministic-ish on the fix: 9 failures in 12 runs
# before the slice boundary drove a frame, 0 in 12 after, measured twice. Read a
# single failure as "run it again"; read three as a regression. RUN_SERIAL: it
# spawns twelve engine processes in sequence and a parallel gate would both slow
# it and change what it measures.
jah_gpu_row(open.crash_soak CLASS app TIER epic NEEDS photon
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/open_crash_soak.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_SOURCE_DIR}/scenes
                 ${CMAKE_CURRENT_BINARY_DIR}/soak 12)
set_tests_properties(open.crash_soak PROPERTIES
    TIMEOUT 900 RUN_SERIAL TRUE LABELS "measure")
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW, TSAN-GCC15-THREAD-REGISTRY (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(open.crash_soak)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, a stage close) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST open.responsive.timing)
    jah_fresh_home_fixture(open.responsive.timing.fresh_home openResponsiveTimingFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-open-timing" WARM LABELS "openasync")
    set_property(TEST open.responsive.timing APPEND PROPERTY FIXTURES_REQUIRED openResponsiveTimingFreshHome)
endif()
if(TEST open.frames)
    jah_fresh_home_fixture(open.frames.fresh_home openFramesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-frames" WARM LABELS "openasync")
    set_property(TEST open.frames APPEND PROPERTY FIXTURES_REQUIRED openFramesFreshHome)
endif()

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "open.crash_soak=5946"
        "open.frames=539"
        "open.responsive.timing=2342"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
