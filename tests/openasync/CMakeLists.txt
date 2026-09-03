# open.responsive — the force-quit fix, measured.
#
# Spawns the REAL binary (the import.shutdown pattern), imports the Showroom
# sample, opens it through the THREADED path while a main-thread heartbeat
# probe records the worst gap between its own ticks, and asserts:
#   * the open completes and really loaded the world,
#   * the app answered requests while it was in flight,
#   * no UI-thread gap beyond the budget (500 ms),
#   * the synchronous project.open still opens the same world unchanged,
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
set_tests_properties(open.responsive PROPERTIES
    ENVIRONMENT "HOME=${_open_home}"
    TIMEOUT 480 LABELS "openasync")
