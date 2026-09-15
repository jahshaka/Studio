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
#   * quitting with an open in flight terminates the process cleanly.
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
add_test(NAME open.responsive COMMAND test_open_responsive
         WORKING_DIRECTORY ${_open_home}/run)
# TIMEOUT 400, from the MEASUREMENT and not from an estimate. OPEN-ASSIMP-1
# added the seven remaining shipped samples (import + a synchronous open each,
# plus a reopen and an in-flight case); it was first widened to 600 on a "~95 s"
# guess the numbers did not support. Measured on this box: 21 s warm solo, 40 s
# COLD solo (home wiped, all eight archives imported and their bakes built) and
# 37-60 s inside a -j2 scoped gate. Six times the WORST of those is 360 s, above
# the 300 s floor, so the floor is not enough for the work added — 400 is the
# next value this tree uses.
set_tests_properties(open.responsive PROPERTIES
    ENVIRONMENT "HOME=${_open_home};JAHSHAKA_DATA_ROOT=${_open_home}/.local/share/Jahshaka"
    TIMEOUT 400 LABELS "openasync")

# The race lane (scripts/sanitize.sh tsan): a threaded open racing a main-thread
# heartbeat is exactly what TSan is for. Note the 500 ms gap budget above is a
# WALL-CLOCK assertion — under TSan's 5-15x slowdown it can legitimately fail
# while proving no race; read the race reports, not the verdict.
jah_tsan_blocked(open.responsive)   # spawns the app: see jah_tsan_blocked
