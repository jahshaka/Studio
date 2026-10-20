# archive.responsive (archived: archive/tests/archive) — the threaded project archive (STABILITY_PROGRAM_SPEC
# Lane 4), measured the way open.responsive measures an open: spawn the REAL
# binary, drive it over MCP, and read the main-thread heartbeat's worst gap
# while an export and an import are in flight. Also asserts the two things the
# threading is not allowed to cost — a round trip through the threaded export
# reproduces the world, and a cancelled import leaves no orphan project — and
# that quitting mid-archive still terminates cleanly (shutdownArchives).
# AND (EXPORT-THREAD-1) the share file: assets.exportBundle of a 200-entry pack is written on
# a worker (the verb reports it) while the UI thread keeps ticking; the stage-close row holds the
# gap to the control + one frame (16.7 ms). Qt6::Gui: the suite paints the pack's pictures.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)
# ZipHelper joins the sources so the suite can READ what the archiver wrote
# (PUBLISH_AUDIT #2: no mesh bake may travel in a .jaf). It is the app's own
# zip loop — pure QtCore over thirdparty/zip — so the suite stays free of the
# rest of the app.
add_executable(test_archive_responsive test_archive_responsive.cpp
    ${CMAKE_SOURCE_DIR}/src/io/ziphelper.cpp)
set_target_properties(test_archive_responsive PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_archive_responsive PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src)
target_link_libraries(test_archive_responsive PRIVATE Qt6::Core Qt6::Gui Qt6::Network zip)
target_compile_definitions(test_archive_responsive PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

# KEPT (the lead, TESTING-SPEED-B2 fix round 2026-10-12): the merge tier's ONE guard of src/services/projectarchiver.cpp
# (the threaded export/import, the cancel, the quit mid-export) — its .timing twin runs at the stage close only.
# budget 70 s (the ROWS table; solo median 68.4 s on 2026-10-12: scripts/gate-times.txt).
set(_archive_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-archive)
file(MAKE_DIRECTORY ${_archive_home}/run)
# archive.responsive.timing — THE UI-GAP BUDGETS, stage-close (lane D6B-GATE-SHAPE; audit §5,
# FIX-TIME): the same binary with JAHSHAKA_TIMING_BARS=1, on a quiet box, under the
# GPU-timing lock; the push row above asserts everything else and prints the gaps.
set(_archive_timing_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-archive-timing)
file(MAKE_DIRECTORY ${_archive_timing_home}/run)
jah_gpu_exclusive_test(NAME archive.responsive.timing SHIP CLASS app TIER epic NEEDS photon bloom ssao smaa planar RUN_TIMEOUT 300 COMMAND test_archive_responsive
         WORKING_DIRECTORY ${_archive_timing_home}/run)
set_tests_properties(archive.responsive.timing PROPERTIES
    ENVIRONMENT "HOME=${_archive_timing_home};JAHSHAKA_DATA_ROOT=${_archive_timing_home}/.local/share/Jahshaka;JAHSHAKA_TIMING_BARS=1"
    LABELS "archive;measure")

# The race lane (scripts/sanitize.sh tsan): the threaded export/import runner
# plus its cancel path. Same wall-clock caveat as open.responsive.
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked( archive.responsive.timing)   # spawns the app: see jah_tsan_blocked

if(TEST archive.responsive.timing)
    jah_fresh_home_fixture(archive.responsive.timing.fresh_home archiveResponsiveTimingFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-archive-timing" WARM LABELS "archive")
    set_property(TEST archive.responsive.timing APPEND PROPERTY FIXTURES_REQUIRED archiveResponsiveTimingFreshHome)
endif()

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. 0 = the row renders none.
if(TEST archive.responsive.timing)
    set_property(TEST archive.responsive.timing PROPERTY JAHSHAKA_TEST_FRAMES 1099)
endif()
