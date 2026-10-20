# capture.first_recording.timing — THE RECORDER'S MILLISECOND BARS (TESTING-CLEANUP-2 P10f;
# tests/support/timingbars.h's rule): the first click's 50 ms and the encoder probe's 4 ms (DBUILD-FIX-1: the
# backend's construction with its libraries preloaded, lane max 1.01 ms x the worst stretch on record 3.5, rounded
# up; the lead's decision 2026-10-11; the derivation in the script), ARMED in a jah_timing_script copy of the
# script, its own process on its own wiped home (warm caches), on the quiet box under the GPU-timing lock — every
# count and the bar together (the capture pool and capture.offline_unique.timing (archived: archive/tests/capture) were archived by TEST-CUT-1).
# RUN_TIMEOUT = 6x the measured run after the admission (2026-10-09 on :71: 32 s). TIER document: a bar measures
# the boot its scene counts on (the recording view takes the scene's own chain at its own tier).
set(_cap_timing_first_recording 200)
foreach(_arm first_recording)
    jah_timing_script(${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_${_arm}.js
                      ${CMAKE_CURRENT_BINARY_DIR}/capture_${_arm}.timing.js)
    set(_home ${CMAKE_CURRENT_BINARY_DIR}/capture-timing-home-${_arm})
    jah_gpu_exclusive_test(NAME capture.${_arm}.timing CLASS app TIER document RUN_TIMEOUT ${_cap_timing_${_arm}}
        COMMAND sh "${JAH_FRESH_HOME}" --warm "${_home}" --
                $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/capture_${_arm}.timing.js
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(capture.${_arm}.timing PROPERTIES
        ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
        LABELS "scripting;capture;measure")
endforeach()
jah_ship_row(capture.first_recording.timing)   # THE SHIP SET (TEST-CUT-1)

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST capture.first_recording.timing)
    set_property(TEST capture.first_recording.timing PROPERTY JAHSHAKA_TEST_FRAMES 552)
endif()
