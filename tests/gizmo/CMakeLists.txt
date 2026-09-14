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

# gizmo.ring_pick — EVERY ROTATION RING IS CLICKABLE FROM EVERY CAMERA ANGLE
# (smoke S15, the owner's "sometimes I can't click R, sometimes G or B"). Walks
# each ring's own projected circle from six cameras — including the edge-on case
# for all three axes — and asserts the screen-space pick answers at every pixel,
# that the old 3D annulus rule answered at none of them when the ring is
# edge-on, and that the drag angle stays finite and bounded. Document-only, like
# gizmo.screen_size: no display, no GPU, no pixels.
add_executable(test_gizmo_ring_pick test_gizmo_ring_pick.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmomeshes.cpp ${CMAKE_SOURCE_DIR}/src/viewport/translationgizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/rotationgizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scalegizmo.cpp)
set_target_properties(test_gizmo_ring_pick PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(test_gizmo_ring_pick PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_gizmo_ring_pick PRIVATE IrisGL Qt6::Gui Qt6::OpenGL Qt6::Widgets)
jah_document_graph(test_gizmo_ring_pick)
add_test(NAME gizmo.ring_pick COMMAND ${CMAKE_BINARY_DIR}/bin/test_gizmo_ring_pick
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(gizmo.ring_pick PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "document")
jah_no_display(gizmo.ring_pick)

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

# gizmo.drag_frame — THE ROTATION GIZMO'S FRAME IS FROZEN FOR THE LENGTH OF A
# DRAG (owner report 2026-09-15, ledger §345: "they seem to move on their own
# with the asset"). Drives a real drag through the very calls a mouse press and
# move make and asserts the drawn AND picked frame is bit-identical from press
# to release, that the grabbed ring stays under the cursor, that the node still
# turns, and — inline, as a number — how far the frame the gizmo used to draw
# ran away. Document-only, like gizmo.ring_pick: no display, no GPU, no pixels.
add_executable(test_gizmo_drag_frame test_gizmo_drag_frame.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmomeshes.cpp ${CMAKE_SOURCE_DIR}/src/viewport/translationgizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/rotationgizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scalegizmo.cpp)
set_target_properties(test_gizmo_drag_frame PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(test_gizmo_drag_frame PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_gizmo_drag_frame PRIVATE IrisGL Qt6::Gui Qt6::OpenGL Qt6::Widgets)
jah_document_graph(test_gizmo_drag_frame)
add_test(NAME gizmo.drag_frame COMMAND ${CMAKE_BINARY_DIR}/bin/test_gizmo_drag_frame
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(gizmo.drag_frame PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "document")
jah_no_display(gizmo.drag_frame)

# gizmo.plane_handles — THE TRANSLATE GIZMO'S THREE PLANE HANDLES (owner report
# 2026-09-15, ledger §346: "it helps with the spatial connection for the user").
# Picks each square by pixel, drags it and asserts the node moves in exactly the
# two axes it spans and tracks the cursor, that an edge-on square is neither
# drawn nor pickable, that the group delta applies, and that the arrows and the
# centre ball still answer beside them. Document-only: no display, no GPU.
add_executable(test_gizmo_planes test_gizmo_planes.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmomeshes.cpp ${CMAKE_SOURCE_DIR}/src/viewport/translationgizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/rotationgizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scalegizmo.cpp)
set_target_properties(test_gizmo_planes PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(test_gizmo_planes PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_gizmo_planes PRIVATE IrisGL Qt6::Gui Qt6::OpenGL Qt6::Widgets)
jah_document_graph(test_gizmo_planes)
add_test(NAME gizmo.plane_handles COMMAND ${CMAKE_BINARY_DIR}/bin/test_gizmo_planes
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(gizmo.plane_handles PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "document")
jah_no_display(gizmo.plane_handles)
