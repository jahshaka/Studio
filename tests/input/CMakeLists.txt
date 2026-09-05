# input.actions — the gameplay action layer (AVATAR_LOCOMOTION_SPEC §8.2,
# Stage 1's gate): binding round-trip, conflict report, latch semantics.
#
# Compiles inputmap.cpp beside the test rather than linking IrisGL: the input
# layer is deliberately self-contained (QtCore + QKeySequence, no scene, no
# engine, no Bullet), and a suite that proves that by LINKING is worth more
# than a comment saying it.
find_package(Qt6 REQUIRED COMPONENTS Gui)

add_executable(test_input_actions test_input_actions.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/document/input/inputmap.cpp)
set_target_properties(test_input_actions PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_input_actions PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_input_actions PRIVATE Qt6::Gui)
add_test(NAME input.actions COMMAND test_input_actions
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(input.actions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     LABELS "document")
# No display, no engine, no GPU — and enforced, not asserted.
jah_no_display(input.actions)
jah_lsan_lane(input.actions)
