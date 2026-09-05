# Sockets (CAMERAS_SPEC §5 / phase 4 — owner decision D9).
#
# sockets.document  the socket maths against hand-computed matrices, the
#                   bind-pose fallback (no engine at all), every refusal, the
#                   dangling cases, duplication, the scene's attachment
#                   registry, and the avatar module's bone-name table. Pure
#                   iris:: + avatarsockets.cpp, offscreen.
# sockets.render    the same thing through the WHOLE chain — engine pose ->
#                   mirror read-back -> resolver -> applyCamera -> pixels. A
#                   camera on an animated bone must photograph a different shot
#                   as the rig moves, and a detached one must not.
# sockets.e2e       the verbs in the real binary: node.addSocket / sockets /
#                   attachToSocket / detachFromSocket / removeSocket,
#                   avatar.addSockets, camera.screenshot, the refusals, undo,
#                   and the save/close/open round trip through the real writer
#                   and reader.

find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# ---- sockets.document -----------------------------------------------------
# Compiles avatarsockets.cpp beside the test: the built-in bone-name mapping is
# pure document logic with no module, page or engine behind it.
add_executable(test_sockets_document test_sockets_document.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/avatar/avatarsockets.cpp)
set_target_properties(test_sockets_document PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_sockets_document PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_sockets_document PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_sockets_document)
add_test(NAME sockets.document COMMAND test_sockets_document
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(sockets.document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120 LABELS "document")
jah_no_display(sockets.document)

# ---- sockets.render -------------------------------------------------------
add_executable(test_socket_render test_socket_render.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_socket_render PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_socket_render PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_socket_render PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_socket_render PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_socket_render JahshakaEngineMedia)
add_test(NAME sockets.render COMMAND test_socket_render
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sockets.render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "mirror")

# ---- sockets.e2e ----------------------------------------------------------
# The real app binary, --script, with HOME redirected to a scratch dir so the
# run gets a fresh library and can never touch the user's.
set(_sockets_home ${CMAKE_CURRENT_BINARY_DIR}/sockets-home)
file(MAKE_DIRECTORY ${_sockets_home}/run)
# The script is CONFIGURED, not copied: a --script run has no way to read the
# environment, and the rigged fixture lives in the source tree, which a build
# tree has no fixed relation to.
set(JAHSHAKA_SOCKETS_RIG "${CMAKE_SOURCE_DIR}/tests/avatar/fixtures/rig2.glb")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sockets.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_sockets.js @ONLY)
add_test(NAME sockets.e2e
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_sockets.js
         WORKING_DIRECTORY ${_sockets_home}/run)
set_tests_properties(sockets.e2e PROPERTIES
    ENVIRONMENT "HOME=${_sockets_home}"
    LABELS "scripting"
    TIMEOUT 300)
