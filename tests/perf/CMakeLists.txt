# perf.pacing — the frame-pacing interval policy (fps audit F1).
#
# DISPLAY-FREE and dependency-free: it links src/services/framepacing.cpp and
# Qt Core, nothing else. No engine, no widgets, no QScreen — the policy is a
# pure function and the suite exists so that it stays one. (The driver half,
# "the timer really re-times", needs a live engine and belongs to the app
# suites; what would break silently is this arithmetic.)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_frame_pacing test_frame_pacing.cpp
    ${CMAKE_SOURCE_DIR}/src/services/framepacing.cpp)
set_target_properties(test_frame_pacing PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_frame_pacing PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_frame_pacing PRIVATE Qt6::Core)
add_test(NAME perf.pacing COMMAND test_frame_pacing
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(perf.pacing PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")

# perf.offscreen_isolation — a thumbnail/screenshot readback must not redraw
# (and present) the editor's on-screen view (fps audit F5). Engine up, its own
# HOME like every other scripted e2e; the script's header explains the counter
# it asserts on.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-offscreen_isolation)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME perf.offscreen_isolation
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/offscreen_isolation.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(perf.offscreen_isolation PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 240)

# perf.epic_steady_state — the GROWTH-ONLY steady-state gate (fps audit F11).
#
# The regression net for the class that shipped on 2026-09-05: a frame that got
# more expensive, and object counts that grew, while every pixel and every
# document field stayed identical. The script's header is the reference for what
# it measures and why; what belongs HERE is why it is wired the way it is.
#
#   * A VIRGIN HOME, wiped by a setup fixture, exactly like samples.cleanstart.
#     The suite imports the Matcaps archive itself, so a HOME that survived the
#     previous ctest would make it a re-import — and, worse, would carry the
#     previous run's shader cache, which changes what sample 1 costs.
#   * ENGINE UP (no --headless): the whole subject is a frame being rendered.
#   * NOT a benchmark, and not labelled one. It asserts NOTHING absolute — every
#     comparison is one sample of a run against another sample of the SAME run —
#     so it is machine-independent, costs ~350 frames of one small scene, and
#     belongs in the ordinary gate rather than behind `ctest -LE benchmark`.
#   * RUN_SERIAL anyway. The equalities do not need it (an object count does not
#     care what else the box is doing), but the cost ratio it prints and the one
#     inequality it asserts do, and a number nobody can trust is worse than no
#     number. Serial keeps ctest's own suites off its back.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(epic_steady_state.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/epic_steady_state.js @ONLY)
set(_ess_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-epic_steady_state)

# Reuses the samples suite's wipe script rather than copying it: "a scratch HOME
# that is virgin every run" is one idea and deserves one implementation.
add_test(NAME perf.freshhome.epic_steady_state
         COMMAND ${CMAKE_COMMAND} -DHOME_DIR=${_ess_home}
                 -P ${CMAKE_SOURCE_DIR}/tests/samples/freshhome.cmake)
set_tests_properties(perf.freshhome.epic_steady_state PROPERTIES
    FIXTURES_SETUP perf_home_epic_steady_state
    LABELS "perf")

add_test(NAME perf.epic_steady_state
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/epic_steady_state.js
         WORKING_DIRECTORY ${_ess_home}/run)
set_tests_properties(perf.epic_steady_state PROPERTIES
    ENVIRONMENT "HOME=${_ess_home};JAHSHAKA_DATA_ROOT=${_ess_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED perf_home_epic_steady_state
    LABELS "perf"
    RUN_SERIAL TRUE
    TIMEOUT 480)
