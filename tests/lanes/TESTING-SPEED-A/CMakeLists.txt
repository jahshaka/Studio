# TESTING-SPEED-A's OWN TESTS (tests/lanes/<LANE>/, SPECS/TESTING_SPEED_SPEC.md §2 A7) — deleted at the lane's land.
#
# testing_speed_a.boot_profile — WHERE A FIXTURE'S BOOT GOES (item 10, A11): the engine selftest cold then warm on one
# private data root, its output stamped on arrival, the boot's phases printed (BOOT <run> <phase> <s>). The lane's
# measurement for Part B (spikes/testing-speed-a/boot.txt); PASS = both runs exit 0 with every pose. FRAMES: the
# selftest's fixed counts twice (40 + 240 + 30 + 12 + 300 + 300 per run; the GI-at-rest settles add a measured few).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
jah_lane_row(TESTING-SPEED-A testing_speed_a.boot_profile CLASS selftest FRAMES 1844
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/boot_profile.py"
                     $<TARGET_FILE:${CMAKE_PROJECT_NAME}> "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(testing_speed_a.boot_profile PROPERTIES TIMEOUT 120)
