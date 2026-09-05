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
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 240)
