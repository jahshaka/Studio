# archive.responsive.roundtrip / .lifecycle (+ their .timing twins) — the threaded project archive (STABILITY_PROGRAM_SPEC
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

# THE ROWS - SPLIT BY PROOF (TEST-STREAMLINE-1 item 4, the lead's ruling 2026-11-03): the whole binary ran 67-74 s
# (archive.responsive 67.61-74.28 s, archived 10-20; archive.responsive.timing 67.0-68.5 s on four ship runs 10-20..10-22
# against a stale 49.60-s line), past the 60-s cap. `--part roundtrip` (the threaded import, the threaded and synchronous
# exports, what the archive ships, the round trip, a clean quit) and `--part lifecycle` (the share file off the UI thread,
# the cancel that leaves no orphan, the quit with an archive in flight) - every check kept, each part its own app on its own
# home, wiped before it by its WARM fresh-home fixture (the empty library the bake-row checks need).
#   archive.responsive.<part>         UNARMED (no JAHSHAKA_TIMING_BARS): every count, the round trip, the manifest, the
#                                     cancel, the quit - the gaps PRINTED against their budgets (timingbars.h); archiveasync's
#                                     FAST row (SHIP), a normal card row.
#   archive.responsive.timing.<part>  the same part with the UI-gap budgets ARMED (and the 600-entry big pack), on the quiet box
#                                     under the GPU-timing lock: `measure`, LONG only.
# archive.responsive.part_usage — the binary REFUSES a run that names no part (a CPU row: it prints the usage line and
# exits before it spawns anything; no display, no card), so a registration that loses its --part never runs the app.
add_test(NAME archive.responsive.part_usage COMMAND test_archive_responsive --part none)
set_tests_properties(archive.responsive.part_usage PROPERTIES TIMEOUT 60 LABELS "archive"
    PASS_REGULAR_EXPRESSION "FAIL: usage: test_archive_responsive --part roundtrip\\|lifecycle \\(got 'none'\\)")
jah_no_display(archive.responsive.part_usage)
foreach(_part roundtrip lifecycle)
    set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-archive-${_part})
    file(MAKE_DIRECTORY ${_home}/run)
    jah_gpu_row(archive.responsive.${_part} WARM_HOME "${_home}" SHIP CLASS app TIER epic NEEDS photon bloom ssao smaa planar
                COMMAND test_archive_responsive --part ${_part} WORKING_DIRECTORY ${_home}/run)
    set_tests_properties(archive.responsive.${_part} PROPERTIES
        ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
        TIMEOUT 60 LABELS "archive")
    jah_fresh_home_fixture(archive.responsive.${_part}.fresh_home archiveResponsive_${_part}_FreshHome "${_home}" WARM
                           LABELS "archive")
    set_property(TEST archive.responsive.${_part} APPEND PROPERTY FIXTURES_REQUIRED archiveResponsive_${_part}_FreshHome)

    set(_thome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-archive-timing-${_part})
    file(MAKE_DIRECTORY ${_thome}/run)
    jah_gpu_exclusive_test(NAME archive.responsive.timing.${_part} WARM_HOME "${_thome}" SHIP CLASS app TIER epic
        NEEDS photon bloom ssao smaa planar RUN_TIMEOUT 60 COMMAND test_archive_responsive --part ${_part}
        WORKING_DIRECTORY ${_thome}/run)
    set_tests_properties(archive.responsive.timing.${_part} PROPERTIES
        ENVIRONMENT "HOME=${_thome};JAHSHAKA_DATA_ROOT=${_thome}/.local/share/Jahshaka;JAHSHAKA_TIMING_BARS=1"
        LABELS "archive;measure")
    jah_fresh_home_fixture(archive.responsive.timing.${_part}.fresh_home archiveResponsiveTiming_${_part}_FreshHome
                           "${_thome}" WARM LABELS "archive")
    set_property(TEST archive.responsive.timing.${_part} APPEND PROPERTY FIXTURES_REQUIRED
                 archiveResponsiveTiming_${_part}_FreshHome)
endforeach()

# The race lane (scripts/sanitize.sh tsan): the threaded export/import runner
# plus its cancel path. Same wall-clock caveat as open.responsive.
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(archive.responsive.roundtrip archive.responsive.lifecycle
                 archive.responsive.timing.roundtrip archive.responsive.timing.lifecycle)   # spawn the app: see jah_tsan_blocked

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. 0 = the row renders none.
# PROVISIONAL, BLOCK D (TEST-STREAMLINE-1 item 4): each part states its WHOLE binary's measured count (archive.responsive
# 1129, archive.responsive.timing 1099, 2026-10-12) until block D counts the parts.
foreach(_part roundtrip lifecycle)
    set_property(TEST archive.responsive.${_part} PROPERTY JAHSHAKA_TEST_FRAMES 1129)
    set_property(TEST archive.responsive.timing.${_part} PROPERTY JAHSHAKA_TEST_FRAMES 1099)
endforeach()
