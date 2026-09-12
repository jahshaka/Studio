# avatar.responsive — the Avatar module's import and character switch must not
# freeze the app (AV1, owner 2026-09-13), measured the way archive.responsive
# and open.responsive measure theirs: spawn the REAL binary, drive it over MCP,
# and read the main-thread heartbeat's worst gap while the work is in flight.
# Also asserts what the threading is not allowed to cost — the threaded import
# produces the same avatar the synchronous verb does, a cancelled one leaves the
# library untouched — the per-avatar animation persistence (across a switch AND
# across a restart of the app), and that quitting mid-import still terminates.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(test_avatar_async test_avatar_async.cpp)
set_target_properties(test_avatar_async PROPERTIES CXX_STANDARD 17 AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_avatar_async PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_avatar_async PRIVATE Qt6::Core Qt6::Network)
# The fixture pair comes from tests/avatar (which configures AFTER this dir, so
# the paths are spelled out exactly as the scripting suites' are): the rigged
# Mixamo Beta mannequin and the animation-only walk that shares its joints.
target_compile_definitions(test_avatar_async PRIVATE
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
    JAHSHAKA_MANNEQUIN_DAE="${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_mannequin.dae"
    JAHSHAKA_BETA_WALK_DAE="${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_walk.dae")

set(_avatar_async_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-avatar-async)
file(MAKE_DIRECTORY ${_avatar_async_home}/run)
add_test(NAME avatar.responsive COMMAND test_avatar_async
         WORKING_DIRECTORY ${_avatar_async_home}/run)
set_tests_properties(avatar.responsive PROPERTIES
    ENVIRONMENT "HOME=${_avatar_async_home};JAHSHAKA_DATA_ROOT=${_avatar_async_home}/.local/share/Jahshaka"
    TIMEOUT 900 LABELS "avatar")

# Spawns the app (and therefore a Vulkan driver): neither sanitizer lane can
# take it — same reason import.shutdown and archive.responsive are blocked.
jah_tsan_blocked(avatar.responsive)
