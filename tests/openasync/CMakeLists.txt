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
#     project, and its ledger carries the close it began with.
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
# DEVPROCESS-1: registered through the GPU-timing lock; TIMEOUT = RUN_TIMEOUT + the lock's 900 s wait.
jah_gpu_exclusive_test(NAME open.responsive RUN_TIMEOUT 400 COMMAND test_open_responsive
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
    LABELS "openasync")

# The race lane (scripts/sanitize.sh tsan): a threaded open racing a main-thread
# heartbeat is exactly what TSan is for. Note the 500 ms gap budget above is a
# WALL-CLOCK assertion — under TSan's 5-15x slowdown it can legitimately fail
# while proving no race; read the race reports, not the verdict.
jah_tsan_blocked(open.responsive)   # spawns the app: see jah_tsan_blocked

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
add_test(NAME open.frames
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
# NIGHTLY, and labelled `benchmark` because that label IS this tree's nightly
# marker: the MERGE and PUSH tiers both run with
# -LE "^(benchmark|shadercache-attack)$", so a suite that needs a quiet box and
# minutes of it carries one of those two labels rather than a third that would
# have to be added to every tier's command line (docs/TESTING_GATE.md).
#
# PROBABILISTIC ON THE BASE, deterministic-ish on the fix: 9 failures in 12 runs
# before the slice boundary drove a frame, 0 in 12 after, measured twice. Read a
# single failure as "run it again"; read three as a regression. RUN_SERIAL: it
# spawns twelve engine processes in sequence and a parallel gate would both slow
# it and change what it measures.
add_test(NAME open.crash_soak
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/open_crash_soak.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_SOURCE_DIR}/scenes
                 ${CMAKE_CURRENT_BINARY_DIR}/soak 12)
set_tests_properties(open.crash_soak PROPERTIES
    TIMEOUT 900 RUN_SERIAL TRUE LABELS "benchmark")
jah_tsan_blocked(open.crash_soak)
