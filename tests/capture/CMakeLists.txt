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
jah_add_pool(capture TIER epic LABELS scripting capture)

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
