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

set(_open_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-open)
file(MAKE_DIRECTORY ${_open_home}/run)
# THE PUSH ROW ASSERTS THE COUNTS, THE NIGHTLY ROW THE MILLISECONDS (lane D6B-GATE-SHAPE;
# audit §5, verdict FIX-TIME): open.responsive runs every count, structure and ledger
# assertion and prints each UI-gap / frame / watchdog reading — the cold and per-sample ceilings
# and the exit budget too, since GATE-SPEED-1 (39 of its 41 reds on disk were the cold
# ceiling read on a loaded box); open.responsive.timing
# (below) is the same binary with JAHSHAKA_TIMING_BARS=1 — the bars armed, on a quiet box,
# under the GPU-timing lock. The push row measures no time, so it left the lock.
jah_gpu_row(open.responsive CLASS app COMMAND test_open_responsive
         WORKING_DIRECTORY ${_open_home}/run)
# TIMEOUT 400, from the MEASUREMENT and not from an estimate. OPEN-ASSIMP-1
# added the seven remaining shipped samples (import + a synchronous open each,
# plus a reopen and an in-flight case); it was first widened to 600 on a "~95 s"
# guess the numbers did not support. Measured on this box: 21 s warm solo, 40 s
# COLD solo (home wiped, all eight archives imported and their bakes built) and
# 37-60 s inside a -j2 scoped gate. Six times the WORST of those is 360 s, above
# the 300 s floor, so the floor is not enough for the work added — 400 is the
# next value this tree uses.
#
# RE-MEASURED with OPEN-COVER-2b's streaming arm in (2026-09-21, this box): 66 s
# solo on a cold-ish home, 38 s inside a -j2 scoped gate on a warm one. The arm
# adds two creates, an asynchronous create and one Showroom 2 open, each with
# its own control measurement and settle — about 45 s solo. 400 still stands.
set_tests_properties(open.responsive PROPERTIES
    ENVIRONMENT "HOME=${_open_home};JAHSHAKA_DATA_ROOT=${_open_home}/.local/share/Jahshaka"
    # 1740: 6x the worst measured run (281 s, -j4 beside other lanes; D6B fix round F1 —
    # the lock used to add 900 s on top of 400, so leaving it cut the budget below 3x).
    TIMEOUT 1740 LABELS "openasync")
# open.responsive.timing — the millisecond bars (nightly; `quiet-box`: never a scoped gate).
# Its own home, so the nightly run and a lane's push-row run never share a library.
set(_open_timing_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-open-timing)
file(MAKE_DIRECTORY ${_open_timing_home}/run)
jah_gpu_exclusive_test(NAME open.responsive.timing CLASS app RUN_TIMEOUT 1740 COMMAND test_open_responsive
         WORKING_DIRECTORY ${_open_timing_home}/run)
set_tests_properties(open.responsive.timing PROPERTIES
    ENVIRONMENT "HOME=${_open_timing_home};JAHSHAKA_DATA_ROOT=${_open_timing_home}/.local/share/Jahshaka;JAHSHAKA_TIMING_BARS=1"
    LABELS "openasync;nightly;quiet-box")

# The race lane (scripts/sanitize.sh tsan): a threaded open racing a main-thread
# heartbeat is exactly what TSan is for. Note the 500 ms gap budget above is a
# WALL-CLOCK assertion — under TSan's 5-15x slowdown it can legitimately fail
# while proving no race; read the race reports, not the verdict.
jah_tsan_blocked(open.responsive open.responsive.timing)   # spawns the app: see jah_tsan_blocked

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
jah_gpu_row(open.frames CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_frames.js
         WORKING_DIRECTORY ${_frames_home}/run)
# TIMEOUT 300: three opens of the dragon sample plus two archive imports, cold
# (the home is not wiped between runs, so only the first is a cold bake).
set_tests_properties(open.frames PROPERTIES
    ENVIRONMENT "HOME=${_frames_home};JAHSHAKA_DATA_ROOT=${_frames_home}/.local/share/Jahshaka"
    TIMEOUT 300 LABELS "openasync")
jah_tsan_blocked(open.frames)   # spawns the app: see jah_tsan_blocked

# open.crash_soak — the crash itself, N times, under glibc's heap checks.
#
# NIGHTLY (labels `nightly;quiet-box`, D6B-GATE-SHAPE — it was labelled `benchmark` when that
# label doubled as the nightly marker): out of the MERGE and PUSH tiers (`gate-scope.py
# --merge-tier`) AND out of every scoped gate — twelve RUN_SERIAL engine processes read
# probabilistically would stop a lane's scoped gate for minutes and ask for re-runs.
#
# PROBABILISTIC ON THE BASE, deterministic-ish on the fix: 9 failures in 12 runs
# before the slice boundary drove a frame, 0 in 12 after, measured twice. Read a
# single failure as "run it again"; read three as a regression. RUN_SERIAL: it
# spawns twelve engine processes in sequence and a parallel gate would both slow
# it and change what it measures.
jah_gpu_row(open.crash_soak CLASS app
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/open_crash_soak.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_SOURCE_DIR}/scenes
                 ${CMAKE_CURRENT_BINARY_DIR}/soak 12)
set_tests_properties(open.crash_soak PROPERTIES
    TIMEOUT 900 RUN_SERIAL TRUE LABELS "nightly;quiet-box")
jah_tsan_blocked(open.crash_soak)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, the nightly) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST open.responsive)
    jah_fresh_home_fixture(open.responsive.fresh_home openResponsiveFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-open" WARM LABELS "openasync")
    set_property(TEST open.responsive APPEND PROPERTY FIXTURES_REQUIRED openResponsiveFreshHome)
endif()
if(TEST open.responsive.timing)
    jah_fresh_home_fixture(open.responsive.timing.fresh_home openResponsiveTimingFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-open-timing" WARM LABELS "openasync")
    set_property(TEST open.responsive.timing APPEND PROPERTY FIXTURES_REQUIRED openResponsiveTimingFreshHome)
endif()
if(TEST open.frames)
    jah_fresh_home_fixture(open.frames.fresh_home openFramesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-frames" WARM LABELS "openasync")
    set_property(TEST open.frames APPEND PROPERTY FIXTURES_REQUIRED openFramesFreshHome)
endif()
