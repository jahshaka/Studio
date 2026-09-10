# Gizmo overlay through the engine. Output goes to bin/ so the gizmos' asset paths
# (relative to the executable: app/models/axis_*.obj) resolve.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_gizmo_overlay test_gizmo_overlay.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmooverlay.cpp ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp ${CMAKE_SOURCE_DIR}/src/viewport/gizmomeshes.cpp ${CMAKE_SOURCE_DIR}/src/viewport/translationgizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/rotationgizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scalegizmo.cpp)
set_target_properties(test_gizmo_overlay PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gizmo_overlay PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gizmo_overlay PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gizmo_overlay PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL Qt6::Widgets)
add_dependencies(test_gizmo_overlay JahshakaEngineMedia)
add_test(NAME gizmo.overlay COMMAND ${CMAKE_BINARY_DIR}/bin/test_gizmo_overlay WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(gizmo.overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")

# gizmo.screen_size — "the gizmo is the same size on screen, everywhere"
# (owner requirement 2026-09-08). Document-only: it projects the gizmo's REAL
# draw items through the DOCUMENT camera's own matrices, so it needs the
# headless graph (NULL render system) and no display at all — jah_no_display
# below is the proof of that claim.
add_executable(test_gizmo_screen_size test_gizmo_screen_size.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmomeshes.cpp ${CMAKE_SOURCE_DIR}/src/viewport/translationgizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/rotationgizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scalegizmo.cpp)
set_target_properties(test_gizmo_screen_size PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(test_gizmo_screen_size PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_gizmo_screen_size PRIVATE IrisGL Qt6::Gui Qt6::OpenGL Qt6::Widgets)
jah_document_graph(test_gizmo_screen_size)
add_test(NAME gizmo.screen_size COMMAND ${CMAKE_BINARY_DIR}/bin/test_gizmo_screen_size
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(gizmo.screen_size PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "document")
jah_no_display(gizmo.screen_size)

# gizmo.group_transform — the SELECTION SET's transform (EDITOR_MULTISELECT_SPEC
# §2.4 / §5). Document-only, like gizmo.screen_size, but with a REAL undo stack
# and the real TransformSceneNodeCommand: the one-undo-step-for-N-members claim
# is the point, and it cannot be asserted from a --script run (a run is itself
# one open macro, which QUndoStack refuses to undo into).
add_executable(test_gizmo_group test_gizmo_group.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmomeshes.cpp ${CMAKE_SOURCE_DIR}/src/viewport/translationgizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/rotationgizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scalegizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    test_group_stubs.cpp)
set_target_properties(test_gizmo_group PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(test_gizmo_group PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_gizmo_group PRIVATE IrisGL Qt6::Gui Qt6::OpenGL Qt6::Widgets)
jah_document_graph(test_gizmo_group)
add_test(NAME gizmo.group_transform COMMAND ${CMAKE_BINARY_DIR}/bin/test_gizmo_group
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(gizmo.group_transform PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "document")
jah_no_display(gizmo.group_transform)
