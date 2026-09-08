# The shutdown/threading gates (STABILITY_PROGRAM_SPEC.md Lanes 3 and 5).
# Both spawn the REAL binary and drive it over MCP (the import.shutdown /
# open.responsive pattern), because both assert on what the PROCESS does —
# the order its teardown runs in, and what it prints when its UI thread stops
# answering. Neither needs a display: offscreen MCP, scratch HOME.
find_package(Qt6 REQUIRED COMPONENTS Core Network)

# app.shutdown_order — Lane 3: the seven steps of src/shell/shutdownorder.h
# fire once each, in order, and the engine dies before the database closes.
add_executable(test_shutdown_order test_shutdown_order.cpp)
set_target_properties(test_shutdown_order PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_shutdown_order PRIVATE Qt6::Core Qt6::Network)
target_include_directories(test_shutdown_order PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_shutdown_order PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

set(_order_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-order)
file(MAKE_DIRECTORY ${_order_home}/run)
add_test(NAME app.shutdown_order COMMAND test_shutdown_order
         WORKING_DIRECTORY ${_order_home}/run)
set_tests_properties(app.shutdown_order PROPERTIES
    ENVIRONMENT "HOME=${_order_home}"
    TIMEOUT 300 LABELS "shutdown")

# app.watchdog_stall — Lane 5: a 3 s UI-thread block produces exactly one
# [warn] and a non-empty stack OF THE UI THREAD; a 100 ms block produces none.
add_executable(test_watchdog_stall test_watchdog_stall.cpp)
set_target_properties(test_watchdog_stall PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_watchdog_stall PRIVATE Qt6::Core Qt6::Network)
target_include_directories(test_watchdog_stall PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_watchdog_stall PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

set(_watchdog_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-watchdog)
file(MAKE_DIRECTORY ${_watchdog_home}/run)
add_test(NAME app.watchdog_stall COMMAND test_watchdog_stall
         WORKING_DIRECTORY ${_watchdog_home}/run)
# RUN_SERIAL: this is a pure TIMING gate — it blocks the UI thread for 3 s and
# for 100 ms and asserts the watchdog counted exactly one stall and exactly
# none. Both halves are wall-clock judgements made INSIDE a spawned app, so a
# parallel ctest schedule can manufacture a stall the test never asked for (the
# 100 ms half) or delay the stack capture past the window (the 3 s half). Every
# other timing gate in the tree already carries it — benchmarks (wall-clock
# measurements), perf.*, threading.* — and this suite was recorded as a new
# contention-flake candidate on 2026-09-08 (JOURNAL) for exactly that reason.
# N7, ENGINEERING_DEBT_SPEC re-audit.
set_tests_properties(app.watchdog_stall PROPERTIES
    ENVIRONMENT "HOME=${_watchdog_home}"
    RUN_SERIAL TRUE
    TIMEOUT 300 LABELS "shutdown")

# The race lane (scripts/sanitize.sh tsan): teardown ordering and the watchdog
# are pure thread-interaction contracts, and shutdown is where read-after-destroy
# across threads traditionally hides.
jah_tsan_blocked(app.shutdown_order app.watchdog_stall)   # spawn the app
