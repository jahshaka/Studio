# Waveform peak computation (ASSET_MEDIA_SPEC §2 — the audio preview's
# waveform): pure wav parse on the committed fixture + synthetic wavs with
# known amplitudes, the JSON cache round-trip, and the QAudioDecoder
# (ffmpeg backend) fallback. Headless (offscreen platform).
find_package(Qt6 REQUIRED COMPONENTS Gui Multimedia)
add_executable(test_audio_peaks test_audio_peaks.cpp
    ${CMAKE_SOURCE_DIR}/src/services/audiopeaks.cpp)
target_include_directories(test_audio_peaks PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_audio_peaks PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_audio_peaks PRIVATE Qt6::Gui Qt6::Multimedia)
add_test(NAME audio.peaks COMMAND test_audio_peaks
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(audio.peaks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")

# The leak lane (scripts/sanitize.sh lsan): decoder buffers per file.
jah_lsan_lane(audio.peaks)

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
jah_no_display(audio.peaks)
