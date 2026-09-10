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

# input.axis_view_lock — THE AXIS-VIEW ROTATION LOCK (owner report 2026-09-08):
# in an axis view the camera cannot be TURNED by a gesture, only panned, zoomed
# and flown. Drives both editor camera controllers at the gesture level (the
# same calls EngineSceneViewport's mouse handlers make) and asserts the
# quaternion is untouched by every rotation gesture while locked — and moves
# while not, so the suite cannot pass on a controller that never rotates.
#
# Same link shape as input.fly_controls: the controllers plus the settings they
# read, no engine view, no display, no GPU.
add_executable(test_axis_view_lock test_axis_view_lock.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/editorcameracontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/orbitalcameracontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/cameracontrollerbase.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/flyspeedsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp)
set_target_properties(test_axis_view_lock PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_axis_view_lock PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_axis_view_lock PRIVATE IrisGL Qt6::Gui Qt6::Widgets)
jah_document_graph(test_axis_view_lock)
add_test(NAME input.axis_view_lock COMMAND test_axis_view_lock
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(input.axis_view_lock PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "document")
jah_no_display(input.axis_view_lock)
