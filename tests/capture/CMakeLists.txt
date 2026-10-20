# THE VIDEO RECORDER (VIDEO-REC-1; SPECS/VIDEO_CAPTURE_SPEC.md §2 + §10). One pool, `capture`,
# one app process for the four arms — each records its own short clips into this run's own
# data root (never the owner's ~/Videos: HOME is the pool's fresh home) and asserts on the
# FILE, the recorder's frames and the button, never on wall-clock time (the recorder runs on
# the scene's clock, and every arm steps that clock itself with editor.frame(1, 1/60)).
#
# EPIC, deliberately: `helpers_switch` asserts a PICTURE (the editor's furniture in or out of
# the recorded frame), and the recording view takes the scene's own chain at the shipped tier.
#
# capture.record_basic — the verb records N frames at the fixed 1/60 s step: the MP4 holds N
#   frames of 1920x1080 avc1 at a constant 60/1, its index BEFORE its media (fast-start), Qt's
#   decoder reads it, and the status names the H.264 encoder Qt opened (a HARDWARE one here).
jah_pool_arm(capture record_basic ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_record_basic.js 20)
# capture.helpers_switch — scene only by default: a recorded frame with the grid, gizmo and
#   outline present but the switch OFF equals one of a scene with no furniture at all; with
#   the switch ON the furniture is in the frame.
jah_pool_arm(capture helpers_switch ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_helpers_switch.js 25)
# capture.ui — the record button: press records (red dot, "REC m:ss"), Esc in the viewport
#   stops it only while recording, press again stops; the file is in the button's popup.
jah_pool_arm(capture ui ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_ui.js 20)
# capture.failure — no encoder, an unwritable folder and an encoder error mid-recording each
#   end cleanly with one message (and no published file); the next recording works.
jah_pool_arm(capture failure ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_failure.js 15)
# capture.abandon — quit while saving: the shutdown's abandonSave publishes the encoder's own
#   complete file at the final path (playable, said in the warning), never only a hidden raw file.
jah_pool_arm(capture abandon ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_abandon.js 10)
# capture.first_recording — the session's FIRST recording compiles no shader: the startup gate
#   drew the recorder's own view (its chain and the NV12 job) behind the splash (VIDEO-REC-2).
jah_pool_arm(capture first_recording ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_first_recording.js 15 FIRST)
# capture.offline_unique — THE OFFLINE MODE (VIDEO-REC-2): no longer an arm of this pool. Alone it was 73 s of
#   arm (50 ms blocked per frame on purpose), over the pool budget (TESTING-SPEED-B2: <= 60 s of arms), and its
#   .timing twin below runs THE SAME SCRIPT (every count, plus its 100 ms readback bar) at the stage close — that row
#   is the offline mode's guard. Deleted from the merge pool on the lead's word (2026-10-12).
# capture.offline_video — a PLAYING video texture rides the offline clock: decoded forward one
#   1/60 s per recorded frame (8 video frames a second of an 8 fps clip, whatever the wall time).
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(scripts/capture_offline_video.js.in ${CMAKE_CURRENT_BINARY_DIR}/capture_offline_video.js @ONLY)
jah_pool_arm(capture offline_video ${CMAKE_CURRENT_BINARY_DIR}/capture_offline_video.js 30)
jah_add_pool(capture TIER document   LABELS scripting capture)

# capture.first_recording.timing / capture.offline_unique.timing — THE RECORDER'S MILLISECOND BARS
# (TESTING-CLEANUP-2 P10f; tests/support/timingbars.h's rule): the first click's 50 ms and the
# encoder probe's 4 ms (DBUILD-FIX-1: the backend's construction with its libraries preloaded,
# lane max 1.01 ms x the worst stretch on record 3.5, rounded up; the lead's decision 2026-10-11;
# the derivation in the script), and the readback ring's
# 100 ms worst UI-thread wait, ARMED in a
# jah_timing_script copy of each arm's script, each its own process on its own wiped home (warm
# caches, like the pool), on the quiet box under the GPU-timing lock. The pool's first_recording arm keeps
# every count and prints these readings; offline_unique is no pool arm (TESTING-SPEED-B2): its .timing row is
# its only run, counts and bar together. RUN_TIMEOUT = 6x the measured run after the admission
# (2026-10-09 on :71: first_recording 32 s, offline_unique 101 s).
# TIER document, as the `capture` pool these scripts belong to: a bar measures the boot its
# pool counts on (the recording view takes the scene's own chain at its own tier).
set(_cap_timing_first_recording 200)
set(_cap_timing_offline_unique 610)
foreach(_arm first_recording offline_unique)
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

# pool.capture_validation — THE RECORDER UNDER THE KHRONOS VALIDATION LAYER (TESTING-CLEANUP-2 H4):
# the recording view's chain, the NV12 job and the readback ring had never run under the layer in
# a gate. Its FIRST arm proves the layer is live (engine.validation()); then a basic recording runs
# under it; any "Validation Error" in the process's output fails the row.
# TIER epic with every NEEDS word: the layer is only worth what it sees — the whole shipped chain.
if(EXISTS "/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json")
    jah_pool_arm(capture_validation validation_proof ${CMAKE_SOURCE_DIR}/tests/scripting/scripts/e2e_validation_proof.js 5 FIRST)
    jah_pool_arm(capture_validation record_basic ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_record_basic.js 20)
    jah_add_pool(capture_validation TIER epic NEEDS photon bloom ssao smaa planar LABELS scripting capture
        ENV "VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation"
        PROPS FAIL_REGULAR_EXPRESSION "Validation Error")
endif()

# capture.faststart — the fast-start step (src/modules/capture/mp4faststart.cpp) on SYNTHETIC
# files: the 32-bit table shifted, a shift past 4 GiB widened to co64, a co64 table, an
# already-fast-start file copied unchanged, a truncated moov refused. Qt Core only, no display.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_mp4_faststart test_mp4_faststart.cpp ${CMAKE_SOURCE_DIR}/src/modules/capture/mp4faststart.cpp)
set_target_properties(test_mp4_faststart PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_mp4_faststart PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_mp4_faststart PRIVATE Qt6::Core)
add_test(NAME capture.faststart COMMAND test_mp4_faststart)
set_tests_properties(capture.faststart PROPERTIES TIMEOUT 60 LABELS "capture")
jah_no_display(capture.faststart)

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST pool.capture)
    set_property(TEST pool.capture PROPERTY JAHSHAKA_TEST_FRAMES 1303)
endif()
if(TEST pool.capture_validation)
    set_property(TEST pool.capture_validation PROPERTY JAHSHAKA_TEST_FRAMES 601)
endif()
if(TEST capture.first_recording.timing)
    set_property(TEST capture.first_recording.timing PROPERTY JAHSHAKA_TEST_FRAMES 552)
endif()
if(TEST capture.offline_unique.timing)
    set_property(TEST capture.offline_unique.timing PROPERTY JAHSHAKA_TEST_FRAMES 826)
endif()
