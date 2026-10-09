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
jah_gpu_row(avatar.responsive CLASS app TIER epic NEEDS photon COMMAND test_avatar_async
         WORKING_DIRECTORY ${_avatar_async_home}/run)
set_tests_properties(avatar.responsive PROPERTIES
    ENVIRONMENT "HOME=${_avatar_async_home};JAHSHAKA_DATA_ROOT=${_avatar_async_home}/.local/share/Jahshaka"
    TIMEOUT 360 LABELS "avatar")
# avatar.responsive.timing — THE MILLISECOND BARS, nightly (lane D6B-GATE-SHAPE; audit §5,
# F17 FIX-TIME): the same binary with JAHSHAKA_TIMING_BARS=1 on a quiet box, under the
# GPU-timing lock; the push row asserts the rest and prints the readings.
set(_avatar_async_timing_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-avatar-async-timing)
file(MAKE_DIRECTORY ${_avatar_async_timing_home}/run)
jah_gpu_exclusive_test(NAME avatar.responsive.timing CLASS app TIER epic NEEDS photon RUN_TIMEOUT 900 COMMAND test_avatar_async
         WORKING_DIRECTORY ${_avatar_async_timing_home}/run)
set_tests_properties(avatar.responsive.timing PROPERTIES
    ENVIRONMENT "HOME=${_avatar_async_timing_home};JAHSHAKA_DATA_ROOT=${_avatar_async_timing_home}/.local/share/Jahshaka;JAHSHAKA_TIMING_BARS=1"
    LABELS "avatar;nightly;quiet-box")

# Spawns the app (and therefore a Vulkan driver): neither sanitizer lane can
# take it — same reason import.shutdown and archive.responsive are blocked.
jah_tsan_blocked(avatar.responsive avatar.responsive.timing)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, the nightly) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST avatar.responsive)
    jah_fresh_home_fixture(avatar.responsive.fresh_home avatarResponsiveFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-avatar-async" WARM LABELS "avatar")
    set_property(TEST avatar.responsive APPEND PROPERTY FIXTURES_REQUIRED avatarResponsiveFreshHome)
endif()
if(TEST avatar.responsive.timing)
    jah_fresh_home_fixture(avatar.responsive.timing.fresh_home avatarResponsiveTimingFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-avatar-async-timing" WARM LABELS "avatar")
    set_property(TEST avatar.responsive.timing APPEND PROPERTY FIXTURES_REQUIRED avatarResponsiveTimingFreshHome)
endif()
