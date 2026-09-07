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

# input.fly_controls — the two FREE cameras' movement contract (fix wave
# 2026-09-07 items 3 and 5): the arrow keys are W/A/S/D aliases in BOTH the
# editor fly and the player free camera (identical motion, not merely "some"),
# and the FlySpeedSettings multiplier scales the distance exactly.
#
# Links IrisGL for the camera node and the vector maths; no engine view, no
# display, no GPU.
add_executable(test_fly_controls test_fly_controls.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/editorcameracontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/cameracontrollerbase.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/flyspeedsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/keyboardstate.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playermousecontroller.cpp)
set_target_properties(test_fly_controls PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_fly_controls PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_fly_controls PRIVATE IrisGL Qt6::Gui Qt6::Widgets)
# A camera node is an engine node (SCENEGRAPH_SPEC D2), so this boots the
# HEADLESS engine — NULL render system, no display, no GPU — exactly like the
# other document suites. jah_no_display below proves the claim on every run.
jah_document_graph(test_fly_controls)
add_test(NAME input.fly_controls COMMAND test_fly_controls
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(input.fly_controls PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "document")
jah_no_display(input.fly_controls)
