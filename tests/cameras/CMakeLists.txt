# Scene cameras (CAMERAS_SPEC phase 1 — document + verbs).
#
# cameras.document      the document half: the type-enum fix and its sweep
#                       points, the angle <-> focal-length binding in both
#                       directions, reflection of every setting, duplication,
#                       and the scene camera registry + active camera. Pure
#                       iris:: — no engine, no app, offscreen.
# cameras.play_routing  the ONE behavioural change phase 1 makes to rendering:
#                       SceneMirror::applyCamera renders through the active
#                       camera while PLAYING and through the caller's camera
#                       while editing. Real engine, real frames, pixels are the
#                       assertion. Spans the boundary like tests/mirror does.
# cameras.e2e           the real binary under --script: every verb, and the
#                       save/close/open round trip of every camera field
#                       through the real writer and the real reader.

find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# ---- cameras.document -----------------------------------------------------
# Compiles the export walker beside the test: classifyNode is one of the sweep
# sites the type-enum fix re-routes, and it is pure document logic.
add_executable(test_camera_document test_camera_document.cpp
    ${CMAKE_SOURCE_DIR}/src/export/walkers/scenewalker.cpp
    # scenewalker's inventory walk reads material texture slots.
    ${CMAKE_SOURCE_DIR}/src/export/walkers/materialtexturereader.cpp)
set_target_properties(test_camera_document PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17)
target_include_directories(test_camera_document PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_camera_document PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_camera_document)
add_test(NAME cameras.document COMMAND test_camera_document
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(cameras.document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "document")
jah_no_display(cameras.document)

# ---- cameras.play_routing -------------------------------------------------
add_executable(test_camera_play_routing test_camera_play_routing.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_camera_play_routing PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_camera_play_routing PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_camera_play_routing PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_camera_play_routing PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_camera_play_routing JahshakaEngineMedia)
add_test(NAME cameras.play_routing COMMAND test_camera_play_routing
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cameras.play_routing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "mirror")

# ---- cameras.body ---------------------------------------------------------
# The camera BODY + FRUSTUM wires (phase 2b) and the PiP inset (phase 2c), in
# pixels: real backend, real frames, byte-exact off switches.
add_executable(test_camera_body test_camera_body.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_camera_body PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_camera_body PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_camera_body PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_camera_body PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_camera_body JahshakaEngineMedia)
add_test(NAME cameras.body COMMAND test_camera_body
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cameras.body PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "mirror")

# ---- cameras.e2e ----------------------------------------------------------
# The real app binary, --script, with HOME redirected to a scratch dir so the
# run gets a fresh library and can never touch the user's (the same contract
# every scripting.e2e.* suite has).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/cameras-home)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME cameras.e2e
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_cameras.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(cameras.e2e PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 300)

# ---- cameras.e2e.pilot ----------------------------------------------------
# Phase 3 (D4/D8) and the phase-2c preview preference, through the verbs, in
# the real binary. Same scratch-HOME contract as cameras.e2e.
set(_pilot_home ${CMAKE_CURRENT_BINARY_DIR}/pilot-home)
file(MAKE_DIRECTORY ${_pilot_home}/run)
add_test(NAME cameras.e2e.pilot
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_camera_pilot.js
         WORKING_DIRECTORY ${_pilot_home}/run)
set_tests_properties(cameras.e2e.pilot PROPERTIES
    ENVIRONMENT "HOME=${_pilot_home}"
    LABELS "scripting"
    TIMEOUT 300)
