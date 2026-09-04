# archive.responsive — the threaded project archive (STABILITY_PROGRAM_SPEC
# Lane 4), measured the way open.responsive measures an open: spawn the REAL
# binary, drive it over MCP, and read the main-thread heartbeat's worst gap
# while an export and an import are in flight. Also asserts the two things the
# threading is not allowed to cost — a round trip through the threaded export
# reproduces the world, and a cancelled import leaves no orphan project — and
# that quitting mid-archive still terminates cleanly (shutdownArchives).
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(test_archive_responsive test_archive_responsive.cpp)
set_target_properties(test_archive_responsive PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_archive_responsive PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(test_archive_responsive PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

set(_archive_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-archive)
file(MAKE_DIRECTORY ${_archive_home}/run)
add_test(NAME archive.responsive COMMAND test_archive_responsive
         WORKING_DIRECTORY ${_archive_home}/run)
set_tests_properties(archive.responsive PROPERTIES
    ENVIRONMENT "HOME=${_archive_home}"
    TIMEOUT 600 LABELS "archive")

# The race lane (scripts/sanitize.sh tsan): the threaded export/import runner
# plus its cancel path. Same wall-clock caveat as open.responsive.
jah_tsan_blocked(archive.responsive)   # spawns the app: see jah_tsan_blocked
