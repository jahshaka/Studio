# Play-mode lifecycle: the document's physics-world construction/teardown, viewer
# removal, and PlayBack's play/pause/resume/stop state machine. No engine, no GL —
# the document model plus the player's simulation classes, with the same link
# stub the player suite uses for the undo command its controllers can push.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets)
add_executable(test_playmode_lifecycle test_playmode_lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/tests/player/test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playback.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playermousecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/cameracontrollerbase.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/keyboardstate.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp)
set_target_properties(test_playmode_lifecycle PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17)
target_include_directories(test_playmode_lifecycle PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose assimp and bullet types; these are IrisGL's own include paths
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_playmode_lifecycle PRIVATE IrisGL Qt6::Gui Qt6::OpenGL Qt6::Widgets)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_playmode_lifecycle)
add_test(NAME player.lifecycle COMMAND test_playmode_lifecycle
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(player.lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "document")
jah_no_display(player.lifecycle)
